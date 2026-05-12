///
/// @file ScalarEvolution.cpp
/// @brief 保守的标量演化分析实现
///

#include "ScalarEvolution.h"

#include <limits>

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
#include "PhiInst.h"
#include "Type.h"
#include "Value.h"

ScalarEvolution::Expr::Expr(ExprKind kind, Type * type) : kind(kind), type(type)
{}

ScalarEvolution::ConstantExpr::ConstantExpr(Type * type, int32_t intValue, ConstInteger * sourceValue)
    : Expr(ExprKind::Constant, type), intValue(intValue), sourceValue(sourceValue)
{}

ScalarEvolution::BinaryExpr::BinaryExpr(ExprKind kind, Type * type, const Expr * lhs, const Expr * rhs)
    : Expr(kind, type), lhs(lhs), rhs(rhs)
{}

ScalarEvolution::AddExpr::AddExpr(Type * type, const Expr * lhs, const Expr * rhs)
    : BinaryExpr(ExprKind::Add, type, lhs, rhs)
{}

ScalarEvolution::MultiplyExpr::MultiplyExpr(Type * type, const Expr * lhs, const Expr * rhs)
    : BinaryExpr(ExprKind::Multiply, type, lhs, rhs)
{}

ScalarEvolution::UnknownExpr::UnknownExpr(Value * value)
    : Expr(ExprKind::Unknown, value ? value->getType() : nullptr), value(value)
{}

ScalarEvolution::AddRecurrenceExpr::AddRecurrenceExpr(PhiInst * phi,
                                                      Value * startValue,
                                                      const Expr * startExpr,
                                                      Value * backEdgeValue,
                                                      int32_t step,
                                                      StepKind stepKind,
                                                      BasicBlock * loopHeader,
                                                      BasicBlock * preheader,
                                                      BasicBlock * latch)
    : Expr(ExprKind::AddRecurrence, phi ? phi->getType() : nullptr),
      phi(phi),
      startValue(startValue),
      startExpr(startExpr),
      backEdgeValue(backEdgeValue),
      step(step),
      stepKind(stepKind),
      loopHeader(loopHeader),
      preheader(preheader),
      latch(latch)
{}

ScalarEvolution::ScalarEvolution(Function * func, DominatorTree * domTree, LoopInfo * loopInfo)
    : func(func), domTree(domTree), loopInfo(loopInfo)
{}

const ScalarEvolution::Expr * ScalarEvolution::getSCEV(Value * value)
{
    if (!value) {
        return nullptr;
    }

    auto cached = exprCache.find(value);
    if (cached != exprCache.end()) {
        return cached->second;
    }

    const Expr * expr = analyzeValue(value);
    exprCache[value] = expr;
    return expr;
}

const ScalarEvolution::AddRecurrenceExpr * ScalarEvolution::getAddRecurrence(Value * value)
{
    const Expr * expr = getSCEV(value);
    if (!expr || expr->getKind() != ExprKind::AddRecurrence) {
        return nullptr;
    }

    return static_cast<const AddRecurrenceExpr *>(expr);
}

bool ScalarEvolution::matchCanonicalLoop(BasicBlock * header, CanonicalLoop & loop)
{
    loop = {};
    LoopInfo * currentLoopInfo = ensureLoopInfo();
    if (!header || !currentLoopInfo || !currentLoopInfo->isLoopHeader(header)) {
        return false;
    }

    auto * condBr = dynamic_cast<CondBranchInst *>(header->getTerminator());
    if (!condBr) {
        return false;
    }

    auto * cmp = dynamic_cast<ICmpInst *>(condBr->getCondition());
    if (!cmp || cmp->getParentBlock() != header || cmp->getOp() != IRInstOperator::IRINST_OP_LT_I) {
        return false;
    }

    auto * induction = dynamic_cast<PhiInst *>(cmp->getLHS());
    auto * bound = dynamic_cast<ConstInteger *>(cmp->getRHS());
    if (!induction || induction->getParentBlock() != header || !bound || !induction->getType()->isIntegerType()) {
        return false;
    }

    const AddRecurrenceExpr * recurrence = getAddRecurrence(induction);
    if (!recurrence || !recurrence->isIntegerRecurrence() || recurrence->getLoopHeader() != header ||
        recurrence->getStep() <= 0) {
        return false;
    }

    if (header->getPredecessors().size() != 2 || !hasSingleBranchTo(recurrence->getPreheader(), header) ||
        !hasSingleBranchTo(recurrence->getLatch(), header)) {
        return false;
    }

    int32_t initialIntValue = 0;
    const bool hasConstInitialValue = tryEvaluateConstantInt(recurrence->getStartExpr(), initialIntValue);

    loop.header = header;
    loop.preheader = recurrence->getPreheader();
    loop.body = condBr->getTrueDest();
    loop.latch = recurrence->getLatch();
    loop.exit = condBr->getFalseDest();
    loop.induction = induction;
    loop.recurrence = recurrence;
    loop.cmp = cmp;
    loop.branch = condBr;
    loop.bound = bound;
    loop.initialValue = recurrence->getStartValue();
    loop.initialIntValue = initialIntValue;
    loop.hasConstInitialValue = hasConstInitialValue;
    loop.tripCount = hasConstInitialValue ? computeTripCount(initialIntValue, bound->getVal(), recurrence->getStep()) : 0;
    return loop.body != nullptr && loop.exit != nullptr;
}

