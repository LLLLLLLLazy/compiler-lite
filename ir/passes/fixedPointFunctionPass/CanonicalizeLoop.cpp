///
/// @file CanonicalizeLoop.cpp
/// @brief 循环规范化 pass 实现
///

#include "CanonicalizeLoop.h"

#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <utility>

#include "BasicBlock.h"
#include "BranchInst.h"
#include "CondBranchInst.h"
#include "DominatorTree.h"
#include "Function.h"
#include "LoopInfo.h"
#include "Module.h"
#include "PhiInst.h"
#include "Value.h"

CanonicalizeLoop::CanonicalizeLoop(Function * _func, Module * /*_mod*/) : func(_func)
{}

bool CanonicalizeLoop::run()
{
    if (!func || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    bool changed = false;
    while (true) {
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

        bool localChanged = false;
        for (auto * header : headers) {
            const auto * loopBody = loopInfo.getLoopBody(header);
            if (!loopBody || loopBody->empty()) {
                continue;
            }

            if (canonicalizeLoop(header, *loopBody)) {
                localChanged = true;
                changed = true;
                break;
            }
        }

        if (!localChanged) {
            break;
        }
    }

    return changed;
}

std::vector<BasicBlock *> CanonicalizeLoop::collectOutsidePredecessors(
    BasicBlock * header,
    const std::unordered_set<BasicBlock *> & loopBody) const
{
    std::vector<BasicBlock *> outsidePreds;
    if (!header) {
        return outsidePreds;
    }

    for (auto * pred : header->getPredecessors()) {
        if (loopBody.find(pred) == loopBody.end()) {
            outsidePreds.push_back(pred);
        }
    }
    return outsidePreds;
}

std::vector<BasicBlock *> CanonicalizeLoop::collectLoopLatches(
    BasicBlock * header,
    const std::unordered_set<BasicBlock *> & loopBody) const
{
    std::vector<BasicBlock *> latches;
    if (!header) {
        return latches;
    }

    for (auto * pred : header->getPredecessors()) {
        if (loopBody.find(pred) != loopBody.end()) {
            latches.push_back(pred);
        }
    }
    return latches;
}

std::vector<BasicBlock *> CanonicalizeLoop::collectLoopExitBlocks(
    const std::unordered_set<BasicBlock *> & loopBody) const
{
    std::vector<BasicBlock *> exits;
    std::unordered_set<BasicBlock *> seen;
    for (auto * bb : loopBody) {
        for (auto * succ : bb->getSuccessors()) {
            if (loopBody.find(succ) != loopBody.end() || !seen.insert(succ).second) {
                continue;
            }
            exits.push_back(succ);
        }
    }
    return exits;
}

std::vector<BasicBlock *> CanonicalizeLoop::collectInsidePredecessors(
    BasicBlock * bb,
    const std::unordered_set<BasicBlock *> & loopBody) const
{
    std::vector<BasicBlock *> preds;
    if (!bb) {
        return preds;
    }

    for (auto * pred : bb->getPredecessors()) {
        if (loopBody.find(pred) != loopBody.end()) {
            preds.push_back(pred);
        }
    }
    return preds;
}

std::vector<BasicBlock *> CanonicalizeLoop::collectOutsidePredecessorsForExit(
    BasicBlock * bb,
    const std::unordered_set<BasicBlock *> & loopBody) const
{
    std::vector<BasicBlock *> preds;
    if (!bb) {
        return preds;
    }

    for (auto * pred : bb->getPredecessors()) {
        if (loopBody.find(pred) == loopBody.end()) {
            preds.push_back(pred);
        }
    }
    return preds;
}

BasicBlock * CanonicalizeLoop::getExistingPreheader(const std::vector<BasicBlock *> & outsidePreds) const
{
    if (outsidePreds.size() != 1) {
        return nullptr;
    }

    BasicBlock * pred = outsidePreds.front();
    if (!pred || pred->getSuccessors().size() != 1) {
        return nullptr;
    }

    return pred;
}

bool CanonicalizeLoop::canonicalizeLoop(BasicBlock * header, const std::unordered_set<BasicBlock *> & loopBody)
{
    return ensurePreheader(header, loopBody) || ensureSingleLatch(header, loopBody) || ensureDedicatedExits(loopBody);
}

bool CanonicalizeLoop::ensurePreheader(BasicBlock * header, const std::unordered_set<BasicBlock *> & loopBody)
{
    if (!header) {
        return false;
    }

    std::vector<BasicBlock *> outsidePreds = collectOutsidePredecessors(header, loopBody);
    if (outsidePreds.empty() || getExistingPreheader(outsidePreds) != nullptr || header == func->getEntryBlock()) {
        return false;
    }

    std::vector<PhiPlan> phiPlans;
    for (auto * inst : header->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }

        PhiPlan plan;
        plan.phi = phi;
        for (auto * pred : outsidePreds) {
            Value * incomingValue = nullptr;
            for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
                if (phi->getIncomingBlock(index) == pred) {
                    incomingValue = phi->getIncomingValue(index);
                    break;
                }
            }
            if (!incomingValue) {
                return false;
            }
            plan.values.push_back(incomingValue);
        }
        phiPlans.push_back(std::move(plan));
    }

    auto * preheader = func->newBasicBlock();
    insertBlockBefore(preheader, header);
    preheader->addInstruction(new BranchInst(func, header));
    preheader->linkSuccessor(header);

    for (auto * pred : outsidePreds) {
        if (!rewriteTerminatorTarget(pred, header, preheader)) {
            return false;
        }

        pred->removeSuccessor(header);
        pred->addSuccessor(preheader);
        preheader->addPredecessor(pred);
        header->removePredecessor(pred);
    }

    if (outsidePreds.size() == 1) {
        for (auto & plan : phiPlans) {
            plan.phi->replaceIncomingBlock(outsidePreds.front(), preheader);
        }
        return true;
    }

    auto & preheaderInsts = preheader->getInstructions();
    auto insertPos = std::prev(preheaderInsts.end());
    for (auto & plan : phiPlans) {
        auto * preheaderPhi = new PhiInst(func, plan.phi->getType());
        for (std::size_t index = 0; index < outsidePreds.size(); ++index) {
            preheaderPhi->addIncoming(plan.values[index], outsidePreds[index]);
        }
        preheaderPhi->setParentBlock(preheader);
        preheaderInsts.insert(insertPos, preheaderPhi);

        for (auto * pred : outsidePreds) {
            plan.phi->removeIncomingBlock(pred);
        }
        plan.phi->addIncoming(preheaderPhi, preheader);
    }

    return true;
}

