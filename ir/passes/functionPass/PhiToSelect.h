///
/// @file PhiToSelect.h
/// @brief 将简单分支合流处的 phi 转换为 select
///
/// 该 pass 仅处理安全的简单 if-then / if-then-else 形态：
///   - phi 恰好有两个 incoming
///   - CFG 形状可直接由单个 cond_br 描述
///   - cond 与两个候选值都在 merge 块处可直接使用
///

#pragma once

class Function;

class PhiToSelect {

public:
    /// @brief 构造一个函数级 phi-to-select 转换器
    explicit PhiToSelect(Function * func);

    /// @brief 执行转换
    /// @return 若至少有一个 phi 被替换为 select 则返回 true
    bool run();

private:
    Function * func = nullptr;
};