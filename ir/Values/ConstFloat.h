///
/// @file ConstFloat.h
/// @brief float 类型常量
///

#pragma once

#include <cstdint>

#include "Constant.h"
#include "FloatType.h"

class ConstFloat : public Constant {

public:
    explicit ConstFloat(float val);

    [[nodiscard]] std::string getIRName() const override;

    [[nodiscard]] float getVal() const;

    [[nodiscard]] std::uint32_t getBitPattern() const;

private:
    float floatVal = 0.0f;
};