///
/// @file SelectInst.h
/// @brief select 指令
///
/// SelectInst 表示基于 i1 条件值在两个候选值之间进行选择：
///   %result = select i1 %cond, <ty> %trueVal, <ty> %falseVal
///

#pragma once

#include "Instruction.h"

class Value;

class SelectInst final : public Instruction {

public:
    /// @param func 所在函数
    /// @param cond 条件值，语义上应为 i1
    /// @param trueValue 条件为真时选择的值
    /// @param falseValue 条件为假时选择的值
    /// @param resultType 结果类型，应与 true/false value 类型一致
    SelectInst(Function * func, Value * cond, Value * trueValue, Value * falseValue, Type * resultType);

    /// @brief 获取条件值
    [[nodiscard]] Value * getCondition() const;

    /// @brief 获取 true 分支值
    [[nodiscard]] Value * getTrueValue() const;

    /// @brief 获取 false 分支值
    [[nodiscard]] Value * getFalseValue() const;

    /// @brief 将 select 指令格式化为 IR 文本
    /// @param str 输出字符串
    void toString(std::string & str) override;
};