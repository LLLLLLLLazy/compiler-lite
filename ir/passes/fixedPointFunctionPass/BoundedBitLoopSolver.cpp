///
/// @file BoundedBitLoopSolver.cpp
/// @brief 有界位迭代循环求解 pass 实现
///
/// 原理：SysY 无位运算符，程序只能用算术（%2 取位、/2 右移、×2 进位）软件
/// 模拟按位运算。本 pass 对一个迭代次数有界（≤ 字长）的循环做抽象解释，求出
/// 它在有限 bit 域上的闭式效果，再用等价的原生按位指令替换整段循环，并以取值
/// 范围守卫保证语义不变。识别基于"循环计算了什么"而非匹配某个固定 IR 模板，
/// 故对任意等价写法都成立。流程分四个解耦阶段：
///   1. 识别器 recognizeBitLoopSkeleton：确认有界位迭代循环的骨架与各 phi 角色
///   2. 识别器 validateBitLoopBody：校验循环体纯净（无副作用）并定位逐位提取器
///   3. 刻画器+综合器 characterizeBitwiseOp：抽象解释得真值表，综合为原生按位操作
///   4. 守卫+改写 rewriteBitLoop：发射取值范围守卫与原生指令快速路径，保留慢路径
///
///
/// 匹配的循环形态（SSA、select 化之后）：
///   header: a=phi(a0,a/2) [b=phi(b0,b/2)] len=phi(N,len-1)
///           res=phi(0,sel) pow=phi(1,pow*2); br (len!=0) body, exit
///   body:   bit_a=srem a,2; [bit_b=srem b,2;] 谓词计算;
///           sel=select(cond, res+pow, res); ...
/// 其中 N=len 初值（取值 [1,32]）为模拟位宽，位源可为一个（单操作数）或两个
/// （双操作数）。真值表对应 and/or/xor（双操作数）或恒等/取反/常量（单操作数）
/// 时，在 preheader 插入守卫：各操作数均落在 [0,2^N) 时直接用原生指令得结果，
/// 否则仍执行原循环。该范围内逐位模拟与原生指令严格等价（srem 结果为 0/1、
/// sdiv 等价右移、第 N..31 位恒 0 不触发累加），范围外路径保留原循环，因此
/// 任意输入下语义不变。N>=31 时非负整数恒 < 2^N，守卫退化为 guardVal>=0
///

#include "BoundedBitLoopSolver.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AnalysisCache.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CondBranchInst.h"
#include "ConstInteger.h"
#include "DominatorTree.h"
#include "Function.h"
#include "ICmpInst.h"
#include "IntegerType.h"
#include "Instruction.h"
#include "LoopInfo.h"
#include "Module.h"
#include "PhiInst.h"
#include "SelectInst.h"
#include "Value.h"
#include "ZExtInst.h"

namespace {

/// @brief 匹配成功的位模拟循环的全部要素
struct BitLoopShape {
    BasicBlock * header = nullptr;
    BasicBlock * preheader = nullptr;
    BasicBlock * exit = nullptr;
    BasicBlock * latch = nullptr;
    BasicBlock * bodyEntry = nullptr;
    std::unordered_set<BasicBlock *> body;

    PhiInst * aPhi = nullptr;
    PhiInst * bPhi = nullptr;
    PhiInst * lenPhi = nullptr;
    PhiInst * resPhi = nullptr;
    PhiInst * powPhi = nullptr;

    Instruction * sremA = nullptr;
    Instruction * sremB = nullptr;

    /// @brief 结果累加器在回边上的下一值（res_next），应形如 res + pow*f(bits)
    ///
    /// 用抽象解释对其按位代入求值得到每位累加系数，覆盖 select(c,res+pow,res)、
    /// 嵌套 select、以及无分支的算术累加 res+pow*expr 等各种等价写法
    Value * resNext = nullptr;

    Value * aInit = nullptr;
    Value * bInit = nullptr;

    IRInstOperator bitOp = IRInstOperator::IRINST_OP_MAX;

    /// @brief 循环模拟的位宽（取值 [1,32]），决定守卫所需的取值范围
    ///
    /// 倒数计数器取自 len 初值；升序计数器取自 header 比较的上界
    int32_t width = 0;

    /// @brief 计数器是否为升序（init 0、步进 +1、与上界 N 比较），否则为倒数（init N、步进 -1、与 0 比较）
    ///
    /// 两种方向都仅用于确定 ≤ 字长的有界圈数 N，不参与按位语义，故等价
    bool countsUp = false;