bool ScalarEvolution::dependsOnLoop(Value * value, BasicBlock * loopHeader)
{
    std::unordered_set<const Expr *> exprVisiting;
    std::unordered_set<Value *> valueVisiting;
    return dependsOnLoopValue(value, loopHeader, exprVisiting, valueVisiting);
}

LoopInfo * ScalarEvolution::ensureLoopInfo()
{
    if (loopInfo) {
        return loopInfo;
    }

    if (!ownedLoopInfo && func && domTree) {
        ownedLoopInfo = std::make_unique<LoopInfo>(func, domTree);
    }

    loopInfo = ownedLoopInfo.get();
    return loopInfo;
}

const ScalarEvolution::Expr * ScalarEvolution::analyzeValue(Value * value)
{
    if (auto * constant = dynamic_cast<ConstInteger *>(value)) {
        return createConstant(constant->getType(), constant->getVal(), constant);
    }

    if (auto * binary = dynamic_cast<BinaryInst *>(value)) {
        return analyzeBinary(binary);
    }

    auto * phi = dynamic_cast<PhiInst *>(value);
    if (!phi || !phi->getParentBlock()) {
        return createUnknown(value);
    }

    LoopInfo * currentLoopInfo = ensureLoopInfo();
    BasicBlock * header = phi->getParentBlock();
    const auto * loopBody = currentLoopInfo ? currentLoopInfo->getLoopBody(header) : nullptr;
    if (!currentLoopInfo || !loopBody || !currentLoopInfo->isLoopHeader(header)) {
        return createUnknown(value);
    }

    Value * startValue = nullptr;
    Value * backEdgeValue = nullptr;
    int32_t step = 0;
    AddRecurrenceExpr::StepKind stepKind = AddRecurrenceExpr::StepKind::Integer;
    BasicBlock * preheader = nullptr;
    BasicBlock * latch = nullptr;
    if (!matchAddRecurrence(phi, header, *loopBody, startValue, backEdgeValue, step, stepKind, preheader, latch)) {
        return createUnknown(value);
    }

    return createAddRecurrence(phi, startValue, getSCEV(startValue), backEdgeValue, step, stepKind, header, preheader, latch);
}

const ScalarEvolution::Expr * ScalarEvolution::analyzeBinary(BinaryInst * binary)
{
    if (!binary || !binary->getType() || !binary->getType()->isIntegerType()) {
        return createUnknown(binary);
    }

    const Expr * lhs = getSCEV(binary->getLHS());
    const Expr * rhs = getSCEV(binary->getRHS());
    switch (binary->getOp()) {
    case IRInstOperator::IRINST_OP_ADD_I:
        return createAdd(binary->getType(), lhs, rhs);
    case IRInstOperator::IRINST_OP_SUB_I:
        return createAdd(binary->getType(), lhs, createMultiply(binary->getType(), rhs, createConstant(binary->getType(), -1)));
    case IRInstOperator::IRINST_OP_MUL_I:
        return createMultiply(binary->getType(), lhs, rhs);
    default:
        return createUnknown(binary);
    }
}

const ScalarEvolution::UnknownExpr * ScalarEvolution::createUnknown(Value * value)
{
    exprArena.push_back(std::make_unique<UnknownExpr>(value));
    return static_cast<const UnknownExpr *>(exprArena.back().get());
}

