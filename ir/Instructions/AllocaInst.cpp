///
/// @file AllocaInst.cpp
/// @brief alloca 指令实现
///

#include "AllocaInst.h"

#include "Function.h"
#include "Types/PointerType.h"

AllocaInst::AllocaInst(Function * func, Type * _allocaType)
    : Instruction(func, IRInstOperator::IRINST_OP_ALLOCA,
                  const_cast<PointerType *>(PointerType::get(_allocaType))),
      allocaType(_allocaType)
{}

void AllocaInst::toString(std::string & str)
{
    str = getIRName() + " = alloca " + allocaType->toString();
}