bool CanonicalizeLoop::ensureSingleLatch(BasicBlock * header, const std::unordered_set<BasicBlock *> & loopBody)
{
    if (!header) {
        return false;
    }

    std::vector<BasicBlock *> latches = collectLoopLatches(header, loopBody);
    if (latches.size() <= 1) {
        return false;
    }

    std::vector<PhiPlan> phiPlans;
    for (auto * inst : header->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }

        PhiPlan plan;
        plan.phi = phi;
        for (auto * latch : latches) {
            Value * incomingValue = nullptr;
            for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
                if (phi->getIncomingBlock(index) == latch) {
                    incomingValue = phi->getIncomingValue(index);
                    break;
                }
            }
            if (!incomingValue) {
                return false;
            }
            plan.values.push_back(incomingValue);
        }
        phiPlans.push_back(std::move(plan));
    }

    auto * latch = func->newBasicBlock();
    insertBlockBefore(latch, header);
    latch->addInstruction(new BranchInst(func, header));
    latch->linkSuccessor(header);

    auto & latchInsts = latch->getInstructions();
    auto insertPos = std::prev(latchInsts.end());
    for (auto & plan : phiPlans) {
        auto * latchPhi = new PhiInst(func, plan.phi->getType());
        for (std::size_t index = 0; index < latches.size(); ++index) {
            latchPhi->addIncoming(plan.values[index], latches[index]);
        }
        latchPhi->setParentBlock(latch);
        latchInsts.insert(insertPos, latchPhi);

        for (auto * oldLatch : latches) {
            plan.phi->removeIncomingBlock(oldLatch);
        }
        plan.phi->addIncoming(latchPhi, latch);
    }

    for (auto * oldLatch : latches) {
        if (!rewriteTerminatorTarget(oldLatch, header, latch)) {
            return false;
        }

        oldLatch->removeSuccessor(header);
        oldLatch->addSuccessor(latch);
        latch->addPredecessor(oldLatch);
        header->removePredecessor(oldLatch);
    }

    return true;
}

