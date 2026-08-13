///
/// @file LoopTiling.cpp
/// @brief 保守的二维循环分块 pass 实现
///

#include "LoopTiling.h"

#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <vector>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "CondBranchInst.h"
#include "ConstInteger.h"
#include "DominatorTree.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "GlobalVariable.h"
#include "ICmpInst.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "LoopInfo.h"
#include "Module.h"
#include "PhiInst.h"
#include "ScalarEvolution.h"
#include "SelectInst.h"
#include "StoreInst.h"
#include "Use.h"
#include "Value.h"
#include "AnalysisCache.h"
#include "CostModel.h"

namespace {

constexpr int32_t kMinTileTripCount = 64;
constexpr int32_t kLargeNestTileSize = 128;
constexpr int32_t kLargeNestTripCount = 128;

using CanonicalLoop = ScalarEvolution::CanonicalLoop;

enum class RootKind {
    Unknown,
    Formal,
    Global,
    Alloca,
};

struct PointerRoot {
    RootKind kind = RootKind::Unknown;
    Value * value = nullptr;
};

ConstInteger * asConstInt(Value * value)
{
    return dynamic_cast<ConstInteger *>(value);
}

bool sameRoot(const PointerRoot & lhs, const PointerRoot & rhs);
bool isKnownRoot(const PointerRoot & root);
PointerRoot stripPointerRoot(Value * value);
bool isAddConstStep(Value * value, PhiInst * induction, int32_t expectedStep);
void insertBeforeTerminator(BasicBlock * bb, Instruction * inst);

/// @brief 判断值是否定义在循环体内部
/// @param value 待判断的值
/// @param loopBody 循环体基本块集合
/// @return 若值是指令且定义在循环体内则返回true
bool isDefinedInLoop(Value * value, const std::unordered_set<BasicBlock *> & loopBody)
{
    auto * inst = dynamic_cast<Instruction *>(value);
    return inst && loopBody.find(inst->getParentBlock()) != loopBody.end();
}

/// @brief 递归判断value的操作数链中是否依赖needle
/// @param value 待判断的值
/// @param needle 目标依赖值
/// @param visiting 已访问集合，防止循环
/// @return 若value依赖needle则返回true
bool valueDependsOn(Value * value, Value * needle, std::unordered_set<Value *> & visiting)
{
    if (value == needle) {
        return true;
    }
    if (!value || !visiting.insert(value).second) {
        return false;
    }

    auto * inst = dynamic_cast<Instruction *>(value);
    if (!inst) {
        return false;
    }

    for (auto * operand : inst->getOperandsValue()) {
        if (valueDependsOn(operand, needle, visiting)) {
            return true;
        }
    }

    return false;
}

/// @brief valueDependsOn的无状态包装，内部创建visiting集合
bool valueDependsOn(Value * value, Value * needle)
{
    std::unordered_set<Value *> visiting;
    return valueDependsOn(value, needle, visiting);
}

bool getPhiIncomingSplit(PhiInst * phi,
                         const std::unordered_set<BasicBlock *> & loopBody,
                         Value *& entryValue,
                         BasicBlock *& entryBlock,
                         Value *& backedgeValue,
                         BasicBlock *& backedgeBlock)
{
    entryValue = nullptr;
    entryBlock = nullptr;
    backedgeValue = nullptr;
    backedgeBlock = nullptr;
    if (!phi || phi->getIncomingCount() != 2) {
        return false;
    }

    for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
        BasicBlock * block = phi->getIncomingBlock(index);
        Value * value = phi->getIncomingValue(index);
        if (loopBody.find(block) == loopBody.end()) {
            if (entryValue) {
                return false;
            }
            entryValue = value;
            entryBlock = block;
        } else {
            if (backedgeValue) {
                return false;
            }
            backedgeValue = value;
            backedgeBlock = block;
        }
    }

    return entryValue && entryBlock && backedgeValue && backedgeBlock;
}

void insertAfterPhis(BasicBlock * bb, const std::vector<Instruction *> & newInsts)
{
    if (!bb || newInsts.empty()) {
        return;
    }

    auto & insts = bb->getInstructions();
    auto insertPos = insts.begin();
    while (insertPos != insts.end() && dynamic_cast<PhiInst *>(*insertPos)) {
        ++insertPos;
    }

    for (auto * inst : newInsts) {
        inst->setParentBlock(bb);
        insts.insert(insertPos, inst);
    }
}

struct RepeatedReductionMatch {
    CanonicalLoop loop;
    PhiInst * reductionPhi = nullptr;
    Value * entryValue = nullptr;
    Value * backedgeValue = nullptr;
};

bool instructionEscapesLoop(Instruction * inst, const std::unordered_set<BasicBlock *> & loopBody)
{
    if (!inst || !inst->hasResultValue()) {
        return false;
    }

    for (auto * use : inst->getUseList()) {
        auto * userInst = use ? dynamic_cast<Instruction *>(use->getUser()) : nullptr;
        if (!userInst || !userInst->getParentBlock()) {
            continue;
        }
        if (loopBody.find(userInst->getParentBlock()) == loopBody.end()) {
            return true;
        }
    }
    return false;
}

bool hasPhiUseOutsideLoop(Value * value, const std::unordered_set<BasicBlock *> & loopBody)
{
    if (!value) {
        return false;
    }
    for (auto * use : value->getUseList()) {
        auto * userInst = use ? dynamic_cast<Instruction *>(use->getUser()) : nullptr;
        if (!userInst || !userInst->getParentBlock() ||
            loopBody.find(userInst->getParentBlock()) != loopBody.end()) {
            continue;
        }
        if (dynamic_cast<PhiInst *>(userInst)) {
            return true;
        }
    }
    return false;
}

