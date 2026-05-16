///
/// @file LoopRotate.cpp
/// @brief 循环旋转 pass 实现
///

#include "LoopRotate.h"

#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

#include "BasicBlock.h"
#include "BranchInst.h"
#include "CondBranchInst.h"
#include "DominatorTree.h"
#include "Function.h"
#include "ICmpInst.h"
#include "Instruction.h"
#include "LoopInfo.h"
#include "Module.h"
#include "PhiInst.h"
#include "ScalarEvolution.h"
#include "Value.h"

namespace {

void eraseInstructionIfUnused(Instruction * inst)
{
    if (!inst || !inst->getUseList().empty()) {
        return;
    }

    auto * bb = inst->getParentBlock();
    if (!bb) {
        return;
    }

    auto & insts = bb->getInstructions();
    auto pos = std::find(insts.begin(), insts.end(), inst);
    if (pos == insts.end()) {
        return;
    }

    insts.erase(pos);
    inst->clearOperands();
    delete inst;
}

} // namespace

LoopRotate::LoopRotate(Function * _func, Module * /*_mod*/) : func(_func)
{}

bool LoopRotate::run()
{
    if (!func || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    bool changed = false;
    while (true) {
        DominatorTree domTree(func);
        LoopInfo loopInfo(func, &domTree);
        ScalarEvolution scev(func, &domTree, &loopInfo);

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
            if (tryRotateHeader(header, loopInfo, scev)) {
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

bool LoopRotate::tryRotateHeader(BasicBlock * header, LoopInfo & loopInfo, ScalarEvolution & scev) const
{
    if (!header) {
        return false;
    }

    ScalarEvolution::CanonicalLoop loop;
    if (!scev.matchCanonicalLoop(header, loop) || !loop.preheader || !loop.body || !loop.latch || !loop.exit ||
        !loop.induction || !loop.branch || !loop.cmp || !loop.initialValue || !loop.recurrence) {
        return false;
    }

    const auto * loopBody = loopInfo.getLoopBody(header);
    if (!loopBody || loopBody->empty()) {
        return false;
    }

    // Multi-block loop rotation needs stronger late cleanup/cost modeling; in the
    // current pipeline it regresses hot kernels like conv2d more often than it helps.
    if (loop.body != loop.latch) {
        return false;
    }

    if (!hasOnlySupportedOutsideUses(header, loop.exit, *loopBody)) {
        return false;
    }

    const IRInstOperator compareOp = getCompareOp(loop.compareKind);
    if (compareOp == IRInstOperator::IRINST_OP_MAX) {
        return false;
    }

    std::vector<BasicBlock *> exits = collectLoopExitBlocks(*loopBody);
    if (exits.size() != 1 || exits.front() != loop.exit) {
        return false;
    }

    std::vector<BasicBlock *> exitPreds = collectInsidePredecessors(loop.exit, *loopBody);
    if (exitPreds.size() != 1 || exitPreds.front() != header) {
        return false;
    }

    if (!hasSingleBranchTo(loop.preheader, header) || !hasSingleBranchTo(loop.latch, header)) {
        return false;
    }

    std::unordered_map<Value *, Value *> latchValueMap;
    std::unordered_map<Value *, Value *> initialValueMap;
    for (auto * inst : header->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }

        if (phi->getIncomingCount() != 2) {
            return false;
        }

        Value * preheaderValue = nullptr;
        Value * latchValue = nullptr;
        for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
            if (phi->getIncomingBlock(index) == loop.preheader) {
                preheaderValue = phi->getIncomingValue(index);
            }
            if (phi->getIncomingBlock(index) == loop.latch) {
                latchValue = phi->getIncomingValue(index);
            }
        }

        if (!preheaderValue || !latchValue) {
            return false;
        }

        initialValueMap[phi] = preheaderValue;
        latchValueMap[phi] = latchValue;
    }

    auto * nextInduction = latchValueMap[loop.induction];
    if (!nextInduction) {
        return false;
    }

    for (auto * inst : loop.exit->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }

        for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
            if (phi->getIncomingBlock(index) != header) {
                continue;
            }

            Value * incomingValue = phi->getIncomingValue(index);
            if (latchValueMap.find(incomingValue) != latchValueMap.end()) {
                continue;
            }

            auto * incomingInst = dynamic_cast<Instruction *>(incomingValue);
            if (incomingInst && incomingInst->getParentBlock() &&
                loopBody->find(incomingInst->getParentBlock()) != loopBody->end()) {
                return false;
            }
        }
    }

    auto & preheaderInsts = loop.preheader->getInstructions();
    auto * preCmp = new ICmpInst(func, compareOp, loop.initialValue, loop.boundValue, loop.cmp->getType());
    preCmp->setParentBlock(loop.preheader);
    preheaderInsts.insert(std::prev(preheaderInsts.end()), preCmp);
    if (!replaceBranchWithCondBranch(loop.preheader, preCmp, header, loop.exit)) {
        return false;
    }
    loop.preheader->addSuccessor(loop.exit);
    loop.exit->addPredecessor(loop.preheader);

    if (!replaceCondBranchWithBranch(header, loop.body)) {
        return false;
    }
    eraseInstructionIfUnused(loop.cmp);
    header->removeSuccessor(loop.exit);
    loop.exit->removePredecessor(header);

    auto & latchInsts = loop.latch->getInstructions();
    auto * latchCmp = new ICmpInst(func, compareOp, nextInduction, loop.boundValue, loop.cmp->getType());
    latchCmp->setParentBlock(loop.latch);
    latchInsts.insert(std::prev(latchInsts.end()), latchCmp);
    if (!replaceBranchWithCondBranch(loop.latch, latchCmp, header, loop.exit)) {
        return false;
    }
    loop.latch->addSuccessor(loop.exit);
    loop.exit->addPredecessor(loop.latch);

    for (auto * inst : loop.exit->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }

        for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
            if (phi->getIncomingBlock(index) != header) {
                continue;
            }

            Value * incomingValue = phi->getIncomingValue(index);
            Value * latchValue = incomingValue;
            auto latchIt = latchValueMap.find(incomingValue);
            if (latchIt != latchValueMap.end()) {
                latchValue = latchIt->second;
            }
            phi->setOperand(index, latchValue);
            phi->replaceIncomingBlock(header, loop.latch);

            Value * zeroTripValue = incomingValue;
            auto initIt = initialValueMap.find(incomingValue);
            if (initIt != initialValueMap.end()) {
                zeroTripValue = initIt->second;
            }
            phi->addIncoming(zeroTripValue, loop.preheader);
            break;
        }
    }

    return true;
}

