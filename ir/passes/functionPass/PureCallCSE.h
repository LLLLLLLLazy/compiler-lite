///
/// @file PureCallCSE.h
/// @brief 轻量纯函数调用公共子表达式消除 pass
///

#pragma once

class Function;
class Module;

class PureCallCSE {

public:
    PureCallCSE(Function * func, Module * mod);

    /// @brief 对函数内同一基本块的重复纯调用做 CSE
    /// @return 若删除了至少一个重复调用则返回 true
    bool run();

private:
    Function * func = nullptr;
    Module * mod = nullptr;
};
