///
/// @file FCmpInst.cpp
/// @brief 浮点比较指令实现
///

#include "FCmpInst.h"

#include "Function.h"

FCmpInst::FCmpInst(Function * func, IRInstOperator _op, Value * lhs, Value * rhs, Type * _type)
    : Instruction(func, _op, _type)
{
    addOperand(lhs);
    addOperand(rhs);
}

Value * FCmpInst::getLHS()
{
    return getOperand(0);
}

Value * FCmpInst::getRHS()
{
    return getOperand(1);
}

const char * FCmpInst::predToLLVMName(IRInstOperator op)
{
    switch (op) {
        case IRInstOperator::IRINST_OP_EQ_F:
            return "oeq";
        case IRInstOperator::IRINST_OP_NE_F:
            return "one";
        case IRInstOperator::IRINST_OP_LT_F:
            return "olt";
        case IRInstOperator::IRINST_OP_GT_F:
            return "ogt";
        case IRInstOperator::IRINST_OP_LE_F:
            return "ole";
        case IRInstOperator::IRINST_OP_GE_F:
            return "oge";
        default:
            return "???";
    }
}

void FCmpInst::toString(std::string & str)
{
    str = getIRName() + " = fcmp " + predToLLVMName(op) + " " + getLHS()->getType()->toString() + " " +
          getLHS()->getIRName() + ", " + getRHS()->getIRName();
}