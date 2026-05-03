///
/// @file ConstFloat.cpp
/// @brief float 类型常量
///

#include "ConstFloat.h"

#include <cstring>
#include <iomanip>
#include <sstream>

/// @brief 构造 float 常量对象
/// @param val float 常量值
ConstFloat::ConstFloat(float val) : Constant(FloatType::getTypeFloat()), floatVal(val)
{
    double asDouble = static_cast<double>(val);
    std::uint64_t bits = 0;
    std::memcpy(&bits, &asDouble, sizeof(bits));

    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << bits;
    name = oss.str();
}

/// @brief 获取 IR 名称
/// @return IR 名称字符串
std::string ConstFloat::getIRName() const
{
    return name;
}

/// @brief 获取 float 常量值
/// @return float 常量值
float ConstFloat::getVal() const
{
    return floatVal;
}

/// @brief 获取 float 原始位模式
/// @return 位模式
std::uint32_t ConstFloat::getBitPattern() const
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &floatVal, sizeof(bits));
    return bits;
}
