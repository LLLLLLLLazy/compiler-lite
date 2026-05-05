///
/// @file ArrayScalarize.h
/// @brief 局部数组标量化 pass
///

#pragma once

class Function;

class ArrayScalarize {

public:
    /// @brief 构造数组标量化 pass
    /// @param func 待优化函数
    explicit ArrayScalarize(Function * func);

    /// @brief 将局部数组的常量下标元素拆成独立标量槽位
    /// @return true 表示 IR 发生变化
    bool run();

private:
    Function * func = nullptr;
};