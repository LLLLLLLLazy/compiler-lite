///
/// @file StoreInst.h
/// @brief store 指令 – 将值写入指针所指向的内存位置（void 结果）
///

#pragma once

#include "Instruction.h"

class Value;

class StoreInst final : public Instruction {

public:
    /// @param func  所在函数
    /// @param val   要写入的值
    /// @param ptr   目标指针（必须是 PointerType）
    StoreInst(Function * func, Value * val, Value * ptr);

    /// 返回被写入的值操作数
    Value * getValueOperand();

    /// 返回目标指针操作数
    Value * getPointerOperand();

    void toString(std::string & str) override;
};
