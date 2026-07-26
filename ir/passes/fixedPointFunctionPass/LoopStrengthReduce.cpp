///
/// @file LoopStrengthReduce.cpp
/// @brief 循环地址强度削减 pass 实现
///

#include "LoopStrengthReduce.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <unordered_set>
#include <vector>

#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CondBranchInst.h"
#include "ConstInteger.h"
#include "DominatorTree.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "ICmpInst.h"
#include "Instruction.h"
#include "LoopInfo.h"
#include "Module.h"
#include "PhiInst.h"
#include "ScalarEvolution.h"
#include "Type.h"
#include "Value.h"
#include "Types/ArrayType.h"
#include "Types/PointerType.h"
#include "AnalysisCache.h"

namespace {

bool isDefinedInLoop(Value * value, const std::unordered_set<BasicBlock *> & loopBody)
{
    auto * inst = dynamic_cast<Instruction *>(value);
    return inst && inst->getParentBlock() && loopBody.find(inst->getParentBlock()) != loopBody.end();
}

bool isLoopInvariantValue(Value * value, const std::unordered_set<BasicBlock *> & loopBody)
{
    return !isDefinedInLoop(value, loopBody);
}

BasicBlock * findExistingPreheader(BasicBlock * header, const std::unordered_set<BasicBlock *> & loopBody)
{
    if (!header) {
        return nullptr;
    }

    BasicBlock * preheader = nullptr;
    for (auto * pred : header->getPredecessors()) {
        if (loopBody.find(pred) != loopBody.end()) {
            continue;
        }

        if (preheader != nullptr) {
            return nullptr;
        }
        preheader = pred;
    }

    if (!preheader || preheader->getSuccessors().size() != 1) {
        return nullptr;
    }

    return preheader;
}

BasicBlock * findUniqueLatch(BasicBlock * header, const std::unordered_set<BasicBlock *> & loopBody)
{
    if (!header) {
        return nullptr;
    }

    BasicBlock * latch = nullptr;
    for (auto * pred : header->getPredecessors()) {
        if (loopBody.find(pred) == loopBody.end()) {
            continue;
        }

        if (latch != nullptr) {
            return nullptr;
        }
        latch = pred;
    }

    return latch;
}

bool allUsesStayInLoop(Value * value, const std::unordered_set<BasicBlock *> & loopBody)
{
    if (!value) {
        return false;
    }

    for (auto * use : value->getUseList()) {
        auto * userInst = dynamic_cast<Instruction *>(use->getUser());
        if (!userInst || !userInst->getParentBlock() ||
            loopBody.find(userInst->getParentBlock()) == loopBody.end()) {
            return false;
        }
    }

    return true;
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

void insertBeforeInstruction(Instruction * before, Instruction * inst)
{
    if (!before || !inst || !before->getParentBlock()) {
        return;
    }

    auto * bb = before->getParentBlock();
    auto & insts = bb->getInstructions();
    auto pos = std::find(insts.begin(), insts.end(), before);
    if (pos == insts.end()) {
        return;
    }

    inst->setParentBlock(bb);
    insts.insert(pos, inst);
}

void insertPhiAtHeader(BasicBlock * header, PhiInst * phi)
{
    if (!header || !phi) {
        return;
    }

    auto & insts = header->getInstructions();
    auto insertPos = insts.begin();
    while (insertPos != insts.end() && dynamic_cast<PhiInst *>(*insertPos) != nullptr) {
        ++insertPos;
    }

    phi->setParentBlock(header);
    insts.insert(insertPos, phi);
}

const ScalarEvolution::AddRecurrenceExpr * getLoopIndexRecurrence(GetElementPtrInst * gep,
                                                                  BasicBlock * header,
                                                                  BasicBlock * preheader,
                                                                  BasicBlock * latch,
                                                                  ScalarEvolution & scev)
{
    if (!gep || gep->isDead()) {
        return nullptr;
    }

    const auto * recurrence = scev.getAddRecurrence(gep->getIndexOperand());
    if (!recurrence || !recurrence->isIntegerRecurrence() || recurrence->getLoopHeader() != header ||
        recurrence->getPreheader() != preheader || recurrence->getLatch() != latch) {
        return nullptr;
    }

    return recurrence;
}

/// @brief 判断两个 SCEV 表达式是否表示同一 affine 索引值
bool areEquivalentSCEVExpr(const ScalarEvolution::Expr * lhs, const ScalarEvolution::Expr * rhs)
{
    if (lhs == rhs) {
        return true;
    }
    if (!lhs || !rhs || lhs->getKind() != rhs->getKind() || lhs->getType() != rhs->getType()) {
        return false;
    }

    switch (lhs->getKind()) {
    case ScalarEvolution::ExprKind::Constant:
        return static_cast<const ScalarEvolution::ConstantExpr *>(lhs)->getIntValue() ==
               static_cast<const ScalarEvolution::ConstantExpr *>(rhs)->getIntValue();
    case ScalarEvolution::ExprKind::Unknown:
        return static_cast<const ScalarEvolution::UnknownExpr *>(lhs)->getValue() ==
               static_cast<const ScalarEvolution::UnknownExpr *>(rhs)->getValue();
    case ScalarEvolution::ExprKind::Add:
    case ScalarEvolution::ExprKind::Multiply: {
        const auto * lhsBinary = static_cast<const ScalarEvolution::BinaryExpr *>(lhs);
        const auto * rhsBinary = static_cast<const ScalarEvolution::BinaryExpr *>(rhs);
        return (areEquivalentSCEVExpr(lhsBinary->getLHS(), rhsBinary->getLHS()) &&
                areEquivalentSCEVExpr(lhsBinary->getRHS(), rhsBinary->getRHS())) ||
               (areEquivalentSCEVExpr(lhsBinary->getLHS(), rhsBinary->getRHS()) &&
                areEquivalentSCEVExpr(lhsBinary->getRHS(), rhsBinary->getLHS()));
    }
    case ScalarEvolution::ExprKind::AddRecurrence: {
        const auto * lhsRecurrence = static_cast<const ScalarEvolution::AddRecurrenceExpr *>(lhs);
        const auto * rhsRecurrence = static_cast<const ScalarEvolution::AddRecurrenceExpr *>(rhs);
        return lhsRecurrence->getLoopHeader() == rhsRecurrence->getLoopHeader() &&
               lhsRecurrence->getPreheader() == rhsRecurrence->getPreheader() &&
               lhsRecurrence->getLatch() == rhsRecurrence->getLatch() &&
               lhsRecurrence->getStep() == rhsRecurrence->getStep() &&
               lhsRecurrence->getStepKind() == rhsRecurrence->getStepKind() &&
               areEquivalentSCEVExpr(lhsRecurrence->getStartExpr(), rhsRecurrence->getStartExpr());
    }
    }

    return false;
}

void collectAdditiveSCEVTerms(const ScalarEvolution::Expr * expr,
                              std::vector<const ScalarEvolution::Expr *> & terms,
                              int64_t & constantSum,
                              bool & overflow)
{
    if (!expr || overflow) {
        return;
    }

    switch (expr->getKind()) {
    case ScalarEvolution::ExprKind::Constant: {
        const int64_t updated = constantSum + static_cast<const ScalarEvolution::ConstantExpr *>(expr)->getIntValue();
        if (updated < std::numeric_limits<int32_t>::min() || updated > std::numeric_limits<int32_t>::max()) {
            overflow = true;
            return;
        }
        constantSum = updated;
        return;
    }
    case ScalarEvolution::ExprKind::Add: {
        const auto * add = static_cast<const ScalarEvolution::BinaryExpr *>(expr);
        collectAdditiveSCEVTerms(add->getLHS(), terms, constantSum, overflow);
        collectAdditiveSCEVTerms(add->getRHS(), terms, constantSum, overflow);
        return;
    }
    case ScalarEvolution::ExprKind::Unknown:
    case ScalarEvolution::ExprKind::Multiply:
    case ScalarEvolution::ExprKind::AddRecurrence:
        terms.push_back(expr);
        return;
    }
}

bool tryComputeConstantAdditiveOffset(const ScalarEvolution::Expr * candidateExpr,
                                      const ScalarEvolution::Expr * seedExpr,
                                      int32_t & offset)
{
    std::vector<const ScalarEvolution::Expr *> candidateTerms;
    std::vector<const ScalarEvolution::Expr *> seedTerms;
    int64_t candidateConstant = 0;
    int64_t seedConstant = 0;
    bool overflow = false;
    collectAdditiveSCEVTerms(candidateExpr, candidateTerms, candidateConstant, overflow);
    collectAdditiveSCEVTerms(seedExpr, seedTerms, seedConstant, overflow);
    if (overflow || candidateTerms.size() != seedTerms.size()) {
        return false;
    }

    std::vector<bool> matched(seedTerms.size(), false);
    for (const auto * candidateTerm : candidateTerms) {
        bool found = false;
        for (std::size_t index = 0; index < seedTerms.size(); ++index) {
            if (matched[index] || !areEquivalentSCEVExpr(candidateTerm, seedTerms[index])) {
                continue;
            }

            matched[index] = true;
            found = true;
            break;
        }

        if (!found) {
            return false;
        }
    }

    const int64_t delta = candidateConstant - seedConstant;
    if (delta < std::numeric_limits<int32_t>::min() || delta > std::numeric_limits<int32_t>::max()) {
        return false;
    }

    offset = static_cast<int32_t>(delta);
    return true;
}

bool tryComputePointerOffset(const ScalarEvolution::Expr * candidateIndexExpr,
                             const ScalarEvolution::Expr * seedIndexExpr,
                             const ScalarEvolution::AddRecurrenceExpr * seedRecurrence,
                             int32_t & offset)
{
    offset = 0;
    if (!candidateIndexExpr || !seedIndexExpr || !seedRecurrence || !seedRecurrence->isIntegerRecurrence()) {
        return false;
    }

    if (areEquivalentSCEVExpr(candidateIndexExpr, seedIndexExpr)) {
        return true;
    }

    const auto * candidateRecurrence = dynamic_cast<const ScalarEvolution::AddRecurrenceExpr *>(candidateIndexExpr);
    if (candidateRecurrence && candidateRecurrence->isIntegerRecurrence() &&
        candidateRecurrence->getLoopHeader() == seedRecurrence->getLoopHeader() &&
        candidateRecurrence->getPreheader() == seedRecurrence->getPreheader() &&
        candidateRecurrence->getLatch() == seedRecurrence->getLatch() &&
        candidateRecurrence->getStep() == seedRecurrence->getStep() &&
        candidateRecurrence->getStepKind() == seedRecurrence->getStepKind()) {
        return tryComputeConstantAdditiveOffset(candidateRecurrence->getStartExpr(),
                                                seedRecurrence->getStartExpr(),
                                                offset);
    }

    return tryComputeConstantAdditiveOffset(candidateIndexExpr, seedIndexExpr, offset);
}

bool canRewriteConstantOffsetGEP(Type * pointerType)
{
    auto * ptrType = dynamic_cast<const PointerType *>(pointerType);
    return ptrType != nullptr && dynamic_cast<const ArrayType *>(ptrType->getPointeeType()) == nullptr;
}

struct ReducedGEPCandidate {
    GetElementPtrInst * gep = nullptr;
    int32_t offset = 0;
};

Value * materializeSCEVExpr(const ScalarEvolution::Expr * expr,
                           Function * func,
                           Module * mod,
                           BasicBlock * insertBlock)
{
    if (!expr || !func || !mod || !insertBlock || !expr->getType()) {
        return nullptr;
    }

    switch (expr->getKind()) {
    case ScalarEvolution::ExprKind::Constant: {
        const auto * constant = static_cast<const ScalarEvolution::ConstantExpr *>(expr);
        return mod->newConstInteger(expr->getType(), constant->getIntValue());
    }
    case ScalarEvolution::ExprKind::Unknown:
        return static_cast<const ScalarEvolution::UnknownExpr *>(expr)->getValue();
    case ScalarEvolution::ExprKind::AddRecurrence:
        return static_cast<const ScalarEvolution::AddRecurrenceExpr *>(expr)->getRepresentativeValue();
    case ScalarEvolution::ExprKind::Add:
    case ScalarEvolution::ExprKind::Multiply: {
        const auto * binary = static_cast<const ScalarEvolution::BinaryExpr *>(expr);
        Value * lhs = materializeSCEVExpr(binary->getLHS(), func, mod, insertBlock);
        Value * rhs = materializeSCEVExpr(binary->getRHS(), func, mod, insertBlock);
        if (!lhs || !rhs) {
            return nullptr;
        }

        IRInstOperator op = expr->getKind() == ScalarEvolution::ExprKind::Add ? IRInstOperator::IRINST_OP_ADD_I
                                                                               : IRInstOperator::IRINST_OP_MUL_I;
        auto * inst = new BinaryInst(func, op, lhs, rhs, expr->getType());
        insertBeforeTerminator(insertBlock, inst);
        return inst;
    }
    }

    return nullptr;
}

} // namespace

LoopStrengthReduce::LoopStrengthReduce(Function * _func, Module * _mod) : func(_func), mod(_mod)
{}

bool LoopStrengthReduce::run()
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
        std::vector<BasicBlock *> headers;
        for (auto * bb : func->getBlocks()) {
            if (loopInfo.isLoopHeader(bb)) {
                headers.push_back(bb);
            }
        }

