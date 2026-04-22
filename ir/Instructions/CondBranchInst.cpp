///
/// @file CondBranchInst.cpp
/// @brief 条件跳转指令实现
///

#include "CondBranchInst.h"

#include "Function.h"
#include "Types/VoidType.h"

CondBranchInst::CondBranchInst(Function * func, Value * cond, BasicBlock * _trueDest, BasicBlock * _falseDest)
    : Instruction(func, IRInstOperator::IRINST_OP_COND_BR, VoidType::getType()),
      trueDest(_trueDest),
      falseDest(_falseDest)
{
    addOperand(cond);
}

Value * CondBranchInst::getCondition()
{
    return getOperand(0);
}

void CondBranchInst::toString(std::string & str)
{
    str = "br i1 " + getCondition()->getIRName() + ", label " + trueDest->getIRName() + ", label " +
          falseDest->getIRName();
}
