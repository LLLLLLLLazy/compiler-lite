///
/// @file BitwiseLoopIdiom.cpp
/// @brief 位模拟循环惯用法识别 pass 实现
///
/// 匹配的循环形态（SSA、select 化之后）：
///   header: a=phi(a0,a/2) b=phi(b0,b/2) len=phi(32,len-1)
///           res=phi(0,sel) pow=phi(1,pow*2); br (len!=0) body, exit
///   body:   bit_a=srem a,2; bit_b=srem b,2; 谓词计算;
///           sel=select(cond, res+pow, res); ...
/// 谓词与 (bit_a,bit_b) 的真值表对应 and/or/xor 时，
/// 在 preheader 插入守卫：(a0|b0)>=0 时直接用原生按位指令得到结果，
/// 否则仍执行原循环。两个操作数非负时逐位模拟与原生指令严格等价
/// （srem 结果为 0/1、sdiv 等价右移、符号位恒 0 不触发累加），
/// 负数路径保留原循环，因此任意输入下语义不变
///

#include "BitwiseLoopIdiom.h"

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
    SelectInst * resSelect = nullptr;
    bool addOnTrue = true;

    Value * aInit = nullptr;
    Value * bInit = nullptr;

    IRInstOperator bitOp = IRInstOperator::IRINST_OP_MAX;
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

