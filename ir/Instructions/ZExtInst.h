///
/// @file ZExtInst.h
/// @brief zero-extend 指令 – 将整数类型零扩展到更宽的类型（如 i1 -> i32）
///

#pragma once

#include "Instruction.h"

class Value;

class ZExtInst final : public Instruction {

public:
    /// @param func     所在函数
    /// @param src      源操作数（如 i1 类型）
    /// @param dstType  目标类型（如 i32）
    ZExtInst(Function * func, Value * src, Type * dstType);

    /// 返回被扩展的源操作数
    Value * getSource();

    void toString(std::string & str) override;
};