        std::stable_sort(headers.begin(),
                         headers.end(),
                         [&loopInfo](BasicBlock * lhs, BasicBlock * rhs) {
                             return loopInfo.getLoopDepth(lhs) > loopInfo.getLoopDepth(rhs);
                         });

        for (auto * header : headers) {
            if (tryReduceHeader(header)) {
                localChanged = true;
                changed = true;
                break;
            }
        }

        if (!localChanged) {
            break;
        }
        // 强度削减仅新增/改写指令而不改动 CFG，因此只失效依赖具体指令的标量演化
        cache.invalidateValueAnalyses();
    }

    bool swept = sweepDeadInstructions();
    if (swept) {
        cache.invalidateValueAnalyses();
    }
    return swept || changed;
}

bool LoopStrengthReduce::tryReduceHeader(BasicBlock * header)
{
    if (!header) {
        return false;
    }

    auto & cache = func->getAnalysisCache();
    auto & domTree = cache.getOrCompute<DominatorTree>([this] { return DominatorTree(func); });
    auto & loopInfo =
        cache.getOrCompute<LoopInfo>([this, &domTree] { return LoopInfo(func, &domTree); });
    auto & scev = cache.getOrCompute<ScalarEvolution>(
        [this, &domTree, &loopInfo] { return ScalarEvolution(func, &domTree, &loopInfo); });
    const auto * bodyPtr = loopInfo.getLoopBody(header);
    if (!bodyPtr || bodyPtr->empty()) {
        return false;
    }

    const auto & loopBody = *bodyPtr;
    BasicBlock * preheader = findExistingPreheader(header, loopBody);
    BasicBlock * latch = findUniqueLatch(header, loopBody);
    if (!preheader || !latch) {
        return false;
    }

    if (reduceFirstCandidate(header, preheader, latch, scev, loopBody)) {
        return true;
    }

    if (reducePointerIVOffsetGEP(header, preheader, latch, scev, loopBody)) {
        return true;
    }

    return reduceInvariantStrideGEP(header, preheader, latch, loopBody);
}

