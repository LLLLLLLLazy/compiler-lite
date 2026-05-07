///
/// @file DeadInstElim.cpp
/// @brief 死指令删除 pass 实现
///

#include "DeadInstElim.h"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "BasicBlock.h"
#include "BranchInst.h"
#include "CFGStateCleanup.h"
#include "CondBranchInst.h"
#include "Function.h"
#include "Instruction.h"
#include "PostDominatorTree.h"
#include "PhiInst.h"
#include "Value.h"

namespace {

using BlockSet = std::unordered_set<BasicBlock *>;
using InstSet = std::unordered_set<Instruction *>;

struct MarkState {
    Function * func = nullptr;
    const PostDominatorTree * postDomTree = nullptr;
    InstSet liveInstructions;
    BlockSet liveBlocks;
    std::deque<Instruction *> instWorklist;
    std::deque<BasicBlock *> controlWorklist;
    BlockSet queuedControlBlocks;
};

/// @brief 收集一个基本块的去重后继列表
/// @param bb 待分析的基本块
/// @return 该块终结指令直接指向的后继块列表
std::vector<BasicBlock *> collectUniqueSuccessors(BasicBlock * bb)
{
    std::vector<BasicBlock *> succs;
    if (!bb) {
        return succs;
    }

    for (auto * succ : bb->getSuccessors()) {
        if (!succ) {
            continue;
        }

        if (std::find(succs.begin(), succs.end(), succ) == succs.end()) {
            succs.push_back(succ);
        }
    }

    return succs;
}


/// @brief 判断指令是否天然必须保留
/// @param inst 待判断的指令
/// @return true 表示该指令是 Mark 的根
bool isAlwaysLiveInstruction(Instruction * inst)
{
    if (!inst) {
        return false;
    }

    if (dynamic_cast<CondBranchInst *>(inst)) {
        return false;
    }

    return inst->mayHaveSideEffects();
}

/// @brief 将基本块加入控制依赖工作队列
/// @param state Mark 状态
/// @param bb 待加入的基本块
void enqueueControlBlock(MarkState & state, BasicBlock * bb)
{
    if (!bb || !state.queuedControlBlocks.insert(bb).second) {
        return;
    }

    state.controlWorklist.push_back(bb);
}

/// @brief 标记一条指令为活指令并加入工作队列
/// @param state Mark 状态
/// @param inst 待标记的指令
void markInstructionLive(MarkState & state, Instruction * inst)
{
    if (!inst || !state.liveInstructions.insert(inst).second) {
        return;
    }

    state.instWorklist.push_back(inst);

    BasicBlock * parent = inst->getParentBlock();
    if (parent) {
        state.liveBlocks.insert(parent);
        enqueueControlBlock(state, parent);
    }

    auto * phi = dynamic_cast<PhiInst *>(inst);
    if (!phi) {
        return;
    }

    for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
        enqueueControlBlock(state, phi->getIncomingBlock(index));
    }
}

/// @brief 判断条件跳转是否控制了指定活块的可达性
/// @param branchBlock 候选分支所在基本块
/// @param liveBlock 已知活块
/// @param postDomTree 后支配树
/// @return true 表示 liveBlock 对 branchBlock 存在控制依赖
bool controlsLiveBlock(BasicBlock * branchBlock, BasicBlock * liveBlock, const PostDominatorTree & postDomTree)
{
    if (!branchBlock || !liveBlock) {
        return false;
    }

    auto * condBranch = dynamic_cast<CondBranchInst *>(branchBlock->getTerminator());
    if (!condBranch) {
        return false;
    }

    if (postDomTree.postDominates(liveBlock, branchBlock)) {
        return false;
    }

    for (auto * succ : collectUniqueSuccessors(branchBlock)) {
        if (postDomTree.postDominates(liveBlock, succ)) {
            return true;
        }
    }

    return false;
}

/// @brief 沿 def-use 和控制依赖传播活指令
/// @param state Mark 状态
void runMark(MarkState & state)
{
    while (!state.instWorklist.empty() || !state.controlWorklist.empty()) {
        while (!state.instWorklist.empty()) {
            Instruction * inst = state.instWorklist.front();
            state.instWorklist.pop_front();

            for (auto * operand : inst->getOperandsValue()) {
                auto * operandInst = dynamic_cast<Instruction *>(operand);
                markInstructionLive(state, operandInst);
            }
        }

        while (!state.controlWorklist.empty()) {
            BasicBlock * liveBlock = state.controlWorklist.front();
            state.controlWorklist.pop_front();

            for (auto * bb : state.func->getBlocks()) {
                if (!state.postDomTree || !controlsLiveBlock(bb, liveBlock, *state.postDomTree)) {
                    continue;
                }

                markInstructionLive(state, bb->getTerminator());
            }
        }
    }
}

/// @brief 寻找最近的活后支配块
/// @param bb 待查询的基本块
/// @param state Mark 状态
/// @return 严格后支配 bb 的最近活块，不存在时返回 nullptr
BasicBlock * findNearestLivePostDominator(BasicBlock * bb, const MarkState & state)
{
    if (!bb || !state.postDomTree) {
        return nullptr;
    }

    for (BasicBlock * candidate = state.postDomTree->getIPDom(bb); candidate != nullptr;
         candidate = state.postDomTree->getIPDom(candidate)) {
        if (state.liveBlocks.count(candidate)) {
            return candidate;
        }
    }

    return nullptr;
}