void replaceUsesOutsideLoopExcept(Value * oldValue,
                                  Value * newValue,
                                  const std::unordered_set<BasicBlock *> & loopBody,
                                  const std::unordered_set<Instruction *> & skip)
{
    if (!oldValue || !newValue || oldValue == newValue) {
        return;
    }

    std::vector<Use *> uses(oldValue->getUseList().begin(), oldValue->getUseList().end());
    for (auto * use : uses) {
        auto * userInst = use ? dynamic_cast<Instruction *>(use->getUser()) : nullptr;
        if (!userInst || !userInst->getParentBlock() ||
            loopBody.find(userInst->getParentBlock()) != loopBody.end() || skip.count(userInst)) {
            continue;
        }
        userInst->replaceOperand(oldValue, newValue);
    }
}

bool isIndependentOfRepeatedReduction(Value * value,
                                      Value * accumulator,
                                      PhiInst * outerInduction,
                                      const std::unordered_set<BasicBlock *> & loopBody)
{
    // 累加项必须是循环不变量：把 N 次 `acc += term` 折叠成 `entry + N*delta`
    // 的前提是每次迭代加上的 term 完全相同。若 term 定义在循环体内，它会随迭代
    // 变化（内层归约结果、随下标变化的内存 load、LSR 生成的指针 IV 派生值等），
    // 此时折叠不成立——这正是把 `sum += c[i][j]` 误折叠成 `sum*N` 的根因。
    return value && !isDefinedInLoop(value, loopBody) && !valueDependsOn(value, accumulator) &&
           !valueDependsOn(value, outerInduction);
}

bool isAdditiveReductionValue(Value * value,
                              Value * accumulator,
                              PhiInst * outerInduction,
                              const std::unordered_set<BasicBlock *> & loopBody,
                              std::unordered_set<Value *> & visiting);

bool isAdditiveReductionPhi(PhiInst * phi,
                            Value * accumulator,
                            PhiInst * outerInduction,
                            const std::unordered_set<BasicBlock *> & loopBody,
                            std::unordered_set<Value *> & visiting)
{
    if (!phi || phi->getIncomingCount() != 2 || !phi->getType()->isInt32Type() ||
        loopBody.find(phi->getParentBlock()) == loopBody.end()) {
        return false;
    }

    for (int32_t seedIndex = 0; seedIndex < phi->getIncomingCount(); ++seedIndex) {
        const int32_t updateIndex = 1 - seedIndex;
        if (isAdditiveReductionValue(phi->getIncomingValue(seedIndex),
                                     accumulator,
                                     outerInduction,
                                     loopBody,
                                     visiting) &&
            isAdditiveReductionValue(phi->getIncomingValue(updateIndex),
                                     phi,
                                     outerInduction,
                                     loopBody,
                                     visiting)) {
            return true;
        }
    }

    return false;
}

bool isAdditiveReductionValue(Value * value,
                              Value * accumulator,
                              PhiInst * outerInduction,
                              const std::unordered_set<BasicBlock *> & loopBody,
                              std::unordered_set<Value *> & visiting)
{
    if (value == accumulator) {
        return true;
    }
    if (!value || !visiting.insert(value).second) {
        return false;
    }

    if (auto * phi = dynamic_cast<PhiInst *>(value)) {
        return isAdditiveReductionPhi(phi, accumulator, outerInduction, loopBody, visiting);
    }

    auto * binary = dynamic_cast<BinaryInst *>(value);
    if (!binary) {
        return false;
    }

    if (binary->getOp() == IRInstOperator::IRINST_OP_ADD_I) {
        const bool lhsAdditive =
            isAdditiveReductionValue(binary->getLHS(), accumulator, outerInduction, loopBody, visiting);
        const bool rhsAdditive =
            isAdditiveReductionValue(binary->getRHS(), accumulator, outerInduction, loopBody, visiting);
        if (lhsAdditive == rhsAdditive) {
            return false;
        }
        Value * term = lhsAdditive ? binary->getRHS() : binary->getLHS();
        return isIndependentOfRepeatedReduction(term, accumulator, outerInduction, loopBody);
    }

    if (binary->getOp() == IRInstOperator::IRINST_OP_SUB_I) {
        return isAdditiveReductionValue(binary->getLHS(), accumulator, outerInduction, loopBody, visiting) &&
               isIndependentOfRepeatedReduction(binary->getRHS(), accumulator, outerInduction, loopBody);
    }

    return false;
}

bool isAdditiveReductionValue(Value * value,
                              Value * accumulator,
                              PhiInst * outerInduction,
                              const std::unordered_set<BasicBlock *> & loopBody)
{
    std::unordered_set<Value *> visiting;
    return isAdditiveReductionValue(value, accumulator, outerInduction, loopBody, visiting);
}

bool isAllowedRepeatedReductionInstruction(Instruction * inst,
                                           const RepeatedReductionMatch & match,
                                           Instruction * inductionNext)
{
    return inst == match.loop.cmp || inst == match.loop.branch || inst == inductionNext ||
           inst == match.reductionPhi || dynamic_cast<PhiInst *>(inst) != nullptr ||
           dynamic_cast<BranchInst *>(inst) != nullptr || dynamic_cast<CondBranchInst *>(inst) != nullptr ||
           dynamic_cast<ICmpInst *>(inst) != nullptr || dynamic_cast<LoadInst *>(inst) != nullptr ||
           dynamic_cast<GetElementPtrInst *>(inst) != nullptr || dynamic_cast<BinaryInst *>(inst) != nullptr;
}

bool loopBodyIsReadOnlyReduction(const std::unordered_set<BasicBlock *> & loopBody,
                                 const RepeatedReductionMatch & match,
                                 Instruction * inductionNext)
{
    for (auto * bb : loopBody) {
        for (auto * inst : bb->getInstructions()) {
            if (!isAllowedRepeatedReductionInstruction(inst, match, inductionNext)) {
                return false;
            }
            if (dynamic_cast<StoreInst *>(inst) || dynamic_cast<CallInst *>(inst) || inst->mayWriteMemory()) {
                return false;
            }
            if (inst != match.reductionPhi && inst != match.loop.induction && instructionEscapesLoop(inst, loopBody)) {
                return false;
            }
        }
    }
    return true;
}

