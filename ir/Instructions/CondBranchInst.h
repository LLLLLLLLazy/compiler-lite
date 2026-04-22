///
/// @file CondBranchInst.h
/// @brief 条件跳转指令（终结指令，Phase 2 块结构版）
///
/// 与旧的 CondGotoInstruction（跳转到 LabelInstruction）不同，
/// 此指令直接持有 BasicBlock* 目标，适用于块结构 CFG。
///

#pragma once

#include "BasicBlock.h"
#include "Instruction.h"

class Value;

class CondBranchInst final : public Instruction {

public:
    /// @param func       所在函数
    /// @param cond       条件值（i1）
    /// @param trueDest   条件为真时跳转的目标块
    /// @param falseDest  条件为假时跳转的目标块
    CondBranchInst(Function * func, Value * cond, BasicBlock * trueDest, BasicBlock * falseDest);

    Value * getCondition();

    BasicBlock * getTrueDest() const
    {
        return trueDest;
    }

    BasicBlock * getFalseDest() const
    {
        return falseDest;
    }

    void toString(std::string & str) override;

private:
    BasicBlock * trueDest = nullptr;
    BasicBlock * falseDest = nullptr;
};