bool LoopStrengthReduce::reduceFirstCandidate(BasicBlock * header,
                                              BasicBlock * preheader,
                                              BasicBlock * latch,
                                              ScalarEvolution & scev,
                                              const std::unordered_set<BasicBlock *> & loopBody)
{
    GetElementPtrInst * seed = nullptr;
    const ScalarEvolution::AddRecurrenceExpr * seedRecurrence = nullptr;
    for (auto * bb : func->getBlocks()) {
        if (loopBody.find(bb) == loopBody.end()) {
            continue;
        }

        for (auto * inst : bb->getInstructions()) {
            auto * gep = dynamic_cast<GetElementPtrInst *>(inst);
            if (!gep || !isLoopInvariantValue(gep->getBasePointer(), loopBody) || !allUsesStayInLoop(gep, loopBody)) {
                continue;
            }

            const auto * recurrence = getLoopIndexRecurrence(gep, header, preheader, latch, scev);
            if (!recurrence) {
                continue;
            }

            seed = gep;
            seedRecurrence = recurrence;
            break;
        }

        if (seed) {
            break;
        }
    }

    if (!seed) {
        return false;
    }

    Value * initValue = seedRecurrence->getStartValue();
    if (!initValue) {
        initValue = materializeSCEVExpr(seedRecurrence->getStartExpr(), func, mod, preheader);
    }
    if (!initValue) {
        return false;
    }

    Value * base = seed->getBasePointer();
    Type * pointerType = seed->getType();
    const bool decayArray = seed->isArrayDecayGEP();
    const bool allowConstantOffsetRewrite = canRewriteConstantOffsetGEP(pointerType);
    const ScalarEvolution::Expr * seedIndexExpr = scev.getSCEV(seed->getIndexOperand());
    if (!seedIndexExpr) {
        return false;
    }

    std::vector<ReducedGEPCandidate> candidates;
    for (auto * bb : func->getBlocks()) {
        if (loopBody.find(bb) == loopBody.end()) {
            continue;
        }

        for (auto * inst : bb->getInstructions()) {
            auto * gep = dynamic_cast<GetElementPtrInst *>(inst);
            if (!gep || gep->isDead()) {
                continue;
            }

            if (gep->getBasePointer() != base || gep->getType() != pointerType ||
                gep->isArrayDecayGEP() != decayArray || !allUsesStayInLoop(gep, loopBody)) {
                continue;
            }

            const ScalarEvolution::Expr * candidateIndexExpr = scev.getSCEV(gep->getIndexOperand());
            int32_t offset = 0;
            if (tryComputePointerOffset(candidateIndexExpr, seedIndexExpr, seedRecurrence, offset) &&
                (offset == 0 || allowConstantOffsetRewrite)) {
                candidates.push_back({gep, offset});
            }
        }
    }

    if (candidates.empty()) {
        return false;
    }

    auto * initPtr = new GetElementPtrInst(func, base, initValue, pointerType, decayArray);
    auto * ptrPhi = new PhiInst(func, pointerType);
    auto * stepValue = mod->newConstInteger(seed->getIndexOperand()->getType(), seedRecurrence->getStep());
    auto * nextPtr = new GetElementPtrInst(func, ptrPhi, stepValue, pointerType, false);

    insertBeforeTerminator(preheader, initPtr);
    insertPhiAtHeader(header, ptrPhi);
    insertBeforeTerminator(latch, nextPtr);

    ptrPhi->addIncoming(initPtr, preheader);
    ptrPhi->addIncoming(nextPtr, latch);

    for (const auto & candidate : candidates) {
        Value * replacement = ptrPhi;
        if (candidate.offset != 0) {
            auto * offsetValue = mod->newConstInteger(candidate.gep->getIndexOperand()->getType(), candidate.offset);
            auto * offsetPtr = new GetElementPtrInst(func, ptrPhi, offsetValue, candidate.gep->getType(), false);
            insertBeforeInstruction(candidate.gep, offsetPtr);
            replacement = offsetPtr;
        }

        candidate.gep->replaceAllUseWith(replacement);
        candidate.gep->clearOperands();
        candidate.gep->setDead(true);
    }

    return true;
}

