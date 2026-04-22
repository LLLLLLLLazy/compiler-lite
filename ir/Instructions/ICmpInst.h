///
/// @file ICmpInst.h
/// @brief 整数比较指令（icmp eq / ne / slt / sgt / sle / sge）
///

#pragma once

#include "Instruction.h"

class Value;

class ICmpInst final : public Instruction {

public:
    /// @param func 所在函数
    /// @param op   比较操作码（LT_I / GT_I / LE_I / GE_I / EQ_I / NE_I）
    /// @param lhs  左操作数
    /// @param rhs  右操作数
    /// @param type 结果类型（通常为 i1）
    ICmpInst(Function * func, IRInstOperator op, Value * lhs, Value * rhs, Type * type);

    Value * getLHS();
    Value * getRHS();

    void toString(std::string & str) override;

private:
    static const char * predToLLVMName(IRInstOperator op);
};
