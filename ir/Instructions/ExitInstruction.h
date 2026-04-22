///
/// @file ExitInstruction.h
/// @brief 函数出口或返回指令
///

#pragma once

#include "Instruction.h"

class ExitInstruction : public Instruction {

public:
    ExitInstruction(Function * _func, Value * result = nullptr);

    void toString(std::string & str) override;
};
