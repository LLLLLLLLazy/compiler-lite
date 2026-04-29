#include "SIToFPInst.h"

#include "Function.h"

SIToFPInst::SIToFPInst(Function * func, Value * src, Type * dstType)
    : Instruction(func, IRInstOperator::IRINST_OP_SITOFP, dstType)
{
    addOperand(src);
}

Value * SIToFPInst::getSource()
{
    return getOperand(0);
}

void SIToFPInst::toString(std::string & str)
{
    str = getIRName() + " = sitofp " + getSource()->getType()->toString() + " " + getSource()->getIRName() +
          " to " + getType()->toString();
}