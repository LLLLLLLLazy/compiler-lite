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
#include "AnalysisCache.h"
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

    /// @brief 延迟到全部前置条件验证完成后执行的一项 header phi 改写
    struct HeaderPhiRewrite {
        PhiInst * headerPhi = nullptr;
        Value * incomingValue = nullptr;
        PhiInst * latchPhi = nullptr;
    };

    std::vector<BasicBlock *> preds = latch->getPredecessors();
    std::vector<HeaderPhiRewrite> rewrites;
    for (auto * inst : header->getInstructions()) {
        auto * headerPhi = dynamic_cast<PhiInst *>(inst);
        if (!headerPhi) {
            break;
        }

        Value * incomingValue = nullptr;
        for (int32_t index = 0; index < headerPhi->getIncomingCount(); ++index) {
            if (headerPhi->getIncomingBlock(index) != latch) {
                continue;
            }

            if (incomingValue != nullptr) {
                return false;
            }
            incomingValue = headerPhi->getIncomingValue(index);
        }

        if (!incomingValue) {
            return false;
        }

        auto * latchPhi = dynamic_cast<PhiInst *>(incomingValue);
        if (latchPhi && latchPhiSet.find(latchPhi) == latchPhiSet.end()) {
            latchPhi = nullptr;
        }
        rewrites.push_back({headerPhi, incomingValue, latchPhi});
    }

    if (rewrites.empty()) {
        return false;
    }

    for (auto * pred : preds) {
        auto * predBranch = dynamic_cast<BranchInst *>(pred ? pred->getTerminator() : nullptr);
        if (!predBranch || predBranch->getTarget() != latch) {
            return false;
        }
    }

    auto & blocks = func->getBlocks();
    auto blockPos = std::find(blocks.begin(), blocks.end(), latch);
    if (blockPos == blocks.end()) {
        return false;
    }

    // 所有可失败检查完成后再统一改写，避免中途退出留下不完整的 header phi
    for (const auto & rewrite : rewrites) {
        rewrite.headerPhi->removeIncomingBlock(latch);
        if (rewrite.latchPhi) {
            for (int32_t index = 0; index < rewrite.latchPhi->getIncomingCount(); ++index) {
                rewrite.headerPhi->addIncoming(rewrite.latchPhi->getIncomingValue(index),
                                               rewrite.latchPhi->getIncomingBlock(index));
            }
            continue;
        }

        // SCCP 可能把 latch phi 折叠成支配该 latch 的普通值；旁路时每条新边继承该值
        for (auto * pred : preds) {
            rewrite.headerPhi->addIncoming(rewrite.incomingValue, pred);
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
            // 删除 synthetic latch 后块集已变，缓存的 LoopInfo/DominatorTree
            // 仍引用被释放的块指针；不失效会让后续 CanonicalizeLoop 等
            // 复用陈旧 CFG 分析时读已释放内存（use-after-free）
            func->getAnalysisCache().invalidateCFGAnalyses();
        }
    } while (localChanged);

    return changed;
}
