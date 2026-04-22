///
/// @file MoveInstruction.h
/// @brief 结构化IR中的赋值/拷贝指令
///

#pragma once

#include "Instruction.h"

class MoveInstruction : public Instruction {

public:
    MoveInstruction(Function * _func, Value * result, Value * srcVal1);

    void toString(std::string & str) override;
};
