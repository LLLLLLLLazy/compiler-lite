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
// Step 1: Reverse Post Order (iterative DFS)
// ---------------------------------------------------------------------------
void DominatorTree::computeRPO(Function * func)
{
    BasicBlock * entry = func->getEntryBlock();
    if (entry == nullptr) {
        return;
    }

    std::unordered_map<BasicBlock *, bool> visited;
    std::vector<BasicBlock *> postOrder;

    // Iterative post-order DFS using an explicit stack of (block, iterator).
    // We need to process all successors before recording the node.
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

    // RPO = reverse of post-order
    rpoOrder.reserve(postOrder.size());
    for (int i = (int) postOrder.size() - 1; i >= 0; --i) {
        rpoOrder.push_back(postOrder[i]);
    }

    for (int i = 0; i < (int) rpoOrder.size(); ++i) {
        rpoNum[rpoOrder[i]] = i;
    }
}

// ---------------------------------------------------------------------------
// Step 2: Compute immediate dominators (Cooper et al.)
// ---------------------------------------------------------------------------
void DominatorTree::computeIDom()
{
    if (rpoOrder.empty()) {
        return;
    }

    BasicBlock * entry = rpoOrder[0];

    // Initialize: idom[entry] = entry, all others = nullptr (undefined)
    idomMap[entry] = entry;
    for (std::size_t i = 1; i < rpoOrder.size(); ++i) {
        idomMap[rpoOrder[i]] = nullptr;
    }

    bool changed = true;
    while (changed) {
        changed = false;

        // Iterate in RPO order, skipping entry
        for (std::size_t i = 1; i < rpoOrder.size(); ++i) {
            BasicBlock * bb = rpoOrder[i];
            BasicBlock * newIdom = nullptr;

            for (BasicBlock * pred : bb->getPredecessors()) {
                // Only consider predecessors that have been processed
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
// Step 3: Build dominator-tree children map
// ---------------------------------------------------------------------------
void DominatorTree::buildDomChildren()
{
    if (rpoOrder.empty()) {
        return;
    }

    BasicBlock * entry = rpoOrder[0];
    // Ensure entry has an entry in domChildrenMap (even if empty)
    domChildrenMap[entry];

    for (std::size_t i = 1; i < rpoOrder.size(); ++i) {
        BasicBlock * bb = rpoOrder[i];
        BasicBlock * parent = idomMap[bb];
        if (parent != nullptr) {
            domChildrenMap[parent].push_back(bb);
        }
        // Ensure bb itself has an entry
        domChildrenMap[bb];
    }
}

// ---------------------------------------------------------------------------
// Step 4: DFS timestamps on dominator tree for O(1) dominates()
// ---------------------------------------------------------------------------
void DominatorTree::computeDFSTimestamps()
{
    if (rpoOrder.empty()) {
        return;
    }

    int timer = 0;
    dfsTimestamp(rpoOrder[0], timer);
}

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
// intersect: LCA on the dominator tree via RPO numbers
// ---------------------------------------------------------------------------
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
// Public query methods
// ---------------------------------------------------------------------------
BasicBlock * DominatorTree::getIDom(BasicBlock * bb) const
{
    auto it = idomMap.find(bb);
    if (it == idomMap.end()) {
        return nullptr;
    }
    return it->second;
}

const std::vector<BasicBlock *> & DominatorTree::getDomChildren(BasicBlock * bb) const
{
    auto it = domChildrenMap.find(bb);
    if (it == domChildrenMap.end()) {
        return emptyVec;
    }
    return it->second;
}

int DominatorTree::getRPONumber(BasicBlock * bb) const
{
    auto it = rpoNum.find(bb);
    if (it == rpoNum.end()) {
        return -1;
    }
    return it->second;
}

bool DominatorTree::dominates(BasicBlock * a, BasicBlock * b) const
{
    auto itA = dfIn.find(a);
    auto itB = dfIn.find(b);
    if (itA == dfIn.end() || itB == dfIn.end()) {
        return false;
    }
    // a dominates b iff a's DFS interval contains b's DFS interval
    return dfIn.at(a) <= dfIn.at(b) && dfOut.at(b) <= dfOut.at(a);
}

bool DominatorTree::strictlyDominates(BasicBlock * a, BasicBlock * b) const
{
    return a != b && dominates(a, b);
}

// ---------------------------------------------------------------------------
// Debug print
// ---------------------------------------------------------------------------
void DominatorTree::print(std::string & str) const
{
    str += "=== Dominator Tree ===\n";

    if (rpoOrder.empty()) {
        str += "  (empty)\n";
        return;
    }

    // Print RPO order
    str += "RPO order:";
    for (auto * bb : rpoOrder) {
        str += " " + bb->getIRName();
    }
    str += "\n";

    // Print idom for each block
    str += "idom:\n";
    for (auto * bb : rpoOrder) {
        auto it = idomMap.find(bb);
        std::string idomName = (it != idomMap.end() && it->second != nullptr)
                                   ? it->second->getIRName()
                                   : "(none)";
        str += "  " + bb->getIRName() + " -> idom: " + idomName + "\n";
    }

    // Print tree structure (children)
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
