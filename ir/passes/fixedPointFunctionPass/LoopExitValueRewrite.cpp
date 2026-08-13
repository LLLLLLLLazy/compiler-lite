///
/// @file LoopExitValueRewrite.cpp
/// @brief 基于 SCEV 的循环出口值闭式替换 pass 实现
///

#include "LoopExitValueRewrite.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <unordered_set>
#include <vector>

#include "AnalysisCache.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "ConstInteger.h"
#include "DominatorTree.h"
#include "Function.h"
#include "ICmpInst.h"
#include "Instruction.h"
#include "LoopInfo.h"
#include "Module.h"
#include "PhiInst.h"
#include "ScalarEvolution.h"
#include "SelectInst.h"
#include "Type.h"
#include "Use.h"
#include "Value.h"

namespace {

/// @brief 判断值是否定义在循环体内
/// @param value 待判断值
/// @param loopBody 循环体基本块集合
/// @return true 表示该值由循环体内的指令定义
bool isDefinedInLoop(Value * value, const std::unordered_set<BasicBlock *> & loopBody)
{
    auto * inst = dynamic_cast<Instruction *>(value);
    return inst && inst->getParentBlock() && loopBody.find(inst->getParentBlock()) != loopBody.end();
}

/// @brief 判断值是否为循环不变量（非循环体内定义）
/// @param value 待判断值
/// @param loopBody 循环体基本块集合
/// @return true 表示该值在整个循环执行期间保持不变
bool isLoopInvariant(Value * value, const std::unordered_set<BasicBlock *> & loopBody)
{
    return value != nullptr && !isDefinedInLoop(value, loopBody);
}

/// @brief 在基本块的 phi 指令之后插入一条指令
/// @param bb 目标基本块
/// @param inst 待插入指令
void insertAfterPhis(BasicBlock * bb, Instruction * inst)
{
    auto & insts = bb->getInstructions();
    auto insertPos = insts.begin();
    while (insertPos != insts.end() && dynamic_cast<PhiInst *>(*insertPos) != nullptr) {
        ++insertPos;
    }
    inst->setParentBlock(bb);
    insts.insert(insertPos, inst);
}

/// @brief 头部 phi 的递推形态描述
struct RecurrenceShape {
    enum class Kind {
        None,
        Affine,     ///< p_{k+1} = p_k + c
        DivPow2,    ///< p_{k+1} = p_k / 2^shiftPerStep（每步除以同一个 2 的幂常量）
        Quadratic,  ///< p_{k+1} = p_k + a*i + b，其中 i 是另一个仿射 IV
    };

