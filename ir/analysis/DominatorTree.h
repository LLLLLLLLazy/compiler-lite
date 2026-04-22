///
/// @file DominatorTree.h
/// @brief 支配树（Dominator Tree）
///
/// 实现 Cooper et al. (2001) "A Simple, Fast Dominance Algorithm"。
/// 提供：
///   - 逆后序（RPO）遍历
///   - 直接支配者（idom）计算
///   - 支配树子结点查询
///   - dominates() / strictlyDominates() O(1) 判断（基于 DFS 时间戳）
///

#pragma once

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class BasicBlock;
class Function;

class DominatorTree {

public:
    /// 构造时立即对给定函数完成 RPO + idom + 时间戳计算。
    explicit DominatorTree(Function * func);

    // ----- 查询接口 -----

    /// 返回 bb 的直接支配者（entry 的 idom 为自身）。
    BasicBlock * getIDom(BasicBlock * bb) const;

    /// 返回支配树中 bb 的直接子节点列表。
    const std::vector<BasicBlock *> & getDomChildren(BasicBlock * bb) const;

    /// 返回 bb 在 RPO 中的编号（entry = 0）。
    int getRPONumber(BasicBlock * bb) const;

    /// 返回函数内所有基本块的 RPO 有序列表（entry 在最前）。
    const std::vector<BasicBlock *> & getRPO() const
    {
        return rpoOrder;
    }

    /// a 支配 b（a == b 也算）。
    bool dominates(BasicBlock * a, BasicBlock * b) const;

    /// a 严格支配 b（a != b 且 a 支配 b）。
    bool strictlyDominates(BasicBlock * a, BasicBlock * b) const;

    /// 将支配树信息打印到 str（调试用）。
    void print(std::string & str) const;

private:
    void computeRPO(Function * func);
    void computeIDom();
    void buildDomChildren();
    void computeDFSTimestamps();
    void dfsTimestamp(BasicBlock * bb, int & timer);

    /// Cooper intersect：找到 b1 和 b2 在支配树上的最近公共祖先。
    BasicBlock * intersect(BasicBlock * b1, BasicBlock * b2) const;

    std::vector<BasicBlock *> rpoOrder;                           ///< RPO 有序列表
    std::unordered_map<BasicBlock *, int> rpoNum;                 ///< 块 -> RPO 编号
    std::unordered_map<BasicBlock *, BasicBlock *> idomMap;       ///< 块 -> 直接支配者
    std::unordered_map<BasicBlock *, std::vector<BasicBlock *>> domChildrenMap; ///< 支配树子节点

    /// DFS 进入/离开时间戳（用于 O(1) dominates() 判断）
    std::unordered_map<BasicBlock *, int> dfIn;
    std::unordered_map<BasicBlock *, int> dfOut;

    static const std::vector<BasicBlock *> emptyVec;
};