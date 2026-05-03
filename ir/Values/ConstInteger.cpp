///
/// @file ConstInteger.cpp
/// @brief 带整数类型的常量，可表示 i1、i32 等整数常量
///

#include "ConstInteger.h"

/// @brief 构造整数常量对象
/// @param val 整数常量值
/// @param type 常量类型
ConstInteger::ConstInteger(int32_t val, Type * type) : Constant(type)
{
    name = std::to_string(val);
    intVal = val;
}

/// @brief 获取 IR 名称
/// @return IR 名称字符串
std::string ConstInteger::getIRName() const
{
    return name;
}

/// @brief 获取整数常量值
/// @return 整数常量值
int32_t ConstInteger::getVal() const
{
    return intVal;
}

/// @brief 判断是否为 i1 常量
/// @return 是否为 i1 常量
bool ConstInteger::isInt1Constant()
{
    return getType()->isInt1Type();
}
