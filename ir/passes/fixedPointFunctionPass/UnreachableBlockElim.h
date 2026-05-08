///
/// @file UnreachableBlockElim.h
/// @brief 不可达基本块删除 pass
///
/// 当前实现专注于 entry 可达性驱动的死块删除，覆盖：
///   1. 从入口基本块遍历 CFG，识别可达块集合
///   2. 删除不可达基本块，并同步修剪前驱/后继关系
///   3. 清理幸存块中对应的 phi incoming
///

#pragma once

class Function;

class UnreachableBlockElim {

public:
    /// @brief 构造不可达基本块删除器
    explicit UnreachableBlockElim(Function * func);

    /// @brief 对函数原地删除从入口不可达的基本块
    /// @return 若本轮删除了至少一个基本块则返回 true
    bool run();

private:
    /// @brief 待优化的函数
    Function * func = nullptr;
};