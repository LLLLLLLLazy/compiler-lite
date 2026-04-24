///
/// @file Instruction.cpp
/// @brief IR指令类实现
///

#include "Instruction.h"

#include "Function.h"

/// @brief 构造一条 IR 指令
/// @param _func 指令所属函数
/// @param _op 指令操作码
/// @param _type 指令结果类型
Instruction::Instruction(Function * _func, IRInstOperator _op, Type * _type) : User(_type), op(_op), func(_func)
{}

/// @brief 获取指令操作码
/// @return 当前指令的操作码
IRInstOperator Instruction::getOp()
{
    return op;
}

/// @brief 将指令转换为文本形式
/// @param str 输出的文本字符串
void Instruction::toString(std::string & str)
{
    str = "Unkown IR Instruction";
}

/// @brief 判断指令是否已被标记为死代码
/// @return true 表示为死代码，false 表示不是死代码
bool Instruction::isDead()
{
    return dead;
}

/// @brief 设置指令的死代码标记
/// @param _dead 是否标记为死代码
void Instruction::setDead(bool _dead)
{
    dead = _dead;
}

/// @brief 获取指令所属函数
/// @return 指令所属的函数对象
Function * Instruction::getFunction()
{
    return func;
}

/// @brief 判断指令是否会产生结果值
/// @return true 表示会产生结果值，false 表示无返回值
bool Instruction::hasResultValue()
{
    return !type->isVoidType();
}

/// @brief 判断指令是否为基本块终结指令
/// @return true 表示为终结指令，false 表示不是终结指令
bool Instruction::isTerminator() const
{
    return op == IRInstOperator::IRINST_OP_BR || op == IRInstOperator::IRINST_OP_COND_BR ||
           op == IRInstOperator::IRINST_OP_RET;
}
