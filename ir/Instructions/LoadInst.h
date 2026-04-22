///
/// @file LoadInst.h
/// @brief load 指令 – 从指针处加载一个值
///

#pragma once

#include "Instruction.h"

class Value;

class LoadInst final : public Instruction {

public:
    /// @param func    所在函数
    /// @param ptr     被加载的指针（必须是 PointerType）
    /// @param valType 加载出的值类型（即指针所指向的类型）
    LoadInst(Function * func, Value * ptr, Type * valType);

    /// 返回被加载的指针操作数
    Value * getPointerOperand();

    void toString(std::string & str) override;
};
