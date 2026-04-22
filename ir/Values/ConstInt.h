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

    int32_t getLoadRegId()
    {
        return this->loadRegNo;
    }

    void setLoadRegId(int32_t regId)
    {
        this->loadRegNo = regId;
    }

private:
    int32_t intVal = 0;
    int32_t loadRegNo = -1;
};
