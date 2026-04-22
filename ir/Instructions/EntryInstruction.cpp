///
/// @file EntryInstruction.cpp
/// @brief 函数的入口指令
///

#include "EntryInstruction.h"

#include "Types/VoidType.h"

EntryInstruction::EntryInstruction(Function * _func)
    : Instruction(_func, IRInstOperator::IRINST_OP_ENTRY, VoidType::getType())
{}

void EntryInstruction::toString(std::string & str)
{
    str = "entry";
}
