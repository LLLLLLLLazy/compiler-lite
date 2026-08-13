///
/// @file LoopStrengthReduce.cpp
/// @brief 循环地址强度削减 pass 实现
///

#include "LoopStrengthReduce.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CondBranchInst.h"
#include "ConstInteger.h"
#include "DominatorTree.h"
#include "FCmpInst.h"
#include "FPToSIInst.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "ICmpInst.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "LoopInfo.h"
#include "Module.h"
#include "PhiInst.h"
#include "ScalarEvolution.h"
#include "SelectInst.h"
#include "SIToFPInst.h"
#include "StoreInst.h"
#include "Type.h"
#include "Value.h"
#include "ZExtInst.h"
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

/// @brief 判断指令是否属于循环版本化克隆支持的类型
///
/// 白名单限定为纯数据流指令与普通访存：二元运算、比较、访存、gep、类型转换、
/// select、phi。call/alloca/copy/向量指令与 return 一律拒绝——版本化只面向
/// 简单计数循环，遇到复杂形态放弃版本化仅保留精确慢路径即可，不承担风险。
bool isVersionCloneableInstruction(Instruction * inst)
{
    return dynamic_cast<BinaryInst *>(inst) != nullptr || dynamic_cast<ICmpInst *>(inst) != nullptr ||
           dynamic_cast<FCmpInst *>(inst) != nullptr || dynamic_cast<LoadInst *>(inst) != nullptr ||
           dynamic_cast<StoreInst *>(inst) != nullptr || dynamic_cast<GetElementPtrInst *>(inst) != nullptr ||
           dynamic_cast<ZExtInst *>(inst) != nullptr || dynamic_cast<SelectInst *>(inst) != nullptr ||
           dynamic_cast<SIToFPInst *>(inst) != nullptr || dynamic_cast<FPToSIInst *>(inst) != nullptr ||
           dynamic_cast<PhiInst *>(inst) != nullptr;
}

/// @brief 克隆指令外壳（操作数暂指向原值，由调用方统一重映射；phi 入边不填）
Instruction * cloneVersionInstructionShell(Instruction * inst, Function * func)
{
    if (auto * binary = dynamic_cast<BinaryInst *>(inst)) {
        return new BinaryInst(func, binary->getOp(), binary->getLHS(), binary->getRHS(), binary->getType());
    }
    if (auto * icmp = dynamic_cast<ICmpInst *>(inst)) {
        return new ICmpInst(func, icmp->getOp(), icmp->getLHS(), icmp->getRHS(), icmp->getType());
    }
    if (auto * fcmp = dynamic_cast<FCmpInst *>(inst)) {
        return new FCmpInst(func, fcmp->getOp(), fcmp->getLHS(), fcmp->getRHS(), fcmp->getType());
    }
    if (auto * load = dynamic_cast<LoadInst *>(inst)) {
        return new LoadInst(func, load->getPointerOperand(), load->getType());
    }
    if (auto * store = dynamic_cast<StoreInst *>(inst)) {
        return new StoreInst(func, store->getValueOperand(), store->getPointerOperand());
    }
    if (auto * gep = dynamic_cast<GetElementPtrInst *>(inst)) {
        return new GetElementPtrInst(func, gep->getBasePointer(), gep->getIndexOperand(), gep->getType(),
                                     gep->isArrayDecayGEP(), gep->isIndexPreScaled());
    }
    if (auto * zext = dynamic_cast<ZExtInst *>(inst)) {
        return new ZExtInst(func, zext->getSource(), zext->getType());
    }
    if (auto * select = dynamic_cast<SelectInst *>(inst)) {
        return new SelectInst(func, select->getCondition(), select->getTrueValue(), select->getFalseValue(),
                              select->getType());
    }
    if (auto * sitofp = dynamic_cast<SIToFPInst *>(inst)) {
        return new SIToFPInst(func, sitofp->getSource(), sitofp->getType());
    }
    if (auto * fptosi = dynamic_cast<FPToSIInst *>(inst)) {
        return new FPToSIInst(func, fptosi->getSource(), fptosi->getType());
    }
    if (dynamic_cast<PhiInst *>(inst) != nullptr) {
        return new PhiInst(func, inst->getType());
    }
    return nullptr;
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
        // 普通强度削减只改写值；循环版本化会自行失效 CFG 分析。这里统一清除
        // 依赖具体指令的值分析，供下一轮从当前 IR 重新识别候选。
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

    // 先尝试构造运行期无回绕检查守护的快路径克隆：此刻循环仍是原始 mul 形态，
    // 克隆体内的下标计算被改写为 64 位指针递推。无论成败，原循环随后都改写为
    // i32 下标递推——版本化成功时作为检查不通过的慢路径兜底，失败时作为唯一路径。
    // 版本化成功后 header 的环外前驱变为 slowEntry，新增 phi 的入边块须随之切换。
    BasicBlock * slowEntry = tryVersionInvariantStrideLoop(header, preheader, latch, loopBody, seed, ivPhi,
                                                           ivInit, ivStep, initIdx, stepIdx);

    // 下标递推：idxPhi = phi [initIdx, preheader], [idxPhi + stepIdx, latch]
    // 全程停留在 i32，回绕行为与原 IV*S+C 逐迭代一致
    auto * idxPhi = new PhiInst(func, i32Type);
    auto * nextIdx = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, idxPhi, stepIdx, i32Type);

    insertPhiAtHeader(header, idxPhi);
    insertBeforeTerminator(latch, nextIdx);
    idxPhi->addIncoming(initIdx, slowEntry != nullptr ? slowEntry : preheader);
    idxPhi->addIncoming(nextIdx, latch);

    // 保留 gep 本身（地址仍由 base + sext(idx)*elemSize 现算），只换掉下标来源
    auto * reducedGep = new GetElementPtrInst(func, base, idxPhi, elemPtrType, seed->isArrayDecayGEP());
    insertBeforeInstruction(seed, reducedGep);
    seed->replaceAllUseWith(reducedGep);
    seed->clearOperands();
    seed->setDead(true);

    if (slowEntry != nullptr) {
        func->getAnalysisCache().invalidateCFGAnalyses();
    }
    return true;
}

