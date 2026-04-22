///
/// @file ConstInt.h
/// @brief int类型的常量
///

#pragma once

#include <cstdint>

#include "Constant.h"
#include "IntegerType.h"

class ConstInt : public Constant {

public:
    explicit ConstInt(int32_t val) : Constant(IntegerType::getTypeInt())
    {
        name = std::to_string(val);
        intVal = val;
    }

    [[nodiscard]] std::string getIRName() const override
    {
        return name;
    }

    int32_t getVal() const
    {
        return intVal;
    }

private:
    int32_t intVal = 0;
};
