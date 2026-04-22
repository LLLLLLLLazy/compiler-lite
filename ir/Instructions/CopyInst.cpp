///
/// @file CopyInst.cpp
/// @brief copy 指令实现
///

#include "CopyInst.h"

#include "Function.h"
#include "Types/VoidType.h"

// ---------------------------------------------------------------------------
// 新值复制构造函数
// ---------------------------------------------------------------------------

CopyInst::CopyInst(Function * func, Value * src)
    : Instruction(func, IRInstOperator::IRINST_OP_COPY, src->getType())
{
    addOperand(src);
}

// ---------------------------------------------------------------------------
// phi 降级复制构造函数
// ---------------------------------------------------------------------------

CopyInst::CopyInst(Function * func, Value * src, Value * dst)
    : Instruction(func, IRInstOperator::IRINST_OP_COPY, VoidType::getType()), explicitDst(dst)
{
    addOperand(src);
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

Value * CopyInst::getSource()
{
    return getOperand(0);
}

// ---------------------------------------------------------------------------
// toString
// ---------------------------------------------------------------------------

void CopyInst::toString(std::string & str)
{
    Value * src = getSource();
    if (explicitDst) {
        // phi 降级复制：%dst = copy %src
        str = explicitDst->getIRName() + " = copy " + src->getType()->toString() + " " + src->getIRName();
    } else {
        // 新值复制：%this = copy %src
        str = getIRName() + " = copy " + src->getType()->toString() + " " + src->getIRName();
    }
}
