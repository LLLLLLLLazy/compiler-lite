///
/// @file LoopRotate.h
/// @brief 循环旋转 pass
///
/// 将规范计数循环从 header-test 形态旋转为 latch-test 形态：
///   1. 在 preheader 处理首轮进入判定
///   2. 将 header 条件跳转改写为无条件跳转到循环体
///   3. 将回边 latch 改写为新的退出判定点
///

#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Instruction.h"
#include "ScalarEvolution.h"

class BasicBlock;
class Function;
class Module;
class Value;

class LoopRotate {

public:
    /// @brief 构造循环旋转 pass
    /// @param func 待处理函数
    explicit LoopRotate(Function * func, Module * mod = nullptr);

    /// @brief 对函数原地执行循环旋转
    /// @return 若有循环被成功旋转则返回 true
    bool run();

private:
    bool tryRotateHeader(BasicBlock * header, class LoopInfo & loopInfo, ScalarEvolution & scev) const;
    std::vector<BasicBlock *> collectLoopExitBlocks(const std::unordered_set<BasicBlock *> & loopBody) const;
    std::vector<BasicBlock *> collectInsidePredecessors(
        BasicBlock * bb,
        const std::unordered_set<BasicBlock *> & loopBody) const;
    bool hasOnlySupportedOutsideUses(BasicBlock * header,
                                     BasicBlock * exit,
                                     const std::unordered_set<BasicBlock *> & loopBody) const;
    bool hasSingleBranchTo(BasicBlock * bb, BasicBlock * target) const;
    bool replaceBranchWithCondBranch(BasicBlock * bb,
                                     Value * condition,
                                     BasicBlock * trueDest,
                                     BasicBlock * falseDest) const;
    bool replaceCondBranchWithBranch(BasicBlock * bb, BasicBlock * target) const;
    static IRInstOperator getCompareOp(ScalarEvolution::CompareKind compareKind);

    Function * func = nullptr;
};