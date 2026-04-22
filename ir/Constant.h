///
/// @file Constant.h
/// @brief 常量类
///

#pragma once

#include "User.h"

class Constant : public User {

protected:
    explicit Constant(Type * _type) : User(_type)
    {}
};
