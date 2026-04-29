#include "FPToSIInst.h"

#include "Function.h"

FPToSIInst::FPToSIInst(Function * func, Value * src, Type * dstType)
    : Instruction(func, IRInstOperator::IRINST_OP_FPTOSI, dstType)
{
    addOperand(src);
}

Value * FPToSIInst::getSource()
{
    return getOperand(0);
}

void FPToSIInst::toString(std::string & str)
{
    str = getIRName() + " = fptosi " + getSource()->getType()->toString() + " " + getSource()->getIRName() +
          " to " + getType()->toString();
}