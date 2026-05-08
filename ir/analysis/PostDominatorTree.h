///
/// @file PostDominatorTree.h
/// @brief 后支配树分析
///
/// 基于函数 CFG 的反图和虚拟退出块构造轻量后支配树
/// 提供：
///   - getIPDom 获取直接后支配者
///   - postDominates O(1) 后支配判断
///

#pragma once

#include <unordered_map>
#include <vector>

class BasicBlock;
class Function;

class PostDominatorTree {

public:
    /// @brief 构造时立即完成指定函数的后支配树计算
    explicit PostDominatorTree(Function * func);

    /// @brief 判断 postDom 是否后支配 bb
    /// @param postDom 候选后支配块
    /// @param bb 被判断的基本块
    /// @return true 表示 postDom 后支配 bb
    bool postDominates(BasicBlock * postDom, BasicBlock * bb) const;

    /// @brief 获取基本块的直接后支配者
    /// @param bb 目标基本块
    /// @return 直接后支配块，不存在或为虚拟根时返回 nullptr
    BasicBlock * getIPDom(BasicBlock * bb) const;

private:
    /// @brief 获取基本块的内部编号
    /// @param bb 目标基本块
    /// @return 对应编号，不存在时返回 -1
    int getBlockIndex(BasicBlock * bb) const;

    /// @brief 根据编号获取真实基本块
    /// @param index 基本块编号
    /// @return 对应基本块，虚拟根或非法编号返回 nullptr
    BasicBlock * getBlockByIndex(int index) const;

    /// @brief 计算两点在后支配树上的最近公共祖先
    /// @param lhs 第一个节点编号
    /// @param rhs 第二个节点编号
    /// @return 最近公共祖先编号
    int intersect(int lhs, int rhs) const;

    /// @brief 从函数 CFG 构造后支配树
    /// @param func 待分析的函数
    void build(Function * func);

    /// @brief 计算逆图上的逆后序编号
    /// @param totalNodes 总节点数量，含虚拟根
    void computeRPO(int totalNodes);

    /// @brief 计算每个节点的直接后支配者
    void computeIDom();

    /// @brief 构建后支配树孩子列表
    /// @param totalNodes 总节点数量，含虚拟根
    void buildChildren(int totalNodes);

    /// @brief 为后支配树打 DFS 时间戳
    /// @param totalNodes 总节点数量，含虚拟根
    void computeDFSTimestamps(int totalNodes);

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