///
/// @file DeadInstElim.cpp
/// @brief 死指令删除 pass 实现
///

#include "DeadInstElim.h"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "BasicBlock.h"
#include "BranchInst.h"
#include "CFGStateCleanup.h"
#include "CondBranchInst.h"
#include "Function.h"
#include "Instruction.h"
#include "PhiInst.h"
#include "Value.h"

namespace {

using BlockSet = std::unordered_set<BasicBlock *>;
using InstSet = std::unordered_set<Instruction *>;

struct PostDominatorTree {

    explicit PostDominatorTree(Function * func)
    {
        build(func);
    }

    /// @brief 判断 postDom 是否后支配 bb
    /// @param postDom 候选后支配块
    /// @param bb 被判断的基本块
    /// @return true 表示 postDom 后支配 bb
    bool postDominates(BasicBlock * postDom, BasicBlock * bb) const
    {
        int postDomIndex = getBlockIndex(postDom);
        int bbIndex = getBlockIndex(bb);
        if (postDomIndex < 0 || bbIndex < 0) {
            return false;
        }

        return dfIn[postDomIndex] <= dfIn[bbIndex] && dfOut[bbIndex] <= dfOut[postDomIndex];
    }

    /// @brief 获取基本块的直接后支配者
    /// @param bb 目标基本块
    /// @return 直接后支配块，不存在或为虚拟根时返回 nullptr
    BasicBlock * getIPDom(BasicBlock * bb) const
    {
        int bbIndex = getBlockIndex(bb);
        if (bbIndex < 0) {
            return nullptr;
        }

        return getBlockByIndex(idom[bbIndex]);
    }

private:
    /// @brief 获取基本块的内部编号
    /// @param bb 目标基本块
    /// @return 对应编号，不存在时返回 -1
    int getBlockIndex(BasicBlock * bb) const
    {
        auto it = blockIndex.find(bb);
        if (it == blockIndex.end()) {
            return -1;
        }

        return it->second;
    }

    /// @brief 根据编号获取真实基本块
    /// @param index 基本块编号
    /// @return 对应基本块，虚拟根或非法编号返回 nullptr
    BasicBlock * getBlockByIndex(int index) const
    {
        if (index < 0 || index >= rootIndex) {
            return nullptr;
        }

        return blocks[static_cast<std::size_t>(index)];
    }

    /// @brief 计算两点在后支配树上的最近公共祖先
    /// @param lhs 第一个节点编号
    /// @param rhs 第二个节点编号
    /// @return 最近公共祖先编号
    int intersect(int lhs, int rhs) const
    {
        while (lhs != rhs) {
            while (rpoNumber[lhs] > rpoNumber[rhs]) {
                lhs = idom[lhs];
            }
            while (rpoNumber[rhs] > rpoNumber[lhs]) {
                rhs = idom[rhs];
            }
        }

        return lhs;
    }

    /// @brief 从函数 CFG 构造轻量后支配树
    /// @param func 待分析的函数
    void build(Function * func)
    {
        if (!func || func->getBlocks().empty()) {
            return;
        }

        blocks = func->getBlocks();
        rootIndex = static_cast<int>(blocks.size());
        const int totalNodes = rootIndex + 1;

        for (int index = 0; index < rootIndex; ++index) {
            blockIndex.emplace(blocks[static_cast<std::size_t>(index)], index);
        }

        reverseSuccs.assign(static_cast<std::size_t>(totalNodes), {});
        reversePreds.assign(static_cast<std::size_t>(totalNodes), {});

        for (int index = 0; index < rootIndex; ++index) {
            auto * bb = blocks[static_cast<std::size_t>(index)];
            for (auto * succ : bb->getSuccessors()) {
                auto succIt = blockIndex.find(succ);
                if (succIt == blockIndex.end()) {
                    continue;
                }

                int succIndex = succIt->second;
                reverseSuccs[static_cast<std::size_t>(succIndex)].push_back(index);
                reversePreds[static_cast<std::size_t>(index)].push_back(succIndex);
            }

            if (!bb->getSuccessors().empty()) {
                continue;
            }

            reverseSuccs[static_cast<std::size_t>(rootIndex)].push_back(index);
            reversePreds[static_cast<std::size_t>(index)].push_back(rootIndex);
        }

        // 没有真实退出块可达的区域视为额外伪退出，避免遗漏非终止区域
        std::vector<bool> reverseReachable(static_cast<std::size_t>(totalNodes), false);
        std::vector<int> reachWorklist{rootIndex};
        reverseReachable[static_cast<std::size_t>(rootIndex)] = true;
        while (!reachWorklist.empty()) {
            int node = reachWorklist.back();
            reachWorklist.pop_back();

            for (int succ : reverseSuccs[static_cast<std::size_t>(node)]) {
                if (reverseReachable[static_cast<std::size_t>(succ)]) {
                    continue;
                }

                reverseReachable[static_cast<std::size_t>(succ)] = true;
                reachWorklist.push_back(succ);
            }
        }

        for (int index = 0; index < rootIndex; ++index) {
            if (reverseReachable[static_cast<std::size_t>(index)]) {
                continue;
            }

            reverseSuccs[static_cast<std::size_t>(rootIndex)].push_back(index);
            reversePreds[static_cast<std::size_t>(index)].push_back(rootIndex);
        }

        computeRPO(totalNodes);
        computeIDom();
        buildChildren(totalNodes);
        computeDFSTimestamps(totalNodes);
    }

