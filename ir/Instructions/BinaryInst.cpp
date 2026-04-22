///
/// @file BinaryInst.cpp
/// @brief 二元算术指令实现
///

#include "BinaryInst.h"

#include "Function.h"

BinaryInst::BinaryInst(Function * func, IRInstOperator _op, Value * lhs, Value * rhs, Type * _type)
    : Instruction(func, _op, _type)
{
    addOperand(lhs);
    addOperand(rhs);
}

Value * BinaryInst::getLHS()
{
    return getOperand(0);
}

Value * BinaryInst::getRHS()
{
    return getOperand(1);
}

const char * BinaryInst::opToLLVMName(IRInstOperator op)
{
    switch (op) {
        case IRInstOperator::IRINST_OP_ADD_I:
            return "add";
        case IRInstOperator::IRINST_OP_SUB_I:
            return "sub";
        case IRInstOperator::IRINST_OP_MUL_I:
            return "mul";
        case IRInstOperator::IRINST_OP_DIV_I:
            return "sdiv";
        case IRInstOperator::IRINST_OP_MOD_I:
            return "srem";
        default:
            return "???";
    }
}

void BinaryInst::toString(std::string & str)
{
    str = getIRName() + " = " + opToLLVMName(op) + " " + getType()->toString() + " " + getLHS()->getIRName() + ", " +
          getRHS()->getIRName();
}
