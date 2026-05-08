///
/// @file TailRecursionElim.h
/// @brief 尾递归消除优化 pass。
///
/// 将函数末尾的自身调用改写为参数 phi 循环，
/// 消除递归调用开销，避免深递归导致的栈溢出。
///

#pragma once

class Function;

class TailRecursionElim {

public:
    explicit TailRecursionElim(Function * func);

    /// @brief 将尾自调用改写为跳回参数 phi 循环头的循环
    /// @return true 表示函数被修改
    bool run();

private:
    Function * func = nullptr;
};
