///
/// @file ExitInstruction.cpp
/// @brief 函数出口或返回指令
///

#include "ExitInstruction.h"

#include "VoidType.h"

ExitInstruction::ExitInstruction(Function * _func, Value * _result)
    : Instruction(_func, IRInstOperator::IRINST_OP_EXIT, VoidType::getType())
{
    if (_result != nullptr) {
        addOperand(_result);
    }
}

void ExitInstruction::toString(std::string & str)
{
    if (getOperandsNum() == 0) {
        str = "exit";
    } else {
        Value * src1 = getOperand(0);
        str = "exit " + src1->getIRName();
    }
}
