///
/// @file GotoInstruction.h
/// @brief 无条件跳转指令即goto指令
///

#pragma once

#include "Instruction.h"
#include "LabelInstruction.h"

class GotoInstruction final : public Instruction {

public:
    GotoInstruction(Function * _func, Instruction * _target);

    void toString(std::string & str) override;

    [[nodiscard]] LabelInstruction * getTarget() const;

private:
    LabelInstruction * target = nullptr;
};
