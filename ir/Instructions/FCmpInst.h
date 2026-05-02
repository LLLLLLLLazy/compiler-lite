///
/// @file FCmpInst.h
/// @brief 浮点比较指令（fcmp oeq / one / olt / ogt / ole / oge）
///

#pragma once

#include "Instruction.h"

class Value;

class FCmpInst final : public Instruction {

public:
    /// @param func 所在函数
    /// @param op   比较操作码（LT_F / GT_F / LE_F / GE_F / EQ_F / NE_F）
    /// @param lhs  左操作数
    /// @param rhs  右操作数
    /// @param type 结果类型（通常为 i1）
    FCmpInst(Function * func, IRInstOperator op, Value * lhs, Value * rhs, Type * type);

    Value * getLHS();
    Value * getRHS();

    void toString(std::string & str) override;

private:
    static const char * predToLLVMName(IRInstOperator op);
};