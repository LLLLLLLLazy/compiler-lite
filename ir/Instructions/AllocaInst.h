///
/// @file AllocaInst.h
/// @brief alloca 指令 – 在栈上分配 <type> 大小的空间，返回指向该空间的指针
///

#pragma once

#include "Instruction.h"

class Type;

class AllocaInst final : public Instruction {

public:
    /// @param func       所在函数
    /// @param allocaType 被分配对象的类型（结果类型为 ptr-to-allocaType）
    AllocaInst(Function * func, Type * allocaType);

    /// 返回被分配对象的类型（不是指针类型）
    Type * getAllocaType() const
    {
        return allocaType;
    }

    void toString(std::string & str) override;

private:
    Type * allocaType = nullptr;
};
