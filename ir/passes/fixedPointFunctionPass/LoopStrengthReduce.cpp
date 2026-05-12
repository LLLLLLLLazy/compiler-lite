///
/// @file LoopStrengthReduce.cpp
/// @brief 循环地址强度削减 pass 实现
///

#include "LoopStrengthReduce.h"

#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <vector>

#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "ConstInteger.h"
#include "DominatorTree.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "Instruction.h"
#include "LoopInfo.h"
#include "Module.h"
#include "PhiInst.h"
#include "ScalarEvolution.h"
#include "Type.h"
#include "Value.h"

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
        return static_cast<const ScalarEvolution::AddRecurrenceExpr *>(expr)->getPhi();
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
    while (true) {
        bool localChanged = false;
        DominatorTree domTree(func);
        LoopInfo loopInfo(func, &domTree);
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
    }

    return sweepDeadInstructions() || changed;
}

bool LoopStrengthReduce::tryReduceHeader(BasicBlock * header)
{
    if (!header) {
        return false;
    }

    DominatorTree domTree(func);
    LoopInfo loopInfo(func, &domTree);
    ScalarEvolution scev(func, &domTree, &loopInfo);
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

    return reduceFirstCandidate(header, preheader, latch, scev, loopBody);
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

    Value * initValue = materializeSCEVExpr(seedRecurrence->getStartExpr(), func, mod, preheader);
    if (!initValue) {
        return false;
    }

    Value * base = seed->getBasePointer();
    Type * pointerType = seed->getType();
    const bool decayArray = seed->isArrayDecayGEP();
    Value * seedIndex = seed->getIndexOperand();

    std::vector<GetElementPtrInst *> candidates;
    for (auto * bb : func->getBlocks()) {
        if (loopBody.find(bb) == loopBody.end()) {
            continue;
        }

        for (auto * inst : bb->getInstructions()) {
            auto * gep = dynamic_cast<GetElementPtrInst *>(inst);
            if (!gep || gep->isDead()) {
                continue;
            }

            if (gep->getBasePointer() == base && gep->getIndexOperand() == seedIndex &&
                gep->getType() == pointerType && gep->isArrayDecayGEP() == decayArray &&
                allUsesStayInLoop(gep, loopBody)) {
                candidates.push_back(gep);
            }
        }
    }

    if (candidates.empty()) {
        return false;
    }

    auto * initPtr = new GetElementPtrInst(func, base, initValue, pointerType, decayArray);
    auto * ptrPhi = new PhiInst(func, pointerType);
    auto * stepValue = mod->newConstInteger(seedIndex->getType(), seedRecurrence->getStep());
    auto * nextPtr = new GetElementPtrInst(func, ptrPhi, stepValue, pointerType, false);

    insertBeforeTerminator(preheader, initPtr);
    insertPhiAtHeader(header, ptrPhi);
    insertBeforeTerminator(latch, nextPtr);

    ptrPhi->addIncoming(initPtr, preheader);
    ptrPhi->addIncoming(nextPtr, latch);

    for (auto * gep : candidates) {
        gep->replaceAllUseWith(ptrPhi);
        gep->clearOperands();
        gep->setDead(true);
    }

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