    Kind kind = Kind::None;
    Value * start = nullptr;   ///< 来自 preheader 的初值
    Value * step = nullptr;    ///< 每次迭代增量 c（循环不变量）
    int32_t shiftPerStep = 0;  ///< 每步除数对应的右移位数 log2(divisor)（仅 DivPow2）
    Value * quadIVStart = nullptr; ///< 二次递推中辅助 IV 的初值（仅 Quadratic）
    Value * quadIVStep = nullptr; ///< 二次递推中辅助 IV 的步长（仅 Quadratic）
    Value * quadCoeffA = nullptr; ///< 二次递推的一次项系数 a（仅 Quadratic）
    Value * quadCoeffB = nullptr; ///< 二次递推的常数项系数 b（仅 Quadratic）
};

/// @brief 判断正整数是否为 2 的幂，并返回其 log2
/// @param value 待判断值
/// @param shift 输出：当 value 为 2 的幂时其以 2 为底的对数
/// @return true 表示 value 是大于 1 的 2 的幂
bool isPositivePowerOfTwo(int32_t value, int32_t & shift)
{
    if (value <= 1 || (value & (value - 1)) != 0) {
        return false;
    }
    shift = 0;
    int32_t v = value;
    while ((v & 1) == 0) {
        v >>= 1;
        ++shift;
    }
    return true;
}

/// @brief 在出口块发射 32 位有符号除以 2 的幂的闭式表达式 start sdiv 2^s
///
/// 其中 s = shiftPerStep * trip 为运行时不变量。有符号除法向零取整，需对负数
/// 做偏置修正：q = (n + ((n>>31) >>u (32-s))) >> s。由于 s 可能为 0 或 >= 32，
/// 而 RISC-V 的 W 后缀移位仅取移位量低 5 位，故用 select 处理这两种边界：
///   - s == 0      ：q = n
///   - 0 < s < 32  ：q = (n + bias) ashr s
///   - s >= 32     ：反复向零取整后的结果恒为 0
///
/// @param func 所在函数
/// @param mod 所属模块
/// @param start 被除数 n（循环不变量）
/// @param shiftPerStep 每步右移位数 log2(每步除数)
/// @param trip 循环执行次数
/// @param intType 32 位整型
/// @param boolType 比较结果布尔类型
/// @param appendInst 将新指令追加到出口块的回调
/// @return 闭式结果值
Value * emitSignedDivByPow2(Function * func,
                            Module * mod,
                            Value * start,
                            int32_t shiftPerStep,
                            Value * trip,
                            Type * intType,
                            Type * boolType,
                            const std::function<void(Instruction *)> & appendInst)
{
    auto * shiftPerStepConst = mod->newConstInteger(intType, shiftPerStep);
    auto * zero = mod->newConstInteger(intType, 0);
    const int32_t wideTripThreshold = (32 + shiftPerStep - 1) / shiftPerStep;
    auto * wideTripConst = mod->newConstInteger(intType, wideTripThreshold);
    auto * isWideShift =
        new ICmpInst(func, IRInstOperator::IRINST_OP_GE_I, trip, wideTripConst, boolType);
    appendInst(isWideShift);

    // 宽移位时先把 trip 钳制为零，避免 shiftPerStep * trip 自身回绕
    auto * safeTrip = new SelectInst(func, isWideShift, zero, trip, intType);
    appendInst(safeTrip);
    auto * totalShift =
        new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I, shiftPerStepConst, safeTrip, intType);
    appendInst(totalShift);

    // sign = start ashr 31（非负为 0，负数为 -1）
    auto * c31 = mod->newConstInteger(intType, 31);
    auto * sign = new BinaryInst(func, IRInstOperator::IRINST_OP_ASHR_I, start, c31, intType);
    appendInst(sign);

    // totalShift 为零时用 1 完成无效的辅助计算，最终仍选择原始 start
    auto * isZeroShift =
        new ICmpInst(func, IRInstOperator::IRINST_OP_EQ_I, totalShift, zero, boolType);
    appendInst(isZeroShift);
    auto * one = mod->newConstInteger(intType, 1);
    auto * safeShift = new SelectInst(func, isZeroShift, one, totalShift, intType);
    appendInst(safeShift);

    // bias = sign lshr (32 - safeShift)，仅在 0 < totalShift < 32 时参与结果
    auto * c32 = mod->newConstInteger(intType, 32);
    auto * shiftBack = new BinaryInst(func, IRInstOperator::IRINST_OP_SUB_I, c32, safeShift, intType);
    appendInst(shiftBack);
    auto * bias = new BinaryInst(func, IRInstOperator::IRINST_OP_LSHR_I, sign, shiftBack, intType);
    appendInst(bias);

    // biased = start + bias；qNormal = biased ashr safeShift
    auto * biased = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, start, bias, intType);
    appendInst(biased);
    auto * qNormal =
        new BinaryInst(func, IRInstOperator::IRINST_OP_ASHR_I, biased, safeShift, intType);
    appendInst(qNormal);

    // totalShift == 0 时结果应为 start（移位偏置序列在 s==0 时不成立）
    auto * qNonNeg = new SelectInst(func, isZeroShift, start, qNormal, intType);
    appendInst(qNonNeg);

    // 反复有符号除法向零取整，累计移位达到 32 位时结果恒为 0
    auto * result = new SelectInst(func, isWideShift, zero, qNonNeg, intType);
    appendInst(result);

    return result;
}