    /// @brief 计算逆图上的逆后序编号
    /// @param totalNodes 总节点数量（含虚拟根）
    void computeRPO(int totalNodes)
    {
        if (rootIndex < 0) {
            return;
        }

        std::vector<bool> visited(static_cast<std::size_t>(totalNodes), false);
        std::vector<int> postOrder;

        struct Frame {
            int node = -1;
            std::size_t nextSuccIndex = 0;
        };

        std::stack<Frame> stk;
        stk.push({rootIndex, 0});
        visited[static_cast<std::size_t>(rootIndex)] = true;

        while (!stk.empty()) {
            Frame & frame = stk.top();
            auto & succs = reverseSuccs[static_cast<std::size_t>(frame.node)];

            if (frame.nextSuccIndex < succs.size()) {
                int succ = succs[frame.nextSuccIndex++];
                if (visited[static_cast<std::size_t>(succ)]) {
                    continue;
                }

                visited[static_cast<std::size_t>(succ)] = true;
                stk.push({succ, 0});
                continue;
            }

            postOrder.push_back(frame.node);
            stk.pop();
        }

        rpoOrder.reserve(postOrder.size());
        for (auto it = postOrder.rbegin(); it != postOrder.rend(); ++it) {
            rpoOrder.push_back(*it);
        }

        rpoNumber.assign(static_cast<std::size_t>(totalNodes), -1);
        for (int index = 0; index < static_cast<int>(rpoOrder.size()); ++index) {
            rpoNumber[static_cast<std::size_t>(rpoOrder[static_cast<std::size_t>(index)])] = index;
        }
    }

    /// @brief 计算每个节点的直接后支配者
    void computeIDom()
    {
        if (rpoOrder.empty()) {
            return;
        }

        idom.assign(reverseSuccs.size(), -1);
        idom[static_cast<std::size_t>(rpoOrder.front())] = rpoOrder.front();

        bool changed = true;
        while (changed) {
            changed = false;

            for (std::size_t index = 1; index < rpoOrder.size(); ++index) {
                int node = rpoOrder[index];
                int newIDom = -1;

                for (int pred : reversePreds[static_cast<std::size_t>(node)]) {
                    if (pred < 0 || idom[static_cast<std::size_t>(pred)] < 0) {
                        continue;
                    }

                    newIDom = (newIDom < 0) ? pred : intersect(newIDom, pred);
                }

                if (newIDom >= 0 && idom[static_cast<std::size_t>(node)] != newIDom) {
                    idom[static_cast<std::size_t>(node)] = newIDom;
                    changed = true;
                }
            }
        }
    }

    /// @brief 构建后支配树孩子列表
    /// @param totalNodes 总节点数量（含虚拟根）
    void buildChildren(int totalNodes)
    {
        children.assign(static_cast<std::size_t>(totalNodes), {});
        for (std::size_t index = 1; index < rpoOrder.size(); ++index) {
            int node = rpoOrder[index];
            int parent = idom[static_cast<std::size_t>(node)];
            if (parent < 0) {
                continue;
            }

            children[static_cast<std::size_t>(parent)].push_back(node);
        }
    }

    /// @brief 为后支配树打 DFS 时间戳
    /// @param totalNodes 总节点数量（含虚拟根）
    void computeDFSTimestamps(int totalNodes)
    {
        dfIn.assign(static_cast<std::size_t>(totalNodes), -1);
        dfOut.assign(static_cast<std::size_t>(totalNodes), -1);
        if (rpoOrder.empty()) {
            return;
        }

        int timer = 0;
        using Frame = std::pair<int, std::size_t>;
        std::vector<Frame> stk;
        stk.push_back({rpoOrder.front(), 0});
        dfIn[static_cast<std::size_t>(rpoOrder.front())] = timer++;

        while (!stk.empty()) {
            Frame & frame = stk.back();
            auto & childList = children[static_cast<std::size_t>(frame.first)];

            if (frame.second < childList.size()) {
                int child = childList[frame.second++];
                dfIn[static_cast<std::size_t>(child)] = timer++;
                stk.push_back({child, 0});
                continue;
            }

            dfOut[static_cast<std::size_t>(frame.first)] = timer++;
            stk.pop_back();
        }
    }

    std::vector<BasicBlock *> blocks;
    std::unordered_map<BasicBlock *, int> blockIndex;
    std::vector<std::vector<int>> reverseSuccs;
    std::vector<std::vector<int>> reversePreds;
    std::vector<int> rpoOrder;
    std::vector<int> rpoNumber;
    std::vector<int> idom;
    std::vector<std::vector<int>> children;
    std::vector<int> dfIn;
    std::vector<int> dfOut;
    int rootIndex = -1;
};

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