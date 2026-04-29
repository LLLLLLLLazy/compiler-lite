///
/// @file FloatType.h
/// @brief 单精度浮点类型
///

#pragma once

#include "Type.h"

class FloatType final : public Type {

public:
    FloatType() : Type(FloatTyID)
    {}

    static FloatType * getTypeFloat();

    [[nodiscard]] int32_t getSize() const override
    {
        return 4;
    }

    [[nodiscard]] std::string toString() const override
    {
        return "float";
    }
};