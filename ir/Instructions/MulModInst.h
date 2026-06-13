///
/// @file MulModInst.h
/// @brief 宽乘取模指令 (i64)a * (i64)b % m
///
/// 表示对两个 i32 操作数做 64 位无截断乘法后，再对编译期正常量 m 取模，
/// 结果回落到 i32。用于把"递归倍加模乘"惯用法折叠成 O(1) 的单条宽乘加取模，
/// 避免 32 位乘法溢出。语义上 a、b 均按有符号扩展到 64 位参与运算
///

#pragma once

#include <cstdint>

#include "Instruction.h"

class Value;
class Function;

class MulModInst final : public Instruction {

public:
    /// @brief 构造宽乘取模指令
    /// @param func 所在函数
    /// @param a    被乘数（i32）
    /// @param b    乘数（i32）
    /// @param m    取模的正常量（编译期已知）
    MulModInst(Function * func, Value * a, Value * b, int32_t m);

    /// @brief 获取被乘数
    Value * getA();

    /// @brief 获取乘数
    Value * getB();

    /// @brief 获取模数常量
    [[nodiscard]] int32_t getModulus() const
    {
        return modulus;
    }

    /// @brief 序列化为等价的 LLVM IR 文本（sext/mul/srem/trunc 展开）
    void toString(std::string & str) override;

private:
    int32_t modulus = 0;
};
