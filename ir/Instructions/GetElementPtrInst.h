///
/// @file GetElementPtrInst.h
/// @brief 数组/指针元素地址计算指令
///

#pragma once

#include "Instruction.h"

class Value;

class GetElementPtrInst final : public Instruction {

public:
    /// @param func        所在函数
    /// @param basePtr     基地址指针
    /// @param index       下标值
    /// @param resultType  结果指针类型
    /// @param decayArray  true 表示从数组对象地址中取第 index 个元素地址
    GetElementPtrInst(Function * func, Value * basePtr, Value * index, Type * resultType, bool decayArray);

    Value * getBasePointer();
    Value * getIndexOperand();

    [[nodiscard]] bool isArrayDecayGEP() const
    {
        return decayArray;
    }

    void toString(std::string & str) override;

private:
    bool decayArray = false;
};
