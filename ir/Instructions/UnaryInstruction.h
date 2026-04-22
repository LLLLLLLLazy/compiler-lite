///
/// @file UnaryInstruction.h
/// @brief 一元操作指令，如负号和逻辑非
///

#pragma once

#include "Instruction.h"

class UnaryInstruction : public Instruction {

public:
    UnaryInstruction(Function * _func, IRInstOperator _op, Value * _srcVal, Type * _type);

    void toString(std::string & str) override;
};
