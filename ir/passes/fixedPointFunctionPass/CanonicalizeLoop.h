///
/// @file CanonicalizeLoop.h
/// @brief 循环规范化 pass
///
/// 对自然循环保守地建立后续循环优化所需的结构性质：
///   1. 唯一 preheader
///   2. 唯一 latch
///   3. dedicated exits
///

#pragma once

#include <unordered_set>
#include <vector>

class BasicBlock;
class Function;
class Module;

class CanonicalizeLoop {

public:
    /// @brief 构造循环规范化 pass
    /// @param func 待处理函数
    explicit CanonicalizeLoop(Function * func, Module * mod = nullptr);

    /// @brief 对函数原地执行循环规范化
    /// @return 若 CFG 或 phi 结构被修改则返回 true
    bool run();

private:
    struct PhiPlan {
        class PhiInst * phi = nullptr;
        std::vector<class Value *> values;
    };

    std::vector<BasicBlock *> collectOutsidePredecessors(
        BasicBlock * header,
        const std::unordered_set<BasicBlock *> & loopBody) const;
    std::vector<BasicBlock *> collectLoopLatches(
        BasicBlock * header,
        const std::unordered_set<BasicBlock *> & loopBody) const;
    std::vector<BasicBlock *> collectLoopExitBlocks(const std::unordered_set<BasicBlock *> & loopBody) const;
    std::vector<BasicBlock *> collectInsidePredecessors(
        BasicBlock * bb,
        const std::unordered_set<BasicBlock *> & loopBody) const;
    std::vector<BasicBlock *> collectOutsidePredecessorsForExit(
        BasicBlock * bb,
        const std::unordered_set<BasicBlock *> & loopBody) const;
    BasicBlock * getExistingPreheader(const std::vector<BasicBlock *> & outsidePreds) const;
    bool canonicalizeLoop(BasicBlock * header, const std::unordered_set<BasicBlock *> & loopBody);
    bool ensurePreheader(BasicBlock * header, const std::unordered_set<BasicBlock *> & loopBody);
    bool ensureSingleLatch(BasicBlock * header, const std::unordered_set<BasicBlock *> & loopBody);
    bool ensureDedicatedExits(const std::unordered_set<BasicBlock *> & loopBody);
    bool splitDedicatedExit(BasicBlock * exitBlock,
                            const std::vector<BasicBlock *> & insidePreds,
                            const std::vector<BasicBlock *> & outsidePreds);
    bool rewriteTerminatorTarget(BasicBlock * pred, BasicBlock * oldTarget, BasicBlock * newTarget) const;
    void insertBlockBefore(BasicBlock * bb, BasicBlock * before) const;

    Function * func = nullptr;
};