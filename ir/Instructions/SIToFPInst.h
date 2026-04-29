///
/// @file SIToFPInst.h
/// @brief 有符号整数转浮点指令
///

#pragma once

#include "Instruction.h"

class Value;

class SIToFPInst final : public Instruction {

public:
    SIToFPInst(Function * func, Value * src, Type * dstType);

    Value * getSource();

    void toString(std::string & str) override;
};