    /// @brief 快速路径第二操作数为常量时的取值（单操作数位运算用），无值表示取 bInit
    ///
    /// 双操作数（and/or/xor）时为空，快速路径为 bitOp(aInit, bInit)；
    /// 单操作数（恒等/取反/常量）时为掩码或 0，快速路径为 bitOp(aInit, 常量)
    std::optional<int32_t> fastRhsConst;
};

/// @brief 取常量整数值
std::optional<int32_t> asConstInt(Value * value)
{
    if (auto * constInt = dynamic_cast<ConstInteger *>(value)) {
        return constInt->getVal();
    }
    return std::nullopt;
}

/// @brief 判断二元指令是否为指定操作码且两操作数匹配（可交换时允许换序）
/// @return 匹配成功时返回 true
bool matchBinary(Value * value, IRInstOperator op, Value * lhs, int32_t rhsConst, bool commutative)
{
    auto * binary = dynamic_cast<BinaryInst *>(value);
    if (!binary || binary->getOp() != op) {
        return false;
    }
    auto rhsVal = asConstInt(binary->getRHS());
    if (binary->getLHS() == lhs && rhsVal && *rhsVal == rhsConst) {
        return true;
    }
    if (commutative) {
        auto lhsVal = asConstInt(binary->getLHS());
        if (binary->getRHS() == lhs && lhsVal && *lhsVal == rhsConst) {
            return true;
        }
    }
    return false;
}

/// @brief 判断 value 是否为 phi 的"自加倍增"（phi + phi），等价于 phi * 2
///
/// 位权累加器常写作 power=power*2，也常写作 power=power+power，二者语义相同
/// @return 形如 phi+phi 时返回 true
bool matchSelfDouble(Value * value, Value * phi)
{
    auto * binary = dynamic_cast<BinaryInst *>(value);
    return binary != nullptr && binary->getOp() == IRInstOperator::IRINST_OP_ADD_I &&
           binary->getLHS() == phi && binary->getRHS() == phi;
}

/// @brief 判断指令类型是否允许出现在位模拟循环体内
///
/// 仅允许纯计算与控制流指令，出现 load/store/call 等一律拒绝，
/// 保证循环体无副作用、可被守卫跳过
bool isAllowedBodyInstruction(Instruction * inst)
{
    if (dynamic_cast<PhiInst *>(inst) || dynamic_cast<ICmpInst *>(inst) || dynamic_cast<ZExtInst *>(inst) ||
        dynamic_cast<SelectInst *>(inst) || dynamic_cast<BranchInst *>(inst) ||
        dynamic_cast<CondBranchInst *>(inst)) {
        return true;
    }
    if (auto * binary = dynamic_cast<BinaryInst *>(inst)) {
        switch (binary->getOp()) {
            case IRInstOperator::IRINST_OP_ADD_I:
            case IRInstOperator::IRINST_OP_SUB_I:
            case IRInstOperator::IRINST_OP_MUL_I:
            case IRInstOperator::IRINST_OP_DIV_I:
            case IRInstOperator::IRINST_OP_MOD_I:
                return true;
            default:
                return false;
        }
    }
    return false;
}

/// @brief 获取基本块的后继列表（按终结指令解析）
std::vector<BasicBlock *> blockSuccessors(BasicBlock * bb)
{
    std::vector<BasicBlock *> succs;
    if (!bb || bb->getInstructions().empty()) {
        return succs;
    }
    auto * term = bb->getInstructions().back();
    if (auto * branch = dynamic_cast<BranchInst *>(term)) {
        succs.push_back(branch->getTarget());
    } else if (auto * condBranch = dynamic_cast<CondBranchInst *>(term)) {
        succs.push_back(condBranch->getTrueDest());
        succs.push_back(condBranch->getFalseDest());
    }
    return succs;
}

/// @brief 给定各位取值与累加器/位权的哨兵值，抽象求值得到回边上的 res_next
///
/// 从循环体入口沿可判定的分支单路径执行，初始已知值为 srem 结果（即各位）、累加器
/// 与位权的哨兵值及常量，再沿途对 icmp、zext、select 及整型四则运算做常量折叠
/// （算术按 i32 补码回绕语义，与目标硬件一致），从而推导任意算术/分支写法下的
/// res_next；其余指令视为不透明。phi 按实际到达边取值，遇到无法判定的分支条件或
/// 访问到未知值的关键位置即失败。调用方据 res_next 与哨兵值反解每位累加系数
/// @param shape 已部分匹配的循环要素
/// @param bitA a 的当前位取值（0/1）
/// @param bitB b 的当前位取值（0/1）
/// @param resSentinel 代入累加器 res 的哨兵值
/// @param powSentinel 代入位权 pow 的哨兵值
/// @return res_next 的具体取值，失败时返回 nullopt
std::optional<int32_t> evaluateResNext(const BitLoopShape & shape, int32_t bitA, int32_t bitB,
                                       int32_t resSentinel, int32_t powSentinel)
{
    std::unordered_map<Value *, int32_t> env;
    env[shape.sremA] = bitA;
    if (shape.sremB) {
        env[shape.sremB] = bitB;
    }
    env[shape.resPhi] = resSentinel;
    env[shape.powPhi] = powSentinel;

    auto resolve = [&env](Value * value) -> std::optional<int32_t> {
        if (auto constVal = asConstInt(value)) {
            return constVal;
        }
        auto found = env.find(value);
        if (found != env.end()) {
            return found->second;
        }
        return std::nullopt;
    };

    BasicBlock * prev = shape.header;
    BasicBlock * cur = shape.bodyEntry;
    int32_t visited = 0;

    while (cur != nullptr && visited < 8) {
        ++visited;
        BasicBlock * next = nullptr;

        for (auto * inst : cur->getInstructions()) {
            if (auto * phi = dynamic_cast<PhiInst *>(inst)) {
                Value * incoming = nullptr;
                for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
                    if (phi->getIncomingBlock(i) == prev) {
                        incoming = phi->getIncomingValue(i);
                        break;
                    }
                }
                if (incoming) {
                    if (auto val = resolve(incoming)) {
                        env[phi] = *val;
                    }
                }
                continue;
            }

            if (auto * icmp = dynamic_cast<ICmpInst *>(inst)) {
                auto lhs = resolve(icmp->getLHS());
                auto rhs = resolve(icmp->getRHS());
                if (lhs && rhs) {
                    bool result = false;
                    switch (icmp->getOp()) {
                        case IRInstOperator::IRINST_OP_EQ_I:
                            result = *lhs == *rhs;
                            break;
                        case IRInstOperator::IRINST_OP_NE_I:
                            result = *lhs != *rhs;
                            break;
                        case IRInstOperator::IRINST_OP_LT_I:
                            result = *lhs < *rhs;
                            break;
                        case IRInstOperator::IRINST_OP_GT_I:
                            result = *lhs > *rhs;
                            break;
                        case IRInstOperator::IRINST_OP_LE_I:
                            result = *lhs <= *rhs;
                            break;
                        case IRInstOperator::IRINST_OP_GE_I:
                            result = *lhs >= *rhs;
                            break;
                        default:
                            continue;
                    }
                    env[icmp] = result ? 1 : 0;
                }
                continue;
            }

            if (auto * zext = dynamic_cast<ZExtInst *>(inst)) {
                if (auto val = resolve(zext->getSource())) {
                    env[zext] = *val;
                }
                continue;
            }

            if (auto * select = dynamic_cast<SelectInst *>(inst)) {
                auto cond = resolve(select->getCondition());
                if (cond) {
                    auto chosen = resolve(*cond != 0 ? select->getTrueValue() : select->getFalseValue());
                    if (chosen) {
                        env[select] = *chosen;
                    }
                }
                continue;
            }

            if (auto * branch = dynamic_cast<BranchInst *>(inst)) {
                next = branch->getTarget();
                continue;
            }

            if (auto * condBranch = dynamic_cast<CondBranchInst *>(inst)) {
                auto cond = resolve(condBranch->getCondition());
                if (!cond) {
                    return std::nullopt;
                }
                next = *cond != 0 ? condBranch->getTrueDest() : condBranch->getFalseDest();
                continue;
            }

            // 两操作数均已知的整型四则运算：折叠求值，使谓词可含位的算术组合
            // （如 bit_a+bit_b==2 表与）。加减乘按无符号回绕 = i32 补码语义，避免 C++ 有符号溢出 UB；
            // 除/模避开除零与 INT_MIN/-1 溢出，无法确定时留作不透明
            if (auto * binary = dynamic_cast<BinaryInst *>(inst)) {
                auto lhs = resolve(binary->getLHS());
                auto rhs = resolve(binary->getRHS());
                if (lhs && rhs) {
                    auto lu = static_cast<uint32_t>(*lhs);
                    auto ru = static_cast<uint32_t>(*rhs);
                    std::optional<int32_t> folded;
                    switch (binary->getOp()) {
                        case IRInstOperator::IRINST_OP_ADD_I:
                            folded = static_cast<int32_t>(lu + ru);
                            break;
                        case IRInstOperator::IRINST_OP_SUB_I:
                            folded = static_cast<int32_t>(lu - ru);
                            break;
                        case IRInstOperator::IRINST_OP_MUL_I:
                            folded = static_cast<int32_t>(lu * ru);
                            break;
                        case IRInstOperator::IRINST_OP_DIV_I:
                            if (*rhs != 0 && !(*lhs == INT32_MIN && *rhs == -1)) {
                                folded = *lhs / *rhs;
                            }
                            break;
                        case IRInstOperator::IRINST_OP_MOD_I:
                            if (*rhs != 0 && !(*lhs == INT32_MIN && *rhs == -1)) {
                                folded = *lhs % *rhs;
                            }
                            break;
                        default:
                            break;
                    }
                    if (folded) {
                        env[binary] = *folded;
                    }
                }
                continue;
            }

            // 其余白名单指令视为不透明，不参与求值
        }

        if (next == shape.header) {
            break;
        }
        if (!next || shape.body.find(next) == shape.body.end()) {
            return std::nullopt;
        }
        prev = cur;
        cur = next;
    }