/// @brief 对单次迭代做抽象求值，求出给定 (bitA,bitB) 下 res 累加条件的取值
///
/// 从循环体入口沿可判定的分支单路径执行，已知值只有 srem 结果与常量，
/// 其余指令视为不透明。phi 按实际到达边取值，遇到无法判定的分支条件
/// 或访问到未知值的关键位置即失败
/// @param shape 已部分匹配的循环要素
/// @param bitA a 的当前位取值（0/1）
/// @param bitB b 的当前位取值（0/1）
/// @return 累加条件值（0/1），失败时返回 nullopt
std::optional<int32_t> evaluateIteration(const BitLoopShape & shape, int32_t bitA, int32_t bitB)
{
    std::unordered_map<Value *, int32_t> env;
    env[shape.sremA] = bitA;
    env[shape.sremB] = bitB;

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

    std::optional<int32_t> condValue;
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
                if (select == shape.resSelect) {
                    if (!cond) {
                        return std::nullopt;
                    }
                    condValue = *cond;
                    continue;
                }
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

    return condValue;
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

/// @brief 匹配单个循环头是否为位模拟循环
/// @param shape 输出匹配结果
/// @return 匹配成功返回 true
bool matchBitLoop(BasicBlock * header, LoopInfo & loopInfo, BitLoopShape & shape)
{
    const auto * loopBody = loopInfo.getLoopBody(header);
    if (!loopBody || loopBody->empty() || loopBody->size() > 8) {
        return false;
    }

    shape.header = header;
    shape.body = *loopBody;
    shape.body.insert(header);

    // preheader：唯一的循环外前驱，且以无条件跳转结尾（改写后变为条件跳转，天然防止重复匹配）
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

    // 收集 header 的 5 个 i32 phi
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
    if (phis.size() != 5) {
        return false;
    }

    auto incomingOf = [](PhiInst * phi, BasicBlock * block) -> Value * {
        for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
            if (phi->getIncomingBlock(i) == block) {
                return phi->getIncomingValue(i);
            }
        }
        return nullptr;
    };

    // 按 latch 递推形态分类 5 个 phi
    std::vector<PhiInst *> divPhis;
    for (auto * phi : phis) {
        Value * init = incomingOf(phi, preheader);
        Value * next = incomingOf(phi, latch);
        if (!init || !next) {
            return false;
        }

        auto initConst = asConstInt(init);
        if (initConst && *initConst == 32 && matchBinary(next, IRInstOperator::IRINST_OP_SUB_I, phi, 1, false) &&
            !shape.lenPhi) {
            shape.lenPhi = phi;
            continue;
        }
        if (initConst && *initConst == 1 && matchBinary(next, IRInstOperator::IRINST_OP_MUL_I, phi, 2, true) &&
            !shape.powPhi) {
            shape.powPhi = phi;
            continue;
        }
        if (auto * select = dynamic_cast<SelectInst *>(next); select && initConst && *initConst == 0 &&
                                                              !shape.resPhi) {
            shape.resPhi = phi;
            shape.resSelect = select;
            continue;
        }
        if (matchBinary(next, IRInstOperator::IRINST_OP_DIV_I, phi, 2, false)) {
            divPhis.push_back(phi);
            continue;
        }
        return false;
    }
    if (!shape.lenPhi || !shape.powPhi || !shape.resPhi || divPhis.size() != 2) {
        return false;
    }
    shape.aPhi = divPhis[0];
    shape.bPhi = divPhis[1];
    shape.aInit = incomingOf(shape.aPhi, preheader);
    shape.bInit = incomingOf(shape.bPhi, preheader);

    // header 比较必须是 len 与 0 的继续/退出判定
    BasicBlock * trueDest = headerBranch->getTrueDest();
    BasicBlock * falseDest = headerBranch->getFalseDest();
    Value * cmpLhs = headerCmp->getLHS();
    Value * cmpRhs = headerCmp->getRHS();
    auto rhsConst = asConstInt(cmpRhs);
    if (cmpLhs != shape.lenPhi || !rhsConst || *rhsConst != 0) {
        return false;
    }
    BasicBlock * bodyEntry = nullptr;
    BasicBlock * exit = nullptr;
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

    // res 的 select 递推形态：select(c, res+pow, res) 或两臂互换
    Value * trueVal = shape.resSelect->getTrueValue();
    Value * falseVal = shape.resSelect->getFalseValue();
    auto isResPlusPow = [&shape](Value * value) {
        auto * binary = dynamic_cast<BinaryInst *>(value);
        if (!binary || binary->getOp() != IRInstOperator::IRINST_OP_ADD_I) {
            return false;
        }
        return (binary->getLHS() == shape.resPhi && binary->getRHS() == shape.powPhi) ||
               (binary->getLHS() == shape.powPhi && binary->getRHS() == shape.resPhi);
    };
    if (isResPlusPow(trueVal) && falseVal == shape.resPhi) {
        shape.addOnTrue = true;
    } else if (isResPlusPow(falseVal) && trueVal == shape.resPhi) {
        shape.addOnTrue = false;
    } else {
        return false;
    }

    // 循环体指令白名单 + 副作用检查 + 定位 srem
    for (auto * bb : shape.body) {
        for (auto * inst : bb->getInstructions()) {
            if (!isAllowedBodyInstruction(inst)) {
                return false;
            }
            if (matchBinary(inst, IRInstOperator::IRINST_OP_MOD_I, shape.aPhi, 2, false)) {
                shape.sremA = inst;
            } else if (matchBinary(inst, IRInstOperator::IRINST_OP_MOD_I, shape.bPhi, 2, false)) {
                shape.sremB = inst;
            }
        }
    }
    if (!shape.sremA || !shape.sremB) {
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

    // 抽象求值得到真值表并判定操作码
    int32_t table[4] = {0, 0, 0, 0};
    for (int32_t bitA = 0; bitA <= 1; ++bitA) {
        for (int32_t bitB = 0; bitB <= 1; ++bitB) {
            auto cond = evaluateIteration(shape, bitA, bitB);
            if (!cond) {
                return false;
            }
            int32_t add = shape.addOnTrue ? *cond : (*cond != 0 ? 0 : 1);
            table[bitA * 2 + bitB] = add != 0 ? 1 : 0;
        }
    }
    shape.bitOp = classifyTruthTable(table);
    return shape.bitOp != IRInstOperator::IRINST_OP_MAX;
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

    // (a0 | b0) >= 0 等价于 a0 与 b0 均非负（符号位按位或）
    auto * orInst = new BinaryInst(func, IRInstOperator::IRINST_OP_OR_I, shape.aInit, shape.bInit, i32Type);
    orInst->setParentBlock(shape.preheader);
    preheaderInsts.insert(std::prev(preheaderInsts.end()), orInst);
    auto * geCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_GE_I, orInst, mod->newConstInt32(0), i1Type);
    geCmp->setParentBlock(shape.preheader);
    preheaderInsts.insert(std::prev(preheaderInsts.end()), geCmp);

    // 快速路径块：直接用原生按位指令计算结果后跳到 exit
    auto * fastBB = func->newBasicBlock();
    auto * fastOp = new BinaryInst(func, shape.bitOp, shape.aInit, shape.bInit, i32Type);
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
    auto * guardBranch = new CondBranchInst(func, geCmp, fastBB, shape.header);
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

    return true;
}

} // namespace

bool BitwiseLoopIdiom::run()
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
