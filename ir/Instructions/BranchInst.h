///
/// @file BranchInst.h
/// @brief 无条件跳转指令（终结指令）
///

#pragma once

#include "BasicBlock.h"
#include "Instruction.h"

class BranchInst final : public Instruction {

public:
    /// @param func   所在函数
    /// @param target 跳转目标基本块
    BranchInst(Function * func, BasicBlock * target);

    BasicBlock * getTarget() const
    {
        return target;
    }

    /// @brief 原地改写跳转目标基本块
    /// @param newTarget 新的跳转目标
    void setTarget(BasicBlock * newTarget)
    {
        target = newTarget;
    }

    void toString(std::string & str) override;

private:
    BasicBlock * target = nullptr;
};
