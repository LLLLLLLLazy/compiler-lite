///
/// @file Use.cpp
/// @brief Use类定义了一条Define-Use边，usee为定义的Value，user代表使用该Value的User
///

#include "Use.h"

#include "User.h"

void Use::setUsee(Value * newVal)
{
    this->usee->removeUse(this);
    this->usee = newVal;
    this->usee->addUse(this);
}

void Use::remove()
{
    usee->removeUse(this);
    user->removeOperandRaw(this);
}
