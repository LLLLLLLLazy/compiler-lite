///
/// @file FPToSIInst.h
/// @brief 浮点转有符号整数指令
///

#pragma once

#include "Instruction.h"

class Value;

class FPToSIInst final : public Instruction {

public:
    FPToSIInst(Function * func, Value * src, Type * dstType);

    Value * getSource();

    void toString(std::string & str) override;
};