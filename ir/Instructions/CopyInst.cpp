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

/// @brief 构造一个生成新结果值的 copy 指令
/// @param func 所属函数
/// @param src 源值
CopyInst::CopyInst(Function * func, Value * src)
    : Instruction(func, IRInstOperator::IRINST_OP_COPY, src->getType())
{
    addOperand(src);
}

// ---------------------------------------------------------------------------
// phi 降级复制构造函数
// ---------------------------------------------------------------------------

/// @brief 构造一个面向 phi 降级的 copy 指令
/// @param func 所属函数
/// @param src 源值
/// @param dst 显式指定的目标值
CopyInst::CopyInst(Function * func, Value * src, Value * dst)
    : Instruction(func, IRInstOperator::IRINST_OP_COPY, VoidType::getType()), explicitDst(dst)
{
    addOperand(src);
}

// ---------------------------------------------------------------------------
// 访问接口
// ---------------------------------------------------------------------------

/// @brief 获取 copy 指令的源操作数
/// @return 源值对象
Value * CopyInst::getSource()
{
    return getOperand(0);
}

// ---------------------------------------------------------------------------
// 文本化输出
// ---------------------------------------------------------------------------

/// @brief 将 copy 指令转换为文本形式
/// @param str 输出的文本字符串
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