/// @brief 识别头部 phi 的递推形态
/// @param phi 头部 phi（恰有两个 incoming：preheader 与 latch）
/// @param preheader 循环前置头
/// @param latch 循环 latch
/// @param loopBody 循环体基本块集合
/// @return 识别到的递推形态，无法识别时 kind 为 None
RecurrenceShape analyzeRecurrence(PhiInst * phi,
                                  BasicBlock * preheader,
                                  BasicBlock * latch,
                                  const std::unordered_set<BasicBlock *> & loopBody,
                                  Module * mod)
{
    RecurrenceShape shape;
    if (!phi || phi->getIncomingCount() != 2 || !phi->getType()->isIntegerType()) {
        return shape;
    }

    Value * startValue = nullptr;
    Value * latchValue = nullptr;
    for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
        if (phi->getIncomingBlock(i) == preheader) {
            startValue = phi->getIncomingValue(i);
        } else if (phi->getIncomingBlock(i) == latch) {
            latchValue = phi->getIncomingValue(i);
        }
    }
    if (!startValue || !latchValue || !isLoopInvariant(startValue, loopBody)) {
        return shape;
    }

    // latchValue 必须由循环体内的二元指令计算
    auto * latchInst = dynamic_cast<BinaryInst *>(latchValue);
    if (!latchInst || !isDefinedInLoop(latchInst, loopBody)) {
        return shape;
    }

    // 形态一：p_next = p + c
    if (latchInst->getOp() == IRInstOperator::IRINST_OP_ADD_I) {
        Value * lhs = latchInst->getLHS();
        Value * rhs = latchInst->getRHS();
        Value * step = nullptr;
        if (lhs == phi && isLoopInvariant(rhs, loopBody)) {
            step = rhs;
        } else if (rhs == phi && isLoopInvariant(lhs, loopBody)) {
            step = lhs;
        }
        if (step) {
            shape.kind = RecurrenceShape::Kind::Affine;
            shape.start = startValue;
            shape.step = step;
            return shape;
        }
    }

    // 形态二：p_next = p / D，其中 D 为大于 1 的 2 的幂常量
    // 经过 trip 次迭代后 p_exit = start / D^trip = start / 2^(log2(D)*trip)
    if (latchInst->getOp() == IRInstOperator::IRINST_OP_DIV_I && latchInst->getLHS() == phi) {
        auto * divisorConst = dynamic_cast<ConstInteger *>(latchInst->getRHS());
        int32_t shift = 0;
        if (divisorConst && isPositivePowerOfTwo(divisorConst->getVal(), shift)) {
            shape.kind = RecurrenceShape::Kind::DivPow2;
            shape.start = startValue;
            shape.shiftPerStep = shift;
            return shape;
        }
    }

    // 形态三：p_next = p + incr，其中 incr 包含另一个 IV 的引用（二次递推）
    //   例如：sum = phi(start, sum + i)    其中 i = {0,+,1}
    //         sum = phi(start, sum + 2*i + 3)
    //   incr = a * iv + b，a 和 b 为循环不变量，iv 是另一个头部 phi
    if (latchInst->getOp() == IRInstOperator::IRINST_OP_ADD_I && latchInst->getLHS() == phi) {
        Value * incr = latchInst->getRHS();
        if (!isLoopInvariant(incr, loopBody) && isDefinedInLoop(incr, loopBody)) {
            // 尝试分解 incr = a * iv + b
            // 其中 iv 是另一个 phi，a 和 b 为循环不变量
            auto * incrInst = dynamic_cast<Instruction *>(incr);
            if (!incrInst) {
                return shape;
            }

            Value * innerIV = nullptr;
            Value * coeffA = nullptr;  // a
            Value * coeffB = nullptr;  // b

            // 情况 A：incr = add(mul(a, iv), b) 或 incr = add(b, mul(a, iv))
            if (incrInst->getOp() == IRInstOperator::IRINST_OP_ADD_I) {
                auto * addInst = static_cast<BinaryInst *>(incrInst);
                Value * lhs = addInst->getLHS();
                Value * rhs = addInst->getRHS();

                // 尝试 lhs = mul(a, iv) 且 rhs = b
                if (auto * mulInst = dynamic_cast<BinaryInst *>(lhs)) {
                    if (mulInst->getOp() == IRInstOperator::IRINST_OP_MUL_I && isDefinedInLoop(mulInst, loopBody)) {
                        Value * mulLHS = mulInst->getLHS();
                        Value * mulRHS = mulInst->getRHS();
                        if (isLoopInvariant(mulLHS, loopBody) && isDefinedInLoop(mulRHS, loopBody)) {
                            coeffA = mulLHS; innerIV = mulRHS;
                        } else if (isLoopInvariant(mulRHS, loopBody) && isDefinedInLoop(mulLHS, loopBody)) {
                            coeffA = mulRHS; innerIV = mulLHS;
                        }
                        if (innerIV && isLoopInvariant(rhs, loopBody)) {
                            coeffB = rhs;
                        } else {
                            innerIV = nullptr; coeffA = nullptr;
                        }
                    }
                }
                // 尝试 rhs = mul(a, iv) 且 lhs = b
                if (!innerIV) {
                    if (auto * mulInst = dynamic_cast<BinaryInst *>(rhs)) {
                        if (mulInst->getOp() == IRInstOperator::IRINST_OP_MUL_I && isDefinedInLoop(mulInst, loopBody)) {
                            Value * mulLHS = mulInst->getLHS();
                            Value * mulRHS = mulInst->getRHS();
                            if (isLoopInvariant(mulLHS, loopBody) && isDefinedInLoop(mulRHS, loopBody)) {
                                coeffA = mulLHS; innerIV = mulRHS;
                            } else if (isLoopInvariant(mulRHS, loopBody) && isDefinedInLoop(mulLHS, loopBody)) {
                                coeffA = mulRHS; innerIV = mulLHS;
                            }
                            if (innerIV && isLoopInvariant(lhs, loopBody)) {
                                coeffB = lhs;
                            } else {
                                innerIV = nullptr; coeffA = nullptr;
                            }
                        }
                    }
                }
            }

            // 情况 B：incr = add(iv, b)（即 a = 1）
            if (!innerIV && incrInst->getOp() == IRInstOperator::IRINST_OP_ADD_I) {
                auto * addInst = static_cast<BinaryInst *>(incrInst);
                Value * lhs = addInst->getLHS();
                Value * rhs = addInst->getRHS();
                if (isDefinedInLoop(lhs, loopBody) && isLoopInvariant(rhs, loopBody)) {
                    innerIV = lhs;
                    coeffB = rhs;
                    coeffA = mod->newConstInteger(phi->getType(), 1);
                } else if (isDefinedInLoop(rhs, loopBody) && isLoopInvariant(lhs, loopBody)) {
                    innerIV = rhs;
                    coeffB = lhs;
                    coeffA = mod->newConstInteger(phi->getType(), 1);
                }
            }

            // 情况 C：incr = iv（即 a = 1, b = 0）
            if (!innerIV && isDefinedInLoop(incr, loopBody)) {
                innerIV = incr;
                coeffA = mod->newConstInteger(phi->getType(), 1);
                coeffB = mod->newConstInteger(phi->getType(), 0);
            }

            // 验证 innerIV 是头部 phi 且自身是简单的 {start, +, const} 递推
            // 二次递推公式假定 innerIV 的增量是常量，否则不能应用闭式
            if (innerIV && dynamic_cast<PhiInst *>(innerIV) &&
                innerIV->getType()->isIntegerType()) {
                auto * innerPhi = static_cast<PhiInst *>(innerIV);
                bool innerPhiInHeader = false;
                for (auto * hdrInst : phi->getParentBlock()->getInstructions()) {
                    if (hdrInst == innerPhi) {
                        innerPhiInHeader = true;
                        break;
                    }
                    if (!dynamic_cast<PhiInst *>(hdrInst)) {
                        break;
                    }
                }

                // 验证 innerIV 是简单仿射递推：latch 值 = add(innerPhi, const)
                bool innerIsSimple = false;
                Value * innerStart = nullptr;
                Value * innerStep = nullptr;
                if (innerPhiInHeader && innerPhi->getIncomingCount() == 2) {
                    Value * innerLatch = nullptr;
                    for (int32_t i = 0; i < innerPhi->getIncomingCount(); ++i) {
                        if (innerPhi->getIncomingBlock(i) == latch) {
                            innerLatch = innerPhi->getIncomingValue(i);
                        } else if (innerPhi->getIncomingBlock(i) == preheader) {
                            innerStart = innerPhi->getIncomingValue(i);
                        }
                    }
                    if (innerLatch && innerStart && isLoopInvariant(innerStart, loopBody)) {
                        auto * innerLatchInst = dynamic_cast<BinaryInst *>(innerLatch);
                        if (innerLatchInst && innerLatchInst->getOp() == IRInstOperator::IRINST_OP_ADD_I) {
                            Value * l = innerLatchInst->getLHS();
                            Value * r = innerLatchInst->getRHS();
                            if (l == innerPhi && isLoopInvariant(r, loopBody)) {
                                innerStep = r;
                                innerIsSimple = true;
                            } else if (r == innerPhi && isLoopInvariant(l, loopBody)) {
                                innerStep = l;
                                innerIsSimple = true;
                            }
                        }
                    }
                }

                if (innerPhiInHeader && innerIsSimple && coeffA && coeffB &&
                    isLoopInvariant(coeffA, loopBody) && isLoopInvariant(coeffB, loopBody)) {
                    shape.kind = RecurrenceShape::Kind::Quadratic;
                    shape.start = startValue;
                    shape.quadIVStart = innerStart;
                    shape.quadIVStep = innerStep;
                    shape.quadCoeffA = coeffA;
                    shape.quadCoeffB = coeffB;
                    return shape;
                }
            }
        }
    }

    return shape;
}

} // namespace

