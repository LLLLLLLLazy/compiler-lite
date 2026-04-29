///
/// @file ICmpInst.cpp
/// @brief 整数比较指令实现
///

#include "ICmpInst.h"

#include "Function.h"

ICmpInst::ICmpInst(Function * func, IRInstOperator _op, Value * lhs, Value * rhs, Type * _type)
    : Instruction(func, _op, _type)
{
    addOperand(lhs);
    addOperand(rhs);
}

Value * ICmpInst::getLHS()
{
    return getOperand(0);
}

Value * ICmpInst::getRHS()
{
    return getOperand(1);
}

const char * ICmpInst::predToLLVMName(IRInstOperator op)
{
    switch (op) {
        case IRInstOperator::IRINST_OP_EQ_I:
            return "eq";
        case IRInstOperator::IRINST_OP_NE_I:
            return "ne";
        case IRInstOperator::IRINST_OP_EQ_F:
            return "oeq";
        case IRInstOperator::IRINST_OP_NE_F:
            return "one";
        case IRInstOperator::IRINST_OP_LT_I:
            return "slt";
        case IRInstOperator::IRINST_OP_LT_F:
            return "olt";
        case IRInstOperator::IRINST_OP_GT_I:
            return "sgt";
        case IRInstOperator::IRINST_OP_GT_F:
            return "ogt";
        case IRInstOperator::IRINST_OP_LE_I:
            return "sle";
        case IRInstOperator::IRINST_OP_LE_F:
            return "ole";
        case IRInstOperator::IRINST_OP_GE_I:
            return "sge";
        case IRInstOperator::IRINST_OP_GE_F:
            return "oge";
        default:
            return "???";
    }
}

void ICmpInst::toString(std::string & str)
{
    bool isFloatCmp = getLHS()->getType()->isFloatType() || getRHS()->getType()->isFloatType();
    str = getIRName() + " = " + (isFloatCmp ? "fcmp " : "icmp ") + predToLLVMName(op) + " " +
          getLHS()->getType()->toString() + " " +
          getLHS()->getIRName() + ", " + getRHS()->getIRName();
}
