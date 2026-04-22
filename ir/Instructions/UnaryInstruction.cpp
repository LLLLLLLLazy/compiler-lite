///
/// @file UnaryInstruction.cpp
/// @brief 一元操作指令
///

#include "UnaryInstruction.h"

UnaryInstruction::UnaryInstruction(Function * _func, IRInstOperator _op, Value * _srcVal, Type * _type)
    : Instruction(_func, _op, _type)
{
    addOperand(_srcVal);
}

void UnaryInstruction::toString(std::string & str)
{
    Value * src = getOperand(0);

    switch (op) {
        case IRInstOperator::IRINST_OP_NEG_I:
            str = getIRName() + " = neg " + src->getIRName();
            break;
        case IRInstOperator::IRINST_OP_NOT_I:
            str = getIRName() + " = not " + src->getIRName();
            break;
        default:
            Instruction::toString(str);
            break;
    }
}
