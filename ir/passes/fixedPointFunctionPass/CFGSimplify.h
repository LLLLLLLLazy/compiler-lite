///
/// @file CFGSimplify.h
/// @brief 控制流图化简 pass
///
/// 当前实现覆盖四类局部 CFG 化简：
///   1. 将 true/false 指向同一目标的条件跳转折叠为无条件跳转
///   2. 删除仅包含无条件跳转的空块，并将其前驱直接重定向到后继
///   3. 合并 `br -> 单前驱后继块` 这种可直接拼接的块对
///   4. 将跳向空条件块的无条件分支直接线程化到条件块的后继
///
/// 所有变换都会同步维护 phi incoming 以及基本块的前驱/后继关系
///

#pragma once

class BasicBlock;
class Function;

class CFGSimplify {

public:
    /// @brief 构造 CFG 化简器
    explicit CFGSimplify(Function * func);

    /// @brief 对函数原地执行 CFG 化简
    /// @return 若本轮对 IR 做了修改则返回 true
    bool run();

private:
    /// @brief 折叠 true/false 指向同一后继的条件跳转
    /// @param bb 待检查的基本块
    /// @return 若成功折叠则返回 true
    bool tryFoldRedundantCondBranch(BasicBlock * bb);

    /// @brief 删除仅包含无条件跳转的空块
    /// @param bb 待检查的基本块
    /// @return 若成功旁路并删除该块则返回 true
    bool tryBypassEmptyBlock(BasicBlock * bb);

    /// @brief 尝试合并 `bb -> succ` 的无条件跳转块对
    /// @param bb 待检查的前驱块
    /// @return 若成功合并则返回 true
    bool tryMergeBranchSuccessor(BasicBlock * bb);

    /// @brief 将跳向空条件块的无条件分支线程化到其两个后继
    /// @param bb 待检查的前驱块
    /// @return 若成功线程化则返回 true
    bool tryThreadThroughEmptyCondBlock(BasicBlock * bb);

    /// @brief 待优化的函数
    Function * func = nullptr;
};
