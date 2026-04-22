///
/// @file BinaryInst.h
/// @brief 二元算术指令（add / sub / mul / div / mod）
///
/// 使用已有的 IRINST_OP_ADD_I 等操作码；与 ICmpInst 区分。
///

#pragma once

#include "Instruction.h"

class Value;

class BinaryInst final : public Instruction {

public:
    /// @param func 所在函数
    /// @param op   操作码（ADD_I / SUB_I / MUL_I / DIV_I / MOD_I）
    /// @param lhs  左操作数
    /// @param rhs  右操作数
    /// @param type 结果类型
    BinaryInst(Function * func, IRInstOperator op, Value * lhs, Value * rhs, Type * type);

    Value * getLHS();
    Value * getRHS();

    void toString(std::string & str) override;

private:
    static const char * opToLLVMName(IRInstOperator op);
};