bool LoopStrengthReduce::reducePointerIVOffsetGEP(BasicBlock * header,
                                                  BasicBlock * preheader,
                                                  BasicBlock * latch,
                                                  ScalarEvolution & scev,
                                                  const std::unordered_set<BasicBlock *> & loopBody)
{
    auto & domTree = func->getAnalysisCache().getOrCompute<DominatorTree>([this] { return DominatorTree(func); });

    // 寻找种子：下标式 gep(P, idx)，P 为本循环的指针型归纳 phi，idx 循环不变，结果为标量指针
    GetElementPtrInst * seed = nullptr;
    const ScalarEvolution::AddRecurrenceExpr * baseRec = nullptr;
    for (auto * bb : func->getBlocks()) {
        if (loopBody.find(bb) == loopBody.end()) {
            continue;
        }

        for (auto * inst : bb->getInstructions()) {
            auto * gep = dynamic_cast<GetElementPtrInst *>(inst);
            // 种子必须是下标式 gep（decay=true，渲染为 ", i32 0, i32 idx"），
            // 即从行数组基址按列索引取标量元素
            if (!gep || gep->isDead() || !gep->isArrayDecayGEP()) {
                continue;
            }

            // 列索引必须循环不变，且其定义支配 preheader（保证可在 preheader 重物化）
            Value * index = gep->getIndexOperand();
            if (!isLoopInvariantValue(index, loopBody) || !allUsesStayInLoop(gep, loopBody)) {
                continue;
            }
            if (auto * indexInst = dynamic_cast<Instruction *>(index)) {
                if (!indexInst->getParentBlock() || !domTree.dominates(indexInst->getParentBlock(), preheader)) {
                    continue;
                }
            }

            // 基址必须是本循环的指针型仿射递推
            const auto * rec = scev.getAddRecurrence(gep->getBasePointer());
            if (!rec || !rec->isPointerRecurrence() || rec->getLoopHeader() != header ||
                rec->getPreheader() != preheader || rec->getLatch() != latch) {
                continue;
            }

            // 结果必须是标量指针（int*/float*），避免多维数组步长换算的复杂性
            auto * resultPtrType = dynamic_cast<PointerType *>(gep->getType());
            if (!resultPtrType) {
                continue;
            }
            const Type * pointee = resultPtrType->getPointeeType();
            if (!pointee || !(pointee->isIntegerType() || pointee->isFloatType())) {
                continue;
            }

            seed = gep;
            baseRec = rec;
            break;
        }

        if (seed) {
            break;
        }
    }

    if (!seed) {
        return false;
    }

    // 取基址所指行数组的长度 N（[N x T]*），新指针 IV 每步前进 step*N 个元素
    Value * basePtr = seed->getBasePointer();
    auto * basePtrType = dynamic_cast<PointerType *>(basePtr->getType());
    if (!basePtrType) {
        return false;
    }
    const auto * rowArrayType = dynamic_cast<const ArrayType *>(basePtrType->getPointeeType());
    if (!rowArrayType) {
        return false;
    }
    const int64_t newStep = static_cast<int64_t>(baseRec->getStep()) * rowArrayType->getNumElements();
    if (newStep <= 0 || newStep > std::numeric_limits<int32_t>::max()) {
        return false;
    }

    Value * baseInit = baseRec->getStartValue();
    if (!baseInit) {
        return false;
    }

    Type * elemPtrType = seed->getType();
    Value * invariantIndex = seed->getIndexOperand();

    // 收集所有同基址、同不变索引的下标式 gep，可被同一新指针 IV 覆盖
    std::vector<GetElementPtrInst *> targets;
    for (auto * bb : func->getBlocks()) {
        if (loopBody.find(bb) == loopBody.end()) {
            continue;
        }

        for (auto * inst : bb->getInstructions()) {
            auto * gep = dynamic_cast<GetElementPtrInst *>(inst);
            if (!gep || gep->isDead() || !gep->isArrayDecayGEP()) {
                continue;
            }
            if (gep->getBasePointer() == basePtr && gep->getIndexOperand() == invariantIndex &&
                gep->getType() == elemPtrType && allUsesStayInLoop(gep, loopBody)) {
                targets.push_back(gep);
            }
        }
    }
    if (targets.empty()) {
        return false;
    }

    // 新指针 IV：init = gep(baseInit, 0, idx)（下标式 decay=true，得 T*，把不变列偏移折入起点），
    //            step = gep(ptrPhi, step*N)（指针式 decay=false，前进一行）
    auto * initPtr = new GetElementPtrInst(func, baseInit, invariantIndex, elemPtrType, true);
    auto * ptrPhi = new PhiInst(func, elemPtrType);
    auto * stepValue = mod->newConstInteger(seed->getIndexOperand()->getType(), static_cast<int32_t>(newStep));
    auto * nextPtr = new GetElementPtrInst(func, ptrPhi, stepValue, elemPtrType, false);

    insertBeforeTerminator(preheader, initPtr);
    insertPhiAtHeader(header, ptrPhi);
    insertBeforeTerminator(latch, nextPtr);
    ptrPhi->addIncoming(initPtr, preheader);
    ptrPhi->addIncoming(nextPtr, latch);

    for (auto * gep : targets) {
        gep->replaceAllUseWith(ptrPhi);
        gep->clearOperands();
        gep->setDead(true);
    }

    return true;
}

