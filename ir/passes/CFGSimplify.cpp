///
/// @file CFGSimplify.cpp
/// @brief 控制流图化简 pass 实现
///
/// 当前实现覆盖四类局部 CFG 化简：
///   1. 将 true/false 指向同一目标的条件跳转折叠为无条件跳转
///   2. 删除仅包含无条件跳转的空块，并将其前驱直接重定向到后继
///   3. 合并 `br -> 单前驱后继块` 这种可直接拼接的块对
///   4. 将跳向空条件块的无条件分支直接线程化到条件块的后继
///
/// 所有变换都会同步维护 phi incoming 以及基本块的前驱/后继关系
///

#include "CFGSimplify.h"

#include <algorithm>
#include <vector>

#include "BasicBlock.h"
#include "CFGStateCleanup.h"
#include "BranchInst.h"
#include "CondBranchInst.h"
#include "Function.h"
#include "Instruction.h"
#include "PhiInst.h"
#include "Value.h"

namespace {

/// @brief 判断基本块是否只包含一条终结指令
/// @param bb 待检查的基本块
/// @return 若块中仅有一条终结指令则返回 true
bool hasSingleTerminatorOnly(BasicBlock * bb)
{
    return bb && bb->getInstructions().size() == 1 && bb->getTerminator();
}

/// @brief 判断终结指令是否指向给定目标块
/// @param term 待检查的终结指令
/// @param target 目标块
/// @return 若终结指令存在到该目标块的边则返回 true
bool terminatorTargetsBlock(Instruction * term, BasicBlock * target)
{
    if (!term || !target) {
        return false;
    }

    if (auto * branch = dynamic_cast<BranchInst *>(term)) {
        return branch->getTarget() == target;
    }

    if (auto * condBranch = dynamic_cast<CondBranchInst *>(term)) {
        return condBranch->getTrueDest() == target || condBranch->getFalseDest() == target;
    }

    return false;
}

/// @brief 将终结指令中指向旧目标块的边重定向到新目标块
/// @param bb 需要改写终结指令的基本块
/// @param oldTarget 旧目标块
/// @param newTarget 新目标块
/// @return 若至少存在一条边被成功改写则返回 true
bool retargetTerminatorEdge(BasicBlock * bb, BasicBlock * oldTarget, BasicBlock * newTarget)
{
    if (!bb || !oldTarget || !newTarget) {
        return false;
    }

    Instruction * term = bb->getTerminator();
    if (auto * branch = dynamic_cast<BranchInst *>(term)) {
        if (branch->getTarget() != oldTarget) {
            return false;
        }

        branch->setTarget(newTarget);
        bb->removeSuccessor(oldTarget);
        bb->addSuccessor(newTarget);
        return true;
    }

    if (auto * condBranch = dynamic_cast<CondBranchInst *>(term)) {
        bool changed = false;
        if (condBranch->getTrueDest() == oldTarget) {
            condBranch->setTrueDest(newTarget);
            changed = true;
        }
        if (condBranch->getFalseDest() == oldTarget) {
            condBranch->setFalseDest(newTarget);
            changed = true;
        }
        if (!changed) {
            return false;
        }

        bb->removeSuccessor(oldTarget);
        bb->addSuccessor(newTarget);
        return true;
    }

    return false;
}

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

/// @brief 将某个前驱块的 phi incoming 复制到新的前驱块
/// @param bb 需要补充 phi incoming 的基本块
/// @param oldPred 现有 incoming 对应的前驱块
/// @param newPred 新增 incoming 对应的前驱块
void duplicatePhiIncoming(BasicBlock * bb, BasicBlock * oldPred, BasicBlock * newPred)
{
    if (!bb || !oldPred || !newPred || oldPred == newPred) {
        return;
    }

    for (auto * inst : bb->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }

        std::vector<Value *> incomingValues;
        for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
            if (phi->getIncomingBlock(index) == oldPred) {
                incomingValues.push_back(phi->getIncomingValue(index));
            }
        }

        for (auto * incomingValue : incomingValues) {
            phi->addIncoming(incomingValue, newPred);
        }
    }
}