    return resolve(shape.resNext);
}

/// @brief 根据 (bitA,bitB) 四种组合的真值表判定对应的按位操作码
/// @param table 下标为 bitA*2+bitB 的真值表
/// @return 对应的按位操作码，不匹配时返回 IRINST_OP_MAX
IRInstOperator classifyTruthTable(const int32_t table[4])
{
    if (table[0] == 0 && table[1] == 0 && table[2] == 0 && table[3] == 1) {
        return IRInstOperator::IRINST_OP_AND_I;
    }
    if (table[0] == 0 && table[1] == 1 && table[2] == 1 && table[3] == 1) {
        return IRInstOperator::IRINST_OP_OR_I;
    }
    if (table[0] == 0 && table[1] == 1 && table[2] == 1 && table[3] == 0) {
        return IRInstOperator::IRINST_OP_XOR_I;
    }
    return IRInstOperator::IRINST_OP_MAX;
}

/// @brief 取 phi 在指定前驱边上的 incoming 值，不存在则返回 nullptr
Value * phiIncomingFrom(PhiInst * phi, BasicBlock * block)
{
    for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
        if (phi->getIncomingBlock(i) == block) {
            return phi->getIncomingValue(i);
        }
    }
    return nullptr;
}

/// @brief 识别器：确认有界位迭代循环的骨架并按递推形态为 header 的 phi 标定角色
///
/// 识别结构而非语义：唯一的 preheader/latch、基于计数器与 0 比较的 header
/// 退出分支、以及 5 个各司其职的循环携带 phi——
/// 向下计数器（init N∈[1,32]、步进 -1，给出 ≤ 字长的有界迭代次数）、
/// 位权累加器（init 1、步进 ×2，即第 k 位权重 2^k）、
/// 结果累加器（init 0、经 select 累加位权）、一至两个位源（init 为不变量、步进 /2，
/// 配合体内 %2 实现自低位起的逐位提取，分别对应单/双操作数位运算）。
/// 同时确认结果累加器的 select 递推形态
/// 为 select(c, res+pow, res)（或两臂互换）。仅确认结构，不解释具体按位语义
/// @param shape 输出已标定角色的循环要素
/// @return 结构匹配成功返回 true
bool recognizeBitLoopSkeleton(BasicBlock * header, LoopInfo & loopInfo, BitLoopShape & shape)
{
    const auto * loopBody = loopInfo.getLoopBody(header);
    if (!loopBody || loopBody->empty() || loopBody->size() > 8) {
        return false;
    }

    shape.header = header;
    shape.body = *loopBody;
    shape.body.insert(header);

    // preheader：唯一的循环外前驱，且以无条件跳转结尾；改写后另将慢路径位权倍增
    // 规范成左移，防止 CanonicalizeLoop 新建 preheader 后再次匹配同一循环
    BasicBlock * preheader = nullptr;
    BasicBlock * latch = nullptr;
    for (auto * pred : header->getPredecessors()) {
        if (shape.body.find(pred) != shape.body.end()) {
            if (latch) {
                return false;
            }
            latch = pred;
        } else {
            if (preheader) {
                return false;
            }
            preheader = pred;
        }
    }
    if (!preheader || !latch || !dynamic_cast<BranchInst *>(preheader->getTerminator()) ||
        !dynamic_cast<BranchInst *>(latch->getTerminator())) {
        return false;
    }
    shape.preheader = preheader;
    shape.latch = latch;

    // header 终结：基于 len 与 0 的比较决定继续/退出
    auto * headerBranch = dynamic_cast<CondBranchInst *>(header->getTerminator());
    if (!headerBranch) {
        return false;
    }
    auto * headerCmp = dynamic_cast<ICmpInst *>(headerBranch->getCondition());
    if (!headerCmp) {
        return false;
    }

    // 收集 header 的循环携带 phi（角色由递推形态判定，数量随操作数个数为 4 或 5）
    std::vector<PhiInst *> phis;
    for (auto * inst : header->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        if (phi->getIncomingCount() != 2) {
            return false;
        }
        phis.push_back(phi);
    }

    // 按 latch 递推形态为每个 phi 标定角色，任一 phi 不符合任何角色即判定不匹配
    std::vector<PhiInst *> divPhis;
    for (auto * phi : phis) {
        Value * init = phiIncomingFrom(phi, preheader);
        Value * next = phiIncomingFrom(phi, latch);
        if (!init || !next) {
            return false;
        }

        auto initConst = asConstInt(init);
        // 计数器：倒数 (init N∈[1,32]、步进 -1) 或升序 (init 0、步进 +1)，二者都只用于
        // 确定 ≤ 字长的有界圈数 N；升序的位宽 N 在后续 header 比较的上界处确定
        if (initConst && !shape.lenPhi) {
            if (*initConst >= 1 && *initConst <= 32 &&
                matchBinary(next, IRInstOperator::IRINST_OP_SUB_I, phi, 1, false)) {
                shape.width = *initConst;
                shape.countsUp = false;
                shape.lenPhi = phi;
                continue;
            }
            if (*initConst == 0 && matchBinary(next, IRInstOperator::IRINST_OP_ADD_I, phi, 1, true)) {
                shape.countsUp = true;
                shape.lenPhi = phi;
                continue;
            }
        }
        if (initConst && *initConst == 1 &&
            (matchBinary(next, IRInstOperator::IRINST_OP_MUL_I, phi, 2, true) || matchSelfDouble(next, phi)) &&
            !shape.powPhi) {
            shape.powPhi = phi;
            continue;
        }
        if (matchBinary(next, IRInstOperator::IRINST_OP_DIV_I, phi, 2, false)) {
            divPhis.push_back(phi);
            continue;
        }
        // 结果累加器：init 0 的兜底角色（计数器/位权/位源已在上方分流）。其回边值
        // res_next 的具体形态（select / 嵌套 select / 无分支算术累加）留待刻画阶段
        // 用结构校验 + 抽象求值确认其等价于 res + pow*f(bits)
        if (initConst && *initConst == 0 && !shape.resPhi) {
            shape.resPhi = phi;
            shape.resNext = next;
            continue;
        }
        return false;
    }
    // 必须含计数器/位权/结果累加器，及 1~2 个位源（对应单/双操作数位运算）
    if (!shape.lenPhi || !shape.powPhi || !shape.resPhi || divPhis.empty() || divPhis.size() > 2) {
        return false;
    }
    shape.aPhi = divPhis[0];
    shape.aInit = phiIncomingFrom(shape.aPhi, preheader);
    if (divPhis.size() == 2) {
        shape.bPhi = divPhis[1];
        shape.bInit = phiIncomingFrom(shape.bPhi, preheader);
    }

    // header 比较：倒数计数器与 0 比较，升序计数器与上界 N(∈[1,32]) 比较，
    // 据比较谓词区分继续/退出分支（继续分支即循环体入口）
    BasicBlock * trueDest = headerBranch->getTrueDest();
    BasicBlock * falseDest = headerBranch->getFalseDest();
    Value * cmpLhs = headerCmp->getLHS();
    Value * cmpRhs = headerCmp->getRHS();
    auto rhsConst = asConstInt(cmpRhs);
    if (cmpLhs != shape.lenPhi || !rhsConst) {
        return false;
    }
    BasicBlock * bodyEntry = nullptr;
    BasicBlock * exit = nullptr;
    if (!shape.countsUp) {
        // 倒数：len 与 0 比较，len!=0 / len>0 继续，len==0 退出
        if (*rhsConst != 0) {
            return false;
        }
        switch (headerCmp->getOp()) {
            case IRInstOperator::IRINST_OP_NE_I:
            case IRInstOperator::IRINST_OP_GT_I:
                bodyEntry = trueDest;
                exit = falseDest;
                break;
            case IRInstOperator::IRINST_OP_EQ_I:
                bodyEntry = falseDest;
                exit = trueDest;
                break;
            default:
                return false;
        }
    } else {
        // 升序：i 与上界 N 比较，i<N / i!=N 继续，i>=N / i==N 退出；位宽即 N
        if (*rhsConst < 1 || *rhsConst > 32) {
            return false;
        }
        shape.width = *rhsConst;
        switch (headerCmp->getOp()) {
            case IRInstOperator::IRINST_OP_LT_I:
            case IRInstOperator::IRINST_OP_NE_I:
                bodyEntry = trueDest;
                exit = falseDest;
                break;
            case IRInstOperator::IRINST_OP_GE_I:
            case IRInstOperator::IRINST_OP_EQ_I:
                bodyEntry = falseDest;
                exit = trueDest;
                break;
            default:
                return false;
        }
    }
    if (shape.body.find(bodyEntry) == shape.body.end() || shape.body.find(exit) != shape.body.end()) {
        return false;
    }
    shape.bodyEntry = bodyEntry;
    shape.exit = exit;

    // exit 的前驱只能是 header（保证唯一退出边，便于插入合流 phi）
    for (auto * pred : exit->getPredecessors()) {
        if (pred != header) {
            return false;
        }
    }

    return true;
}