bool matchRepeatedInvariantReductionLoop(ScalarEvolution & scev,
                                         LoopInfo & loopInfo,
                                         BasicBlock * header,
                                         RepeatedReductionMatch & match)
{
    match = {};
    if (!scev.matchCanonicalLoop(header, match.loop) || !match.loop.recurrence ||
        match.loop.recurrence->getStep() != 1 || !match.loop.hasConstInitialValue ||
        match.loop.initialIntValue != 0 || !match.loop.boundValue ||
        match.loop.compareKind != ScalarEvolution::CompareKind::LessThan) {
        return false;
    }

    // 首次折叠把上界规范成 select(bound>0, 1, 0)，该标记形态不可再次折叠
    auto * normalizedBound = dynamic_cast<SelectInst *>(match.loop.boundValue);
    auto * normalizedTrue = normalizedBound ? asConstInt(normalizedBound->getTrueValue()) : nullptr;
    auto * normalizedFalse = normalizedBound ? asConstInt(normalizedBound->getFalseValue()) : nullptr;
    if (normalizedTrue && normalizedFalse && normalizedTrue->getVal() == 1 && normalizedFalse->getVal() == 0) {
        return false;
    }

    const auto * loopBody = loopInfo.getLoopBody(header);
    if (!loopBody || loopBody->empty()) {
        return false;
    }

    Instruction * inductionNext = dynamic_cast<Instruction *>(match.loop.recurrence->getBackEdgeValue());
    if (!inductionNext) {
        return false;
    }

    for (auto * inst : header->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        if (phi == match.loop.induction || !phi->getType()->isInt32Type()) {
            continue;
        }
        if (match.reductionPhi) {
            return false;
        }

        Value * entryValue = nullptr;
        BasicBlock * entryBlock = nullptr;
        Value * backedgeValue = nullptr;
        BasicBlock * backedgeBlock = nullptr;
        if (!getPhiIncomingSplit(phi, *loopBody, entryValue, entryBlock, backedgeValue, backedgeBlock)) {
            return false;
        }
        if (!isAdditiveReductionValue(backedgeValue, phi, match.loop.induction, *loopBody) ||
            valueDependsOn(backedgeValue, match.loop.induction)) {
            return false;
        }

        match.reductionPhi = phi;
        match.entryValue = entryValue;
        match.backedgeValue = backedgeValue;
    }

    if (!match.reductionPhi || !match.entryValue || !match.backedgeValue) {
        return false;
    }

    if (!loopBodyIsReadOnlyReduction(*loopBody, match, inductionNext)) {
        return false;
    }

    return true;
}

bool collapseRepeatedInvariantReductionLoop(Function * func,
                                            Module * mod,
                                            LoopInfo & loopInfo,
                                            ScalarEvolution & scev)
{
    if (!func || !mod || func->getBlocks().empty()) {
        return false;
    }

    for (auto * header : func->getBlocks()) {
        if (!loopInfo.isLoopHeader(header)) {
            continue;
        }

        RepeatedReductionMatch match;
        if (!matchRepeatedInvariantReductionLoop(scev, loopInfo, header, match)) {
            continue;
        }
        const auto * loopBody = loopInfo.getLoopBody(header);
        if (!loopBody || hasPhiUseOutsideLoop(match.reductionPhi, *loopBody)) {
            continue;
        }

        auto * zero = mod->newConstInt32(0);
        auto * one = mod->newConstInt32(1);
        auto * positiveCmp = new ICmpInst(func,
                                          IRInstOperator::IRINST_OP_GT_I,
                                          match.loop.boundValue,
                                          zero,
                                          match.loop.cmp->getType());
        auto * singleTripBound = new SelectInst(func, positiveCmp, one, zero, match.loop.boundValue->getType());
        insertBeforeTerminator(match.loop.preheader, positiveCmp);
        insertBeforeTerminator(match.loop.preheader, singleTripBound);
        if (match.loop.cmp->getLHS() == match.loop.boundValue) {
            match.loop.cmp->setOperand(0, singleTripBound);
        } else if (match.loop.cmp->getRHS() == match.loop.boundValue) {
            match.loop.cmp->setOperand(1, singleTripBound);
        } else {
            return false;
        }

        auto * oneLoopDelta = new BinaryInst(func,
                                             IRInstOperator::IRINST_OP_SUB_I,
                                             match.reductionPhi,
                                             match.entryValue,
                                             match.reductionPhi->getType());
        auto * totalDelta = new BinaryInst(func,
                                           IRInstOperator::IRINST_OP_MUL_I,
                                           oneLoopDelta,
                                           match.loop.boundValue,
                                           match.reductionPhi->getType());
        auto * collapsed = new BinaryInst(func,
                                          IRInstOperator::IRINST_OP_ADD_I,
                                          match.entryValue,
                                          totalDelta,
                                          match.reductionPhi->getType());
        insertAfterPhis(match.loop.exit, {oneLoopDelta, totalDelta, collapsed});
        replaceUsesOutsideLoopExcept(match.reductionPhi,
                                     collapsed,
                                     *loopBody,
                                     {oneLoopDelta, totalDelta, collapsed});
        return true;
    }

    return false;
}
bool sameRoot(const PointerRoot & lhs, const PointerRoot & rhs)
{
    return lhs.kind == rhs.kind && lhs.value == rhs.value;
}

bool isKnownRoot(const PointerRoot & root)
{
    return root.kind != RootKind::Unknown && root.value != nullptr;
}