/// @brief 为死条件跳转选择一个可安全保留的原始后继
/// @param bb 条件跳转所在基本块
/// @param preferredLiveBlock 最近活后支配块
/// @param state Mark 状态
/// @return 选中的原始后继块，不存在时返回 nullptr
BasicBlock * chooseRetargetSuccessor(BasicBlock * bb, BasicBlock * preferredLiveBlock, const MarkState & state)
{
    auto succs = collectUniqueSuccessors(bb);
    if (succs.empty()) {
        return nullptr;
    }

    BasicBlock * fallback = nullptr;

    for (auto * succ : succs) {
        if (!succ) {
            continue;
        }

        if (preferredLiveBlock && (!state.postDomTree || !state.postDomTree->postDominates(preferredLiveBlock, succ))) {
            continue;
        }

        if (state.liveBlocks.count(succ)) {
            return succ;
        }

        if (!fallback) {
            fallback = succ;
        }
    }

    if (fallback) {
        return fallback;
    }

    if (preferredLiveBlock) {
        return nullptr;
    }

    for (auto * succ : succs) {
        if (succ && state.liveBlocks.count(succ)) {
            return succ;
        }
    }

    return succs.front();
}

/// @brief 将死条件跳转改写为无条件跳转
/// @param func 条件跳转所在函数
/// @param condBranch 待改写的条件跳转
/// @param target 新的跳转目标
/// @return true 表示改写成功
bool rewriteDeadCondBranch(Function * func, CondBranchInst * condBranch, BasicBlock * target)
{
    if (!func || !condBranch || !target) {
        return false;
    }

    BasicBlock * parent = condBranch->getParentBlock();
    if (!parent) {
        return false;
    }

    auto & insts = parent->getInstructions();
    auto pos = std::find(insts.begin(), insts.end(), static_cast<Instruction *>(condBranch));
    if (pos == insts.end()) {
        return false;
    }

    insts.erase(pos);
    condBranch->clearOperands();
    delete condBranch;

    auto * branch = new BranchInst(func, target);
    branch->setParentBlock(parent);
    insts.push_back(branch);
    return true;
}

/// @brief 改写所有不再活跃的条件跳转
/// @param state Mark 状态
/// @return 被成功改写的条件跳转数量
int32_t rewriteDeadConditionalBranches(const MarkState & state)
{
    int32_t rewrittenCount = 0;

    for (auto * bb : state.func->getBlocks()) {
        auto * condBranch = dynamic_cast<CondBranchInst *>(bb->getTerminator());
        if (!condBranch || state.liveInstructions.count(condBranch)) {
            continue;
        }

        BasicBlock * preferredLiveBlock = findNearestLivePostDominator(bb, state);
        BasicBlock * target = chooseRetargetSuccessor(bb, preferredLiveBlock, state);
        if (!target) {
            continue;
        }

        if (rewriteDeadCondBranch(state.func, condBranch, target)) {
            ++rewrittenCount;
        }
    }

    return rewrittenCount;
}

/// @brief 清扫所有未被标记为活跃的非终结指令
/// @param func 待清扫的函数
/// @param liveInstructions Mark 阶段保留下来的活指令集合
/// @return 被真正移除的死指令数量
int32_t sweepDeadInstructions(Function * func, const InstSet & liveInstructions)
{
    if (!func) {
        return 0;
    }

    InstSet deadInstructions;
    for (auto * bb : func->getBlocks()) {
        for (auto * inst : bb->getInstructions()) {
            if (inst->isTerminator() || liveInstructions.count(inst)) {
                continue;
            }

            deadInstructions.insert(inst);
        }
    }

    for (auto * inst : deadInstructions) {
        inst->clearOperands();
    }

    int32_t removedCount = 0;
    for (auto * bb : func->getBlocks()) {
        auto & insts = bb->getInstructions();
        auto it = insts.begin();
        while (it != insts.end()) {
            Instruction * inst = *it;
            if (!deadInstructions.count(inst)) {
                ++it;
                continue;
            }

            auto next = std::next(it);
            insts.erase(it);
            delete inst;
            it = next;
            ++removedCount;
        }
    }

    return removedCount;
}

} // namespace

/// @brief 构造死指令删除器
/// @param _func 待优化的函数
DeadInstElim::DeadInstElim(Function * _func) : func(_func)
{}

/// @brief 执行死指令删除
/// @return 若 IR 被修改则返回 true
bool DeadInstElim::run()
{
    if (!func || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    bool changed = sanitizeCFGState(func);

    PostDominatorTree postDomTree(func);

    MarkState state;
    state.func = func;
    state.postDomTree = &postDomTree;

    for (auto * bb : func->getBlocks()) {
        for (auto * inst : bb->getInstructions()) {
            if (isAlwaysLiveInstruction(inst)) {
                markInstructionLive(state, inst);
            }
        }
    }

    runMark(state);

    if (rewriteDeadConditionalBranches(state) > 0) {
        changed = true;
        changed = sanitizeCFGState(func) || changed;
    }

    return sweepDeadInstructions(func, state.liveInstructions) > 0 || changed;
}