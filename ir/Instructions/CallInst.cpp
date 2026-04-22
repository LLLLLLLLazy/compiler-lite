///
/// @file CallInst.cpp
/// @brief 函数调用指令实现
///

#include "CallInst.h"

#include "Function.h"

CallInst::CallInst(Function * parentFunc, Function * _callee, const std::vector<Value *> & args, Type * resultType)
    : Instruction(parentFunc, IRInstOperator::IRINST_OP_CALL, resultType), callee(_callee)
{
    for (auto * arg : args) {
        addOperand(arg);
    }
}

int32_t CallInst::getArgCount()
{
    return getOperandsNum();
}

Value * CallInst::getArg(int32_t i)
{
    return getOperand(i);
}

void CallInst::toString(std::string & str)
{
    bool hasResult = hasResultValue();
    if (hasResult) {
        str = getIRName() + " = ";
    }
    str += "call " + getType()->toString() + " " + callee->getIRName() + "(";
    for (int32_t i = 0; i < getArgCount(); ++i) {
        if (i > 0) {
            str += ", ";
        }
        Value * arg = getArg(i);
        str += arg->getType()->toString() + " " + arg->getIRName();
    }
    str += ")";
}
