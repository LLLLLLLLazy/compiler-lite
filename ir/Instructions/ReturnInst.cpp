///
/// @file ReturnInst.cpp
/// @brief 函数返回指令实现
///

#include "ReturnInst.h"

#include "Function.h"
#include "Types/VoidType.h"

ReturnInst::ReturnInst(Function * func, Value * retVal)
    : Instruction(func, IRInstOperator::IRINST_OP_RET, VoidType::getType())
{
    if (retVal != nullptr) {
        addOperand(retVal);
    }
}

Value * ReturnInst::getReturnValue()
{
    if (getOperandsNum() == 0) {
        return nullptr;
    }
    return getOperand(0);
}

void ReturnInst::toString(std::string & str)
{
    if (!hasReturnValue()) {
        str = "ret void";
    } else {
        Value * rv = getReturnValue();
        str = "ret " + rv->getType()->toString() + " " + rv->getIRName();
    }
}
