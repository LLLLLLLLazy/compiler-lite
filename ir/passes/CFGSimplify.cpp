///
/// @file CFGSimplify.cpp
/// @brief 控制流图化简 pass 实现
///
///
/// 当前实现只做最保守的一类块合并：
///   1. 前驱块必须以无条件跳转结束
///   2. 后继块必须只有这一个前驱
///   3. 合并时需要同步处理 successor 中的 phi，以及 successor 后继块里的 incoming block
///

#include "CFGSimplify.h"

#include <algorithm>
#include <vector>

#include "BasicBlock.h"
#include "BranchInst.h"
#include "Function.h"
#include "Instruction.h"
#include "PhiInst.h"
#include "Value.h"

namespace {

/// @brief 将后继块里指向旧块的 phi incoming block 改写为新块
/// @param bb 需要修正 phi incoming 的基本块
/// @param oldPred 被合并掉的旧前驱块
/// @param newPred 合并后的新前驱块
void rewritePhiIncomingFromMergedBlock(BasicBlock * bb, BasicBlock * oldPred, BasicBlock * newPred)
{
    if (!bb || !oldPred || !newPred) {
        return;
    }

    for (auto * inst : bb->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }

        phi->replaceIncomingBlock(oldPred, newPred);
    }
}

/// @brief 折叠只依赖唯一前驱的 phi 指令
/// @param bb 即将被并入前驱的后继块
/// @param pred 该后继块的唯一前驱
/// @return true 表示所有位于块首的 phi 都已安全折叠
bool collapseSinglePredPhis(BasicBlock * bb, BasicBlock * pred)
{
    if (!bb || !pred) {
        return false;
    }

    auto & insts = bb->getInstructions();
    auto it = insts.begin();
    while (it != insts.end()) {
        auto * phi = dynamic_cast<PhiInst *>(*it);
        if (!phi) {
            break;
        }

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

        phi->replaceAllUseWith(incomingValue);
        phi->clearOperands();

        auto next = std::next(it);
        insts.erase(it);
        delete phi;
        it = next;
    }

    return true;
}

} // namespace

/// @brief 构造 CFG 化简器
/// @param _func 待优化的函数
CFGSimplify::CFGSimplify(Function * _func) : func(_func)
{}

/// @brief 执行 CFG 化简
/// @return 若 IR 被修改则返回 true
bool CFGSimplify::run()
{
    if (!func || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    bool changed = false;
    bool localChanged = false;
    do {
        localChanged = false;
        std::vector<BasicBlock *> snapshot = func->getBlocks();
        for (auto * bb : snapshot) {
            auto & blocks = func->getBlocks();
            if (std::find(blocks.begin(), blocks.end(), bb) == blocks.end()) {
                continue;
            }

            if (tryMergeBranchSuccessor(bb)) {
                localChanged = true;
                changed = true;
                break;
            }
        }
    } while (localChanged);

    return changed;
}

/// @brief 尝试合并 `bb -> succ` 的无条件跳转块对
/// @param bb 待检查的前驱块
/// @return true 表示合并成功并修改了 IR
bool CFGSimplify::tryMergeBranchSuccessor(BasicBlock * bb)
{
    if (!bb) {
        return false;
    }

    // bb 是否以无条件跳转结束
    auto * branch = dynamic_cast<BranchInst *>(bb->getTerminator());
    if (!branch) {
        return false;
    }

    // succ 非空且不是 bb 自己
    BasicBlock * succ = branch->getTarget();
    if (!succ || succ == bb) {
        return false;
    }

    // succ 有且仅有一个前驱 bb
    const auto & preds = succ->getPredecessors();
    if (preds.size() != 1 || preds.front() != bb) {
        return false;
    }

    // 尝试折叠 succ 中只依赖 bb 的 phi 指令，若失败则中止
    if (!collapseSinglePredPhis(succ, bb)) {
        return false;
    }

    // 定位到 bb 末尾的 branch 指令并删除
    auto & bbInsts = bb->getInstructions();
    auto branchPos = std::find(bbInsts.begin(), bbInsts.end(), static_cast<Instruction *>(branch));
    if (branchPos == bbInsts.end()) return false;
    bbInsts.erase(branchPos);
    branch->clearOperands();
    delete branch;

    // 之后的操作类似链表节点合并

    bb->removeSuccessor(succ);

    std::vector<BasicBlock *> succSuccessors = succ->getSuccessors();
    for (auto * next : succSuccessors) {
        rewritePhiIncomingFromMergedBlock(next, succ, bb);
        next->removePredecessor(succ);
        next->addPredecessor(bb);
        bb->addSuccessor(next);
    }

    // 将 succ 的 inst 一个个搬到 bb 末尾
    auto & succInsts = succ->getInstructions();
    while (!succInsts.empty()) {
        Instruction * inst = succInsts.front();
        succInsts.pop_front();
        inst->setParentBlock(bb);
        bbInsts.push_back(inst);
    }

    // 删除 succ 块
    auto & blocks = func->getBlocks();
    auto blockPos = std::find(blocks.begin(), blocks.end(), succ);
    if (blockPos != blocks.end()) {
        blocks.erase(blockPos);
    }

    succ->removePredecessor(bb);
    for (auto * next : succSuccessors) {
        succ->removeSuccessor(next);
    }
    delete succ;
    
    return true;
}