const ScalarEvolution::ConstantExpr * ScalarEvolution::createConstant(Type * type,
                                                                      int32_t intValue,
                                                                      ConstInteger * sourceValue)
{
    exprArena.push_back(std::make_unique<ConstantExpr>(type, intValue, sourceValue));
    return static_cast<const ConstantExpr *>(exprArena.back().get());
}

const ScalarEvolution::Expr * ScalarEvolution::createAdd(Type * type, const Expr * lhs, const Expr * rhs)
{
    int32_t lhsConstant = 0;
    int32_t rhsConstant = 0;
    const bool lhsIsConstant = tryEvaluateConstantInt(lhs, lhsConstant);
    const bool rhsIsConstant = tryEvaluateConstantInt(rhs, rhsConstant);
    if (lhsIsConstant && rhsIsConstant) {
        return createConstant(type, lhsConstant + rhsConstant);
    }
    if (lhsIsConstant && lhsConstant == 0) {
        return rhs;
    }
    if (rhsIsConstant && rhsConstant == 0) {
        return lhs;
    }

    if (const Expr * affineRecurrence = tryCreateAffineAddRecurrenceForAdd(type, lhs, rhs)) {
        return affineRecurrence;
    }

    exprArena.push_back(std::make_unique<AddExpr>(type, lhs, rhs));
    return exprArena.back().get();
}

const ScalarEvolution::Expr * ScalarEvolution::createMultiply(Type * type,
                                                              const Expr * lhs,
                                                              const Expr * rhs)
{
    int32_t lhsConstant = 0;
    int32_t rhsConstant = 0;
    const bool lhsIsConstant = tryEvaluateConstantInt(lhs, lhsConstant);
    const bool rhsIsConstant = tryEvaluateConstantInt(rhs, rhsConstant);
    if (lhsIsConstant && rhsIsConstant) {
        return createConstant(type, lhsConstant * rhsConstant);
    }
    if ((lhsIsConstant && lhsConstant == 0) || (rhsIsConstant && rhsConstant == 0)) {
        return createConstant(type, 0);
    }
    if (lhsIsConstant && lhsConstant == 1) {
        return rhs;
    }
    if (rhsIsConstant && rhsConstant == 1) {
        return lhs;
    }

    if (const Expr * affineRecurrence = tryCreateAffineAddRecurrenceForMultiply(type, lhs, rhs)) {
        return affineRecurrence;
    }

    exprArena.push_back(std::make_unique<MultiplyExpr>(type, lhs, rhs));
    return exprArena.back().get();
}

const ScalarEvolution::AddRecurrenceExpr * ScalarEvolution::createAddRecurrence(PhiInst * phi,
                                                                                Value * startValue,
                                                                                const Expr * startExpr,
                                                                                Value * backEdgeValue,
                                                                                int32_t step,
                                                                                AddRecurrenceExpr::StepKind stepKind,
                                                                                BasicBlock * loopHeader,
                                                                                BasicBlock * preheader,
                                                                                BasicBlock * latch)
{
    exprArena.push_back(std::make_unique<AddRecurrenceExpr>(
        phi, startValue, startExpr, backEdgeValue, step, stepKind, loopHeader, preheader, latch));
    return static_cast<const AddRecurrenceExpr *>(exprArena.back().get());
}

