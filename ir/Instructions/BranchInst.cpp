///
/// @file BranchInst.cpp
/// @brief 无条件跳转指令实现
///

#include "BranchInst.h"

#include "Function.h"
#include "Types/VoidType.h"

BranchInst::BranchInst(Function * func, BasicBlock * _target)
    : Instruction(func, IRInstOperator::IRINST_OP_BR, VoidType::getType()), target(_target)
{}

void BranchInst::toString(std::string & str)
{
    str = "br label " + target->getIRName();
}
