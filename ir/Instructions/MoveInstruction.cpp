///
/// @file MoveInstruction.cpp
/// @brief 结构化IR中的赋值/拷贝指令
///

#include "MoveInstruction.h"

#include "VoidType.h"

MoveInstruction::MoveInstruction(Function * _func, Value * _result, Value * _srcVal1)
    : Instruction(_func, IRInstOperator::IRINST_OP_ASSIGN, VoidType::getType())
{
    addOperand(_result);
    addOperand(_srcVal1);
}

void MoveInstruction::toString(std::string & str)
{
    Value * dstVal = getOperand(0);
    Value * srcVal = getOperand(1);
    str = dstVal->getIRName() + " = " + srcVal->getIRName();
}