/// @brief 构造循环出口值闭式替换 pass
/// @param _func 待优化函数
/// @param _mod 所属模块
LoopExitValueRewrite::LoopExitValueRewrite(Function * _func, Module * _mod) : func(_func), mod(_mod)
{}

/// @brief 对所有可识别的规范计数循环替换头部 phi 的出口取值
/// @return true 表示 IR 发生变化
bool LoopExitValueRewrite::run()
{
    if (!func || !mod || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    bool changed = false;
    auto & cache = func->getAnalysisCache();
    while (true) {
        bool localChanged = false;
        auto & domTree = cache.getOrCompute<DominatorTree>([this] { return DominatorTree(func); });
        auto & loopInfo =
            cache.getOrCompute<LoopInfo>([this, &domTree] { return LoopInfo(func, &domTree); });
        auto & scev = cache.getOrCompute<ScalarEvolution>(
            [this, &domTree, &loopInfo] { return ScalarEvolution(func, &domTree, &loopInfo); });

        currentLoopInfo = &loopInfo;
        std::vector<BasicBlock *> blocks = func->getBlocks();
        for (auto * bb : blocks) {
            if (tryRewriteHeader(bb, scev)) {
                localChanged = true;
                changed = true;
                break;
            }
        }
        if (!localChanged) {
            break;
        }
        // 仅改写了循环外的 use，CFG 未变，但值依赖发生变化
        cache.invalidateValueAnalyses();
    }

    return changed;
}

/// @brief 尝试改写以 header 为头的循环
/// @param header 循环头基本块
/// @param scev 复用的标量演化分析
/// @return true 表示成功改写至少一个出口值
bool LoopExitValueRewrite::tryRewriteHeader(BasicBlock * header, ScalarEvolution & scev)
{
    ScalarEvolution::CanonicalLoop loop;
    if (!scev.matchCanonicalLoop(header, loop)) {
        return false;
    }

    // 仅处理非负常量初值、步长为 1、判定为 < 的循环
    // 此时 bound > init 可同时证明循环终止且 bound - init 不会溢出
    if (!loop.recurrence || loop.recurrence->getStep() != 1 ||
        loop.compareKind != ScalarEvolution::CompareKind::LessThan ||
        !loop.hasConstInitialValue || loop.initialIntValue < 0) {
        return false;
    }

    BasicBlock * preheader = loop.preheader;
    BasicBlock * latch = loop.latch;
    BasicBlock * exit = loop.exit;
    Value * bound = loop.boundValue;
    Value * inductionStart = loop.initialValue;
    if (!preheader || !latch || !exit || !bound || !inductionStart || exit == header) {
        return false;
    }

    // 出口块必须只有循环头这一个前驱，确保插入的闭式计算只在循环结束后执行一次
    if (exit->getPredecessors().size() != 1 || exit->getPredecessors().front() != header) {
        return false;
    }

    const std::unordered_set<BasicBlock *> * loopBodyPtr =
        currentLoopInfo ? currentLoopInfo->getLoopBody(header) : nullptr;
    if (!loopBodyPtr) {
        return false;
    }
    // 复制一份循环体集合，避免依赖临时分析对象的生命周期
    const std::unordered_set<BasicBlock *> loopBody = *loopBodyPtr;

    // 循环必须单出口：除循环头的计数判定外，循环体内不得存在其他跳出循环的边
    // （例如 break）。否则真实迭代次数不等于规范计数 trip，闭式替换会得到错误结果
    for (auto * bb : loopBody) {
        if (bb == header) {
            continue;
        }
        for (auto * succ : bb->getSuccessors()) {
            if (loopBody.find(succ) == loopBody.end()) {
                return false; // 存在额外出口，放弃改写
            }
        }
    }

    // 收集头部所有候选 phi（排除归纳变量本身）
    std::vector<std::pair<PhiInst *, RecurrenceShape>> candidates;
    for (auto * inst : header->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        if (phi == loop.induction) {
            continue;
        }

        RecurrenceShape shape = analyzeRecurrence(phi, preheader, latch, loopBody, mod);
        if (shape.kind == RecurrenceShape::Kind::None) {
            continue;
        }

        // phi 必须存在循环体外的使用，否则无需改写
        bool hasOutsideUse = false;
        for (auto * use : phi->getUseList()) {
            auto * userInst = dynamic_cast<Instruction *>(use->getUser());
            if (userInst && userInst->getParentBlock() &&
                loopBody.find(userInst->getParentBlock()) == loopBody.end()) {
                hasOutsideUse = true;
                break;
            }
        }
        if (hasOutsideUse) {
            candidates.emplace_back(phi, shape);
        }
    }

    if (candidates.empty()) {
        return false;
    }

    Type * intType = loop.induction->getType();
    Type * boolType = loop.cmp ? loop.cmp->getType() : intType;

    // 在出口块 phi 之后维护一个递增插入位置，确保新指令按定义顺序排列
    auto & exitInsts = exit->getInstructions();
    auto insertPos = exitInsts.begin();
    while (insertPos != exitInsts.end() && dynamic_cast<PhiInst *>(*insertPos) != nullptr) {
        ++insertPos;
    }
    auto appendInst = [&](Instruction * inst) {
        inst->setParentBlock(exit);
        insertPos = std::next(exitInsts.insert(insertPos, inst));
    };

    // 计算循环执行次数 trip = (N > init) ? N - init : 0
    //   diff       = N - init
    //   tripPos    = N > init
    //   trip       = tripPos ? diff : 0
    auto * zero = mod->newConstInteger(intType, 0);
    auto * diff = new BinaryInst(func, IRInstOperator::IRINST_OP_SUB_I, bound, inductionStart, intType);
    auto * tripPos = new ICmpInst(func, IRInstOperator::IRINST_OP_GT_I, bound, inductionStart, boolType);
    appendInst(diff);
    appendInst(tripPos);

    Value * trip = new SelectInst(func, tripPos, diff, zero, intType);
    appendInst(static_cast<Instruction *>(trip));

    bool changed = false;
    for (auto & entry : candidates) {
        PhiInst * phi = entry.first;
        const RecurrenceShape & shape = entry.second;

        Value * finalValue = nullptr;

        if (shape.kind == RecurrenceShape::Kind::DivPow2) {
            // 每步除以 2^shiftPerStep，trip 步后等价于 start sdiv 2^(shiftPerStep*trip)
            finalValue = emitSignedDivByPow2(func, mod, shape.start, shape.shiftPerStep, trip, intType,
                                             boolType, appendInst);
        } else if (shape.kind == RecurrenceShape::Kind::Quadratic) {
            // 二次递推：p_{k+1} = p_k + a * iv + b
            // 其中 iv 是另一个仿射 IV：iv_k = iv_start + k * iv_step
            // p_exit = start + (a * iv_start + b) * N
            //          + a * iv_step * N * (N-1) / 2
            //
            // 先除偶数因子再相乘，避免 N*(N-1) 在除以 2 前丢失最高位
            auto * one = mod->newConstInteger(intType, 1);
            auto * tripMinusOne =
                new BinaryInst(func, IRInstOperator::IRINST_OP_SUB_I, trip, one, intType);
            appendInst(tripMinusOne);
            auto * two = mod->newConstInteger(intType, 2);
            auto * tripHalf = new BinaryInst(func, IRInstOperator::IRINST_OP_DIV_I, trip, two, intType);
            auto * tripMinusOneHalf =
                new BinaryInst(func, IRInstOperator::IRINST_OP_DIV_I, tripMinusOne, two, intType);
            appendInst(tripHalf);
            appendInst(tripMinusOneHalf);
            auto * tripLowBit =
                new BinaryInst(func, IRInstOperator::IRINST_OP_AND_I, trip, one, intType);
            appendInst(tripLowBit);
            auto * tripIsEven =
                new ICmpInst(func, IRInstOperator::IRINST_OP_EQ_I, tripLowBit, zero, boolType);
            appendInst(tripIsEven);
            auto * halfFactor = new SelectInst(func, tripIsEven, tripHalf, tripMinusOneHalf, intType);
            auto * otherFactor = new SelectInst(func, tripIsEven, tripMinusOne, trip, intType);
            appendInst(halfFactor);
            appendInst(otherFactor);
            auto * triangular =
                new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I, halfFactor, otherFactor, intType);
            appendInst(triangular);

            // quadraticTerm = a * iv_step * N*(N-1)/2
            auto * scaledStep = new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I,
                                               shape.quadCoeffA, shape.quadIVStep, intType);
            appendInst(scaledStep);
            auto * quadraticTerm =
                new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I, scaledStep, triangular, intType);
            appendInst(quadraticTerm);

            // linearTerm = (a * iv_start + b) * N
            auto * scaledStart = new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I,
                                                shape.quadCoeffA, shape.quadIVStart, intType);
            appendInst(scaledStart);
            auto * linearCoeff = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I,
                                                scaledStart, shape.quadCoeffB, intType);
            appendInst(linearCoeff);
            auto * linearTerm =
                new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I, linearCoeff, trip, intType);
            appendInst(linearTerm);

            // base = start + quadraticTerm + linearTerm
            auto * base1 = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I,
                                           shape.start, quadraticTerm, intType);
            appendInst(base1);
            auto * base2 = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I,
                                           base1, linearTerm, intType);
            appendInst(base2);

            // trip 为 0 时循环未执行，出口值应为 start
            auto * select = new SelectInst(func, tripPos, base2, shape.start, intType);
            appendInst(select);
            finalValue = select;
        } else {
            // Affine：delta = step * trip，base = start + delta
            auto * delta =
                new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I, shape.step, trip, intType);
            appendInst(delta);
            auto * base =
                new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, shape.start, delta, intType);
            appendInst(base);
            finalValue = base;
        }

        // 仅替换循环体外对 phi 的使用
        std::vector<Use *> outsideUses;
        for (auto * use : phi->getUseList()) {
            auto * userInst = dynamic_cast<Instruction *>(use->getUser());
            if (userInst && userInst->getParentBlock() &&
                loopBody.find(userInst->getParentBlock()) == loopBody.end()) {
                outsideUses.push_back(use);
            }
        }
        for (auto * use : outsideUses) {
            use->setUsee(finalValue);
        }

        changed = changed || !outsideUses.empty();
    }

    return changed;
}