bool isDerivedFrom(Value * value, Value * root, std::unordered_set<Value *> & visiting)
{
    if (value == root) {
        return true;
    }
    if (!value || !visiting.insert(value).second) {
        return false;
    }

    auto * inst = dynamic_cast<Instruction *>(value);
    if (!inst) {
        return false;
    }

    for (auto * operand : inst->getOperandsValue()) {
        if (isDerivedFrom(operand, root, visiting)) {
            return true;
        }
    }

    return false;
}

bool isDerivedFrom(Value * value, Value * root)
{
    std::unordered_set<Value *> visiting;
    return isDerivedFrom(value, root, visiting);
}

PointerRoot stripPointerRoot(Value * value, std::unordered_set<Value *> & visiting)
{
    if (!value || !visiting.insert(value).second) {
        return {};
    }

    while (auto * gep = dynamic_cast<GetElementPtrInst *>(value)) {
        value = gep->getBasePointer();
    }

    if (auto * phi = dynamic_cast<PhiInst *>(value)) {
        PointerRoot merged;
        bool foundRoot = false;
        for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
            Value * incoming = phi->getIncomingValue(index);
            if (isDerivedFrom(incoming, phi)) {
                continue;
            }

            PointerRoot incomingRoot = stripPointerRoot(incoming, visiting);
            if (!isKnownRoot(incomingRoot)) {
                return {};
            }
            if (!foundRoot) {
                merged = incomingRoot;
                foundRoot = true;
                continue;
            }
            if (!sameRoot(merged, incomingRoot)) {
                return {};
            }
        }

        return foundRoot ? merged : PointerRoot{};
    }

    if (dynamic_cast<FormalParam *>(value)) {
        return {RootKind::Formal, value};
    }
    if (dynamic_cast<GlobalVariable *>(value)) {
        return {RootKind::Global, value};
    }
    if (dynamic_cast<AllocaInst *>(value)) {
        return {RootKind::Alloca, value};
    }

    return {};
}

PointerRoot stripPointerRoot(Value * value)
{
    std::unordered_set<Value *> visiting;
    return stripPointerRoot(value, visiting);
}

bool isAddConstStep(Value * value, PhiInst * induction, int32_t expectedStep)
{
    auto * binary = dynamic_cast<BinaryInst *>(value);
    if (!binary || !induction || binary->getOp() != IRInstOperator::IRINST_OP_ADD_I) {
        return false;
    }

    if (binary->getLHS() == induction) {
        auto * rhs = asConstInt(binary->getRHS());
        return rhs && rhs->getVal() == expectedStep;
    }

    if (binary->getRHS() == induction) {
        auto * lhs = asConstInt(binary->getLHS());
        return lhs && lhs->getVal() == expectedStep;
    }

    return false;
}

bool hasSingleBranchTo(BasicBlock * bb, BasicBlock * target)
{
    auto * branch = bb ? dynamic_cast<BranchInst *>(bb->getTerminator()) : nullptr;
    return branch && branch->getTarget() == target && bb->getSuccessors().size() == 1;
}

bool hasPred(BasicBlock * bb, BasicBlock * pred)
{
    if (!bb || !pred) {
        return false;
    }

    const auto & preds = bb->getPredecessors();
    return std::find(preds.begin(), preds.end(), pred) != preds.end();
}

int32_t chooseTileSize(int32_t requestedTileSize, const CanonicalLoop & outer, const CanonicalLoop & inner)
{
    if (requestedTileSize == 32 && outer.hasConstBoundValue && inner.hasConstBoundValue &&
        outer.boundIntValue >= kLargeNestTripCount && inner.boundIntValue >= kLargeNestTripCount) {
        return kLargeNestTileSize;
    }

    return requestedTileSize;
}

bool matchCanonicalLoop(ScalarEvolution & scev, BasicBlock * header, CanonicalLoop & loop)
{
    if (!header || !scev.matchCanonicalLoop(header, loop) || !loop.recurrence || !loop.hasConstInitialValue ||
        !loop.hasConstTripCount || !loop.boundValue || loop.initialIntValue != 0 || loop.recurrence->getStep() != 1) {
        return false;
    }
    if (loop.compareKind != ScalarEvolution::CompareKind::LessThan || loop.cmp->getLHS() != loop.induction ||
        loop.cmp->getRHS() != loop.boundValue || loop.branch->getTrueDest() != loop.body ||
        loop.branch->getFalseDest() != loop.exit) {
        return false;
    }
    if (loop.tripCount < kMinTileTripCount) {
        return false;
    }

    return loop.body != nullptr && loop.exit != nullptr;
}

bool loopHasOnlyCanonicalExit(const std::unordered_set<BasicBlock *> & loopBody,
                              BasicBlock * header,
                              BasicBlock * exit)
{
    if (loopBody.empty() || !header || !exit) {
        return false;
    }

    for (auto * bb : loopBody) {
        for (auto * succ : bb->getSuccessors()) {
            if (loopBody.find(succ) == loopBody.end()) {
                if (bb != header || succ != exit) {
                    return false;
                }
            }
        }
    }

    return true;
}

bool isAdjustableHeaderPhi(PhiInst * phi, const CanonicalLoop & loop, ScalarEvolution & scev)
{
    if (!phi || phi->getParentBlock() != loop.header || phi->getIncomingCount() != 2) {
        return false;
    }
    if (phi == loop.induction) {
        return true;
    }

    const auto * recurrence = scev.getAddRecurrence(phi);
    return recurrence && recurrence->getLoopHeader() == loop.header && recurrence->getPreheader() == loop.preheader &&
           recurrence->getLatch() == loop.latch && recurrence->getStep() == 1;
}

bool headerPhisAreAdjustable(const CanonicalLoop & loop, ScalarEvolution & scev)
{
    for (auto * inst : loop.header->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        if (!isAdjustableHeaderPhi(phi, loop, scev)) {
            return false;
        }
    }

    return true;
}

bool valueDependsOnLoopIndex(Value * value, const CanonicalLoop & loop, ScalarEvolution & scev)
{
    return scev.dependsOnLoop(value, loop.header);
}

