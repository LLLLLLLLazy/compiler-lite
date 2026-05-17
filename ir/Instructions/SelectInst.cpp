///
/// @file SelectInst.cpp
/// @brief select 指令实现
///

#include "SelectInst.h"

#include "Function.h"

/// @brief 构造一条 select 指令
/// @param func 所在函数
/// @param cond 条件值
/// @param trueValue 条件为真时的结果值
/// @param falseValue 条件为假时的结果值
/// @param resultType 结果类型
SelectInst::SelectInst(Function * func, Value * cond, Value * trueValue, Value * falseValue, Type * resultType)
    : Instruction(func, IRInstOperator::IRINST_OP_SELECT, resultType)
{
    addOperand(cond);
    addOperand(trueValue);
    addOperand(falseValue);
}

/// @brief 获取条件值
/// @return select 的第一个操作数
Value * SelectInst::getCondition() const
{
    return const_cast<SelectInst *>(this)->getOperand(0);
}

/// @brief 获取 true 分支值
/// @return 条件为真时返回的候选值
Value * SelectInst::getTrueValue() const
{
    return const_cast<SelectInst *>(this)->getOperand(1);
}

/// @brief 获取 false 分支值
/// @return 条件为假时返回的候选值
Value * SelectInst::getFalseValue() const
{
    return const_cast<SelectInst *>(this)->getOperand(2);
}

/// @brief 将 select 指令转换为 IR 文本
/// @param str 输出字符串
void SelectInst::toString(std::string & str)
{
    str = getIRName() + " = select i1 " + getCondition()->getIRName() + ", " +
          getType()->toString() + " " + getTrueValue()->getIRName() + ", " +
          getType()->toString() + " " + getFalseValue()->getIRName();
}