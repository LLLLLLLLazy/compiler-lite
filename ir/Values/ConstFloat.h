///
/// @file ConstFloat.h
/// @brief float 类型常量
///

#pragma once

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>

#include "Constant.h"
#include "FloatType.h"

class ConstFloat : public Constant {

public:
    explicit ConstFloat(float val) : Constant(FloatType::getTypeFloat()), floatVal(val)
    {
        double asDouble = static_cast<double>(val);
        std::uint64_t bits = 0;
        std::memcpy(&bits, &asDouble, sizeof(bits));

        std::ostringstream oss;
        oss << "0x" << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << bits;
        name = oss.str();
    }

    [[nodiscard]] std::string getIRName() const override
    {
        return name;
    }

    [[nodiscard]] float getVal() const
    {
        return floatVal;
    }

    [[nodiscard]] std::uint32_t getBitPattern() const
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &floatVal, sizeof(bits));
        return bits;
    }

private:
    float floatVal = 0.0f;
};