/// @brief 识别器：校验循环体为无副作用的纯计算并定位逐位提取器
///
/// 要求循环体只含白名单纯指令（无 load/store/call），定位两个位源的 %2
/// 提取指令，并确认除结果累加器外循环内定义的值不流出循环——这三项共同
/// 保证整段循环可被守卫安全跳过
/// @param shape 已完成骨架识别的循环要素，输出补全 sremA/sremB
/// @return 校验通过返回 true
bool validateBitLoopBody(BitLoopShape & shape)
{
    // 循环体指令白名单 + 副作用检查 + 定位 srem
    for (auto * bb : shape.body) {
        for (auto * inst : bb->getInstructions()) {
            if (!isAllowedBodyInstruction(inst)) {
                return false;
            }
            if (matchBinary(inst, IRInstOperator::IRINST_OP_MOD_I, shape.aPhi, 2, false)) {
                shape.sremA = inst;
            } else if (shape.bPhi && matchBinary(inst, IRInstOperator::IRINST_OP_MOD_I, shape.bPhi, 2, false)) {
                shape.sremB = inst;
            }
        }
    }
    // 单操作数循环没有 bPhi，故只在存在 bPhi 时才要求 sremB
    if (!shape.sremA || (shape.bPhi && !shape.sremB)) {
        return false;
    }

    // 除 resPhi 外，循环内定义的值不得在循环外被使用
    for (auto * bb : shape.body) {
        for (auto * inst : bb->getInstructions()) {
            if (inst == shape.resPhi) {
                continue;
            }
            for (auto * use : inst->getUseList()) {
                auto * userInst = dynamic_cast<Instruction *>(use->getUser());
                if (userInst && userInst->getParentBlock() &&
                    shape.body.find(userInst->getParentBlock()) == shape.body.end()) {
                    return false;
                }
            }
        }
    }

    return true;
}

