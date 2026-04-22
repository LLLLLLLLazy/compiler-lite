///
/// @file CondGotoInstruction.h
/// @brief 条件跳转指令
///

#pragma once

#include "Instruction.h"
#include "LabelInstruction.h"

class CondGotoInstruction final : public Instruction {

public:
    CondGotoInstruction(Function * _func, Value * _cond, Instruction * _trueTarget, Instruction * _falseTarget);

    void toString(std::string & str) override;

    [[nodiscard]] LabelInstruction * getTrueTarget() const;

    [[nodiscard]] LabelInstruction * getFalseTarget() const;

private:
    LabelInstruction * trueTarget = nullptr;
    LabelInstruction * falseTarget = nullptr;
};