const ScalarEvolution::Expr * ScalarEvolution::tryCreateAffineAddRecurrenceForAdd(Type * type,
                                                                                   const Expr * lhs,
                                                                                   const Expr * rhs)
{
    const auto * lhsRecurrence = dynamic_cast<const AddRecurrenceExpr *>(lhs);
    const auto * rhsRecurrence = dynamic_cast<const AddRecurrenceExpr *>(rhs);

    if (lhsRecurrence && lhsRecurrence->isIntegerRecurrence() &&
        isLoopInvariantExpr(rhs, lhsRecurrence->getLoopHeader())) {
        const Expr * startExpr = createAdd(type, lhsRecurrence->getStartExpr(), rhs);
        return createAddRecurrence(lhsRecurrence->getPhi(),
                                   getRepresentativeValue(startExpr),
                                   startExpr,
                                   nullptr,
                                   lhsRecurrence->getStep(),
                                   lhsRecurrence->getStepKind(),
                                   lhsRecurrence->getLoopHeader(),
                                   lhsRecurrence->getPreheader(),
                                   lhsRecurrence->getLatch());
    }

    if (rhsRecurrence && rhsRecurrence->isIntegerRecurrence() &&
        isLoopInvariantExpr(lhs, rhsRecurrence->getLoopHeader())) {
        const Expr * startExpr = createAdd(type, lhs, rhsRecurrence->getStartExpr());
        return createAddRecurrence(rhsRecurrence->getPhi(),
                                   getRepresentativeValue(startExpr),
                                   startExpr,
                                   nullptr,
                                   rhsRecurrence->getStep(),
                                   rhsRecurrence->getStepKind(),
                                   rhsRecurrence->getLoopHeader(),
                                   rhsRecurrence->getPreheader(),
                                   rhsRecurrence->getLatch());
    }

    if (lhsRecurrence && rhsRecurrence && lhsRecurrence->isIntegerRecurrence() && rhsRecurrence->isIntegerRecurrence() &&
        lhsRecurrence->getPhi() == rhsRecurrence->getPhi() &&
        lhsRecurrence->getLoopHeader() == rhsRecurrence->getLoopHeader() &&
        lhsRecurrence->getPreheader() == rhsRecurrence->getPreheader() &&
        lhsRecurrence->getLatch() == rhsRecurrence->getLatch()) {
        const int64_t combinedStep = static_cast<int64_t>(lhsRecurrence->getStep()) + rhsRecurrence->getStep();
        if (combinedStep < std::numeric_limits<int32_t>::min() ||
            combinedStep > std::numeric_limits<int32_t>::max()) {
            return nullptr;
        }

        const Expr * startExpr = createAdd(type, lhsRecurrence->getStartExpr(), rhsRecurrence->getStartExpr());
        if (combinedStep == 0) {
            return startExpr;
        }

        return createAddRecurrence(lhsRecurrence->getPhi(),
                                   getRepresentativeValue(startExpr),
                                   startExpr,
                                   nullptr,
                                   static_cast<int32_t>(combinedStep),
                                   lhsRecurrence->getStepKind(),
                                   lhsRecurrence->getLoopHeader(),
                                   lhsRecurrence->getPreheader(),
                                   lhsRecurrence->getLatch());
    }

    return nullptr;
}

const ScalarEvolution::Expr * ScalarEvolution::tryCreateAffineAddRecurrenceForMultiply(Type * type,
                                                                                        const Expr * lhs,
                                                                                        const Expr * rhs)
{
    const auto * lhsRecurrence = dynamic_cast<const AddRecurrenceExpr *>(lhs);
    const auto * rhsRecurrence = dynamic_cast<const AddRecurrenceExpr *>(rhs);

    auto tryScale = [&](const AddRecurrenceExpr * recurrence, const Expr * factorExpr) -> const Expr * {
        if (!recurrence || !recurrence->isIntegerRecurrence()) {
            return nullptr;
        }

        int32_t factor = 0;
        if (!tryEvaluateConstantInt(factorExpr, factor)) {
            return nullptr;
        }

        const int64_t scaledStep = static_cast<int64_t>(recurrence->getStep()) * factor;
        if (scaledStep < std::numeric_limits<int32_t>::min() ||
            scaledStep > std::numeric_limits<int32_t>::max()) {
            return nullptr;
        }

        const Expr * startExpr = createMultiply(type, recurrence->getStartExpr(), factorExpr);
        if (scaledStep == 0) {
            return startExpr;
        }

        return createAddRecurrence(recurrence->getPhi(),
                                   getRepresentativeValue(startExpr),
                                   startExpr,
                                   nullptr,
                                   static_cast<int32_t>(scaledStep),
                                   recurrence->getStepKind(),
                                   recurrence->getLoopHeader(),
                                   recurrence->getPreheader(),
                                   recurrence->getLatch());
    };

    if (const Expr * scaled = tryScale(lhsRecurrence, rhs)) {
        return scaled;
    }

    return tryScale(rhsRecurrence, lhs);
}

