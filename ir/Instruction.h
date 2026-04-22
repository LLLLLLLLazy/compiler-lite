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
    // Arithmetic and comparison opcodes
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

    // Structured IR opcodes
    IRINST_OP_ALLOCA,    ///< alloca <type> – stack allocation, result is ptr
    IRINST_OP_LOAD,      ///< load <type>, <ptr> – load from memory
    IRINST_OP_STORE,     ///< store <val>, <ptr> – store to memory (void)
    IRINST_OP_BR,        ///< br <dest:BasicBlock> – unconditional branch
    IRINST_OP_COND_BR,   ///< br <cond>, <trueD>, <falseD> – conditional branch
    IRINST_OP_RET,       ///< ret [<val>] – function return
    IRINST_OP_PHI,       ///< phi node for SSA
    IRINST_OP_CALL,      ///< call <func>(<args...>) – block-based call
    IRINST_OP_ZEXT,      ///< zext <src> to <dstType> – zero-extend (e.g. i1 -> i32)
    IRINST_OP_COPY,      ///< copy <src> – value copy; used by PhiLoweringPass to lower phi nodes

    IRINST_OP_MAX
};

class Instruction : public User {

public:
    explicit Instruction(Function * _func, IRInstOperator op, Type * _type);

    virtual ~Instruction() = default;

    IRInstOperator getOp();

    virtual void toString(std::string & str);

    bool isDead();

    void setDead(bool _dead = true);

    Function * getFunction();

    bool hasResultValue();

    /// Returns true if this instruction is a block terminator
    /// (IRINST_OP_BR, IRINST_OP_COND_BR, IRINST_OP_RET).
    bool isTerminator() const;

    BasicBlock * getParentBlock() const
    {
        return parentBlock;
    }

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
