///
/// @file Use.cpp
/// @brief Use类定义了一条Define-Use边，usee为定义的Value，user代表使用该Value的User
///

#include "Use.h"

#include "User.h"

/// @brief 将当前 Use 重新指向新的 Value
/// @param newVal 新的被使用值
void Use::setUsee(Value * newVal)
{
    this->usee->removeUse(this);
    this->usee = newVal;
    this->usee->addUse(this);
}

/// @brief 解除当前 Use 与 Value/User 之间的关联
void Use::remove()
{
    usee->removeUse(this);
    user->removeOperandRaw(this);
}