/// @brief 为不变量步长下标递推构造"运行期无回绕检查 + 快慢双版本循环"
///
/// 慢路径（i32 下标递推）无条件精确但每迭代需 slli+add 现算地址；快路径
/// （64 位指针递推）每迭代仅一条加法，但只有 i32 下标序列全程不回绕时才与
/// 源语义一致。本方法构造两级运行期检查，"证明"该前提后才进入快路径克隆，
/// 否则回落原循环：
///
///   编译期前提：s == 1，IV0 为非负常量 v0（v0 < INT32_MAX），
///              循环体 ≤3 块（收益门控，见正文）
///   preheader:  km = Bound - (v0+1)            —— 步数上界 kmax
///               km ≥ kMinProfitableTrips ? → checkBlock : → slowPre
///   checkBlock: q = (INT32_MAX - initIdx) / km
///               (initIdx|stepIdx) ≥ 0 ∧ stepIdx ≤ q ? → fastPre : → slowPre
///
/// 小行程循环在 preheader 只付 2 条指令便回落精确慢路径——此时每迭代 +2
/// 指令的总代价上限 2·km 本就与检查成本同量级，版本化无利可图。
///
/// 精确性论证（回绕语义下逐点成立，检查只做证明、不做假设）：
///   (i)  头测 IV < Bound 门控每次迭代：若有任何迭代执行则 Bound ≥ v0+1 ≥ 1，
///        km 的 i32 计算不回绕；Bound ≤ v0 时循环零迭代，快慢路径平凡一致，
///        检查取值无关紧要（km 即使回绕也只影响选路，不影响正确性）。
///   (ii) s == 1 且每次继续前都通过 IV < Bound ≤ INT32_MAX，IV+1 恒不回绕，
///        故第 k 次迭代 IV = v0+k 精确成立且 k ≤ km；进入 checkBlock 时
///        km ≥ kMinProfitableTrips ≥ 1，除法无需钳制除数。
///   (iii) 理想下标 E_k = initIdx + k·stepIdx：E_0 = initIdx ≥ 0 且单调不减
///        （两个非负条件合并为符号位测试 (initIdx|stepIdx) ≥ 0），
///        E_k ≤ initIdx + km·((INT32_MAX-initIdx)/km) ≤ INT32_MAX。
///        全程 E_k ∈ [0, INT32_MAX] ⇒ wrap32(E_k) = E_k ⇒ 指针递推
///        base+4·E_k 与源地址 base+4·sext(wrap32(E_k)) 相等。
///   检查不成立（含负步长、负起点、可能越界）一律走慢路径，正确性由
///   慢路径的模 2^32 环同态论证兜底；快路径执行的前提被运行期证明，
///   两条路径均不利用有符号溢出未定义行为。
///
/// 结构改写：preheader 原无条件跳转改为 condbr(km≥T, checkBlock, slowPre)；
/// checkBlock 以 condbr(fast, fastPre, slowPre) 收尾；slowPre 空块入原循环，
/// 使 header 恒保持 slowPre+latch 双前驱。fastPre 物化 initPtr 后进入循环
/// 克隆；克隆内种子 gep 替换为 ptrPhi = phi [initPtr, fastPre],
/// [gep(ptrPhi, stepIdx), latchClone]。出口块 phi 为克隆出口边补入映射值。
/// @brief 匹配 GuardedTailCollapse 钳制出的嵌套三角形态，构造整体提升到外层
///        循环之外的无回绕检查
///
/// 目标形态（transpose 类三角遍历，GTC 折叠后的规范产物）：
///   bound   = select(icmp lt(inv, B0), inv 或 inv+1, B0)   —— min 钳制
///   initIdx = inv = 外层循环头 phi P（起点非负常量、步长 +1、头测 P < C0）
///   S、B0、C0 在外层 preheader（P 的非递增入边块 outerPre）处已可用
///
/// 此时每行 i 的下标峰值可整体伸缩：
///   E_i = i + km_i·S，km_i = bound_i - 1 ≤ B0-1（min 钳制保证），i ≤ C0-1
///   ⇒ max_i E_i ≤ (C0-1) + (B0-1)·S
/// 于是一次性在 outerPre 检查
///   S ≥ 0 ∧ C0-1 ≥ 0 ∧ S ≤ (INT32_MAX - (C0-1)) / max(B0-1, 1)
/// 便覆盖全部行；行内选路退化为一条对提升 i1 的条件跳转，小行程行不再支付
/// 逐行检查。B0 ≤ 1 或 C0 ≤ 0 的角落里被除数/被减数即使回绕也只影响选路
/// （相应行/整个嵌套零迭代，快慢平凡一致），不影响正确性。
/// 外层 IV 自身不回绕由起点非负常量 + 步长 1 + 头测 P < C0 ≤ INT32_MAX 保证，
/// 与内层论证 (ii) 同构。
/// @return 提升检查的 i1 结果值；形态不匹配返回空（回落逐行两级检查）
Value * LoopStrengthReduce::tryBuildHoistedNestCheck(BasicBlock * preheader,
                                                     Value * bound,
                                                     Value * initIdx,
                                                     Value * stepIdx,
                                                     Type * i1Type,
                                                     Type * i32Type)
{
    auto * sel = dynamic_cast<SelectInst *>(bound);
    if (!sel) {
        return nullptr;
    }
    auto * selCmp = dynamic_cast<ICmpInst *>(sel->getCondition());
    if (!selCmp || selCmp->getOp() != IRInstOperator::IRINST_OP_LT_I) {
        return nullptr;
    }
    Value * inv = selCmp->getLHS();
    Value * b0 = sel->getFalseValue();
    if (selCmp->getRHS() != b0) {
        return nullptr;
    }

    // 真臂须为 inv 或 inv+1，保证 select 结果 ≤ B0
    auto matchPlusOne = [](Value * value, Value * base) -> bool {
        auto * add = dynamic_cast<BinaryInst *>(value);
        if (!add || add->getOp() != IRInstOperator::IRINST_OP_ADD_I) {
            return false;
        }
        auto * lhsConst = dynamic_cast<ConstInteger *>(add->getLHS());
        auto * rhsConst = dynamic_cast<ConstInteger *>(add->getRHS());
        return (add->getLHS() == base && rhsConst && rhsConst->getVal() == 1) ||
               (add->getRHS() == base && lhsConst && lhsConst->getVal() == 1);
    };
    Value * trueArm = sel->getTrueValue();
    if (trueArm != inv && !matchPlusOne(trueArm, inv)) {
        return nullptr;
    }

    // initIdx 必须就是钳制不变量本身，且为外层循环头 phi
    if (inv != initIdx) {
        return nullptr;
    }
    auto * outerIv = dynamic_cast<PhiInst *>(inv);
    if (!outerIv || outerIv->getIncomingCount() != 2) {
        return nullptr;
    }

    // 外层归纳结构：一条入边为 add(P, 1)，另一条入边为非负常量起点
    BasicBlock * outerPre = nullptr;
    ConstInteger * outerInit = nullptr;
    bool haveOuterStep = false;
    for (int32_t i = 0; i < outerIv->getIncomingCount(); ++i) {
        Value * incoming = outerIv->getIncomingValue(i);
        if (matchPlusOne(incoming, outerIv)) {
            haveOuterStep = true;
            continue;
        }
        outerPre = outerIv->getIncomingBlock(i);
        outerInit = dynamic_cast<ConstInteger *>(incoming);
    }
    if (!haveOuterStep || !outerPre || !outerInit || outerInit->getVal() < 0 ||
        outerInit->getVal() >= std::numeric_limits<int32_t>::max()) {
        return nullptr;
    }

    // 外层头测 P < C0（真分支入体），内层 preheader 须被外层体支配——
    // 保证内层每次进入时 P < C0 成立
    BasicBlock * outerHeader = outerIv->getParentBlock();
    auto * outerBr = dynamic_cast<CondBranchInst *>(outerHeader->getTerminator());
    if (!outerBr) {
        return nullptr;
    }
    auto * outerCmp = dynamic_cast<ICmpInst *>(outerBr->getCondition());
    if (!outerCmp) {
        return nullptr;
    }
    Value * outerBound = nullptr;
    if (outerCmp->getOp() == IRInstOperator::IRINST_OP_LT_I && outerCmp->getLHS() == outerIv) {
        outerBound = outerCmp->getRHS();
    } else if (outerCmp->getOp() == IRInstOperator::IRINST_OP_GT_I && outerCmp->getRHS() == outerIv) {
        outerBound = outerCmp->getLHS();
    } else {
        return nullptr;
    }
    BasicBlock * outerBody = outerBr->getTrueDest();
    if (outerBody == outerHeader) {
        return nullptr;
    }

    auto & cache = func->getAnalysisCache();
    auto & domTree = cache.getOrCompute<DominatorTree>([this] { return DominatorTree(func); });
    if (!domTree.dominates(outerPre, preheader) || !domTree.dominates(outerBody, preheader)) {
        return nullptr;
    }

    // S、B0、C0 在 outerPre 末尾可用（常量/形参/全局天然可用，指令须支配 outerPre）
    auto availableAtOuterPre = [&domTree, outerPre](Value * value) -> bool {
        auto * inst = dynamic_cast<Instruction *>(value);
        if (!inst) {
            return true;
        }
        BasicBlock * defBlock = inst->getParentBlock();
        return defBlock && (defBlock == outerPre || domTree.dominates(defBlock, outerPre));
    };
    if (!availableAtOuterPre(stepIdx) || !availableAtOuterPre(b0) || !availableAtOuterPre(outerBound)) {
        return nullptr;
    }

    // ---- outerPre 一次性物化整嵌套检查 ----
    auto * zeroConst = mod->newConstInt32(0);
    auto * oneConst = mod->newConstInt32(1);
    auto * intMaxConst = mod->newConstInt32(std::numeric_limits<int32_t>::max());

    auto * b0m1 = new BinaryInst(func, IRInstOperator::IRINST_OP_SUB_I, b0, oneConst, i32Type);
    insertBeforeTerminator(outerPre, b0m1);
    auto * b0Positive = new ICmpInst(func, IRInstOperator::IRINST_OP_GT_I, b0m1, zeroConst, i1Type);
    insertBeforeTerminator(outerPre, b0Positive);
    auto * b0Clamped = new SelectInst(func, b0Positive, b0m1, oneConst, i32Type);
    insertBeforeTerminator(outerPre, b0Clamped);
    auto * c0m1 = new BinaryInst(func, IRInstOperator::IRINST_OP_SUB_I, outerBound, oneConst, i32Type);
    insertBeforeTerminator(outerPre, c0m1);
    auto * room = new BinaryInst(func, IRInstOperator::IRINST_OP_SUB_I, intMaxConst, c0m1, i32Type);
    insertBeforeTerminator(outerPre, room);
    auto * quot = new BinaryInst(func, IRInstOperator::IRINST_OP_DIV_I, room, b0Clamped, i32Type);
    insertBeforeTerminator(outerPre, quot);
    auto * signOr = new BinaryInst(func, IRInstOperator::IRINST_OP_OR_I, stepIdx, c0m1, i32Type);
    insertBeforeTerminator(outerPre, signOr);
    auto * signOk = new ICmpInst(func, IRInstOperator::IRINST_OP_GE_I, signOr, zeroConst, i1Type);
    insertBeforeTerminator(outerPre, signOk);
    auto * stepFits = new ICmpInst(func, IRInstOperator::IRINST_OP_LE_I, stepIdx, quot, i1Type);
    insertBeforeTerminator(outerPre, stepFits);
    auto * signZext = new ZExtInst(func, signOk, i32Type);
    insertBeforeTerminator(outerPre, signZext);
    auto * fitsZext = new ZExtInst(func, stepFits, i32Type);
    insertBeforeTerminator(outerPre, fitsZext);
    auto * andAll = new BinaryInst(func, IRInstOperator::IRINST_OP_AND_I, signZext, fitsZext, i32Type);
    insertBeforeTerminator(outerPre, andAll);
    auto * hoistedOk = new ICmpInst(func, IRInstOperator::IRINST_OP_NE_I, andAll, zeroConst, i1Type);
    insertBeforeTerminator(outerPre, hoistedOk);

    return hoistedOk;
}