/// @brief 将来自旧前驱块的 phi incoming 扩展到一组新前驱块后删除旧项
/// @param bb 需要扩展 phi incoming 的基本块
/// @param oldPred 即将被移除的旧前驱块
/// @param newPreds 替代该旧前驱块的新前驱块列表
void expandPhiIncoming(BasicBlock * bb, BasicBlock * oldPred, const std::vector<BasicBlock *> & newPreds)
{
    if (!bb || !oldPred) {
        return;
    }

    for (auto * inst : bb->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }

        std::vector<Value *> incomingValues;
        for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
            if (phi->getIncomingBlock(index) == oldPred) {
                incomingValues.push_back(phi->getIncomingValue(index));
            }
        }

        if (incomingValues.empty()) {
            continue;
        }

        phi->removeIncomingBlock(oldPred);
        for (auto * pred : newPreds) {
            if (!pred) {
                continue;
            }

            for (auto * incomingValue : incomingValues) {
                phi->addIncoming(incomingValue, pred);
            }
        }
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
        localChanged = sanitizeCFGState(func);
        changed = localChanged || changed;
        std::vector<BasicBlock *> snapshot = func->getBlocks();
        for (auto * bb : snapshot) {
            auto & blocks = func->getBlocks();
            if (std::find(blocks.begin(), blocks.end(), bb) == blocks.end()) {
                continue;
            }

            if (tryFoldRedundantCondBranch(bb) || tryBypassEmptyBlock(bb) || tryMergeBranchSuccessor(bb)
                || tryThreadThroughEmptyCondBlock(bb)) {
                localChanged = true;
                changed = true;
                break;
            }
        }
    } while (localChanged);

    return changed;
}

/// @brief 折叠 true/false 指向同一后继的条件跳转
/// @param bb 待检查的基本块
/// @return 若成功折叠则返回 true
bool CFGSimplify::tryFoldRedundantCondBranch(BasicBlock * bb)
{
    if (!bb) {
        return false;
    }

    auto * condBranch = dynamic_cast<CondBranchInst *>(bb->getTerminator());
    if (!condBranch) {
        return false;
    }

    BasicBlock * target = condBranch->getTrueDest();
    if (!target || target != condBranch->getFalseDest()) {
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

/// @brief 删除仅包含无条件跳转的空块
/// @param bb 待检查的基本块
/// @return 若成功旁路并删除该块则返回 true
bool CFGSimplify::tryBypassEmptyBlock(BasicBlock * bb)
{
    if (!bb || bb == func->getEntryBlock() || !hasSingleTerminatorOnly(bb)) {
        return false;
    }

    auto * branch = dynamic_cast<BranchInst *>(bb->getTerminator());
    if (!branch) {
        return false;
    }

    BasicBlock * succ = branch->getTarget();
    if (!succ || succ == bb) {
        return false;
    }

    std::vector<BasicBlock *> preds = bb->getPredecessors();
    if (preds.empty()) {
        return false;
    }

    for (auto * pred : preds) {
        if (!terminatorTargetsBlock(pred->getTerminator(), bb)) {
            return false;
        }

        // 若 pred 已经直接通向 succ，则旁路 bb 会把两条不同的 CFG 入边
        // 压成同一个 predecessor block，破坏 succ 上 phi 的语义。
        if (pred == succ || terminatorTargetsBlock(pred->getTerminator(), succ)) {
            return false;
        }
    }

    expandPhiIncoming(succ, bb, preds);

    for (auto * pred : preds) {
        if (!retargetTerminatorEdge(pred, bb, succ)) {
            return false;
        }

        succ->addPredecessor(pred);
    }

    succ->removePredecessor(bb);
    bb->removeSuccessor(succ);

    auto & blocks = func->getBlocks();
    auto blockPos = std::find(blocks.begin(), blocks.end(), bb);
    if (blockPos != blocks.end()) {
        blocks.erase(blockPos);
    }
    delete bb;

    return true;
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

/// @brief 将跳向空条件块的无条件分支线程化到其两个后继
/// @param bb 待检查的前驱块
/// @return 若成功线程化则返回 true
bool CFGSimplify::tryThreadThroughEmptyCondBlock(BasicBlock * bb)
{
    if (!bb) {
        return false;
    }

    auto * branch = dynamic_cast<BranchInst *>(bb->getTerminator());
    if (!branch) {
        return false;
    }

    BasicBlock * dest = branch->getTarget();
    if (!dest || !hasSingleTerminatorOnly(dest)) {
        return false;
    }

    auto * condBranch = dynamic_cast<CondBranchInst *>(dest->getTerminator());
    if (!condBranch) {
        return false;
    }

    BasicBlock * trueDest = condBranch->getTrueDest();
    BasicBlock * falseDest = condBranch->getFalseDest();
    if (!trueDest || !falseDest) {
        return false;
    }

    duplicatePhiIncoming(trueDest, dest, bb);
    if (falseDest != trueDest) {
        duplicatePhiIncoming(falseDest, dest, bb);
    }

    auto & insts = bb->getInstructions();
    auto branchPos = std::find(insts.begin(), insts.end(), static_cast<Instruction *>(branch));
    if (branchPos == insts.end()) {
        return false;
    }

    insts.erase(branchPos);
    branch->clearOperands();
    delete branch;

    dest->removePredecessor(bb);
    bb->removeSuccessor(dest);

    auto * newBranch = new CondBranchInst(func, condBranch->getCondition(), trueDest, falseDest);
    newBranch->setParentBlock(bb);
    insts.push_back(newBranch);

    bb->addSuccessor(trueDest);
    trueDest->addPredecessor(bb);
    bb->addSuccessor(falseDest);
    falseDest->addPredecessor(bb);

    return true;
}
