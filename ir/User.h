///
/// @file User.h
/// @brief 使用Value的User，该User也是Value。函数、指令都是User
///

#pragma once

#include <vector>

#include "Use.h"
#include "Value.h"

class User : public Value {

    std::vector<Use *> operands;

public:
    /// @brief 构造一个可使用其他 Value 的 User 对象
    explicit User(Type * _type);

    /// @brief 获取操作数 Use 列表
    std::vector<Use *> & getOperands();

    /// @brief 获取操作数对应的 Value 列表
    std::vector<Value *> getOperandsValue();

    /// @brief 获取操作数个数
    int32_t getOperandsNum();

    /// @brief 获取指定位置的操作数值
    Value * getOperand(int32_t pos);

    /// @brief 设置指定位置的操作数值
    void setOperand(int32_t pos, Value * val);

    /// @brief 在末尾追加一个操作数
    void addOperand(Value * val);

    /// @brief 按位置删除一个操作数
    void removeOperand(int pos);

    /// @brief 按值删除一个操作数
    void removeOperand(Value * val);

    /// @brief 删除指定 Use 对应的操作数
    void removeOperand(Use * val);

    /// @brief 仅从操作数列表中移除 Use，不修改其引用关系
    void removeOperandRaw(Use * use);

    /// @brief 删除并解除一个 Use 引用关系
    void removeUse(Use * use);

    /// @brief 清空全部操作数
    void clearOperands();

    /// @brief 交换两个操作数的位置
    void swapTwoOperands();

    /// @brief 将某个旧操作数替换为新操作数
    void replaceOperand(Value * val, Value * newVal);
};
