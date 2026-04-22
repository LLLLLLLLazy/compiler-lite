///
/// @file LabelInstruction.h
/// @brief Label指令
///

#pragma once

#include "Instruction.h"

class LabelInstruction : public Instruction {

public:
    explicit LabelInstruction(Function * _func);

    void toString(std::string & str) override;
};
