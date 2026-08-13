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
    /// @param preScaled   true 表示 index 已按元素大小缩放（字节偏移），后端不再乘 elemSize
    GetElementPtrInst(Function * func,
                      Value * basePtr,
                      Value * index,
                      Type * resultType,
                      bool decayArray,
                      bool preScaled = false);

    Value * getBasePointer();
    Value * getIndexOperand();

    [[nodiscard]] bool isArrayDecayGEP() const
    {
        return decayArray;
    }

    /// @brief index 是否已缩放为字节偏移（后端跳过 elemSize 缩放）
    [[nodiscard]] bool isIndexPreScaled() const
    {
        return preScaled;
    }

    /// @brief 将 index 标记为已缩放字节偏移（配合 setOperand 就地改写）
    void setIndexPreScaled(bool scaled = true)
    {
        preScaled = scaled;
    }

    void toString(std::string & str) override;

private:
    bool decayArray = false;
    bool preScaled = false;
};
