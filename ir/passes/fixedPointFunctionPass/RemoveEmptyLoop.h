///
/// @file RemoveEmptyLoop.h
/// @brief 空循环消除 pass
///
/// 当一个自然循环的循环体仅包含无副作用指令，且循环内定义的所有值
/// 都不在循环外被使用时，整个循环对程序的可观察行为没有任何影响，
/// 可以被安全地整体删除。该 pass 通常配合 SCEV 出口值闭式替换使用：
/// 后者把循环出口处对归纳变量的依赖替换为闭式表达式，使循环体彻底
/// 失去对外作用，从而被本 pass 清除。
///

#pragma once

#include <unordered_set>

class BasicBlock;
class Function;
class Module;

class RemoveEmptyLoop {

public:
    /// @brief 构造空循环消除 pass
    /// @param func 待优化函数
    /// @param mod 所属模块
    RemoveEmptyLoop(Function * func, Module * mod);

    /// @brief 删除所有无副作用且出口无依赖的自然循环
    /// @return true 表示 IR 发生变化
    bool run();

private:
    /// @brief 尝试删除以 header 为头的循环
    /// @param header 循环头基本块
    /// @return true 表示成功删除该循环
    bool tryRemoveLoop(BasicBlock * header);

    /// @brief 判断循环体是否无副作用且出口无依赖
    /// @param header 循环头
    /// @param loopBody 循环体基本块集合
    /// @param exit 唯一出口块
    /// @return true 表示可安全删除
    bool isRemovableLoop(BasicBlock * header,
                         const std::unordered_set<BasicBlock *> & loopBody,
                         BasicBlock * exit) const;

    Function * func = nullptr;
    Module * mod = nullptr;
};