/// @return 版本化成功返回原循环新的环外前驱 slowPre（CFG 已改变，调用方需
///         失效 CFG 分析，且为 header 新增 phi 须以 slowPre 为入边块）；
///         放弃版本化返回空指针
BasicBlock * LoopStrengthReduce::tryVersionInvariantStrideLoop(BasicBlock * header,
                                                               BasicBlock * preheader,
                                                               BasicBlock * latch,
                                                               const std::unordered_set<BasicBlock *> & loopBody,
                                                               GetElementPtrInst * seed,
                                                               PhiInst * ivPhi,
                                                               Value * ivInit,
                                                               int32_t ivStep,
                                                               Value * initIdx,
                                                               Value * stepIdx)
{
    if (std::getenv("MINIC_DISABLE_LSR_VERSIONING") != nullptr ||
        versionedLoopHeaders.find(header) != versionedLoopHeaders.end()) {
        return nullptr;
    }

    // 编译期前提：s == 1（IV 递增不回绕由 Bound ≤ INT32_MAX 免费保证），
    // IV0 为非负常量（km 的构造与论证 (ii) 依赖 v0 编译期已知且非负）
    if (ivStep != 1) {
        return nullptr;
    }
    auto * ivInitConst = dynamic_cast<ConstInteger *>(ivInit);
    if (!ivInitConst || ivInitConst->getVal() < 0 ||
        ivInitConst->getVal() >= std::numeric_limits<int32_t>::max()) {
        return nullptr;
    }

    // preheader 以无条件跳转入 header。版本化会为原循环和克隆分别创建新的
    // 无条件 preheader，因此不能仅凭 CFG 形态识别重复版本化；上面的显式集合
    // 负责保证同一逻辑循环在本轮 LSR 中最多克隆一次。
    auto * preBr = dynamic_cast<BranchInst *>(preheader->getTerminator());
    if (!preBr || preBr->getTarget() != header) {
        return nullptr;
    }

    // header 终结：condbr(icmp IV < Bound)，真分支入环、假分支出环
    // （与 GuardedTailCollapse 相同的规范非旋转计数循环形态），Bound 循环不变
    auto * headerBr = dynamic_cast<CondBranchInst *>(header->getTerminator());
    if (!headerBr) {
        return nullptr;
    }
    auto * exitCmp = dynamic_cast<ICmpInst *>(headerBr->getCondition());
    if (!exitCmp) {
        return nullptr;
    }
    Value * bound = nullptr;
    if (exitCmp->getOp() == IRInstOperator::IRINST_OP_LT_I && exitCmp->getLHS() == ivPhi) {
        bound = exitCmp->getRHS();
    } else if (exitCmp->getOp() == IRInstOperator::IRINST_OP_GT_I && exitCmp->getRHS() == ivPhi) {
        bound = exitCmp->getLHS();
    } else {
        return nullptr;
    }
    if (isDefinedInLoop(bound, loopBody)) {
        return nullptr;
    }
    BasicBlock * bodyEntry = headerBr->getTrueDest();
    BasicBlock * exitBlock = headerBr->getFalseDest();
    if (loopBody.find(bodyEntry) == loopBody.end() || loopBody.find(exitBlock) != loopBody.end()) {
        return nullptr;
    }

    // 收益门控：仅版本化 ≤3 块的最内层小循环。种子 gep 每迭代都被寻址时
    // 指针递推才有每迭代收益；大循环体（如含嵌套循环的外层行循环）克隆
    // 只带来代码膨胀与寄存器压力，种子 gep 在其中每迭代仅省一条乘法级
    // 的准备指令，得不偿失。≤3 块 + 唯一 latch 的形态下也天然排除嵌套子循环。
    if (loopBody.size() > 3) {
        return nullptr;
    }

    // 唯一出环边 header→exitBlock：论证 (ii) 要求每次迭代都由头测门控，
    // 且出口值合并只需处理一条克隆出口边；环内不得有其他回边（自环块）
    for (auto * bb : loopBody) {
        for (auto * succ : bb->getSuccessors()) {
            if (loopBody.find(succ) == loopBody.end()) {
                if (bb != header || succ != exitBlock) {
                    return nullptr;
                }
                continue;
            }
            if (succ == bb && bb != header) {
                return nullptr;
            }
            if (succ == header && bb != latch) {
                return nullptr;
            }
        }
    }

    // 环内定义值的环外使用仅允许一种形态：exitBlock 内 phi 经 header 出口边引用。
    // 克隆后为其补入克隆出口边的映射值即可保持出口值一致；其余逃逸一律放弃。
    for (auto * bb : loopBody) {
        for (auto * inst : bb->getInstructions()) {
            for (auto * use : inst->getUseList()) {
                auto * userInst = dynamic_cast<Instruction *>(use->getUser());
                if (!userInst || !userInst->getParentBlock()) {
                    return nullptr;
                }
                if (loopBody.find(userInst->getParentBlock()) != loopBody.end()) {
                    continue;
                }
                auto * exitPhi = dynamic_cast<PhiInst *>(userInst);
                if (!exitPhi || exitPhi->getParentBlock() != exitBlock) {
                    return nullptr;
                }
                for (int32_t k = 0; k < exitPhi->getIncomingCount(); ++k) {
                    if (exitPhi->getIncomingValue(k) == inst && exitPhi->getIncomingBlock(k) != header) {
                        return nullptr;
                    }
                }
            }
        }
    }

    // 全部指令可克隆：终结仅限 br/condbr，非终结须在克隆白名单内。
    // 早前削减留下的 isDead 尸体（操作数已清空、待 sweep）直接跳过不克隆。
    for (auto * bb : loopBody) {
        for (auto * inst : bb->getInstructions()) {
            if (inst->isDead()) {
                continue;
            }
            if (inst->isTerminator()) {
                if (dynamic_cast<BranchInst *>(inst) == nullptr &&
                    dynamic_cast<CondBranchInst *>(inst) == nullptr) {
                    return nullptr;
                }
                continue;
            }
            if (!isVersionCloneableInstruction(inst)) {
                return nullptr;
            }
        }
    }

    Type * i1Type = exitCmp->getType();
    Type * i32Type = ivPhi->getType();
    Type * elemPtrType = seed->getType();
    Value * base = seed->getBasePointer();

    // ---- 检查构造：优先嵌套提升，一次覆盖全部行；否则逐行两级检查 ----
    // 提升模式：检查整体位于外层循环之外，preheader 只剩一条对提升 i1 的
    // 条件跳转，小行程行几乎零开销。
    // 两级模式：preheader 付 2 条指令的行程门槛，kmax < kMinProfitableTrips
    // 的小行程循环直接走精确慢路径（此时每迭代 +2 指令的代价上限本就与检查
    // 成本同量级）；大行程才进入 checkBlock 支付含除法的无回绕检查。
    Value * hoistedOk = tryBuildHoistedNestCheck(preheader, bound, initIdx, stepIdx, i1Type, i32Type);

    Value * gateCond = hoistedOk;
    BasicBlock * checkBlock = nullptr;
    Value * fastOk = nullptr;
    if (hoistedOk == nullptr) {
        auto * zeroConst = mod->newConstInt32(0);
        auto * intMaxConst = mod->newConstInt32(std::numeric_limits<int32_t>::max());
        constexpr int32_t kMinProfitableTrips = 8;

        auto * km = new BinaryInst(func, IRInstOperator::IRINST_OP_SUB_I, bound,
                                   mod->newConstInt32(ivInitConst->getVal() + 1), i32Type);
        insertBeforeTerminator(preheader, km);
        auto * kmBig = new ICmpInst(func, IRInstOperator::IRINST_OP_GE_I, km,
                                    mod->newConstInt32(kMinProfitableTrips), i1Type);
        insertBeforeTerminator(preheader, kmBig);
        gateCond = kmBig;

        // checkBlock：kmax ≥ kMinProfitableTrips ≥ 1，无需再钳制除数；
        // initIdx ≥ 0 ∧ stepIdx ≥ 0 合并为符号位测试 (initIdx | stepIdx) ≥ 0
        checkBlock = func->newBasicBlock();
        auto * room = new BinaryInst(func, IRInstOperator::IRINST_OP_SUB_I, intMaxConst, initIdx, i32Type);
        checkBlock->addInstruction(room);
        auto * quot = new BinaryInst(func, IRInstOperator::IRINST_OP_DIV_I, room, km, i32Type);
        checkBlock->addInstruction(quot);
        auto * signOr = new BinaryInst(func, IRInstOperator::IRINST_OP_OR_I, initIdx, stepIdx, i32Type);
        checkBlock->addInstruction(signOr);
        auto * bothNonNeg = new ICmpInst(func, IRInstOperator::IRINST_OP_GE_I, signOr, zeroConst, i1Type);
        checkBlock->addInstruction(bothNonNeg);
        auto * stepFits = new ICmpInst(func, IRInstOperator::IRINST_OP_LE_I, stepIdx, quot, i1Type);
        checkBlock->addInstruction(stepFits);
        auto * nonNegZext = new ZExtInst(func, bothNonNeg, i32Type);
        checkBlock->addInstruction(nonNegZext);
        auto * fitsZext = new ZExtInst(func, stepFits, i32Type);
        checkBlock->addInstruction(fitsZext);
        auto * andAll = new BinaryInst(func, IRInstOperator::IRINST_OP_AND_I, nonNegZext, fitsZext, i32Type);
        checkBlock->addInstruction(andAll);
        auto * checkOk = new ICmpInst(func, IRInstOperator::IRINST_OP_NE_I, andAll, zeroConst, i1Type);
        checkBlock->addInstruction(checkOk);
        fastOk = checkOk;
    }

    // slowPre：选路的任一失败侧经此空块入原循环，维持 header 恒为
    // 双前驱（slowPre + latch），下游循环规范化无需感知版本化
    auto * slowPre = func->newBasicBlock();

    // ---- 克隆循环体（两阶段：先外壳后重映射，仿 SmallFunctionInline） ----
    std::vector<BasicBlock *> orderedLoopBlocks;
    for (auto * bb : func->getBlocks()) {
        if (loopBody.find(bb) != loopBody.end()) {
            orderedLoopBlocks.push_back(bb);
        }
    }

    auto * fastPre = func->newBasicBlock();
    std::unordered_map<BasicBlock *, BasicBlock *> blockMap;
    for (auto * bb : orderedLoopBlocks) {
        blockMap[bb] = func->newBasicBlock();
    }

    std::unordered_map<Value *, Value *> valueMap;
    auto mapValue = [&valueMap](Value * value) -> Value * {
        auto it = valueMap.find(value);
        return it == valueMap.end() ? value : it->second;
    };
    auto mapBlock = [&blockMap](BasicBlock * bb) -> BasicBlock * {
        auto it = blockMap.find(bb);
        return it == blockMap.end() ? bb : it->second;
    };

    std::vector<std::pair<Instruction *, Instruction *>> clonedInsts;
    for (auto * bb : orderedLoopBlocks) {
        BasicBlock * cloneBB = blockMap[bb];
        for (auto * inst : bb->getInstructions()) {
            if (inst->isDead() || inst->isTerminator()) {
                continue;
            }
            Instruction * cloned = cloneVersionInstructionShell(inst, func);
            cloneBB->addInstruction(cloned);
            clonedInsts.push_back({inst, cloned});
            if (inst->hasResultValue()) {
                valueMap[inst] = cloned;
            }
        }
    }

    for (auto & [orig, cloned] : clonedInsts) {
        if (auto * origPhi = dynamic_cast<PhiInst *>(orig)) {
            auto * clonedPhi = static_cast<PhiInst *>(cloned);
            for (int32_t i = 0; i < origPhi->getIncomingCount(); ++i) {
                BasicBlock * inBlock = origPhi->getIncomingBlock(i);
                clonedPhi->addIncoming(mapValue(origPhi->getIncomingValue(i)),
                                       inBlock == preheader ? fastPre : mapBlock(inBlock));
            }
            continue;
        }
        for (int32_t i = 0; i < cloned->getOperandsNum(); ++i) {
            cloned->setOperand(i, mapValue(cloned->getOperand(i)));
        }
    }

    for (auto * bb : orderedLoopBlocks) {
        BasicBlock * cloneBB = blockMap[bb];
        Instruction * term = bb->getTerminator();
        if (auto * br = dynamic_cast<BranchInst *>(term)) {
            BasicBlock * target = mapBlock(br->getTarget());
            auto * clonedBr = new BranchInst(func, target);
            cloneBB->addInstruction(clonedBr);
            cloneBB->linkSuccessor(target);
            continue;
        }
        auto * condBr = static_cast<CondBranchInst *>(term);
        BasicBlock * trueTarget = mapBlock(condBr->getTrueDest());
        BasicBlock * falseTarget = mapBlock(condBr->getFalseDest());
        auto * clonedCond = new CondBranchInst(func, mapValue(condBr->getCondition()), trueTarget, falseTarget);
        cloneBB->addInstruction(clonedCond);
        cloneBB->linkSuccessor(trueTarget);
        if (falseTarget != trueTarget) {
            cloneBB->linkSuccessor(falseTarget);
        }
    }

    // ---- fastPre：物化 initPtr 并进入克隆头 ----
    auto * initPtr = new GetElementPtrInst(func, base, initIdx, elemPtrType, seed->isArrayDecayGEP());
    fastPre->addInstruction(initPtr);
    auto * fastEntryBr = new BranchInst(func, blockMap[header]);
    fastPre->addInstruction(fastEntryBr);
    fastPre->linkSuccessor(blockMap[header]);

    // ---- 克隆内种子 gep 替换为指针递推 ----
    auto * clonedSeed = static_cast<GetElementPtrInst *>(valueMap.at(seed));
    auto * ptrPhi = new PhiInst(func, elemPtrType);
    insertPhiAtHeader(blockMap[header], ptrPhi);
    auto * nextPtr = new GetElementPtrInst(func, ptrPhi, stepIdx, elemPtrType, false);
    insertBeforeTerminator(blockMap[latch], nextPtr);
    ptrPhi->addIncoming(initPtr, fastPre);
    ptrPhi->addIncoming(nextPtr, blockMap[latch]);
    clonedSeed->replaceAllUseWith(ptrPhi);
    clonedSeed->clearOperands();
    clonedSeed->setDead(true);

    // ---- 出口块 phi 补入克隆出口边（值取映射；克隆头零迭代时两侧初值一致） ----
    for (auto * inst : exitBlock->getInstructions()) {
        auto * exitPhi = dynamic_cast<PhiInst *>(inst);
        if (!exitPhi) {
            break;
        }
        Value * fromHeader = nullptr;
        for (int32_t i = 0; i < exitPhi->getIncomingCount(); ++i) {
            if (exitPhi->getIncomingBlock(i) == header) {
                fromHeader = exitPhi->getIncomingValue(i);
                break;
            }
        }
        if (fromHeader != nullptr) {
            exitPhi->addIncoming(mapValue(fromHeader), blockMap[header]);
        }
    }

    // ---- 选路接线 ----
    // 两级模式：preheader --gate--> checkBlock --fastOk--> fastPre --> 克隆循环
    //                    \--else--> slowPre <----else-----/
    // 提升模式：preheader --hoistedOk--> fastPre，else--> slowPre（无 checkBlock）
    // slowPre --> header（原循环，唯一环外前驱改为 slowPre）
    BasicBlock * gateTrueDest = checkBlock != nullptr ? checkBlock : fastPre;
    auto & preInsts = preheader->getInstructions();
    preInsts.pop_back();
    preBr->clearOperands();
    delete preBr;
    auto * gateBr = new CondBranchInst(func, gateCond, gateTrueDest, slowPre);
    gateBr->setParentBlock(preheader);
    preInsts.push_back(gateBr);
    preheader->removeSuccessor(header);
    header->removePredecessor(preheader);
    preheader->linkSuccessor(gateTrueDest);
    preheader->linkSuccessor(slowPre);

    if (checkBlock != nullptr) {
        auto * checkBr = new CondBranchInst(func, fastOk, fastPre, slowPre);
        checkBlock->addInstruction(checkBr);
        checkBlock->linkSuccessor(fastPre);
        checkBlock->linkSuccessor(slowPre);
    }

    auto * slowBr = new BranchInst(func, header);
    slowPre->addInstruction(slowBr);
    slowPre->linkSuccessor(header);
    for (auto * inst : header->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        phi->replaceIncomingBlock(preheader, slowPre);
    }

    // 两个循环仍可能包含其他动态步长 GEP。允许后续把它们改写为精确的 i32
    // 下标递推，但禁止再次构造快慢副本，避免 N 个候选产生近似 2^N 份循环。
    versionedLoopHeaders.insert(header);
    versionedLoopHeaders.insert(blockMap.at(header));


    return slowPre;
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
