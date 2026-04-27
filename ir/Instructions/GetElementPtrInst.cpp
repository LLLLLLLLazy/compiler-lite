///
/// @file GetElementPtrInst.cpp
/// @brief 数组/指针元素地址计算指令实现
///

#include "GetElementPtrInst.h"

#include "ArrayType.h"
#include "Function.h"
#include "Types/PointerType.h"

GetElementPtrInst::GetElementPtrInst(Function * func, Value * basePtr, Value * index, Type * resultType, bool decayArray)
    : Instruction(func, IRInstOperator::IRINST_OP_GEP, resultType), decayArray(decayArray)
{
    addOperand(basePtr);
    addOperand(index);
}

Value * GetElementPtrInst::getBasePointer()
{
    return getOperand(0);
}

Value * GetElementPtrInst::getIndexOperand()
{
    return getOperand(1);
}

void GetElementPtrInst::toString(std::string & str)
{
    auto * ptrType = dynamic_cast<const PointerType *>(getBasePointer()->getType());
    if (ptrType == nullptr) {
        str.clear();
        return;
    }

    std::string baseTypeStr = ptrType->getPointeeType()->toString();
    str = getIRName() + " = getelementptr inbounds " + baseTypeStr + ", " +
          getBasePointer()->getType()->toString() + " " + getBasePointer()->getIRName();

    if (decayArray) {
        str += ", i32 0";
    }

    str += ", i32 " + getIndexOperand()->getIRName();
}
