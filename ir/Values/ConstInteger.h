///
/// @file ConstInteger.h
/// @brief 带整数类型的常量，可表示 i1、i32 等整数常量
///

#pragma once

#include <cstdint>

#include "Constant.h"
#include "IntegerType.h"

class ConstInteger : public Constant {

public:
    explicit ConstInteger(int32_t val, Type * type = IntegerType::getTypeInt32()) : Constant(type)
    {
        name = std::to_string(val);
        intVal = val;
    }

    [[nodiscard]] std::string getIRName() const override
    {
        return name;
    }

    [[nodiscard]] int32_t getVal() const
    {
        return intVal;
    }

    [[nodiscard]] bool isInt1Constant()
    {
        return getType()->isInt1Type();
    }

private:
    int32_t intVal = 0;
};