///
/// @file LabelInstruction.cpp
/// @brief Label指令
///

#include "LabelInstruction.h"

#include "VoidType.h"

LabelInstruction::LabelInstruction(Function * _func)
    : Instruction(_func, IRInstOperator::IRINST_OP_LABEL, VoidType::getType())
{}

void LabelInstruction::toString(std::string & str)
{
    str = IRName + ":";
}
