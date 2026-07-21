///
/// @file RangeModSimplify.h
/// @brief 基于值域的 2 的幂取模/除法削减 pass
///
/// 对 x % 2^k 与 x / 2^k，若能通过 SCEV 表达式区间求值证明 x 非负
/// （典型来源：规范计数循环归纳变量的仿射式，如 (5*i+1) % 16，i∈[0,64)），
/// 则分别改写为 x & (2^k-1) 与 x >> k，消除带符号取模/除法的偏置指令序列。
///

#pragma once

#include <cstdint>
#include <unordered_map>

#include "ScalarEvolution.h"

class BasicBlock;
class Function;
class Module;

class RangeModSimplify {

public:
    RangeModSimplify(Function * func, Module * mod);

    /// @brief 对函数原地执行基于值域的取模/除法削减
    /// @return 若 IR 被修改则返回 true
    bool run();

private:
    /// @brief 闭区间 [lo, hi]，known 为 false 表示无法确定
    struct Range {
        std::int64_t lo = 0;
        std::int64_t hi = 0;
        bool known = false;
    };

    /// @brief 求值 SCEV 表达式的取值区间
    Range rangeOfExpr(ScalarEvolution & scev, const ScalarEvolution::Expr * expr, int depth);

    /// @brief 取加法递归所属规范循环的迭代次数（含缓存，失败记 -1）
    std::int64_t canonicalTripCount(ScalarEvolution & scev, BasicBlock * loopHeader);

    Function * func = nullptr;
    Module * mod = nullptr;
    /// 循环头 -> 常量迭代数（-1 表示匹配失败），run 期间缓存
    std::unordered_map<BasicBlock *, std::int64_t> tripCountCache;
};