/// @brief 判断 value 在循环体内是否（传递地）读取结果累加器 resPhi
///
/// 用于校验 res_next 中 res 只以"res + delta"的加性位置出现——delta 不得依赖 res。
/// 在 header phi 处停止递归（计数器/位权/位源都不是累加器，且避免 phi 环）
bool dependsOnRes(Value * value, PhiInst * resPhi, std::unordered_set<Value *> & visited)
{
    if (value == resPhi) {
        return true;
    }
    auto * inst = dynamic_cast<Instruction *>(value);
    if (inst == nullptr || dynamic_cast<PhiInst *>(inst) != nullptr) {
        return false;
    }
    if (!visited.insert(value).second) {
        return false;
    }
    for (Value * operand : inst->getOperandsValue()) {
        if (dependsOnRes(operand, resPhi, visited)) {
            return true;
        }
    }
    return false;
}

/// @brief 结构性校验 res_next 形如 res + delta（delta 不依赖 res），含 select 树与
///        内层合流 phi（嵌套 if 的结果）
///
/// 保证累加器只在加性位置出现，res 绝不进入 mul/div/mod 等非加性运算，从而排除
/// 形如 (res+pow*bit)%M 这类哨兵采样无法证伪、对真实大 res 不等价的不健全写法。
/// visited 防御性地阻断异常 IR 中的环（重访即保守判否，至多漏优化、不会误判）
bool isResAdditive(Value * value, PhiInst * resPhi, std::unordered_set<Value *> & visited)
{
    if (value == resPhi) {
        return true;
    }
    if (!visited.insert(value).second) {
        return false;
    }
    if (auto * phi = dynamic_cast<PhiInst *>(value)) {
        // resPhi 已在上方放行；header 上的其它循环携带 phi（计数器/位权/位源）不得
        // 出现在加性位置。仅内层合流 phi（非 header）按各 incoming 递归判定
        if (phi->getParentBlock() == resPhi->getParentBlock() || phi->getIncomingCount() == 0) {
            return false;
        }
        for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
            if (!isResAdditive(phi->getIncomingValue(i), resPhi, visited)) {
                return false;
            }
        }
        return true;
    }
    if (auto * select = dynamic_cast<SelectInst *>(value)) {
        return isResAdditive(select->getTrueValue(), resPhi, visited) &&
               isResAdditive(select->getFalseValue(), resPhi, visited);
    }
    if (auto * binary = dynamic_cast<BinaryInst *>(value);
        binary != nullptr && binary->getOp() == IRInstOperator::IRINST_OP_ADD_I) {
        auto resFree = [resPhi](Value * v) {
            std::unordered_set<Value *> seen;
            return !dependsOnRes(v, resPhi, seen);
        };
        if (isResAdditive(binary->getLHS(), resPhi, visited) && resFree(binary->getRHS())) {
            return true;
        }
        if (isResAdditive(binary->getRHS(), resPhi, visited) && resFree(binary->getLHS())) {
            return true;
        }
    }
    return false;
}

