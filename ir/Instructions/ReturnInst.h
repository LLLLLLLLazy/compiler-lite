///
/// @file ReturnInst.h
/// @brief 函数返回指令
///

#pragma once

#include "Instruction.h"

class Value;

class ReturnInst final : public Instruction {

public:
    /// @param func    所在函数
    /// @param retVal  返回值（nullptr 表示 void 返回）
    explicit ReturnInst(Function * func, Value * retVal = nullptr);

    /// 返回返回值（void 返回时为 nullptr）
    Value * getReturnValue();

    bool hasReturnValue()
    {
        return getOperandsNum() > 0;
    }

    void toString(std::string & str) override;
};