/// @brief 将 gep(base, IV*S + C) 的下标改写为每步累加 s*S 的 i32 归纳递推
///
/// reduceFirstCandidate 依赖 SCEV 常量步长递推，无法表达 {C,+,S}（S 为运行期
/// 循环不变量，如按列访问 A[j*n+i] 中的 n）。本方法直接在 IR 上匹配：
///   idx = add(mul(IV, S), C) / add(C, mul(IV, S)) / mul(IV, S)
/// 其中 IV 为本循环头的 i32 归纳 phi（latch 入值为 add(IV, 常量 s)），
/// S、C 均循环不变。改写为：
///   preheader: initIdx = IV0*S + C（IV0 为常量 0 时折叠）
///              stepIdx = s*S（s==1 时直接复用 S）
///   header:    idxPhi = phi [initIdx, preheader], [nextIdx, latch]
///   latch:     nextIdx = add(idxPhi, stepIdx)
///   体  内:    gep 的下标操作数替换为 idxPhi，每迭代省下一条乘法
///
/// 递推保持在 i32 宽度内，因而在二进制补码回绕语义下逐点等价，不依赖
/// "有符号运算不溢出"这一未定义行为假设：模 2^32 剩余类环上乘法对加法可分配，
///   wrap32(wrap32(IV0 + n·s)·S + C) = wrap32((IV0·S + C) + n·(s·S))
///                                   = wrap32(initIdx + n·stepIdx)
/// 对每个 n 恒成立，回绕与否两边同步。注意这里刻意不下沉为 64 位指针递推
/// ptr += S·4：那等价于按无回绕的整数值计算地址，只在 i32 下标回绕时才与
/// 源语义不同，属于对有符号溢出未定义的利用（GCC -O3 正是这么做的）。
/// @param header 循环头
/// @param preheader 唯一 preheader
/// @param latch 唯一 latch
/// @param loopBody 循环体块集合
/// @return true 表示至少改写了一个 gep
bool LoopStrengthReduce::reduceInvariantStrideGEP(BasicBlock * header,
                                                  BasicBlock * preheader,
                                                  BasicBlock * latch,
                                                  const std::unordered_set<BasicBlock *> & loopBody)
{
    // 匹配种子：gep(不变基址, add(mul(IV,S),C) | mul(IV,S))，结果为标量指针
    GetElementPtrInst * seed = nullptr;
    PhiInst * ivPhi = nullptr;
    Value * strideValue = nullptr;   // S
    Value * addendValue = nullptr;   // C，可为空
    int32_t ivStep = 0;              // s

    for (auto * bb : func->getBlocks()) {
        if (loopBody.find(bb) == loopBody.end()) {
            continue;
        }

        for (auto * inst : bb->getInstructions()) {
            auto * gep = dynamic_cast<GetElementPtrInst *>(inst);
            if (!gep || gep->isDead() || gep->isArrayDecayGEP()) {
                continue;
            }
            if (!isLoopInvariantValue(gep->getBasePointer(), loopBody) || !allUsesStayInLoop(gep, loopBody)) {
                continue;
            }

            // 结果必须是标量指针（int*/float*），保证 gep 索引单位即元素大小
            auto * resultPtrType = dynamic_cast<PointerType *>(gep->getType());
            if (!resultPtrType) {
                continue;
            }
            const Type * pointee = resultPtrType->getPointeeType();
            if (!pointee || !(pointee->isIntegerType() || pointee->isFloatType())) {
                continue;
            }

            // 拆解 idx = mul(...) 或 add(mul(...), C)
            Value * index = gep->getIndexOperand();
            auto * indexInst = dynamic_cast<BinaryInst *>(index);
            if (!indexInst) {
                continue;
            }
            BinaryInst * mulInst = nullptr;
            Value * addend = nullptr;
            if (indexInst->getOp() == IRInstOperator::IRINST_OP_MUL_I) {
                mulInst = indexInst;
            } else if (indexInst->getOp() == IRInstOperator::IRINST_OP_ADD_I) {
                mulInst = dynamic_cast<BinaryInst *>(indexInst->getLHS());
                addend = indexInst->getRHS();
                if (!mulInst || mulInst->getOp() != IRInstOperator::IRINST_OP_MUL_I) {
                    mulInst = dynamic_cast<BinaryInst *>(indexInst->getRHS());
                    addend = indexInst->getLHS();
                }
            }
            if (!mulInst || mulInst->getOp() != IRInstOperator::IRINST_OP_MUL_I) {
                continue;
            }
            if (addend && (isDefinedInLoop(addend, loopBody) || !addend->getType() ||
                           !addend->getType()->isInt32Type())) {
                continue;
            }

            // mul 的一侧是本循环归纳 phi，另一侧循环不变
            PhiInst * phi = nullptr;
            Value * stride = nullptr;
            for (int side = 0; side < 2; ++side) {
                Value * a = (side == 0) ? mulInst->getLHS() : mulInst->getRHS();
                Value * b = (side == 0) ? mulInst->getRHS() : mulInst->getLHS();
                auto * candPhi = dynamic_cast<PhiInst *>(a);
                if (candPhi && candPhi->getParentBlock() == header && !isDefinedInLoop(b, loopBody)) {
                    phi = candPhi;
                    stride = b;
                    break;
                }
            }
            if (!phi || !stride || !phi->getType() || !phi->getType()->isInt32Type() ||
                !stride->getType() || !stride->getType()->isInt32Type()) {
                continue;
            }

            // 归纳 phi 结构：恰两条入边（preheader + latch），latch 入值为 add(phi, 常量 s)
            if (phi->getIncomingCount() != 2) {
                continue;
            }
            Value * latchIncoming = nullptr;
            bool shapeOk = true;
            for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
                BasicBlock * inBlock = phi->getIncomingBlock(i);
                if (inBlock == preheader) {
                    continue;
                }
                if (inBlock != latch) {
                    shapeOk = false;
                    break;
                }
                latchIncoming = phi->getIncomingValue(i);
            }
            if (!shapeOk || !latchIncoming) {
                continue;
            }
            auto * stepAdd = dynamic_cast<BinaryInst *>(latchIncoming);
            if (!stepAdd || stepAdd->getOp() != IRInstOperator::IRINST_OP_ADD_I) {
                continue;
            }
            ConstInteger * stepConst = nullptr;
            if (stepAdd->getLHS() == phi) {
                stepConst = dynamic_cast<ConstInteger *>(stepAdd->getRHS());
            } else if (stepAdd->getRHS() == phi) {
                stepConst = dynamic_cast<ConstInteger *>(stepAdd->getLHS());
            }
            if (!stepConst || stepConst->getVal() == 0) {
                continue;
            }

            seed = gep;
            ivPhi = phi;
            strideValue = stride;
            addendValue = addend;
            ivStep = stepConst->getVal();
            break;
        }

        if (seed) {
            break;
        }
    }

    if (!seed) {
        return false;
    }

    Value * ivInit = nullptr;
    for (int32_t i = 0; i < ivPhi->getIncomingCount(); ++i) {
        if (ivPhi->getIncomingBlock(i) == preheader) {
            ivInit = ivPhi->getIncomingValue(i);
            break;
        }
    }
    if (!ivInit) {
        return false;
    }

    Type * i32Type = ivPhi->getType();
    Type * elemPtrType = seed->getType();
    Value * base = seed->getBasePointer();

    // preheader：initIdx = IV0*S + C，IV0 为常量 0 时折叠乘法项
    Value * initIdx = nullptr;
    auto * ivInitConst = dynamic_cast<ConstInteger *>(ivInit);
    if (!ivInitConst || ivInitConst->getVal() != 0) {
        auto * initMul = new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I, ivInit, strideValue, i32Type);
        insertBeforeTerminator(preheader, initMul);
        initIdx = initMul;
    }
    if (addendValue) {
        if (initIdx) {
            auto * initAdd = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, initIdx, addendValue, i32Type);
            insertBeforeTerminator(preheader, initAdd);
            initIdx = initAdd;
        } else {
            initIdx = addendValue;
        }
    }
    if (!initIdx) {
        initIdx = mod->newConstInt32(0);
    }

    // stepIdx = s*S（s==1 时直接复用 S）
    Value * stepIdx = strideValue;
    if (ivStep != 1) {
        auto * stepMul = new BinaryInst(func,
                                        IRInstOperator::IRINST_OP_MUL_I,
                                        strideValue,
                                        mod->newConstInt32(ivStep),
                                        i32Type);
        insertBeforeTerminator(preheader, stepMul);
        stepIdx = stepMul;
    }

    // 下标递推：idxPhi = phi [initIdx, preheader], [idxPhi + stepIdx, latch]
    // 全程停留在 i32，回绕行为与原 IV*S+C 逐迭代一致
    auto * idxPhi = new PhiInst(func, i32Type);
    auto * nextIdx = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, idxPhi, stepIdx, i32Type);

    insertPhiAtHeader(header, idxPhi);
    insertBeforeTerminator(latch, nextIdx);
    idxPhi->addIncoming(initIdx, preheader);
    idxPhi->addIncoming(nextIdx, latch);

    // 保留 gep 本身（地址仍由 base + sext(idx)*elemSize 现算），只换掉下标来源
    auto * reducedGep = new GetElementPtrInst(func, base, idxPhi, elemPtrType, seed->isArrayDecayGEP());
    insertBeforeInstruction(seed, reducedGep);
    seed->replaceAllUseWith(reducedGep);
    seed->clearOperands();
    seed->setDead(true);

    return true;
}

bool LoopStrengthReduce::sweepDeadInstructions() const
{
    if (!func) {
        return false;
    }

    bool removed = false;
    for (auto * bb : func->getBlocks()) {
        auto & insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end();) {
            Instruction * inst = *it;
            if (!inst || !inst->isDead()) {
                ++it;
                continue;
            }

            inst->clearOperands();
            auto next = std::next(it);
            insts.erase(it);
            delete inst;
            it = next;
            removed = true;
        }
    }

    return removed;
}
