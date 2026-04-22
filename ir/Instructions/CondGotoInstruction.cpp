///
/// @file CondGotoInstruction.cpp
/// @brief 条件跳转指令
///

#include "CondGotoInstruction.h"

#include "VoidType.h"

CondGotoInstruction::CondGotoInstruction(
    Function * _func, Value * _cond, Instruction * _trueTarget, Instruction * _falseTarget)
    : Instruction(_func, IRInstOperator::IRINST_OP_COND_GOTO, VoidType::getType())
{
    addOperand(_cond);
    trueTarget = static_cast<LabelInstruction *>(_trueTarget);
    falseTarget = static_cast<LabelInstruction *>(_falseTarget);
}

void CondGotoInstruction::toString(std::string & str)
{
    Value * cond = getOperand(0);
    str = "br " + cond->getIRName() + ", label " + trueTarget->getIRName() + ", label " + falseTarget->getIRName();
}

LabelInstruction * CondGotoInstruction::getTrueTarget() const
{
    return trueTarget;
}

LabelInstruction * CondGotoInstruction::getFalseTarget() const
{
    return falseTarget;
}
