///
/// @file LateLoopCFGCleanup.cpp
/// @brief 固定点循环优化结束后的晚期 CFG 收尾 pass 实现。
///

#include "LateLoopCFGCleanup.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "BasicBlock.h"
#include "BranchInst.h"
#include "Function.h"
#include "Instruction.h"
#include "PhiInst.h"
#include "Value.h"
#include "toolPass/CFGStateCleanup.h"

namespace {

/// @brief 判断基本块是否仍在函数块列表中
bool isLiveBlock(Function * func, BasicBlock * bb)
{
    if (!func || !bb) {
        return false;
    }

    const auto & blocks = func->getBlocks();
    return std::find(blocks.begin(), blocks.end(), bb) != blocks.end();
}

/// @brief 判断所有前驱是否都以无条件跳转进入该块
bool hasOnlyUnconditionalBranchPredecessors(BasicBlock * bb)
{
    if (!bb || bb->getPredecessors().empty()) {
        return false;
    }

    for (auto * pred : bb->getPredecessors()) {
        auto * branch = dynamic_cast<BranchInst *>(pred ? pred->getTerminator() : nullptr);
        if (!branch || branch->getTarget() != bb) {
            return false;
        }
    }

    return true;
}

/// @brief 收集纯 phi+latch 块中的全部 phi 指令
std::vector<PhiInst *> collectPhiOnlyLatchPhis(BasicBlock * bb)
{
    std::vector<PhiInst *> phis;
    if (!bb) {
        return phis;
    }

    auto * branch = dynamic_cast<BranchInst *>(bb->getTerminator());
    if (!branch) {
        return phis;
    }

    for (auto * inst : bb->getInstructions()) {
        if (inst == branch) {
            continue;
        }

        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            phis.clear();
            return phis;
        }

        phis.push_back(phi);
    }

    return phis;
}

/// @brief 判断这些 latch phi 是否只被 header 顶部 phi 使用
bool areLatchPhisOnlyUsedByHeaderPhis(Function * func,
                                      const std::unordered_set<PhiInst *> & latchPhis,
                                      BasicBlock * header)
{
    if (!func || !header) {
        return false;
    }

    for (auto * bb : func->getBlocks()) {
        for (auto * inst : bb->getInstructions()) {
            for (auto * operand : inst->getOperandsValue()) {
                auto * operandPhi = dynamic_cast<PhiInst *>(operand);
                if (!operandPhi || latchPhis.find(operandPhi) == latchPhis.end()) {
                    continue;
                }

                if (bb != header || dynamic_cast<PhiInst *>(inst) == nullptr) {
                    return false;
                }
            }
        }
    }

    return true;
}

/// @brief 尝试撤销一个 synthetic single-latch 块
bool tryRemoveSyntheticLatch(Function * func, BasicBlock * latch)
{
    if (!func || !latch || latch == func->getEntryBlock() || !hasOnlyUnconditionalBranchPredecessors(latch)) {
        return false;
    }

    auto latchPhis = collectPhiOnlyLatchPhis(latch);
    if (latchPhis.empty()) {
        return false;
    }

    auto * branch = dynamic_cast<BranchInst *>(latch->getTerminator());
    BasicBlock * header = branch ? branch->getTarget() : nullptr;
    if (!header || header == latch) {
        return false;
    }

    std::unordered_set<PhiInst *> latchPhiSet(latchPhis.begin(), latchPhis.end());
    if (!areLatchPhisOnlyUsedByHeaderPhis(func, latchPhiSet, header)) {
        return false;
    }

    bool sawHeaderIncomingFromLatch = false;
    for (auto * inst : header->getInstructions()) {
        auto * headerPhi = dynamic_cast<PhiInst *>(inst);
        if (!headerPhi) {
            break;
        }

        PhiInst * latchPhi = nullptr;
        for (int32_t index = 0; index < headerPhi->getIncomingCount(); ++index) {
            if (headerPhi->getIncomingBlock(index) != latch) {
                continue;
            }

            latchPhi = dynamic_cast<PhiInst *>(headerPhi->getIncomingValue(index));
            if (!latchPhi || latchPhiSet.find(latchPhi) == latchPhiSet.end()) {
                return false;
            }
            break;
        }

        if (!latchPhi) {
            continue;
        }

        sawHeaderIncomingFromLatch = true;
        headerPhi->removeIncomingBlock(latch);
        for (int32_t index = 0; index < latchPhi->getIncomingCount(); ++index) {
            headerPhi->addIncoming(latchPhi->getIncomingValue(index), latchPhi->getIncomingBlock(index));
        }
    }

    if (!sawHeaderIncomingFromLatch) {
        return false;
    }

    std::vector<BasicBlock *> preds = latch->getPredecessors();
    for (auto * pred : preds) {
        auto * predBranch = dynamic_cast<BranchInst *>(pred ? pred->getTerminator() : nullptr);
        if (!predBranch || predBranch->getTarget() != latch) {
            return false;
        }
    }

    for (auto * pred : preds) {
        auto * predBranch = static_cast<BranchInst *>(pred->getTerminator());
        predBranch->setTarget(header);
        pred->removeSuccessor(latch);
        pred->addSuccessor(header);
        header->addPredecessor(pred);
    }

    header->removePredecessor(latch);
    latch->removeSuccessor(header);

    auto & blocks = func->getBlocks();
    auto blockPos = std::find(blocks.begin(), blocks.end(), latch);
    if (blockPos == blocks.end()) {
        return false;
    }

    blocks.erase(blockPos);
    delete latch;
    return true;
}

} // namespace

/// @brief 构造固定点后循环 CFG 收尾 pass
/// @param _func 待处理函数
LateLoopCFGCleanup::LateLoopCFGCleanup(Function * _func) : func(_func)
{}

/// @brief 清理固定点优化后不再需要的 synthetic loop CFG
/// @return 若函数被修改则返回 true
bool LateLoopCFGCleanup::run()
{
    if (!func || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    bool changed = sanitizeCFGState(func);
    bool localChanged = false;
    do {
        localChanged = false;
        std::vector<BasicBlock *> snapshot = func->getBlocks();
        for (auto * bb : snapshot) {
            if (!isLiveBlock(func, bb)) {
                continue;
            }

            if (tryRemoveSyntheticLatch(func, bb)) {
                localChanged = true;
                changed = true;
                break;
            }
        }

        if (localChanged) {
            changed = sanitizeCFGState(func) || changed;
        }
    } while (localChanged);

    return changed;
}