int32_t formalIndex(Function * func, Value * value)
{
    if (!func || !value) {
        return -1;
    }

    auto & params = func->getParams();
    for (std::size_t index = 0; index < params.size(); ++index) {
        if (params[index] == value) {
            return static_cast<int32_t>(index);
        }
    }

    return -1;
}

/// @brief 递归验证一对指针形参在全部调用链入口都落到不同对象
/// @param callee 当前被验证函数
/// @param mod 所属模块
/// @param lhsFormal 左侧指针形参
/// @param rhsFormal 右侧指针形参
/// @param visiting 当前调用链上已访问的函数
/// @return 所有实调用入口均可证明不别名时返回 true
bool directCallActualsAreDistinctImpl(Function * callee,
	                                  Module * mod,
	                                  Value * lhsFormal,
	                                  Value * rhsFormal,
	                                  std::unordered_set<Function *> & visiting)
{
	if (!callee || !mod || lhsFormal == rhsFormal || !visiting.insert(callee).second) {
        return false;
    }

    const int32_t lhsIndex = formalIndex(callee, lhsFormal);
    const int32_t rhsIndex = formalIndex(callee, rhsFormal);
    if (lhsIndex < 0 || rhsIndex < 0) {
        return false;
    }

	bool hasProvenEntry = false;
    for (auto * caller : mod->getFunctionList()) {
        if (!caller || caller->isBuiltin()) {
            continue;
        }

        for (auto * bb : caller->getBlocks()) {
            for (auto * inst : bb->getInstructions()) {
                auto * call = dynamic_cast<CallInst *>(inst);
                if (!call || call->getCallee() != callee) {
                    continue;
                }

                if (call->getArgCount() <= lhsIndex || call->getArgCount() <= rhsIndex) {
                    return false;
                }

                PointerRoot lhsRoot = stripPointerRoot(call->getArg(lhsIndex));
                PointerRoot rhsRoot = stripPointerRoot(call->getArg(rhsIndex));
                if (!isKnownRoot(lhsRoot) || !isKnownRoot(rhsRoot) || sameRoot(lhsRoot, rhsRoot)) {
                    return false;
                }

				if (lhsRoot.kind != RootKind::Formal || rhsRoot.kind != RootKind::Formal) {
					if (lhsRoot.kind == RootKind::Formal || rhsRoot.kind == RootKind::Formal) {
						return false;
					}
					hasProvenEntry = true;
					continue;
				}

				// 自递归把同一形参对原样或交换后传下去，只传播入口处的不别名前提
				const bool preservesPair = caller == callee
				    && ((lhsRoot.value == lhsFormal && rhsRoot.value == rhsFormal)
				        || (lhsRoot.value == rhsFormal && rhsRoot.value == lhsFormal));
				if (preservesPair) {
					continue;
				}

				if (!directCallActualsAreDistinctImpl(caller,
				                                      mod,
				                                      lhsRoot.value,
				                                      rhsRoot.value,
				                                      visiting)) {
					return false;
				}
				hasProvenEntry = true;
            }
        }
    }

	visiting.erase(callee);
	return hasProvenEntry;
}

/// @brief 验证一对指针形参在全部直接或传递调用点都不别名
bool directCallActualsAreDistinct(Function * callee, Module * mod, Value * lhsFormal, Value * rhsFormal)
{
	std::unordered_set<Function *> visiting;
	return directCallActualsAreDistinctImpl(callee, mod, lhsFormal, rhsFormal, visiting);
}

bool rootsCannotAlias(Function * func, Module * mod, const PointerRoot & lhs, const PointerRoot & rhs)
{
    if (!isKnownRoot(lhs) || !isKnownRoot(rhs) || sameRoot(lhs, rhs)) {
        return false;
    }

    if (lhs.kind == RootKind::Formal || rhs.kind == RootKind::Formal) {
        if (lhs.kind != RootKind::Formal || rhs.kind != RootKind::Formal) {
            return false;
        }
        return directCallActualsAreDistinct(func, mod, lhs.value, rhs.value);
    }

    return true;
}

/// @brief 验证存储地址是由两维归纳变量分别索引的规范二维数组元素
/// @return 两维下标互不混合、因而每个迭代点写入唯一元素时返回 true
bool storeAddressIsInjective(Value * pointer, const CanonicalLoop & outer, const CanonicalLoop & inner)
{
    auto * elementGep = dynamic_cast<GetElementPtrInst *>(pointer);
    auto * rowGep = elementGep ? dynamic_cast<GetElementPtrInst *>(elementGep->getBasePointer()) : nullptr;
    if (!elementGep || !rowGep || !elementGep->isArrayDecayGEP() || rowGep->isArrayDecayGEP()) {
        return false;
    }

    Value * rowIndex = rowGep->getIndexOperand();
    Value * elementIndex = elementGep->getIndexOperand();
    return (rowIndex == outer.induction && elementIndex == inner.induction) ||
           (rowIndex == inner.induction && elementIndex == outer.induction);
}

bool isDependenceSafe(Function * func,
                      Module * mod,
                      ScalarEvolution & scev,
                      const CanonicalLoop & outer,
                      const CanonicalLoop & inner,
                      const std::unordered_set<BasicBlock *> & innerBody)
{
    StoreInst * onlyStore = nullptr;
    for (auto * bb : innerBody) {
        for (auto * inst : bb->getInstructions()) {
            if (dynamic_cast<CallInst *>(inst)) {
                return false;
            }

            auto * store = dynamic_cast<StoreInst *>(inst);
            if (!store) {
                continue;
            }
            if (onlyStore) {
                return false;
            }
            onlyStore = store;
        }
    }

    if (!onlyStore || !storeAddressIsInjective(onlyStore->getPointerOperand(), outer, inner) ||
        !valueDependsOnLoopIndex(onlyStore->getPointerOperand(), outer, scev) ||
        !valueDependsOnLoopIndex(onlyStore->getPointerOperand(), inner, scev)) {
        return false;
    }

    PointerRoot storeRoot = stripPointerRoot(onlyStore->getPointerOperand());
    if (!isKnownRoot(storeRoot)) {
        return false;
    }

    for (auto * bb : innerBody) {
        for (auto * inst : bb->getInstructions()) {
            auto * load = dynamic_cast<LoadInst *>(inst);
            if (!load) {
                continue;
            }

            PointerRoot loadRoot = stripPointerRoot(load->getPointerOperand());
            if (!rootsCannotAlias(func, mod, storeRoot, loadRoot)) {
                return false;
            }
        }
    }

    return true;
}

