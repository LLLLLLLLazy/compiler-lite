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

/// @brief 判断指令是否可能读取内存
/// @return true 表示该指令可能从内存读取
bool Instruction::mayReadMemory() const
{
    switch (op) {
        case IRInstOperator::IRINST_OP_LOAD:
        case IRInstOperator::IRINST_OP_CALL:
            return true;

        default:
            return false;
    }
}

/// @brief 判断指令是否可能写入内存
/// @return true 表示该指令可能向内存写入
bool Instruction::mayWriteMemory() const
{
    switch (op) {
        case IRInstOperator::IRINST_OP_STORE:
        case IRInstOperator::IRINST_OP_CALL:
            return true;

        default:
            return false;
    }
}

/// @brief 判断指令是否具有副作用
/// @return true 表示该指令不可被当作纯值指令处理
bool Instruction::mayHaveSideEffects() const
{
    // 控制流变化本身即为副作用
    if (isTerminator()) {
        return true;
    }

    switch (op) {
        case IRInstOperator::IRINST_OP_STORE:
        case IRInstOperator::IRINST_OP_CALL:
            return true;

        default:
            return false;
    }
}

/// @brief 判断指令是否可被安全推测执行
/// @return true 表示该指令属于纯计算且不会触发内存副作用
bool Instruction::isSpeculatable() const
{
    if (mayReadMemory() || mayWriteMemory() || mayHaveSideEffects()) {
        return false;
    }

    switch (op) {
        case IRInstOperator::IRINST_OP_ADD_I:
        case IRInstOperator::IRINST_OP_SUB_I:
        case IRInstOperator::IRINST_OP_MUL_I:
        case IRInstOperator::IRINST_OP_DIV_I:
        case IRInstOperator::IRINST_OP_MOD_I:
        case IRInstOperator::IRINST_OP_LT_I:
        case IRInstOperator::IRINST_OP_GT_I:
        case IRInstOperator::IRINST_OP_LE_I:
        case IRInstOperator::IRINST_OP_GE_I:
        case IRInstOperator::IRINST_OP_EQ_I:
        case IRInstOperator::IRINST_OP_NE_I:
        case IRInstOperator::IRINST_OP_ADD_F:
        case IRInstOperator::IRINST_OP_SUB_F:
        case IRInstOperator::IRINST_OP_MUL_F:
        case IRInstOperator::IRINST_OP_DIV_F:
        case IRInstOperator::IRINST_OP_LT_F:
        case IRInstOperator::IRINST_OP_GT_F:
        case IRInstOperator::IRINST_OP_LE_F:
        case IRInstOperator::IRINST_OP_GE_F:
        case IRInstOperator::IRINST_OP_EQ_F:
        case IRInstOperator::IRINST_OP_NE_F:
        case IRInstOperator::IRINST_OP_ZEXT:
        case IRInstOperator::IRINST_OP_SITOFP:
        case IRInstOperator::IRINST_OP_FPTOSI:
        case IRInstOperator::IRINST_OP_SELECT:
        case IRInstOperator::IRINST_OP_COPY:
        case IRInstOperator::IRINST_OP_GEP:
            return true;

        default:
            return false;
    }
}

/// @brief 判断指令是否为基本块终结指令
/// @return true 表示为终结指令，false 表示不是终结指令
bool Instruction::isTerminator() const
{
    return op == IRInstOperator::IRINST_OP_BR || op == IRInstOperator::IRINST_OP_COND_BR ||
           op == IRInstOperator::IRINST_OP_RET;
}
