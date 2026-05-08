///
/// @file DeadInstElim.h
/// @brief 死指令删除 pass
///
/// Mark-Sweep，覆盖：
///   1. 从副作用、返回和无条件跳转出发标记活指令
///   2. 沿 def-use 链追踪活值依赖
///   3. 基于 CFG 后支配关系保留真正控制活代码的条件跳转
///   4. 将死条件跳转改写为无条件跳转，并清扫剩余死指令
///

#pragma once

class Function;

class DeadInstElim {

public:
    /// @brief 构造死指令删除器
    explicit DeadInstElim(Function * func);

    /// @brief 对函数原地执行 CFG 感知的死代码删除
    /// @return 若本轮删除了至少一条指令则返回 true
    bool run();

private:
    /// @brief 待优化的函数
    Function * func = nullptr;
};