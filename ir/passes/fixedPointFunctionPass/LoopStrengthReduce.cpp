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

ConstInteger * asConstInteger(Value * value)
{
    return dynamic_cast<ConstInteger *>(value);
}

bool matchInductionStep(Value * value, PhiInst * induction, int32_t & step)
{
    auto * binary = dynamic_cast<BinaryInst *>(value);
    if (!binary || !induction) {
        return false;
    }

    if (binary->getOp() == IRInstOperator::IRINST_OP_ADD_I) {
        if (binary->getLHS() == induction) {
            auto * rhs = asConstInteger(binary->getRHS());
            if (!rhs) {
                return false;
            }
            step = rhs->getVal();
            return step != 0;
        }

        if (binary->getRHS() == induction) {
            auto * lhs = asConstInteger(binary->getLHS());
            if (!lhs) {
                return false;
            }
            step = lhs->getVal();
            return step != 0;
        }
    }

    if (binary->getOp() == IRInstOperator::IRINST_OP_SUB_I && binary->getLHS() == induction) {
        auto * rhs = asConstInteger(binary->getRHS());
        if (!rhs) {
            return false;
        }
        step = -rhs->getVal();
        return step != 0;
    }

    return false;
}

bool getInductionInfo(PhiInst * phi,
                      BasicBlock * preheader,
                      BasicBlock * latch,
                      const std::unordered_set<BasicBlock *> & loopBody,
                      Value *& initValue,
                      int32_t & step)
{
    if (!phi || !phi->getType()->isInt32Type() || !preheader || !latch || phi->getIncomingCount() != 2) {
        return false;
    }

    Value * backValue = nullptr;
    initValue = nullptr;

    for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
        if (phi->getIncomingBlock(index) == preheader) {
            initValue = phi->getIncomingValue(index);
            continue;
        }

        if (phi->getIncomingBlock(index) == latch) {
            backValue = phi->getIncomingValue(index);
        }
    }

    auto * backInst = dynamic_cast<Instruction *>(backValue);
    if (!initValue || !backInst || !backInst->getParentBlock() ||
        loopBody.find(backInst->getParentBlock()) == loopBody.end()) {
        return false;
    }

    return matchInductionStep(backValue, phi, step);
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

    for (auto * inst : header->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }

        Value * initValue = nullptr;
        int32_t step = 0;
        if (!getInductionInfo(phi, preheader, latch, loopBody, initValue, step)) {
            continue;
        }

        if (reduceFirstCandidate(header, preheader, latch, phi, initValue, step, loopBody)) {
            return true;
        }
    }

    return false;
}

bool LoopStrengthReduce::reduceFirstCandidate(BasicBlock * header,
                                              BasicBlock * preheader,
                                              BasicBlock * latch,
                                              PhiInst * induction,
                                              Value * initValue,
                                              int32_t step,
                                              const std::unordered_set<BasicBlock *> & loopBody)
{
    GetElementPtrInst * seed = nullptr;
    for (auto * bb : func->getBlocks()) {
        if (loopBody.find(bb) == loopBody.end()) {
            continue;
        }

        for (auto * inst : bb->getInstructions()) {
            auto * gep = dynamic_cast<GetElementPtrInst *>(inst);
            if (!gep || gep->isDead() || gep->getIndexOperand() != induction ||
                !isLoopInvariantValue(gep->getBasePointer(), loopBody) || !allUsesStayInLoop(gep, loopBody)) {
                continue;
            }

            seed = gep;
            break;
        }

        if (seed) {
            break;
        }
    }

    if (!seed) {
        return false;
    }

    Value * base = seed->getBasePointer();
    Type * pointerType = seed->getType();
    const bool decayArray = seed->isArrayDecayGEP();

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

            if (gep->getBasePointer() == base && gep->getIndexOperand() == induction &&
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
    auto * stepValue = mod->newConstInt32(step);
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
