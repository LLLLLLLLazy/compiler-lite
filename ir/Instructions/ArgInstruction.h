///
/// @file ArgInstruction.h
/// @brief 函数实参ARG指令
///

#pragma once

#include "Instruction.h"

class ArgInstruction : public Instruction {

public:
    ArgInstruction(Function * _func, Value * src);

    void toString(std::string & str) override;
};