/// @brief 求 (bitA,bitB) 组合下每位累加系数：res_next 应等于 res + pow*coef，coef∈{0,1}
///
/// 固定累加器哨兵、变动位权哨兵两次求值，校验 delta=res_next-res 关于 pow 线性
/// （delta(pow=2)==2*delta(pow=1)），再取 coef=delta(pow=1) 并要求落于 {0,1}。
/// 配合 isResAdditive 的 res-加性结构校验，共同保证等价于真正的逐位累加
/// @return 累加系数 0/1，不满足时返回 nullopt
std::optional<int32_t> bitCoef(const BitLoopShape & shape, int32_t bitA, int32_t bitB)
{
    constexpr int32_t RES_SENTINEL = 1000;
    auto v1 = evaluateResNext(shape, bitA, bitB, RES_SENTINEL, 1);
    auto v2 = evaluateResNext(shape, bitA, bitB, RES_SENTINEL, 2);
    if (!v1 || !v2) {
        return std::nullopt;
    }
	const int64_t d1 = static_cast<int64_t>(*v1) - RES_SENTINEL;
	const int64_t d2 = static_cast<int64_t>(*v2) - RES_SENTINEL;
    if (d2 != 2 * d1 || (d1 != 0 && d1 != 1)) {
        return std::nullopt;
    }
	return static_cast<int32_t>(d1);
}

