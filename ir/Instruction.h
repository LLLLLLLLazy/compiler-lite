///
/// @file Instruction.h
/// @brief IR指令头文件
///

#pragma once

#include <cstdint>

#include "User.h"

class BasicBlock;
class Function;

enum class IRInstOperator : std::int8_t {
    // 算术与比较操作码
    IRINST_OP_ADD_I,
    IRINST_OP_SUB_I,
    IRINST_OP_MUL_I,
    IRINST_OP_DIV_I,
    IRINST_OP_MOD_I,
    IRINST_OP_LT_I,
    IRINST_OP_GT_I,
    IRINST_OP_LE_I,
    IRINST_OP_GE_I,
    IRINST_OP_EQ_I,
    IRINST_OP_NE_I,

    // 块结构 IR 操作码
    IRINST_OP_ALLOCA,    ///< 栈上分配指令，结果为指针
    IRINST_OP_LOAD,      ///< 内存加载指令
    IRINST_OP_STORE,     ///< 内存存储指令，无返回值
    IRINST_OP_BR,        ///< 无条件跳转指令
    IRINST_OP_COND_BR,   ///< 条件跳转指令
    IRINST_OP_RET,       ///< 函数返回指令
    IRINST_OP_PHI,       ///< SSA 形式的 phi 指令
    IRINST_OP_CALL,      ///< 块结构 IR 的函数调用指令
    IRINST_OP_ZEXT,      ///< 零扩展指令，例如将 i1 扩展为 i32
    IRINST_OP_COPY,      ///< 值复制指令，供 PhiLoweringPass 消除 phi 时使用
    IRINST_OP_GEP,       ///< 元素地址计算指令

    IRINST_OP_MAX
};

class Instruction : public User {

public:
    /// @brief 构造一条 IR 指令
    explicit Instruction(Function * _func, IRInstOperator op, Type * _type);

    /// @brief 析构函数
    virtual ~Instruction() = default;

    /// @brief 获取指令操作码
    IRInstOperator getOp();

    /// @brief 将指令转换为文本形式
    virtual void toString(std::string & str);

    /// @brief 判断指令是否已被标记为死代码
    bool isDead();

    /// @brief 设置指令的死代码标记
    void setDead(bool _dead = true);

    /// @brief 获取指令所属函数
    Function * getFunction();

    /// @brief 判断指令是否会产生结果值
    bool hasResultValue();

    /// @brief 判断指令是否为基本块终结指令
    bool isTerminator() const;

    /// @brief 获取指令所属的基本块
    BasicBlock * getParentBlock() const
    {
        return parentBlock;
    }

    /// @brief 设置指令所属的基本块
    void setParentBlock(BasicBlock * bb)
    {
        parentBlock = bb;
    }

protected:
    enum IRInstOperator op = IRInstOperator::IRINST_OP_MAX;
    bool dead = false;
    Function * func = nullptr;
    BasicBlock * parentBlock = nullptr;
};
