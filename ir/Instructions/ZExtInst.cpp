///
/// @file ZExtInst.cpp
/// @brief zero-extend 指令实现
///

#include "ZExtInst.h"

#include "Function.h"

ZExtInst::ZExtInst(Function * func, Value * src, Type * dstType)
    : Instruction(func, IRInstOperator::IRINST_OP_ZEXT, dstType)
{
    addOperand(src);
}

Value * ZExtInst::getSource()
{
    return getOperand(0);
}

void ZExtInst::toString(std::string & str)
{
    str = getIRName() + " = zext " + getSource()->getType()->toString() + " " + getSource()->getIRName() +
          " to " + getType()->toString();
}