/// @brief 刻画器+综合器：抽象解释 res_next 求每位累加系数，综合为原生按位操作
///
/// 刻画：先结构校验 res_next 为 res-加性，再在有限 bit 域上对各位输入组合用哨兵
/// 代入求 res_next、反解每位累加系数（0/1），得真值表。综合：据真值表综合出等价
/// 的原生按位运算。这是本优化的原理核——不硬编码具体累加/谓词形态，而是从循环体
/// 推导其按位语义，故对 select(c,res+pow,res)、嵌套 select、无分支算术累加
/// res+pow*expr 等任意等价写法都成立。
/// - 双操作数：真值表 4 项 → and/or/xor，快速路径 op(aInit,bInit)
/// - 单操作数：真值表 2 项 → 恒等(a&mask=a)/取反(a^mask)/恒0(a&0)/恒满(a|mask)，
///   守卫保证 aInit∈[0,2^width)，掩码为低 width 位全 1，快速路径 op(aInit,常量)
/// @param shape 已通过结构与循环体校验的循环要素，输出补全 bitOp 与 fastRhsConst
/// @return 真值表对应某个原生按位运算时返回 true
bool characterizeBitwiseOp(BitLoopShape & shape)
{
    // res_next 必须为 res-加性（res 只在加性位置），否则哨兵采样不足以证明等价
    std::unordered_set<Value *> additiveVisited;
    if (!isResAdditive(shape.resNext, shape.resPhi, additiveVisited)) {
        return false;
    }

    // 单操作数：系数表只含 f(0)、f(1)，综合为对掩码/0 的原生运算
    if (shape.bPhi == nullptr) {
        int32_t bit[2] = {0, 0};
        for (int32_t bitA = 0; bitA <= 1; ++bitA) {
            auto coef = bitCoef(shape, bitA, 0);
            if (!coef) {
                return false;
            }
            bit[bitA] = *coef;
        }
        int32_t mask = shape.width >= 32 ? -1 : static_cast<int32_t>((1u << shape.width) - 1u);
        if (bit[0] == 0 && bit[1] == 1) {
            shape.bitOp = IRInstOperator::IRINST_OP_AND_I; // 恒等：res = a & mask = a
            shape.fastRhsConst = mask;
        } else if (bit[0] == 1 && bit[1] == 0) {
            shape.bitOp = IRInstOperator::IRINST_OP_XOR_I; // 取反：res = a ^ mask
            shape.fastRhsConst = mask;
        } else if (bit[0] == 0 && bit[1] == 0) {
            shape.bitOp = IRInstOperator::IRINST_OP_AND_I; // 恒 0：res = a & 0
            shape.fastRhsConst = 0;
        } else {
            shape.bitOp = IRInstOperator::IRINST_OP_OR_I; // 恒满：res = a | mask = mask
            shape.fastRhsConst = mask;
        }
        return true;
    }

    int32_t table[4] = {0, 0, 0, 0};
    for (int32_t bitA = 0; bitA <= 1; ++bitA) {
        for (int32_t bitB = 0; bitB <= 1; ++bitB) {
            auto coef = bitCoef(shape, bitA, bitB);
            if (!coef) {
                return false;
            }
            table[bitA * 2 + bitB] = *coef;
        }
    }
    shape.bitOp = classifyTruthTable(table);
    return shape.bitOp != IRInstOperator::IRINST_OP_MAX;
}

/// @brief 在单个循环头上运行识别→刻画→综合流水线，判定其是否为位模拟循环
///
/// 依次执行：① 识别器确认骨架与各 phi 角色；② 识别器校验循环体纯净并定位
/// 逐位提取器；③ 刻画器抽象解释循环体得真值表、综合器据此定出原生按位操作。
/// 任一阶段失败即判定不匹配。守卫与快速路径的改写由 rewriteBitLoop 负责
/// @param shape 输出匹配结果
/// @return 匹配成功返回 true
bool matchBitLoop(BasicBlock * header, LoopInfo & loopInfo, BitLoopShape & shape)
{
    return recognizeBitLoopSkeleton(header, loopInfo, shape) && validateBitLoopBody(shape) &&
           characterizeBitwiseOp(shape);
}

