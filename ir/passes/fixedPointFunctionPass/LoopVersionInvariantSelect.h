///
/// @file LoopVersionInvariantSelect.h
/// @brief 循环不变 select 条件版本化 pass
///
/// 将循环体内条件为循环不变的 select 从循环中剥离：克隆循环为「条件为真 /
/// 条件为假」两个版本，各自把 select 替换为对应分支操作数，在 preheader
/// 用该不变条件选路。与 LSR 版本化共用同一套克隆-重映射-出口 phi 修复机制，
/// 但不要求规范旋转计数循环形态：任意带唯一 preheader 的可克隆循环均可版本化。
///

#pragma once

#include <cstdint>
#include <unordered_set>

class BasicBlock;
class Function;
class Module;

class LoopVersionInvariantSelect {

public:
    LoopVersionInvariantSelect(Function * func, Module * mod);

    /// @brief 对函数内所有可收益的循环做不变 select 条件版本化
    /// @return 若修改了 IR 则返回 true
    bool run();

private:
    /// @brief 尝试对 header 循环内条件为 cond 的一组 select 做版本化
    /// @return 版本化成功返回 true
    bool tryVersionLoop(BasicBlock * header,
                        const std::unordered_set<BasicBlock *> & loopBody,
                        class Value * cond);

    Function * func;
    Module * mod;
    /// @brief 本轮 pass 内已版本化过的循环头（含克隆），防止重复克隆
    std::unordered_set<BasicBlock *> versionedHeaders;
};
