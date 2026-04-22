///
/// @file GotoInstruction.cpp
/// @brief 无条件跳转指令即goto指令
///

#include "GotoInstruction.h"

#include "VoidType.h"

GotoInstruction::GotoInstruction(Function * _func, Instruction * _target)
    : Instruction(_func, IRInstOperator::IRINST_OP_GOTO, VoidType::getType())
{
    target = static_cast<LabelInstruction *>(_target);
}

void GotoInstruction::toString(std::string & str)
{
    str = "br label " + target->getIRName();
}

LabelInstruction * GotoInstruction::getTarget() const
{
    return target;
}
