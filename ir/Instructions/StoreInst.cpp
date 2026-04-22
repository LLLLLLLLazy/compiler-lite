///
/// @file StoreInst.cpp
/// @brief store 指令实现
///

#include "StoreInst.h"

#include "Function.h"
#include "Types/VoidType.h"

StoreInst::StoreInst(Function * func, Value * val, Value * ptr)
    : Instruction(func, IRInstOperator::IRINST_OP_STORE, VoidType::getType())
{
    addOperand(val);
    addOperand(ptr);
}

Value * StoreInst::getValueOperand()
{
    return getOperand(0);
}

Value * StoreInst::getPointerOperand()
{
    return getOperand(1);
}

void StoreInst::toString(std::string & str)
{
    str = "store " + getValueOperand()->getType()->toString() + " " + getValueOperand()->getIRName() + ", " +
          getPointerOperand()->getType()->toString() + " " + getPointerOperand()->getIRName();
}
