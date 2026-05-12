///
/// @file GVN.h
/// @brief 基于支配树的全局值编号 pass
///

#pragma once

class Function;
class Module;

class GVN {

public:
    GVN(Function * func, Module * mod);

    /// @brief 对函数执行带局部内存别名建模的支配树 GVN
    /// @return 若删除了冗余值计算则返回 true
    bool run();

private:
    Function * func = nullptr;
    Module * mod = nullptr;
};
