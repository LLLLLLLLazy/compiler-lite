///
/// @file RemoveEmptyLoop.cpp
/// @brief 空循环消除 pass 实现
///

#include "RemoveEmptyLoop.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "AnalysisCache.h"
#include "BasicBlock.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "CondBranchInst.h"
#include "DominatorTree.h"
#include "Function.h"
#include "FunctionSideEffectAnalysis.h"
#include "Instruction.h"
#include "LoopInfo.h"
#include "Module.h"
#include "PhiInst.h"
#include "StoreInst.h"
#include "Use.h"
#include "Value.h"

namespace {

/// @brief 找到循环的唯一出口块
/// @param header 循环头
/// @param loopBody 循环体基本块集合
/// @return 唯一出口块，存在多个出口或无出口时返回 nullptr
BasicBlock * findUniqueExit(BasicBlock * header, const std::unordered_set<BasicBlock *> & loopBody)
{
    BasicBlock * exit = nullptr;
    for (auto * bb : loopBody) {
        for (auto * succ : bb->getSuccessors()) {
            if (loopBody.find(succ) != loopBody.end()) {
                continue;
            }
            if (exit != nullptr && exit != succ) {
                return nullptr;
            }
            exit = succ;
        }
    }
    (void) header;
    return exit;
}

/// @brief 找到循环头来自循环外的唯一前驱（preheader）
/// @param header 循环头
/// @param loopBody 循环体基本块集合
/// @return 唯一外部前驱，不唯一时返回 nullptr
BasicBlock * findUniquePreheader(BasicBlock * header, const std::unordered_set<BasicBlock *> & loopBody)
{
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
    return preheader;
}

/// @brief 判断指令是否带有不可消除的副作用
/// @param inst 待判断指令
/// @param sideEffects 共享副作用分析器
/// @return true 表示存在副作用
bool hasSideEffect(Instruction * inst, FunctionSideEffectAnalysis & sideEffects)
{
    if (!inst) {
        return false;
    }
    if (inst->mayHaveSideEffects() && !dynamic_cast<CallInst *>(inst)) {
        return true;
    }
    if (dynamic_cast<StoreInst *>(inst) != nullptr) {
        return true;
    }
    if (auto * call = dynamic_cast<CallInst *>(inst)) {
        return !sideEffects.isSideEffectFree(call->getCallee());
    }
    return false;
}

} // namespace

/// @brief 构造空循环消除 pass
/// @param _func 待优化函数
/// @param _mod 所属模块
RemoveEmptyLoop::RemoveEmptyLoop(Function * _func, Module * _mod) : func(_func), mod(_mod)
{}

/// @brief 判断循环体是否无副作用且出口无依赖
/// @param header 循环头
/// @param loopBody 循环体基本块集合
/// @param exit 唯一出口块
/// @return true 表示可安全删除
bool RemoveEmptyLoop::isRemovableLoop(BasicBlock * header,
                                      const std::unordered_set<BasicBlock *> & loopBody,
                                      BasicBlock * exit) const
{
    FunctionSideEffectAnalysis sideEffects;
    for (auto * bb : loopBody) {
        for (auto * inst : bb->getInstructions()) {
            if (hasSideEffect(inst, sideEffects)) {
                return false;
            }

            // 循环内定义的值不能在循环外被使用
            for (auto * use : inst->getUseList()) {
                auto * userInst = dynamic_cast<Instruction *>(use->getUser());
                if (!userInst) {
                    return false;
                }
                BasicBlock * userBlock = userInst->getParentBlock();
                if (!userBlock || loopBody.find(userBlock) == loopBody.end()) {
                    return false;
                }
            }
        }
    }

    // 出口块的 phi 若有来自循环体内块的 incoming edge，说明循环的控制流
    // 决定了该 phi 的取值。删除循环会将所有循环→出口边重定向到 preheader，
    // 导致 phi 失去区分不同路径的能力，产生错误语义，因此不可删除
    for (auto * inst : exit->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
            if (loopBody.find(phi->getIncomingBlock(i)) != loopBody.end()) {
                return false;
            }
        }
    }

    (void) header;
    return true;
}

/// @brief 尝试删除以 header 为头的循环
/// @param header 循环头基本块
/// @return true 表示成功删除该循环
bool RemoveEmptyLoop::tryRemoveLoop(BasicBlock * header)
{
    DominatorTree domTree(func);
    LoopInfo loopInfo(func, &domTree);
    if (!loopInfo.isLoopHeader(header)) {
        return false;
    }

    const auto * loopBodyPtr = loopInfo.getLoopBody(header);
    if (!loopBodyPtr) {
        return false;
    }
    const std::unordered_set<BasicBlock *> loopBody = *loopBodyPtr;

    BasicBlock * exit = findUniqueExit(header, loopBody);
    BasicBlock * preheader = findUniquePreheader(header, loopBody);
    if (!exit || !preheader || loopBody.find(exit) != loopBody.end()) {
        return false;
    }

    // preheader 必须以无条件跳转进入循环头，删除后才能直接接到出口
    auto * preBranch = dynamic_cast<BranchInst *>(preheader->getTerminator());
    if (!preBranch || preBranch->getTarget() != header) {
        return false;
    }

    if (!isRemovableLoop(header, loopBody, exit)) {
        return false;
    }

    // 出口块中若存在以循环块为 incoming 的 phi，统一改写为来自 preheader
    for (auto * inst : exit->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
            BasicBlock * inBlock = phi->getIncomingBlock(i);
            if (loopBody.find(inBlock) != loopBody.end()) {
                phi->replaceIncomingBlock(inBlock, preheader);
            }
        }
    }

    // 将 preheader 的跳转目标从循环头改为出口
    preBranch->setTarget(exit);
    preheader->removeSuccessor(header);
    preheader->addSuccessor(exit);
    exit->addPredecessor(preheader);

    // 拆除循环块之间的 def-use 关系，更新出口块前驱
    exit->removePredecessor(header);
    for (auto * bb : loopBody) {
        for (auto * inst : bb->getInstructions()) {
            inst->clearOperands();
        }
    }

    // 从函数中移除循环块
    auto & blocks = func->getBlocks();
    for (auto * bb : loopBody) {
        auto it = std::find(blocks.begin(), blocks.end(), bb);
        if (it != blocks.end()) {
            blocks.erase(it);
        }
    }
    for (auto * bb : loopBody) {
        delete bb;
    }

    return true;
}

/// @brief 删除所有无副作用且出口无依赖的自然循环
/// @return true 表示 IR 发生变化
bool RemoveEmptyLoop::run()
{
    if (!func || !mod || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    bool changed = false;
    while (true) {
        bool localChanged = false;
        std::vector<BasicBlock *> blocks = func->getBlocks();
        for (auto * bb : blocks) {
            if (tryRemoveLoop(bb)) {
                localChanged = true;
                changed = true;
                func->getAnalysisCache().invalidateCFGAnalyses();
                break;
            }
        }
        if (!localChanged) {
            break;
        }
    }

    return changed;
}