void insertBlockBefore(Function * func, BasicBlock * bb, BasicBlock * before)
{
    if (!func || !bb || !before || bb == before) {
        return;
    }

    auto & blocks = func->getBlocks();
    auto bbPos = std::find(blocks.begin(), blocks.end(), bb);
    auto beforePos = std::find(blocks.begin(), blocks.end(), before);
    if (bbPos == blocks.end() || beforePos == blocks.end()) {
        return;
    }

    blocks.erase(bbPos);
    beforePos = std::find(blocks.begin(), blocks.end(), before);
    blocks.insert(beforePos, bb);
}

void insertBeforeTerminator(BasicBlock * bb, Instruction * inst)
{
    if (!bb || !inst) {
        return;
    }

    auto & insts = bb->getInstructions();
    auto insertPos = insts.end();
    if (!insts.empty() && insts.back()->isTerminator()) {
        insertPos = std::prev(insts.end());
    }

    inst->setParentBlock(bb);
    insts.insert(insertPos, inst);
}

bool rewriteTerminatorTarget(BasicBlock * pred, BasicBlock * oldTarget, BasicBlock * newTarget)
{
    if (!pred || !oldTarget || !newTarget) {
        return false;
    }

    if (auto * branch = dynamic_cast<BranchInst *>(pred->getTerminator())) {
        if (branch->getTarget() != oldTarget) {
            return false;
        }
        branch->setTarget(newTarget);
        return true;
    }

    if (auto * cond = dynamic_cast<CondBranchInst *>(pred->getTerminator())) {
        bool changed = false;
        if (cond->getTrueDest() == oldTarget) {
            cond->setTrueDest(newTarget);
            changed = true;
        }
        if (cond->getFalseDest() == oldTarget) {
            cond->setFalseDest(newTarget);
            changed = true;
        }
        return changed;
    }

    return false;
}

bool updatePhiIncoming(PhiInst * phi, BasicBlock * oldBlock, BasicBlock * newBlock, Value * newValue)
{
    if (!phi || !oldBlock || !newBlock || !newValue) {
        return false;
    }

    for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
        if (phi->getIncomingBlock(index) != oldBlock) {
            continue;
        }

        phi->setOperand(index, newValue);
        phi->replaceIncomingBlock(oldBlock, newBlock);
        return true;
    }

    return false;
}

bool updatePhiIncomingValue(PhiInst * phi, BasicBlock * block, Value * newValue)
{
    if (!phi || !block || !newValue) {
        return false;
    }

    for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
        if (phi->getIncomingBlock(index) == block) {
            phi->setOperand(index, newValue);
            return true;
        }
    }

    return false;
}

void rewritePhiIncomingBlock(BasicBlock * bb, BasicBlock * oldPred, BasicBlock * newPred)
{
    if (!bb || !oldPred || !newPred) {
        return;
    }

    for (auto * inst : bb->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        phi->replaceIncomingBlock(oldPred, newPred);
    }
}

Instruction * createTileInitialValue(Function * func,
                                     ScalarEvolution & scev,
                                     const CanonicalLoop & loop,
                                     PhiInst * phi,
                                     Value * tileOffset)
{
    const auto * recurrence = scev.getAddRecurrence(phi);
    if (!recurrence || recurrence->getLoopHeader() != loop.header || recurrence->getPreheader() != loop.preheader ||
        recurrence->getLatch() != loop.latch || recurrence->getStep() != 1 || !recurrence->getStartValue()) {
        return nullptr;
    }

    if (recurrence->isPointerRecurrence()) {
        return new GetElementPtrInst(func, recurrence->getStartValue(), tileOffset, phi->getType(), false);
    }

    if (recurrence->isIntegerRecurrence()) {
        return new BinaryInst(func,
                              IRInstOperator::IRINST_OP_ADD_I,
                              recurrence->getStartValue(),
                              tileOffset,
                              phi->getType());
    }

    return nullptr;
}

bool retargetHeaderPhiInitialValues(Function * func,
                                    ScalarEvolution & scev,
                                    const CanonicalLoop & loop,
                                    BasicBlock * newIncomingBlock,
                                    Value * tileOffset,
                                    BasicBlock * insertBlock,
                                    bool replaceIncomingBlock)
{
    for (auto * inst : loop.header->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }

        if (phi == loop.induction) {
            if (replaceIncomingBlock) {
                if (!updatePhiIncoming(phi, loop.preheader, newIncomingBlock, tileOffset)) {
                    return false;
                }
            } else if (!updatePhiIncomingValue(phi, loop.preheader, tileOffset)) {
                return false;
            }
            continue;
        }

        Instruction * initInst = createTileInitialValue(func, scev, loop, phi, tileOffset);
        if (!initInst) {
            return false;
        }

        insertBeforeTerminator(insertBlock, initInst);
        if (replaceIncomingBlock) {
            if (!updatePhiIncoming(phi, loop.preheader, newIncomingBlock, initInst)) {
                return false;
            }
        } else if (!updatePhiIncomingValue(phi, loop.preheader, initInst)) {
            return false;
        }
    }

    return true;
}

} // namespace

LoopTiling::LoopTiling(Function * _func, Module * _mod, int32_t _tileSize)
    : func(_func), mod(_mod), tileSize(_tileSize)
{}

