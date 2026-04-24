///
/// @file DominatorTree.cpp
/// @brief 支配树（Dominator Tree）实现
///
/// 算法参考：
///   Cooper, Harvey, Kennedy (2001) "A Simple, Fast Dominance Algorithm"
///   Rice Technical Report TR-06-33870
///

#include "DominatorTree.h"

#include <cassert>
#include <stack>

#include "BasicBlock.h"
#include "Function.h"

const std::vector<BasicBlock *> DominatorTree::emptyVec;

/// @brief 构造并计算指定函数的支配树
/// @param func 待分析的函数
DominatorTree::DominatorTree(Function * func)
{
    if (func == nullptr || func->getBlocks().empty()) {
        return;
    }
    computeRPO(func);
    computeIDom();
    buildDomChildren();
    computeDFSTimestamps();
}

// ---------------------------------------------------------------------------
// 第 1 步：计算逆后序遍历（迭代式 DFS）
// ---------------------------------------------------------------------------
/// @brief 计算函数 CFG 的逆后序编号
/// @param func 待分析的函数
void DominatorTree::computeRPO(Function * func)
{
    BasicBlock * entry = func->getEntryBlock();
    if (entry == nullptr) {
        return;
    }

    std::unordered_map<BasicBlock *, bool> visited;
    std::vector<BasicBlock *> postOrder;

    // 使用显式栈模拟后序 DFS，需要在所有后继处理完成后再记录当前节点。
    struct Frame {
        BasicBlock * bb;
        std::size_t nextSuccIdx;
    };

    std::stack<Frame> stk;
    stk.push({entry, 0});
    visited[entry] = true;

    while (!stk.empty()) {
        Frame & frame = stk.top();
        BasicBlock * bb = frame.bb;
        auto & succs = bb->getSuccessors();

        if (frame.nextSuccIdx < succs.size()) {
            BasicBlock * succ = succs[frame.nextSuccIdx++];
            if (!visited[succ]) {
                visited[succ] = true;
                stk.push({succ, 0});
            }
        } else {
            postOrder.push_back(bb);
            stk.pop();
        }
    }

    // 逆后序等于后序结果反转
    rpoOrder.reserve(postOrder.size());
    for (int i = (int) postOrder.size() - 1; i >= 0; --i) {
        rpoOrder.push_back(postOrder[i]);
    }

    for (int i = 0; i < (int) rpoOrder.size(); ++i) {
        rpoNum[rpoOrder[i]] = i;
    }
}

