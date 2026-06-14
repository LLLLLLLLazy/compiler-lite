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
#include "CondBranchInst.h"
#include "Function.h"
#include "AnalysisCache.h"
#include "Instruction.h"
#include "PostDominatorTree.h"
#include "PhiInst.h"
#include "Value.h"
#include "toolPass/CFGStateCleanup.h"

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
    /// 每个基本块控制依赖的分支块集合，即该块位于这些分支块的后支配边界上
    std::unordered_map<BasicBlock *, std::vector<BasicBlock *>> controlDeps;
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
    if (inst->isDead()) {
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

/// @brief 预计算每个基本块的控制依赖来源（后支配边界）
/// @param state Mark 状态
///
/// 采用 Cytron 等人的支配边界算法在逆向 CFG 上的对偶形式：
/// 逆向 CFG 中的汇合点即原 CFG 中拥有多个后继的分支块；对每个这样的分支块 @c branch，
/// 从其各后继沿后支配树 idom 链向上回溯，直到 @c branch 的直接后支配者，
/// 沿途每个块 @c X 都控制依赖于 @c branch（即 @c branch 在 @c X 的后支配边界上）。
/// 这样把原先 O(B²) 的逐块后支配查询替换为一次 O(B + 边界规模) 的预计算
void buildControlDependence(MarkState & state)
{
    if (!state.func || !state.postDomTree) {
        return;
    }

    for (auto * branch : state.func->getBlocks()) {
        auto succs = collectUniqueSuccessors(branch);
        if (succs.size() < 2) {
            continue; // 不是逆向 CFG 的汇合点（非分支块）
        }

        // 只关心条件跳转，与改写死分支阶段保持一致的语义
        if (!dynamic_cast<CondBranchInst *>(branch->getTerminator())) {
            continue;
        }

        BasicBlock * ipdom = state.postDomTree->getIPDom(branch);
        for (auto * succ : succs) {
            for (BasicBlock * runner = succ; runner != nullptr && runner != ipdom;) {
                state.controlDeps[runner].push_back(branch);
                BasicBlock * runnerIpdom = state.postDomTree->getIPDom(runner);
                if (runnerIpdom == runner) {
                    break; // 保护：idom 指回自身（通常是虚拟出口）时停止
                }
                runner = runnerIpdom;
            }
        }
    }
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

            auto it = state.controlDeps.find(liveBlock);
            if (it == state.controlDeps.end()) {
                continue;
            }

            for (auto * branch : it->second) {
                markInstructionLive(state, branch->getTerminator());
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
    bool cfgChanged = changed;

    PostDominatorTree postDomTree(func);

    MarkState state;
    state.func = func;
    state.postDomTree = &postDomTree;

    buildControlDependence(state);

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
        cfgChanged = true;
        changed = sanitizeCFGState(func) || changed;
    }

    bool swept = sweepDeadInstructions(func, state.liveInstructions) > 0;
    changed = swept || changed;
    auto & cache = func->getAnalysisCache();
    if (cfgChanged) {
        // 改写了死分支或清理了 CFG，CFG 派生分析整体失效
        cache.invalidateCFGAnalyses();
    } else if (swept) {
        // 仅删除死指令，使引用指令的值分析失效
        cache.invalidateValueAnalyses();
    }
    return changed;
}
