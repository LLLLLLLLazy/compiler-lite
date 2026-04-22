///
/// @file BinaryInstruction.h
/// @brief 二元操作指令，如加和减
///

#pragma once

#include "Instruction.h"

class BinaryInstruction : public Instruction {

public:
    BinaryInstruction(Function * _func, IRInstOperator _op, Value * _srcVal1, Value * _srcVal2, Type * _type);

    void toString(std::string & str) override;
};
