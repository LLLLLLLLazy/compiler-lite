///
/// @file Instruction.cpp
/// @brief IR指令类实现
///

#include "Instruction.h"

#include "Function.h"

Instruction::Instruction(Function * _func, IRInstOperator _op, Type * _type) : User(_type), op(_op), func(_func)
{}

IRInstOperator Instruction::getOp()
{
    return op;
}

void Instruction::toString(std::string & str)
{
    str = "Unkown IR Instruction";
}

bool Instruction::isDead()
{
    return dead;
}

void Instruction::setDead(bool _dead)
{
    dead = _dead;
}

Function * Instruction::getFunction()
{
    return func;
}

bool Instruction::hasResultValue()
{
    return !type->isVoidType();
}

bool Instruction::isTerminator() const
{
    return op == IRInstOperator::IRINST_OP_BR || op == IRInstOperator::IRINST_OP_COND_BR ||
           op == IRInstOperator::IRINST_OP_RET;
}
