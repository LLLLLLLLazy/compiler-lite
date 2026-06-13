///
/// @file DeadFunctionElim.h
/// @brief 死函数消除 pass
///
/// 删除模块中从 main 出发不可达的用户定义函数。
/// 内联展开后，原本只被单点调用的初始化型函数会变成无调用者的死函数，
/// 但它们仍引用全局标量，阻止 GlobalToLocal 将这些全局内化到 main。
/// 本 pass 在内联之后、GlobalToLocal 之前清除这些死函数，
/// 使其引用的全局 use 链同步释放
///

#pragma once

#include <unordered_set>

class Function;
class Module;

class DeadFunctionElim {

public:
    /// @brief 构造死函数消除 pass
    /// @param module 待处理模块
    explicit DeadFunctionElim(Module * module);

    /// @brief 删除从 main 不可达的用户函数
    /// @return true 表示移除了至少一个函数
    bool run();

private:
    /// @brief 从入口函数出发标记所有可达的用户函数
    /// @param entry 入口函数
    /// @param reachable 输出可达函数集合
    void markReachable(Function * entry, std::unordered_set<Function *> & reachable) const;

    Module * module = nullptr;
};