/// @brief 对匹配成功的循环插入非负守卫与原生按位指令快速路径
/// @return 改写成功返回 true
bool rewriteBitLoop(Function * func, Module * mod, const BitLoopShape & shape)
{
    Type * i32Type = IntegerType::getTypeInt32();
    Type * i1Type = IntegerType::getTypeInt1();

    auto & preheaderInsts = shape.preheader->getInstructions();
    auto * oldBranch = dynamic_cast<BranchInst *>(shape.preheader->getTerminator());
    if (!oldBranch) {
        return false;
    }
	auto & latchInsts = shape.latch->getInstructions();
	auto * powNext = dynamic_cast<Instruction *>(phiIncomingFrom(shape.powPhi, shape.latch));
	auto powNextPos = std::find(latchInsts.begin(), latchInsts.end(), powNext);
	if (!powNext || powNextPos == latchInsts.end()) {
		return false;
	}

    // 守卫保证逐位模拟与原生按位运算严格等价：循环只计算低 width 位，原生 32 位
    // 指令计算全部 32 位，二者相等当且仅当各操作数第 width..31 位上 f 恒为 0，
    // 对 and/or/xor 与单操作数恒等/取反即要求每个操作数 0 <= x < 2^width，
    // 等价地 guardVal >= 0 且（width<=30 时）guardVal < 2^width；其中 guardVal
    // 双操作数取 a0|b0、单操作数取 a0。width>=31 时非负整数本就 < 2^width，退化为 guardVal>=0
    Value * guardVal = shape.aInit;
    if (shape.bInit) {
        // 两非负数按位或仍非负且 < 2^width ⟺ 各自落在 [0,2^width)
        auto * orInst = new BinaryInst(func, IRInstOperator::IRINST_OP_OR_I, shape.aInit, shape.bInit, i32Type);
        orInst->setParentBlock(shape.preheader);
        preheaderInsts.insert(std::prev(preheaderInsts.end()), orInst);
        guardVal = orInst;
    }
    auto * geCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_GE_I, guardVal, mod->newConstInt32(0), i1Type);
    geCmp->setParentBlock(shape.preheader);
    preheaderInsts.insert(std::prev(preheaderInsts.end()), geCmp);

    Value * guard = geCmp;
    if (shape.width <= 30) {
        // 1<<width 在 width<=30 时不超过 2^30，绝不溢出 i32
        auto * ltCmp = new ICmpInst(func,
                                    IRInstOperator::IRINST_OP_LT_I,
                                    guardVal,
                                    mod->newConstInt32(1 << shape.width),
                                    i1Type);
        ltCmp->setParentBlock(shape.preheader);
        preheaderInsts.insert(std::prev(preheaderInsts.end()), ltCmp);
        // 逻辑与（短路语义）：geCmp 为真时取 ltCmp，否则恒假，避免引入无符号比较
        auto * andGuard = new SelectInst(func, geCmp, ltCmp, mod->newConstInt1(0), i1Type);
        andGuard->setParentBlock(shape.preheader);
        preheaderInsts.insert(std::prev(preheaderInsts.end()), andGuard);
        guard = andGuard;
    }

    // 快速路径块：直接用原生按位指令计算结果后跳到 exit
    // 双操作数右操作数取 bInit，单操作数取综合出的常量（掩码或 0）
    auto * fastBB = func->newBasicBlock();
    Value * fastRhs =
        shape.fastRhsConst ? static_cast<Value *>(mod->newConstInt32(*shape.fastRhsConst)) : shape.bInit;
    auto * fastOp = new BinaryInst(func, shape.bitOp, shape.aInit, fastRhs, i32Type);
    fastOp->setParentBlock(fastBB);
    fastBB->addInstruction(fastOp);
    auto * fastBr = new BranchInst(func, shape.exit);
    fastBr->setParentBlock(fastBB);
    fastBB->addInstruction(fastBr);

    // preheader 终结改为条件跳转：非负走快速路径，否则进入原循环
    auto branchPos = std::find(preheaderInsts.begin(), preheaderInsts.end(), static_cast<Instruction *>(oldBranch));
    if (branchPos == preheaderInsts.end()) {
        return false;
    }
    preheaderInsts.erase(branchPos);
    oldBranch->clearOperands();
    delete oldBranch;
    auto * guardBranch = new CondBranchInst(func, guard, fastBB, shape.header);
    guardBranch->setParentBlock(shape.preheader);
    preheaderInsts.push_back(guardBranch);

    shape.preheader->addSuccessor(fastBB);
    fastBB->addPredecessor(shape.preheader);
    fastBB->addSuccessor(shape.exit);
    shape.exit->addPredecessor(fastBB);

    // exit 中已有的 phi 需补充来自 fastBB 的 incoming
    for (auto * inst : shape.exit->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        Value * fromHeader = nullptr;
        for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
            if (phi->getIncomingBlock(i) == shape.header) {
                fromHeader = phi->getIncomingValue(i);
                break;
            }
        }
        phi->addIncoming(fromHeader == shape.resPhi ? static_cast<Value *>(fastOp) : fromHeader, fastBB);
    }

    // 合流 phi：循环路径取 resPhi，快速路径取 fastOp，替换 resPhi 的全部循环外使用
    std::vector<Use *> outsideUses;
    for (auto * use : shape.resPhi->getUseList()) {
        auto * userInst = dynamic_cast<Instruction *>(use->getUser());
        if (userInst && userInst->getParentBlock() &&
            shape.body.find(userInst->getParentBlock()) == shape.body.end() &&
            userInst->getParentBlock() != fastBB) {
            outsideUses.push_back(use);
        }
    }
    if (!outsideUses.empty()) {
        auto * mergePhi = new PhiInst(func, i32Type);
        mergePhi->setParentBlock(shape.exit);
        auto & exitInsts = shape.exit->getInstructions();
        exitInsts.insert(exitInsts.begin(), mergePhi);
        mergePhi->addIncoming(shape.resPhi, shape.header);
        mergePhi->addIncoming(fastOp, fastBB);
        for (auto * use : outsideUses) {
            if (use->getUser() != static_cast<User *>(mergePhi)) {
                use->setUsee(mergePhi);
            }
        }
    }

	// 把慢路径的位权倍增规范成左移，避免后续 CanonicalizeLoop 新建 preheader 后重复匹配
	auto * shiftPow = new BinaryInst(func,
	                                 IRInstOperator::IRINST_OP_SHL_I,
	                                 shape.powPhi,
	                                 mod->newConstInt32(1),
	                                 i32Type);
	shiftPow->setParentBlock(shape.latch);
	latchInsts.insert(powNextPos, shiftPow);
	powNext->replaceAllUseWith(shiftPow);
	powNext->clearOperands();
	powNext->setDead(true);

    return true;
}

} // namespace

bool BoundedBitLoopSolver::run()
{
    if (!func || !mod || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    bool changed = false;
    while (true) {
        DominatorTree domTree(func);
        LoopInfo loopInfo(func, &domTree);

        bool localChanged = false;
        for (auto * header : func->getBlocks()) {
            if (!loopInfo.isLoopHeader(header)) {
                continue;
            }
            BitLoopShape shape;
            if (!matchBitLoop(header, loopInfo, shape)) {
                continue;
            }
            if (rewriteBitLoop(func, mod, shape)) {
                localChanged = true;
                changed = true;
                break;
            }
        }

        if (!localChanged) {
            break;
        }
    }

    if (changed) {
        func->getAnalysisCache().invalidateCFGAnalyses();
    }
    return changed;
}
