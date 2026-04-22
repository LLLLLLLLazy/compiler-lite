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
    // --- Linear IR opcodes (legacy) ---
    IRINST_OP_ENTRY,
    IRINST_OP_EXIT,
    IRINST_OP_LABEL,
    IRINST_OP_GOTO,
    IRINST_OP_COND_GOTO,
    IRINST_OP_ADD_I,
    IRINST_OP_SUB_I,
    IRINST_OP_MUL_I,
    IRINST_OP_DIV_I,
    IRINST_OP_MOD_I,
    IRINST_OP_NEG_I,
    IRINST_OP_NOT_I,
    IRINST_OP_LT_I,
    IRINST_OP_GT_I,
    IRINST_OP_LE_I,
    IRINST_OP_GE_I,
    IRINST_OP_EQ_I,
    IRINST_OP_NE_I,
    IRINST_OP_ASSIGN,
    IRINST_OP_FUNC_CALL,
    IRINST_OP_ARG,

    // --- Block-structured IR opcodes (Phase 2+) ---
    IRINST_OP_ALLOCA,    ///< alloca <type> – stack allocation, result is ptr
    IRINST_OP_LOAD,      ///< load <type>, <ptr> – load from memory
    IRINST_OP_STORE,     ///< store <val>, <ptr> – store to memory (void)
    IRINST_OP_ICMP,      ///< icmp <pred> <lhs>, <rhs> – integer comparison
    IRINST_OP_BR,        ///< br <dest:BasicBlock> – unconditional branch
    IRINST_OP_COND_BR,   ///< br <cond>, <trueD>, <falseD> – conditional branch
    IRINST_OP_RET,       ///< ret [<val>] – function return
    IRINST_OP_PHI,       ///< phi node for SSA (Phase 6)
    IRINST_OP_CALL,      ///< call <func>(<args...>) – block-based call

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

    int32_t getRegId() override
    {
        return regId;
    }

    bool getMemoryAddr(int32_t * _regId = nullptr, int64_t * _offset = nullptr) override
    {
        if (this->baseRegNo == -1) {
            return false;
        }

        if (_regId) {
            *_regId = this->baseRegNo;
        }

        if (_offset) {
            *_offset = this->offset;
        }

        return true;
    }

    void setMemoryAddr(int32_t _regId, int64_t _offset)
    {
        baseRegNo = _regId;
        offset = _offset;
    }

    int32_t getLoadRegId() override
    {
        return this->loadRegNo;
    }

    void setLoadRegId(int32_t regId) override
    {
        this->loadRegNo = regId;
    }

protected:
    enum IRInstOperator op = IRInstOperator::IRINST_OP_MAX;
    bool dead = false;
    Function * func = nullptr;
    BasicBlock * parentBlock = nullptr;
    int32_t regId = -1;
    int32_t offset = 0;
    int32_t baseRegNo = -1;
    std::string baseRegName;
    int32_t loadRegNo = -1;
};