bool CanonicalizeLoop::ensureDedicatedExits(const std::unordered_set<BasicBlock *> & loopBody)
{
    for (auto * exitBlock : collectLoopExitBlocks(loopBody)) {
        std::vector<BasicBlock *> insidePreds = collectInsidePredecessors(exitBlock, loopBody);
        std::vector<BasicBlock *> outsidePreds = collectOutsidePredecessorsForExit(exitBlock, loopBody);
        if (insidePreds.empty() || outsidePreds.empty()) {
            continue;
        }
        return splitDedicatedExit(exitBlock, insidePreds, outsidePreds);
    }
    return false;
}

bool CanonicalizeLoop::splitDedicatedExit(BasicBlock * exitBlock,
                                          const std::vector<BasicBlock *> & insidePreds,
                                          const std::vector<BasicBlock *> & outsidePreds)
{
    if (!exitBlock || insidePreds.empty() || outsidePreds.empty()) {
        return false;
    }

    std::vector<PhiPlan> phiPlans;
    for (auto * inst : exitBlock->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }

        PhiPlan plan;
        plan.phi = phi;
        for (auto * pred : insidePreds) {
            Value * incomingValue = nullptr;
            for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
                if (phi->getIncomingBlock(index) == pred) {
                    incomingValue = phi->getIncomingValue(index);
                    break;
                }
            }
            if (!incomingValue) {
                return false;
            }
            plan.values.push_back(incomingValue);
        }
        phiPlans.push_back(std::move(plan));
    }

    auto * dedicatedExit = func->newBasicBlock();
    insertBlockBefore(dedicatedExit, exitBlock);
    dedicatedExit->addInstruction(new BranchInst(func, exitBlock));
    dedicatedExit->linkSuccessor(exitBlock);

    if (insidePreds.size() == 1) {
        for (auto & plan : phiPlans) {
            plan.phi->replaceIncomingBlock(insidePreds.front(), dedicatedExit);
        }
    } else {
        auto & dedicatedInsts = dedicatedExit->getInstructions();
        auto insertPos = std::prev(dedicatedInsts.end());
        for (auto & plan : phiPlans) {
            auto * dedicatedPhi = new PhiInst(func, plan.phi->getType());
            for (std::size_t index = 0; index < insidePreds.size(); ++index) {
                dedicatedPhi->addIncoming(plan.values[index], insidePreds[index]);
            }
            dedicatedPhi->setParentBlock(dedicatedExit);
            dedicatedInsts.insert(insertPos, dedicatedPhi);

            for (auto * pred : insidePreds) {
                plan.phi->removeIncomingBlock(pred);
            }
            plan.phi->addIncoming(dedicatedPhi, dedicatedExit);
        }
    }

    for (auto * pred : insidePreds) {
        if (!rewriteTerminatorTarget(pred, exitBlock, dedicatedExit)) {
            return false;
        }

        pred->removeSuccessor(exitBlock);
        pred->addSuccessor(dedicatedExit);
        dedicatedExit->addPredecessor(pred);
        exitBlock->removePredecessor(pred);
    }

    return true;
}

bool CanonicalizeLoop::rewriteTerminatorTarget(BasicBlock * pred, BasicBlock * oldTarget, BasicBlock * newTarget) const
{
    if (!pred || !oldTarget || !newTarget) {
        return false;
    }

    auto * terminator = pred->getTerminator();
    if (auto * branch = dynamic_cast<BranchInst *>(terminator)) {
        if (branch->getTarget() != oldTarget) {
            return false;
        }
        branch->setTarget(newTarget);
        return true;
    }

    if (auto * condBranch = dynamic_cast<CondBranchInst *>(terminator)) {
        bool rewritten = false;
        if (condBranch->getTrueDest() == oldTarget) {
            condBranch->setTrueDest(newTarget);
            rewritten = true;
        }
        if (condBranch->getFalseDest() == oldTarget) {
            condBranch->setFalseDest(newTarget);
            rewritten = true;
        }
        return rewritten;
    }

    return false;
}

void CanonicalizeLoop::insertBlockBefore(BasicBlock * bb, BasicBlock * before) const
{
    if (!bb || !before) {
        return;
    }

    auto & blocks = func->getBlocks();
    auto bbPos = std::find(blocks.begin(), blocks.end(), bb);
    auto beforePos = std::find(blocks.begin(), blocks.end(), before);
    if (bbPos == blocks.end() || beforePos == blocks.end() || bbPos == beforePos) {
        return;
    }

    blocks.erase(bbPos);
    beforePos = std::find(blocks.begin(), blocks.end(), before);
    blocks.insert(beforePos, bb);
}