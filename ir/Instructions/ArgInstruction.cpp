///
/// @file ArgInstruction.cpp
/// @brief 函数调用前的实参指令
///

#include "ArgInstruction.h"

#include "Function.h"
#include "VoidType.h"

ArgInstruction::ArgInstruction(Function * _func, Value * src)
    : Instruction(_func, IRInstOperator::IRINST_OP_ARG, VoidType::getType())
{
    this->addOperand(src);
}

void ArgInstruction::toString(std::string & str)
{
    int32_t regId = -1;
    int64_t offset = 0;
    Value * src = getOperand(0);

    str = "arg " + src->getIRName();

    if (src->getRegId() != -1) {
        str += " ; " + std::to_string(src->getRegId());
    } else if (src->getMemoryAddr(&regId, &offset)) {
        str += " ; " + std::to_string(regId) + "[" + std::to_string(offset) + "]";
    }

    func->realArgCountInc();
}
