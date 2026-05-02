///
/// @file DeadInstElim.h
/// @brief 死指令删除 pass
///
/// 当前实现专注于无副作用 SSA 纯值指令的递归删除，覆盖：
///   1. 识别“无 use 且无副作用”的平凡死指令
///   2. 沿 def-use 链回溯传播新的死指令
///   3. 在基本块中统一清扫已标记的死指令
///

#pragma once

class Function;

class DeadInstElim {

public:
    /// @brief 构造死指令删除器
    explicit DeadInstElim(Function * func);

    /// @brief 对函数原地删除平凡死指令
    /// @return 若本轮删除了至少一条指令则返回 true
    bool run();

private:
    /// @brief 待优化的函数
    Function * func = nullptr;
};