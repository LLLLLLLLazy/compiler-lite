///
/// @file LoopExitValueRewrite.cpp
/// @brief 基于 SCEV 的循环出口值闭式替换 pass 实现
///

#include "LoopExitValueRewrite.h"

#include <algorithm>
#include <cstdint>
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
        Affine,    ///< p_{k+1} = p_k + c
        ModularAdd ///< p_{k+1} = (p_k + c) % M
    };

    Kind kind = Kind::None;
    Value * start = nullptr; ///< 来自 preheader 的初值
    Value * step = nullptr;  ///< 每次迭代增量 c（循环不变量）
    Value * modulus = nullptr; ///< 模数 M（仅 ModularAdd）
};

/// @brief 识别头部 phi 的递推形态
/// @param phi 头部 phi（恰有两个 incoming：preheader 与 latch）
/// @param preheader 循环前置头
/// @param latch 循环 latch
/// @param loopBody 循环体基本块集合
/// @return 识别到的递推形态，无法识别时 kind 为 None
RecurrenceShape analyzeRecurrence(PhiInst * phi,
                                  BasicBlock * preheader,
                                  BasicBlock * latch,
                                  const std::unordered_set<BasicBlock *> & loopBody)
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

    // 形态二：p_next = (p + c) % M
    if (latchInst->getOp() == IRInstOperator::IRINST_OP_MOD_I) {
        Value * modDividend = latchInst->getLHS();
        Value * modulus = latchInst->getRHS();
        auto * addInst = dynamic_cast<BinaryInst *>(modDividend);
        if (addInst && isDefinedInLoop(addInst, loopBody) &&
            addInst->getOp() == IRInstOperator::IRINST_OP_ADD_I &&
            isLoopInvariant(modulus, loopBody)) {
            // 取模中间值只能被该取模使用，避免改变其它语义
            bool addUsedOnlyByMod = true;
            for (auto * use : addInst->getUseList()) {
                if (use->getUser() != static_cast<Value *>(latchInst)) {
                    addUsedOnlyByMod = false;
                    break;
                }
            }

            Value * lhs = addInst->getLHS();
            Value * rhs = addInst->getRHS();
            Value * step = nullptr;
            if (lhs == phi && isLoopInvariant(rhs, loopBody)) {
                step = rhs;
            } else if (rhs == phi && isLoopInvariant(lhs, loopBody)) {
                step = lhs;
            }
            if (addUsedOnlyByMod && step) {
                shape.kind = RecurrenceShape::Kind::ModularAdd;
                shape.start = startValue;
                shape.step = step;
                shape.modulus = modulus;
                return shape;
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

    // 处理形如 for (i = init; i < N; i += step) 的规范计数循环：
    // 起始 init 可为循环不变量、步长 step 为正整数常量、判定为 <
    if (!loop.recurrence || loop.recurrence->getStep() <= 0 ||
        loop.compareKind != ScalarEvolution::CompareKind::LessThan) {
        return false;
    }

    const int32_t inductionStep = loop.recurrence->getStep();

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

        RecurrenceShape shape = analyzeRecurrence(phi, preheader, latch, loopBody);
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

    // 计算循环执行次数 trip = (N > init) ? ceilDiv(N - init, step) : 0
    //   diff       = N - init
    // 当 step 为正整数时 ceilDiv(diff, step) = (diff + step - 1) / step
    //   tripRaw    = (diff + step - 1) / step
    //   tripPos    = diff > 0
    //   trip       = tripPos ? tripRaw : 0
    // tripPos 同时用于 ModularAdd：循环未执行时出口值应为 start
    auto * zero = mod->newConstInteger(intType, 0);
    auto * diff = new BinaryInst(func, IRInstOperator::IRINST_OP_SUB_I, bound, inductionStart, intType);
    auto * tripPos = new ICmpInst(func, IRInstOperator::IRINST_OP_GT_I, diff, zero, boolType);
    appendInst(diff);
    appendInst(tripPos);

    Value * trip = nullptr;
    if (inductionStep == 1) {
        // 步长为 1 时 ceilDiv(diff, 1) = diff，省去多余的加法和除法
        trip = new SelectInst(func, tripPos, diff, zero, intType);
        appendInst(static_cast<Instruction *>(trip));
    } else {
        auto * stepConst = mod->newConstInteger(intType, inductionStep);
        auto * stepMinusOne = mod->newConstInteger(intType, inductionStep - 1);
        auto * numerator =
            new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, diff, stepMinusOne, intType);
        auto * tripRaw =
            new BinaryInst(func, IRInstOperator::IRINST_OP_DIV_I, numerator, stepConst, intType);
        auto * tripSelect = new SelectInst(func, tripPos, tripRaw, zero, intType);
        appendInst(numerator);
        appendInst(tripRaw);
        appendInst(tripSelect);
        trip = tripSelect;
    }

    bool changed = false;
    for (auto & entry : candidates) {
        PhiInst * phi = entry.first;
        const RecurrenceShape & shape = entry.second;

        // delta = step * trip
        auto * delta = new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I, shape.step, trip, intType);
        appendInst(delta);
        // base = start + delta
        auto * base = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, shape.start, delta, intType);
        appendInst(base);

        Value * finalValue = base;
        if (shape.kind == RecurrenceShape::Kind::ModularAdd) {
            // modVal = (start + step * trip) % M
            auto * modVal =
                new BinaryInst(func, IRInstOperator::IRINST_OP_MOD_I, base, shape.modulus, intType);
            appendInst(modVal);
            // trip 为 0 时循环未执行，出口值应为 start，故按 trip>0 选择
            auto * select = new SelectInst(func, tripPos, modVal, shape.start, intType);
            appendInst(select);
            finalValue = select;
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
