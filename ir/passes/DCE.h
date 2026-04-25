///
/// @file DCE.h
/// @brief 死代码消除 pass
///
/// 当前实现包含两部分：
///   1. 删除从入口基本块不可达的基本块，并同步清理 CFG 与 phi incoming
///   2. 对无副作用且结果未被使用的纯指令做保守 dead instruction elimination
///

#pragma once

class Function;

class DCE {

public:
    /// @brief 构造 DCE 优化器
    explicit DCE(Function * func);

    /// @brief 对函数原地执行 DCE
    void run();

private:
    Function * func = nullptr;
};