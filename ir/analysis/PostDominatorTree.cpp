///
/// @file PostDominatorTree.cpp
/// @brief 后支配树分析实现
///

#include "PostDominatorTree.h"

#include <cstddef>
#include <stack>

#include "BasicBlock.h"
#include "Function.h"

/// @brief 构造并计算指定函数的后支配树
/// @param func 待分析的函数
PostDominatorTree::PostDominatorTree(Function * func)
{
    build(func);
}

/// @brief 判断 postDom 是否后支配 bb
/// @param postDom 候选后支配块
/// @param bb 被判断的基本块
/// @return true 表示 postDom 后支配 bb
bool PostDominatorTree::postDominates(BasicBlock * postDom, BasicBlock * bb) const
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
BasicBlock * PostDominatorTree::getIPDom(BasicBlock * bb) const
{
    int bbIndex = getBlockIndex(bb);
    if (bbIndex < 0) {
        return nullptr;
    }

    return getBlockByIndex(idom[bbIndex]);
}

/// @brief 获取基本块的内部编号
/// @param bb 目标基本块
/// @return 对应编号，不存在时返回 -1
int PostDominatorTree::getBlockIndex(BasicBlock * bb) const
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
BasicBlock * PostDominatorTree::getBlockByIndex(int index) const
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
int PostDominatorTree::intersect(int lhs, int rhs) const
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

/// @brief 从函数 CFG 构造后支配树
/// @param func 待分析的函数
void PostDominatorTree::build(Function * func)
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
/// @param totalNodes 总节点数量，含虚拟根
void PostDominatorTree::computeRPO(int totalNodes)
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
void PostDominatorTree::computeIDom()
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
/// @param totalNodes 总节点数量，含虚拟根
void PostDominatorTree::buildChildren(int totalNodes)
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
/// @param totalNodes 总节点数量，含虚拟根
void PostDominatorTree::computeDFSTimestamps(int totalNodes)
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