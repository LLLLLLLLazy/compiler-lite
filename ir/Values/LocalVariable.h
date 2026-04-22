///
/// @file LocalVariable.h
/// @brief 局部变量描述的类
///

#pragma once

#include "IRConstant.h"
#include "Value.h"

class LocalVariable : public Value {

    friend class Function;

private:
    explicit LocalVariable(Type * _type, std::string _name, int32_t _scope_level)
        : Value(_type), scope_level(_scope_level)
    {
        this->name = std::move(_name);
    }

public:
    int32_t getScopeLevel() override
    {
        return scope_level;
    }

private:
    int32_t scope_level = -1;
};
