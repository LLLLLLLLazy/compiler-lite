///
/// @file DominanceFrontier.h
/// @brief 支配边界（Dominance Frontier）– Phase 5 CFG 分析基础设施
///
/// 使用标准算法（Cytron et al.）计算每个基本块的支配边界集合：
///
///   For each join-point b (|preds(b)| >= 2):
///     For each predecessor p of b:
///       runner = p
///       while runner != idom(b):
///         DF[runner] += {b}
///         runner = idom(runner)
///

#pragma once

#include <set>
#include <string>
#include <unordered_map>

class BasicBlock;
class DominatorTree;
class Function;

class DominanceFrontier {

public:
    /// 构造时立即计算所有基本块的支配边界。
    explicit DominanceFrontier(Function * func, const DominatorTree & dt);

    /// 返回基本块 bb 的支配边界集合（只读）。
    const std::set<BasicBlock *> & getFrontier(BasicBlock * bb) const;

    /// 将每个块的支配边界打印到 str（调试用）。
    void print(std::string & str) const;

private:
    void compute(Function * func, const DominatorTree & dt);

    std::unordered_map<BasicBlock *, std::set<BasicBlock *>> frontierMap;
    static const std::set<BasicBlock *> emptySet;
};
