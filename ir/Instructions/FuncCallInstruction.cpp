///
/// @file FuncCallInstruction.cpp
/// @brief 函数调用指令
///

#include "FuncCallInstruction.h"

#include "Common.h"
#include "Function.h"

FuncCallInstruction::FuncCallInstruction(
    Function * _func, Function * calledFunc, std::vector<Value *> & _srcVal, Type * _type)
    : Instruction(_func, IRInstOperator::IRINST_OP_FUNC_CALL, _type), calledFunction(calledFunc)
{
    name = calledFunc->getName();

    for (auto & val: _srcVal) {
        addOperand(val);
    }
}

void FuncCallInstruction::toString(std::string & str)
{
    int32_t argCount = func->getRealArgcount();
    int32_t operandsNum = getOperandsNum();

    if (operandsNum != argCount && argCount != 0) {
        minic_log(LOG_ERROR, "ARG指令的个数与调用函数个数不一致");
    }

    if (type->isVoidType()) {
        str = "call void " + calledFunction->getIRName() + "(";
    } else {
        str = getIRName() + " = call i32 " + calledFunction->getIRName() + "(";
    }

    if (argCount == 0) {
        for (int32_t index = 0; index < operandsNum; ++index) {
            auto operand = getOperand(index);
            str += operand->getType()->toString() + " " + operand->getIRName();
            if (index != operandsNum - 1) {
                str += ", ";
            }
        }
    }

    str += ")";
    func->realArgCountReset();
}

std::string FuncCallInstruction::getCalledName() const
{
    return calledFunction->getName();
}
