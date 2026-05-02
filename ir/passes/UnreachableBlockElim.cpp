///
/// @file UnreachableBlockElim.cpp
/// @brief 不可达基本块删除 pass 实现
///

#include "UnreachableBlockElim.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "BasicBlock.h"
#include "Function.h"
#include "Instruction.h"
#include "PhiInst.h"

namespace {

/// @brief 从基本块中删除所有来自死前驱的 phi incoming
/// @param bb 需要修剪 phi 的幸存基本块
/// @param deadPred 即将被删除的死前驱块
void removePhiIncomingFromDeadPred(BasicBlock * bb, BasicBlock * deadPred)
{
    if (!bb || !deadPred) {
        return;
    }

    for (auto * inst : bb->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }

        phi->removeIncomingBlock(deadPred);
    }
}

} // namespace

/// @brief 构造不可达基本块删除器
/// @param _func 待优化的函数
UnreachableBlockElim::UnreachableBlockElim(Function * _func) : func(_func)
{}

/// @brief 执行不可达基本块删除
/// @return 若 IR 被修改则返回 true
bool UnreachableBlockElim::run()
{
    if (!func || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    BasicBlock * entry = func->getEntryBlock();
    if (!entry) {
        return false;
    }

    std::unordered_set<BasicBlock *> reachable;
    std::vector<BasicBlock *> worklist;
    reachable.insert(entry);
    worklist.push_back(entry);

    while (!worklist.empty()) {
        BasicBlock * bb = worklist.back();
        worklist.pop_back();

        for (auto * succ : bb->getSuccessors()) {
            if (succ && reachable.insert(succ).second) {
                worklist.push_back(succ);
            }
        }
    }

    auto & blocks = func->getBlocks();
    std::vector<BasicBlock *> deadBlocks;
    deadBlocks.reserve(blocks.size());
    for (auto * bb : blocks) {
        if (!reachable.count(bb)) {
            deadBlocks.push_back(bb);
        }
    }

    if (deadBlocks.empty()) {
        return false;
    }

    for (auto * deadBB : deadBlocks) {
        for (auto * pred : deadBB->getPredecessors()) {
            if (reachable.count(pred)) {
                pred->removeSuccessor(deadBB);
            }
        }

        for (auto * succ : deadBB->getSuccessors()) {
            if (!reachable.count(succ)) {
                continue;
            }

            succ->removePredecessor(deadBB);
            removePhiIncomingFromDeadPred(succ, deadBB);
        }
    }

    // 先拆掉死块内所有指令的 use-def 边，再统一释放块对象。
    for (auto * deadBB : deadBlocks) {
        for (auto * inst : deadBB->getInstructions()) {
            inst->clearOperands();
        }
    }

    blocks.erase(std::remove_if(blocks.begin(),
                                blocks.end(),
                                [&](BasicBlock * bb) {
                                    return !reachable.count(bb);
                                }),
                 blocks.end());

    for (auto * deadBB : deadBlocks) {
        delete deadBB;
    }

    return true;
}