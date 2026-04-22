///
/// @file LoadInst.cpp
/// @brief load 指令实现
///

#include "LoadInst.h"

#include "Function.h"

LoadInst::LoadInst(Function * func, Value * ptr, Type * valType)
    : Instruction(func, IRInstOperator::IRINST_OP_LOAD, valType)
{
    addOperand(ptr);
}

Value * LoadInst::getPointerOperand()
{
    return getOperand(0);
}

void LoadInst::toString(std::string & str)
{
    str = getIRName() + " = load " + getType()->toString() + ", " + getPointerOperand()->getType()->toString() + " " +
          getPointerOperand()->getIRName();
}