std::vector<BasicBlock *> LoopRotate::collectLoopExitBlocks(const std::unordered_set<BasicBlock *> & loopBody) const
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

std::vector<BasicBlock *> LoopRotate::collectInsidePredecessors(
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

bool LoopRotate::hasOnlySupportedOutsideUses(BasicBlock * header,
                                             BasicBlock * exit,
                                             const std::unordered_set<BasicBlock *> & loopBody) const
{
    if (!header || !exit) {
        return false;
    }

    for (auto * bb : loopBody) {
        for (auto * inst : bb->getInstructions()) {
            if (!inst || !inst->hasResultValue()) {
                continue;
            }

            const bool isHeaderPhi = inst->getParentBlock() == header && dynamic_cast<PhiInst *>(inst) != nullptr;
            for (auto * use : inst->getUseList()) {
                auto * userInst = dynamic_cast<Instruction *>(use->getUser());
                if (!userInst) {
                    return false;
                }

                BasicBlock * useBlock = userInst->getParentBlock();
                if (useBlock && loopBody.find(useBlock) != loopBody.end()) {
                    continue;
                }

                if (!isHeaderPhi) {
                    return false;
                }

                auto * userPhi = dynamic_cast<PhiInst *>(userInst);
                if (!userPhi || useBlock != exit) {
                    return false;
                }

                bool matched = false;
                for (int32_t index = 0; index < userPhi->getIncomingCount(); ++index) {
                    if (userPhi->getIncomingValue(index) != inst) {
                        continue;
                    }
                    if (userPhi->getIncomingBlock(index) != header) {
                        return false;
                    }
                    matched = true;
                }

                if (!matched) {
                    return false;
                }
            }
        }
    }

    return true;
}

bool LoopRotate::hasSingleBranchTo(BasicBlock * bb, BasicBlock * target) const
{
    auto * branch = bb ? dynamic_cast<BranchInst *>(bb->getTerminator()) : nullptr;
    return branch && branch->getTarget() == target && bb->getSuccessors().size() == 1;
}

bool LoopRotate::replaceBranchWithCondBranch(BasicBlock * bb,
                                             Value * condition,
                                             BasicBlock * trueDest,
                                             BasicBlock * falseDest) const
{
    if (!bb || !condition || !trueDest || !falseDest) {
        return false;
    }

    auto * branch = dynamic_cast<BranchInst *>(bb->getTerminator());
    if (!branch) {
        return false;
    }

    auto & insts = bb->getInstructions();
    auto branchPos = std::find(insts.begin(), insts.end(), static_cast<Instruction *>(branch));
    if (branchPos == insts.end()) {
        return false;
    }

    insts.erase(branchPos);
    branch->clearOperands();
    delete branch;

    auto * condBranch = new CondBranchInst(func, condition, trueDest, falseDest);
    condBranch->setParentBlock(bb);
    insts.push_back(condBranch);
    return true;
}

bool LoopRotate::replaceCondBranchWithBranch(BasicBlock * bb, BasicBlock * target) const
{
    if (!bb || !target) {
        return false;
    }

    auto * condBranch = dynamic_cast<CondBranchInst *>(bb->getTerminator());
    if (!condBranch) {
        return false;
    }

    auto & insts = bb->getInstructions();
    auto branchPos = std::find(insts.begin(), insts.end(), static_cast<Instruction *>(condBranch));
    if (branchPos == insts.end()) {
        return false;
    }

    insts.erase(branchPos);
    condBranch->clearOperands();
    delete condBranch;

    auto * branch = new BranchInst(func, target);
    branch->setParentBlock(bb);
    insts.push_back(branch);
    return true;
}

IRInstOperator LoopRotate::getCompareOp(ScalarEvolution::CompareKind compareKind)
{
    switch (compareKind) {
    case ScalarEvolution::CompareKind::LessThan:
        return IRInstOperator::IRINST_OP_LT_I;
    case ScalarEvolution::CompareKind::LessEqual:
        return IRInstOperator::IRINST_OP_LE_I;
    case ScalarEvolution::CompareKind::GreaterThan:
        return IRInstOperator::IRINST_OP_GT_I;
    case ScalarEvolution::CompareKind::GreaterEqual:
        return IRInstOperator::IRINST_OP_GE_I;
    case ScalarEvolution::CompareKind::Unknown:
    case ScalarEvolution::CompareKind::Equal:
    case ScalarEvolution::CompareKind::NotEqual:
        return IRInstOperator::IRINST_OP_MAX;
    }

    return IRInstOperator::IRINST_OP_MAX;
}