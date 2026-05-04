///
/// @file GlobalVariable.cpp
/// @brief 全局变量描述类
///

#include "GlobalVariable.h"

#include <algorithm>

/// @brief 构造全局变量对象
/// @param _type 全局变量值类型
/// @param _name 全局变量名称
GlobalVariable::GlobalVariable(Type * _type, std::string _name)
    : GlobalValue(const_cast<PointerType *>(PointerType::get(_type)), std::move(_name)), valueType(_type)
{
    setAlignment(4);
}

/// @brief 获取全局变量值类型
/// @return 值类型
Type * GlobalVariable::getValueType() const
{
    return valueType;
}

/// @brief 判断是否为全局变量
/// @return 始终返回 true
bool GlobalVariable::isGlobalVarible() const
{
    return true;
}

/// @brief 判断是否位于 BSS 段
/// @return 是否位于 BSS 段
bool GlobalVariable::isInBSSSection() const
{
    return this->inBSSSection;
}

/// @brief 获取作用域层级
/// @return 全局作用域层级
int32_t GlobalVariable::getScopeLevel()
{
    return 0;
}

/// @brief 设置整数初始化值
/// @param value 初始化值
void GlobalVariable::setInitIntValue(int32_t value)
{
    initIntValue = value;
    inBSSSection = (value == 0);
    initKind = (value == 0) ? InitKind::Zero : InitKind::Int;
}

/// @brief 获取整数初始化值
/// @return 初始化值
int32_t GlobalVariable::getInitIntValue() const
{
    return initIntValue;
}

/// @brief 设置浮点初始化值
/// @param value 初始化值
void GlobalVariable::setInitFloatValue(float value)
{
    initFloatValue = value;
    inBSSSection = (value == 0.0f);
    initKind = (value == 0.0f) ? InitKind::Zero : InitKind::Float;
}

/// @brief 获取浮点初始化值
/// @return 初始化值
float GlobalVariable::getInitFloatValue() const
{
    return initFloatValue;
}

void GlobalVariable::setInitIntArrayValues(const std::vector<int32_t> & values)
{
    initIntArrayValues = values;
    initFloatArrayValues.clear();
    inBSSSection = std::all_of(initIntArrayValues.begin(), initIntArrayValues.end(), [](int32_t value) {
        return value == 0;
    });
    initKind = inBSSSection ? InitKind::Zero : InitKind::IntArray;
}

const std::vector<int32_t> & GlobalVariable::getInitIntArrayValues() const
{
    return initIntArrayValues;
}

void GlobalVariable::setInitFloatArrayValues(const std::vector<float> & values)
{
    initFloatArrayValues = values;
    initIntArrayValues.clear();
    inBSSSection = std::all_of(initFloatArrayValues.begin(), initFloatArrayValues.end(), [](float value) {
        return value == 0.0f;
    });
    initKind = inBSSSection ? InitKind::Zero : InitKind::FloatArray;
}

const std::vector<float> & GlobalVariable::getInitFloatArrayValues() const
{
    return initFloatArrayValues;
}

/// @brief 获取初始化类别
/// @return 初始化类别
GlobalVariable::InitKind GlobalVariable::getInitKind() const
{
    return initKind;
}

/// @brief 输出 declare 语句文本
/// @param str 输出字符串
void GlobalVariable::toDeclareString(std::string & str)
{
    str = "declare " + getValueType()->toString() + " " + getIRName();
}
