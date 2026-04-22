///
/// @file GlobalValue.h
/// @brief 描述全局值或对象的类，可以是常量、函数、全局变量等
///

#pragma once

#include "Constant.h"
#include "IRConstant.h"

class GlobalValue : public Constant {

public:
    GlobalValue(Type * _type, std::string _name) : Constant(_type)
    {
        this->name = std::move(_name);
        this->IRName = IR_GLOBAL_VARNAME_PREFIX + this->name;
    }

    [[nodiscard]] std::string getIRName() const override
    {
        return IRName;
    }

    [[nodiscard]] virtual bool isFunction() const
    {
        return false;
    }

    [[nodiscard]] virtual bool isGlobalVarible() const
    {
        return false;
    }

    [[nodiscard]] int32_t getAlignment() const
    {
        return alignment;
    }

    void setAlignment(int32_t _alignment)
    {
        this->alignment = _alignment;
    }

protected:
    int32_t alignment = 4;
};