bool ScalarEvolution::matchAddRecurrence(PhiInst * phi,
                                         BasicBlock * loopHeader,
                                         const std::unordered_set<BasicBlock *> & loopBody,
                                         Value *& startValue,
                                         Value *& backEdgeValue,
                                         int32_t & step,
                                         AddRecurrenceExpr::StepKind & stepKind,
                                         BasicBlock *& preheader,
                                         BasicBlock *& latch) const
{
    startValue = nullptr;
    backEdgeValue = nullptr;
    step = 0;
    stepKind = AddRecurrenceExpr::StepKind::Integer;
    preheader = nullptr;
    latch = nullptr;

    if (!phi || !loopHeader || phi->getParentBlock() != loopHeader || phi->getIncomingCount() != 2) {
        return false;
    }

    if (!findUniqueLoopEntryAndLatch(loopHeader, loopBody, preheader, latch)) {
        return false;
    }

    for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
        if (phi->getIncomingBlock(index) == preheader) {
            startValue = phi->getIncomingValue(index);
            continue;
        }

        if (phi->getIncomingBlock(index) == latch) {
            backEdgeValue = phi->getIncomingValue(index);
        }
    }

    auto * backInst = dynamic_cast<Instruction *>(backEdgeValue);
    if (!startValue || !backInst || !backInst->getParentBlock() ||
        loopBody.find(backInst->getParentBlock()) == loopBody.end()) {
        return false;
    }

    return matchConstStep(backEdgeValue, phi, step, stepKind);
}

bool ScalarEvolution::findUniqueLoopEntryAndLatch(BasicBlock * loopHeader,
                                                  const std::unordered_set<BasicBlock *> & loopBody,
                                                  BasicBlock *& preheader,
                                                  BasicBlock *& latch) const
{
    preheader = nullptr;
    latch = nullptr;
    if (!loopHeader) {
        return false;
    }

    for (auto * pred : loopHeader->getPredecessors()) {
        if (loopBody.find(pred) != loopBody.end()) {
            if (latch) {
                return false;
            }
            latch = pred;
            continue;
        }

        if (preheader) {
            return false;
        }
        preheader = pred;
    }

    return preheader != nullptr && latch != nullptr;
}

bool ScalarEvolution::matchConstStep(Value * value,
                                     PhiInst * phi,
                                     int32_t & step,
                                     AddRecurrenceExpr::StepKind & stepKind) const
{
    step = 0;
    stepKind = AddRecurrenceExpr::StepKind::Integer;
    if (!phi) {
        return false;
    }

    auto * binary = dynamic_cast<BinaryInst *>(value);
    if (binary && phi->getType()->isIntegerType()) {
        if (binary->getOp() == IRInstOperator::IRINST_OP_ADD_I) {
            if (binary->getLHS() == phi) {
                auto * rhs = dynamic_cast<ConstInteger *>(binary->getRHS());
                if (!rhs) {
                    return false;
                }
                step = rhs->getVal();
                return step != 0;
            }

            if (binary->getRHS() == phi) {
                auto * lhs = dynamic_cast<ConstInteger *>(binary->getLHS());
                if (!lhs) {
                    return false;
                }
                step = lhs->getVal();
                return step != 0;
            }
        }

        if (binary->getOp() == IRInstOperator::IRINST_OP_SUB_I && binary->getLHS() == phi) {
            auto * rhs = dynamic_cast<ConstInteger *>(binary->getRHS());
            if (!rhs) {
                return false;
            }
            step = -rhs->getVal();
            return step != 0;
        }
    }

    auto * gep = dynamic_cast<GetElementPtrInst *>(value);
    if (!gep || gep->isArrayDecayGEP() || gep->getBasePointer() != phi) {
        return false;
    }

    auto * index = dynamic_cast<ConstInteger *>(gep->getIndexOperand());
    if (!index) {
        return false;
    }

    step = index->getVal();
    stepKind = AddRecurrenceExpr::StepKind::Pointer;
    return step != 0;
}

bool ScalarEvolution::tryEvaluateConstantInt(const Expr * expr, int32_t & constant) const
{
    if (!expr) {
        return false;
    }

    switch (expr->getKind()) {
    case ExprKind::Constant:
        constant = static_cast<const ConstantExpr *>(expr)->getIntValue();
        return true;
    case ExprKind::Add: {
        const auto * add = static_cast<const AddExpr *>(expr);
        int32_t lhs = 0;
        int32_t rhs = 0;
        if (!tryEvaluateConstantInt(add->getLHS(), lhs) || !tryEvaluateConstantInt(add->getRHS(), rhs)) {
            return false;
        }
        constant = lhs + rhs;
        return true;
    }
    case ExprKind::Multiply: {
        const auto * mul = static_cast<const MultiplyExpr *>(expr);
        int32_t lhs = 0;
        int32_t rhs = 0;
        if (!tryEvaluateConstantInt(mul->getLHS(), lhs) || !tryEvaluateConstantInt(mul->getRHS(), rhs)) {
            return false;
        }
        constant = lhs * rhs;
        return true;
    }
    case ExprKind::Unknown:
    case ExprKind::AddRecurrence:
        return false;
    }

    return false;
}

