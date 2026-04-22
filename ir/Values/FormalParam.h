///
/// @file FormalParam.h
/// @brief 函数形参描述类
///

#pragma once

#include "Value.h"

class FormalParam : public Value {

public:
    FormalParam(Type * _type, std::string _name) : Value(_type)
    {
        this->name = std::move(_name);
    }
};