// ---------------------------------------------------------------------------
// 第 2 步：计算直接支配者（Cooper 等人的算法）
// ---------------------------------------------------------------------------
/// @brief 根据逆后序结果计算每个基本块的直接支配者
void DominatorTree::computeIDom()
{
    if (rpoOrder.empty()) {
        return;
    }

    BasicBlock * entry = rpoOrder[0];

    // 初始化：入口块的 idom 为自身，其余块暂记为未定义
    idomMap[entry] = entry;
    for (std::size_t i = 1; i < rpoOrder.size(); ++i) {
        idomMap[rpoOrder[i]] = nullptr;
    }

    bool changed = true;
    while (changed) {
        changed = false;

        // 按逆后序迭代，跳过入口块
        for (std::size_t i = 1; i < rpoOrder.size(); ++i) {
            BasicBlock * bb = rpoOrder[i];
            BasicBlock * newIdom = nullptr;

            for (BasicBlock * pred : bb->getPredecessors()) {
                // 只考虑已经拥有 idom 的前驱块
                if (idomMap[pred] == nullptr) {
                    continue;
                }
                if (newIdom == nullptr) {
                    newIdom = pred;
                } else {
                    newIdom = intersect(newIdom, pred);
                }
            }

            if (newIdom != nullptr && idomMap[bb] != newIdom) {
                idomMap[bb] = newIdom;
                changed = true;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 第 3 步：构建支配树孩子节点映射
// ---------------------------------------------------------------------------
/// @brief 根据 idom 关系构建支配树的孩子列表
void DominatorTree::buildDomChildren()
{
    if (rpoOrder.empty()) {
        return;
    }

    BasicBlock * entry = rpoOrder[0];
    // 即使没有孩子，也要确保入口块在映射表中存在项
    domChildrenMap[entry];

    for (std::size_t i = 1; i < rpoOrder.size(); ++i) {
        BasicBlock * bb = rpoOrder[i];
        BasicBlock * parent = idomMap[bb];
        if (parent != nullptr) {
            domChildrenMap[parent].push_back(bb);
        }
        // 同样确保当前块自身也有映射项
        domChildrenMap[bb];
    }
}

// ---------------------------------------------------------------------------
// 第 4 步：为支配树打上 DFS 时间戳，便于 O(1) 判断支配关系
// ---------------------------------------------------------------------------
/// @brief 为支配树节点计算 DFS 进入/退出时间戳
void DominatorTree::computeDFSTimestamps()
{
    if (rpoOrder.empty()) {
        return;
    }

    int timer = 0;
    dfsTimestamp(rpoOrder[0], timer);
}

/// @brief 递归计算某个支配树节点的 DFS 时间戳
/// @param bb 当前节点
/// @param timer 当前时间计数器
void DominatorTree::dfsTimestamp(BasicBlock * bb, int & timer)
{
    dfIn[bb] = timer++;

    auto it = domChildrenMap.find(bb);
    if (it != domChildrenMap.end()) {
        for (BasicBlock * child : it->second) {
            dfsTimestamp(child, timer);
        }
    }

    dfOut[bb] = timer++;
}

// ---------------------------------------------------------------------------
// 借助逆后序编号在支配树上求交汇点
// ---------------------------------------------------------------------------
/// @brief 计算两个基本块在支配关系上的交汇点
/// @param b1 第一个基本块
/// @param b2 第二个基本块
/// @return 两个基本块的公共支配祖先
BasicBlock * DominatorTree::intersect(BasicBlock * b1, BasicBlock * b2) const
{
    while (b1 != b2) {
        while (rpoNum.at(b1) > rpoNum.at(b2)) {
            b1 = idomMap.at(b1);
        }
        while (rpoNum.at(b2) > rpoNum.at(b1)) {
            b2 = idomMap.at(b2);
        }
    }
    return b1;
}

// ---------------------------------------------------------------------------
// 对外查询接口
// ---------------------------------------------------------------------------
/// @brief 获取指定基本块的直接支配者
/// @param bb 目标基本块
/// @return 直接支配者，不存在时返回空指针
BasicBlock * DominatorTree::getIDom(BasicBlock * bb) const
{
    auto it = idomMap.find(bb);
    if (it == idomMap.end()) {
        return nullptr;
    }
    return it->second;
}

/// @brief 获取指定基本块在支配树中的孩子节点列表
/// @param bb 目标基本块
/// @return 孩子节点列表引用
const std::vector<BasicBlock *> & DominatorTree::getDomChildren(BasicBlock * bb) const
{
    auto it = domChildrenMap.find(bb);
    if (it == domChildrenMap.end()) {
        return emptyVec;
    }
    return it->second;
}

/// @brief 获取指定基本块的逆后序编号
/// @param bb 目标基本块
/// @return 逆后序编号，不存在时返回 -1
int DominatorTree::getRPONumber(BasicBlock * bb) const
{
    auto it = rpoNum.find(bb);
    if (it == rpoNum.end()) {
        return -1;
    }
    return it->second;
}

/// @brief 判断基本块 a 是否支配基本块 b
/// @param a 可能的支配者
/// @param b 目标基本块
/// @return true 表示 a 支配 b，false 表示不支配
bool DominatorTree::dominates(BasicBlock * a, BasicBlock * b) const
{
    auto itA = dfIn.find(a);
    auto itB = dfIn.find(b);
    if (itA == dfIn.end() || itB == dfIn.end()) {
        return false;
    }
    // 若 a 的 DFS 区间完整覆盖 b 的 DFS 区间，则 a 支配 b。
    return dfIn.at(a) <= dfIn.at(b) && dfOut.at(b) <= dfOut.at(a);
}

/// @brief 判断基本块 a 是否严格支配基本块 b
/// @param a 可能的严格支配者
/// @param b 目标基本块
/// @return true 表示 a 严格支配 b，false 表示不严格支配
bool DominatorTree::strictlyDominates(BasicBlock * a, BasicBlock * b) const
{
    return a != b && dominates(a, b);
}

// ---------------------------------------------------------------------------
// 调试输出
// ---------------------------------------------------------------------------
/// @brief 输出支配树调试信息
/// @param str 输出字符串
void DominatorTree::print(std::string & str) const
{
    str += "=== Dominator Tree ===\n";

    if (rpoOrder.empty()) {
        str += "  (empty)\n";
        return;
    }

    // 输出逆后序遍历顺序
    str += "RPO order:";
    for (auto * bb : rpoOrder) {
        str += " " + bb->getIRName();
    }
    str += "\n";

    // 输出每个基本块的直接支配者
    str += "idom:\n";
    for (auto * bb : rpoOrder) {
        auto it = idomMap.find(bb);
        std::string idomName = (it != idomMap.end() && it->second != nullptr)
                                   ? it->second->getIRName()
                                   : "(none)";
        str += "  " + bb->getIRName() + " -> idom: " + idomName + "\n";
    }

    // 输出支配树结构
    str += "Tree children:\n";
    for (auto * bb : rpoOrder) {
        const auto & children = getDomChildren(bb);
        str += "  " + bb->getIRName() + ":";
        if (children.empty()) {
            str += " (leaf)";
        } else {
            for (auto * child : children) {
                str += " " + child->getIRName();
            }
        }
        str += "\n";
    }

    str += "=== End Dominator Tree ===\n";
}