bool ScalarEvolution::dependsOnLoopExpr(const Expr * expr,
                                        BasicBlock * loopHeader,
                                        std::unordered_set<const Expr *> & exprVisiting,
                                        std::unordered_set<Value *> & valueVisiting)
{
    if (!expr || !loopHeader || !exprVisiting.insert(expr).second) {
        return false;
    }

    switch (expr->getKind()) {
    case ExprKind::Constant:
        return false;
    case ExprKind::Add: {
        const auto * add = static_cast<const AddExpr *>(expr);
        return dependsOnLoopExpr(add->getLHS(), loopHeader, exprVisiting, valueVisiting) ||
               dependsOnLoopExpr(add->getRHS(), loopHeader, exprVisiting, valueVisiting);
    }
    case ExprKind::Multiply: {
        const auto * mul = static_cast<const MultiplyExpr *>(expr);
        return dependsOnLoopExpr(mul->getLHS(), loopHeader, exprVisiting, valueVisiting) ||
               dependsOnLoopExpr(mul->getRHS(), loopHeader, exprVisiting, valueVisiting);
    }
    case ExprKind::AddRecurrence: {
        const auto * recurrence = static_cast<const AddRecurrenceExpr *>(expr);
        return recurrence->getLoopHeader() == loopHeader ||
               dependsOnLoopExpr(recurrence->getStartExpr(), loopHeader, exprVisiting, valueVisiting);
    }
    case ExprKind::Unknown: {
        auto * value = static_cast<const UnknownExpr *>(expr)->getValue();
        auto * inst = dynamic_cast<Instruction *>(value);
        if (!inst) {
            return false;
        }

        for (auto * operand : inst->getOperandsValue()) {
            if (dependsOnLoopValue(operand, loopHeader, exprVisiting, valueVisiting)) {
                return true;
            }
        }
        return false;
    }
    }

    return false;
}

bool ScalarEvolution::dependsOnLoopValue(Value * value,
                                         BasicBlock * loopHeader,
                                         std::unordered_set<const Expr *> & exprVisiting,
                                         std::unordered_set<Value *> & valueVisiting)
{
    if (!value || !loopHeader || !valueVisiting.insert(value).second) {
        return false;
    }

    return dependsOnLoopExpr(getSCEV(value), loopHeader, exprVisiting, valueVisiting);
}

bool ScalarEvolution::isLoopInvariantExpr(const Expr * expr, BasicBlock * loopHeader)
{
    std::unordered_set<const Expr *> exprVisiting;
    std::unordered_set<Value *> valueVisiting;
    return !dependsOnLoopExpr(expr, loopHeader, exprVisiting, valueVisiting);
}

Value * ScalarEvolution::getRepresentativeValue(const Expr * expr) const
{
    if (!expr) {
        return nullptr;
    }

    switch (expr->getKind()) {
    case ExprKind::Constant:
        return static_cast<const ConstantExpr *>(expr)->getSourceValue();
    case ExprKind::Unknown:
        return static_cast<const UnknownExpr *>(expr)->getValue();
    case ExprKind::AddRecurrence:
        return static_cast<const AddRecurrenceExpr *>(expr)->getPhi();
    case ExprKind::Add:
    case ExprKind::Multiply:
        return nullptr;
    }

    return nullptr;
}

bool ScalarEvolution::hasSingleBranchTo(BasicBlock * bb, BasicBlock * target) const
{
    auto * branch = bb ? dynamic_cast<BranchInst *>(bb->getTerminator()) : nullptr;
    return branch && branch->getTarget() == target && bb->getSuccessors().size() == 1;
}

int32_t ScalarEvolution::computeTripCount(int32_t initialValue, int32_t bound, int32_t step) const
{
    if (step <= 0) {
        return 0;
    }

    const int32_t delta = bound - initialValue;
    if (delta <= 0) {
        return 0;
    }

    return (delta + step - 1) / step;
}