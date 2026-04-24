///
/// @file Use.h
/// @brief Use类定义了一条Define-Use边，usee为定义的Value，user代表使用该Value的User
///

#pragma once

#include <cstdint>

class User;
class Value;

class Use {

protected:
    Value * usee = nullptr;
    User * user = nullptr;

public:
    /// @brief 构造一条 Define-Use 边
    Use(Value * _value, User * _user) : usee(_value), user(_user)
    {}

    /// @brief 将 Use 隐式转换为其引用的 Value
    operator Value *() const
    {
        return usee;
    }

    /// @brief 获取使用该值的 User
    [[nodiscard]] User * getUser() const
    {
        return user;
    }

    /// @brief 获取被使用的 Value
    [[nodiscard]] Value * getUsee() const
    {
        return usee;
    }

    /// @brief 将当前 Use 重新指向新的 Value
    void setUsee(Value * newVal);

    /// @brief 解除当前 Use 与 Value/User 之间的关联
    void remove();
};
