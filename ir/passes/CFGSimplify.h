///
/// @file CFGSimplify.h
/// @brief 控制流图化简 pass
///
/// 当前实现专注于结构安全的局部 CFG 化简，覆盖：
///   1. 识别 `br -> 单前驱后继块` 这种可直接拼接的块对
///   2. 在块合并前折叠 successor 中只依赖唯一前驱的 phi 指令
///   3. 重写 successor 的后继块 phi incoming，并维护 CFG 前驱/后继关系
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
    /// @brief 尝试合并 `bb -> succ` 的无条件跳转块对
    /// @param bb 待检查的前驱块
    /// @return 若成功合并则返回 true
    bool tryMergeBranchSuccessor(BasicBlock * bb);

    /// @brief 待优化的函数
    Function * func = nullptr;
};