bool LoopTiling::run()
{
    if (!func || !mod || func->isBuiltin() || func->getBlocks().empty() || tileSize <= 1) {
        return false;
    }

    // 优先尝试折叠重复不变归约循环
    auto & cache = func->getAnalysisCache();
    {
        auto & domTree = cache.getOrCompute<DominatorTree>([this] { return DominatorTree(func); });
        auto & loopInfo =
            cache.getOrCompute<LoopInfo>([this, &domTree] { return LoopInfo(func, &domTree); });
        auto & scev = cache.getOrCompute<ScalarEvolution>(
            [this, &domTree, &loopInfo] { return ScalarEvolution(func, &domTree, &loopInfo); });
        if (collapseRepeatedInvariantReductionLoop(func, mod, loopInfo, scev)) {
            cache.invalidateCFGAnalyses();
            return true;
        }
    }

    bool changed = false;
    while (true) {
        bool localChanged = false;
        auto & domTree = cache.getOrCompute<DominatorTree>([this] { return DominatorTree(func); });
        auto & loopInfo =
            cache.getOrCompute<LoopInfo>([this, &domTree] { return LoopInfo(func, &domTree); });
        auto & scev = cache.getOrCompute<ScalarEvolution>(
            [this, &domTree, &loopInfo] { return ScalarEvolution(func, &domTree, &loopInfo); });

        std::vector<BasicBlock *> headers;
        for (auto * bb : func->getBlocks()) {
            if (loopInfo.isLoopHeader(bb)) {
                headers.push_back(bb);
            }
        }

        std::stable_sort(headers.begin(),
                         headers.end(),
                         [&loopInfo](BasicBlock * lhs, BasicBlock * rhs) {
                             return loopInfo.getLoopDepth(lhs) < loopInfo.getLoopDepth(rhs);
                         });

        for (auto * header : headers) {
            if (tryTileHeader(header, loopInfo, scev)) {
                localChanged = true;
                changed = true;
                break;
            }
        }

        if (!localChanged) {
            break;
        }
        // 分块新建了多组循环控制基本块，CFG 派生分析整体失效
        cache.invalidateCFGAnalyses();
    }

    return changed;
}

