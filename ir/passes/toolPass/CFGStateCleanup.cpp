///
/// @file CFGStateCleanup.cpp
/// @brief CFG 边集与 phi incoming 一致性清理工具实现
///

#include "CFGStateCleanup.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "BasicBlock.h"
#include "BranchInst.h"
#include "CondBranchInst.h"
#include "Function.h"
#include "Instruction.h"
#include "PhiInst.h"

namespace {

/// @brief 判断一个基本块是否仍在当前函数的活跃块列表中
/// @param blocks 当前函数仍存活的基本块列表
/// @param target 待检查基本块
/// @return true 表示该基本块仍然活跃
bool isLiveBlock(const std::vector<BasicBlock *> & blocks, BasicBlock * target)
{
    return target != nullptr && std::find(blocks.begin(), blocks.end(), target) != blocks.end();
}

/// @brief 判断两个基本块集合是否包含同样的元素
/// @param lhs 左侧集合
/// @param rhs 右侧集合
/// @return true 表示两个集合等价
bool sameBlockSet(const std::vector<BasicBlock *> & lhs, const std::vector<BasicBlock *> & rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (auto * block : lhs) {
        if (std::find(rhs.begin(), rhs.end(), block) == rhs.end()) {
            return false;
        }
    }

    return true;
}

/// @brief 向目标列表追加一个不重复的后继块
/// @param targets 目标块列表
/// @param target 待追加基本块
void appendUniqueTarget(std::vector<BasicBlock *> & targets, BasicBlock * target)
{
    if (target == nullptr || std::find(targets.begin(), targets.end(), target) != targets.end()) {
        return;
    }

    targets.push_back(target);
}

/// @brief 从终结指令提取真实后继块列表
/// @param bb 待分析基本块
/// @return 终结指令直接指向的后继块列表
std::vector<BasicBlock *> collectTerminatorTargets(BasicBlock * bb)
{
    std::vector<BasicBlock *> targets;
    if (bb == nullptr) {
        return targets;
    }

    Instruction * term = bb->getTerminator();
    if (auto * branch = dynamic_cast<BranchInst *>(term)) {
        appendUniqueTarget(targets, branch->getTarget());
        return targets;
    }

    auto * condBranch = dynamic_cast<CondBranchInst *>(term);
    if (condBranch == nullptr) {
        return targets;
    }

    appendUniqueTarget(targets, condBranch->getTrueDest());
    appendUniqueTarget(targets, condBranch->getFalseDest());
    return targets;
}

} // namespace

/// @brief 清理函数 CFG 中悬空或失配的前驱/后继边与 phi incoming
/// @param func 待清理的函数
/// @return true 表示至少移除了一个失效项
bool sanitizeCFGState(Function * func)
{
    if (func == nullptr) {
        return false;
    }

    const auto & blocks = func->getBlocks();
    bool changed = false;

    std::unordered_map<BasicBlock *, std::size_t> blockIndex;
    blockIndex.reserve(blocks.size());
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        blockIndex.emplace(blocks[index], index);
    }

    std::vector<std::vector<BasicBlock *>> expectedPreds(blocks.size());
    std::vector<std::vector<BasicBlock *>> expectedSuccs(blocks.size());

    for (std::size_t index = 0; index < blocks.size(); ++index) {
        auto * bb = blocks[index];
        if (bb == nullptr) {
            continue;
        }

        for (auto * target : collectTerminatorTargets(bb)) {
            if (!isLiveBlock(blocks, target)) {
                continue;
            }

            expectedSuccs[index].push_back(target);
            expectedPreds[blockIndex[target]].push_back(bb);
        }
    }

    for (std::size_t index = 0; index < blocks.size(); ++index) {
        auto * bb = blocks[index];
        if (bb == nullptr) {
            continue;
        }

        auto & preds = bb->getPredecessors();
        auto & succs = bb->getSuccessors();
        if (!sameBlockSet(preds, expectedPreds[index]) || !sameBlockSet(succs, expectedSuccs[index])) {
            changed = true;
        }

        preds = expectedPreds[index];
        succs = expectedSuccs[index];
    }

    for (std::size_t index = 0; index < blocks.size(); ++index) {
        auto * bb = blocks[index];
        if (bb == nullptr) {
            continue;
        }

        const auto & preds = expectedPreds[index];
        for (auto * inst : bb->getInstructions()) {
            auto * phi = dynamic_cast<PhiInst *>(inst);
            if (phi == nullptr) {
                break;
            }

            std::vector<BasicBlock *> staleIncomingBlocks;
            for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
                BasicBlock * incomingBlock = phi->getIncomingBlock(index);
                if (isLiveBlock(blocks, incomingBlock)
                    && std::find(preds.begin(), preds.end(), incomingBlock) != preds.end()) {
                    continue;
                }

                if (std::find(staleIncomingBlocks.begin(), staleIncomingBlocks.end(), incomingBlock)
                    == staleIncomingBlocks.end()) {
                    staleIncomingBlocks.push_back(incomingBlock);
                }
            }

            if (!staleIncomingBlocks.empty()) {
                changed = true;
            }

            for (auto * staleBlock : staleIncomingBlocks) {
                phi->removeIncomingBlock(staleBlock);
            }
        }
    }

    return changed;
}