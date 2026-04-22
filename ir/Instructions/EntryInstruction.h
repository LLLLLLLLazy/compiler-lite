///
/// @file EntryInstruction.h
/// @brief 函数的入口指令
///

#pragma once

#include "Instruction.h"

class EntryInstruction : public Instruction {

public:
    explicit EntryInstruction(Function * _func);

    void toString(std::string & str) override;
};
