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
    Use(Value * _value, User * _user) : usee(_value), user(_user)
    {}

    operator Value *() const
    {
        return usee;
    }

    [[nodiscard]] User * getUser() const
    {
        return user;
    }

    [[nodiscard]] Value * getUsee() const
    {
        return usee;
    }

    void setUsee(Value * newVal);

    void remove();
};