bool LoopTiling::tryTileHeader(BasicBlock * header, LoopInfo & loopInfo, ScalarEvolution & scev)
{
    CanonicalLoop outer;
    if (!matchCanonicalLoop(scev, header, outer)) {
        return false;
    }

    auto * outerBodyBranch = dynamic_cast<BranchInst *>(outer.body->getTerminator());
    if (!outerBodyBranch || outer.body->getPredecessors().size() != 1 || outerBodyBranch->getTarget() == outer.header) {
        return false;
    }

    CanonicalLoop inner;
    if (!matchCanonicalLoop(scev, outerBodyBranch->getTarget(), inner) || inner.preheader != outer.body ||
        inner.exit != outer.latch) {
        return false;
    }

    const auto * outerBody = loopInfo.getLoopBody(outer.header);
    const auto * innerBody = loopInfo.getLoopBody(inner.header);
    if (!outerBody || !innerBody || !headerPhisAreAdjustable(outer, scev) || !headerPhisAreAdjustable(inner, scev) ||
        !loopHasOnlyCanonicalExit(*outerBody, outer.header, outer.exit) ||
        !loopHasOnlyCanonicalExit(*innerBody, inner.header, inner.exit) ||
        !isDependenceSafe(func, mod, scev, outer, inner, *innerBody)) {
        return false;
    }

    // 收益性判断(合法性已过；本地 matchCanonicalLoop 已保证两维 tripCount≥64)：
    // 估算整个嵌套的访存工作集，若明显能放进 L1，则不存在跨 tile 的 cache 复用收益，
    // 分块只会徒增控制流开销。用内层访存条数加权，避免误伤多数组(如 matmul)的循环。
    if (CostModel::profitabilityEnabled() && outer.hasConstTripCount && inner.hasConstTripCount) {
        long innerMemOps = 0;
        for (auto * bb : *innerBody) {
            for (auto * inst : bb->getInstructions()) {
                const IRInstOperator op = inst->getOp();
                if (op == IRInstOperator::IRINST_OP_LOAD || op == IRInstOperator::IRINST_OP_STORE) {
                    ++innerMemOps;
                }
            }
        }
        if (innerMemOps == 0) {
            innerMemOps = 1;
        }
        constexpr long kElemBytes = 4;  // SysY 仅 i32/f32，均为 4 字节
        const long footprint =
            static_cast<long>(outer.tripCount) * inner.tripCount * innerMemOps * kElemBytes;
        if (footprint < CostModel::kL1Bytes) {
            CostModel::remark("tiling", false, "working set fits L1 (no reuse benefit)");
            return false;
        }
    }

    auto * rowHeader = func->newBasicBlock();
    auto * rowLimitCheck = func->newBasicBlock();
    auto * rowLimitThen = func->newBasicBlock();
    auto * rowLimitElse = func->newBasicBlock();
    auto * rowLimitMerge = func->newBasicBlock();
    auto * colHeader = func->newBasicBlock();
    auto * colLimitCheck = func->newBasicBlock();
    auto * colLimitThen = func->newBasicBlock();
    auto * colLimitElse = func->newBasicBlock();
    auto * colLimitMerge = func->newBasicBlock();
    auto * colLatch = func->newBasicBlock();
    auto * rowLatch = func->newBasicBlock();

    std::vector<BasicBlock *> newBlocks = {rowHeader,
                                           rowLimitCheck,
                                           rowLimitThen,
                                           rowLimitElse,
                                           rowLimitMerge,
                                           colHeader,
                                           colLimitCheck,
                                           colLimitThen,
                                           colLimitElse,
                                           colLimitMerge,
                                           colLatch,
                                           rowLatch};
    for (auto * bb : newBlocks) {
        insertBlockBefore(func, bb, outer.header);
    }

    auto * zero = mod->newConstInt32(0);
    auto * tile = mod->newConstInt32(chooseTileSize(tileSize, outer, inner));

    auto * rowTile = new PhiInst(func, outer.induction->getType());
    auto * rowCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, rowTile, outer.boundValue, outer.cmp->getType());
    auto * rowCond = new CondBranchInst(func, rowCmp, rowLimitCheck, outer.exit);
    rowTile->addIncoming(zero, outer.preheader);
    rowHeader->addInstruction(rowTile);
    rowHeader->addInstruction(rowCmp);
    rowHeader->addInstruction(rowCond);
    rowHeader->linkSuccessor(rowLimitCheck);
    rowHeader->linkSuccessor(outer.exit);

    auto * rowPlus = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, rowTile, tile, rowTile->getType());
    auto * rowLimitCmp =
        new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, rowPlus, outer.boundValue, outer.cmp->getType());
    auto * rowLimitCond = new CondBranchInst(func, rowLimitCmp, rowLimitThen, rowLimitElse);
    rowLimitCheck->addInstruction(rowPlus);
    rowLimitCheck->addInstruction(rowLimitCmp);
    rowLimitCheck->addInstruction(rowLimitCond);
    rowLimitCheck->linkSuccessor(rowLimitThen);
    rowLimitCheck->linkSuccessor(rowLimitElse);

    rowLimitThen->addInstruction(new BranchInst(func, rowLimitMerge));
    rowLimitThen->linkSuccessor(rowLimitMerge);
    rowLimitElse->addInstruction(new BranchInst(func, rowLimitMerge));
    rowLimitElse->linkSuccessor(rowLimitMerge);

    auto * rowLimit = new PhiInst(func, outer.induction->getType());
    rowLimit->addIncoming(rowPlus, rowLimitThen);
    rowLimit->addIncoming(outer.boundValue, rowLimitElse);
    rowLimitMerge->addInstruction(rowLimit);
    rowLimitMerge->addInstruction(new BranchInst(func, colHeader));
    rowLimitMerge->linkSuccessor(colHeader);

    auto * colTile = new PhiInst(func, inner.induction->getType());
    auto * colCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, colTile, inner.boundValue, inner.cmp->getType());
    auto * colCond = new CondBranchInst(func, colCmp, colLimitCheck, rowLatch);
    colTile->addIncoming(zero, rowLimitMerge);
    colHeader->addInstruction(colTile);
    colHeader->addInstruction(colCmp);
    colHeader->addInstruction(colCond);
    colHeader->linkSuccessor(colLimitCheck);
    colHeader->linkSuccessor(rowLatch);

    auto * colPlus = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, colTile, tile, colTile->getType());
    auto * colLimitCmp =
        new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, colPlus, inner.boundValue, inner.cmp->getType());
    auto * colLimitCond = new CondBranchInst(func, colLimitCmp, colLimitThen, colLimitElse);
    colLimitCheck->addInstruction(colPlus);
    colLimitCheck->addInstruction(colLimitCmp);
    colLimitCheck->addInstruction(colLimitCond);
    colLimitCheck->linkSuccessor(colLimitThen);
    colLimitCheck->linkSuccessor(colLimitElse);

    colLimitThen->addInstruction(new BranchInst(func, colLimitMerge));
    colLimitThen->linkSuccessor(colLimitMerge);
    colLimitElse->addInstruction(new BranchInst(func, colLimitMerge));
    colLimitElse->linkSuccessor(colLimitMerge);

    auto * colLimit = new PhiInst(func, inner.induction->getType());
    colLimit->addIncoming(colPlus, colLimitThen);
    colLimit->addIncoming(inner.boundValue, colLimitElse);
    colLimitMerge->addInstruction(colLimit);
    colLimitMerge->addInstruction(new BranchInst(func, outer.header));
    colLimitMerge->linkSuccessor(outer.header);

    auto * colNext = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, colTile, tile, colTile->getType());
    colLatch->addInstruction(colNext);
    colLatch->addInstruction(new BranchInst(func, colHeader));
    colLatch->linkSuccessor(colHeader);
    colTile->addIncoming(colNext, colLatch);

    auto * rowNext = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, rowTile, tile, rowTile->getType());
    rowLatch->addInstruction(rowNext);
    rowLatch->addInstruction(new BranchInst(func, rowHeader));
    rowLatch->linkSuccessor(rowHeader);
    rowTile->addIncoming(rowNext, rowLatch);

    if (!rewriteTerminatorTarget(outer.preheader, outer.header, rowHeader) ||
        !retargetHeaderPhiInitialValues(func, scev, outer, colLimitMerge, rowTile, colLimitMerge, true) ||
        !retargetHeaderPhiInitialValues(func, scev, inner, inner.preheader, colTile, inner.preheader, false)) {
        return false;
    }

    outer.preheader->removeSuccessor(outer.header);
    outer.preheader->addSuccessor(rowHeader);
    outer.header->removePredecessor(outer.preheader);
    rowHeader->addPredecessor(outer.preheader);

    outer.branch->setFalseDest(colLatch);
    outer.header->removeSuccessor(outer.exit);
    outer.header->addSuccessor(colLatch);
    outer.exit->removePredecessor(outer.header);
    colLatch->addPredecessor(outer.header);
    rewritePhiIncomingBlock(outer.exit, outer.header, rowHeader);

    outer.cmp->setOperand(1, rowLimit);
    inner.cmp->setOperand(1, colLimit);

    // 分块后外层行循环可安全并行，标记其并行安全来源为Tiling；
    // 内外层分块循环本身不应被并行化，清除其元数据
    rowHeader->markLoopParallelSafeFromTiling();
    outer.header->clearLoopParallelMetadata();
    inner.header->clearLoopParallelMetadata();

    return true;
}
