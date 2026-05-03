///
/// @file FunctionType.cpp
/// @brief 函数类型类，主要由返回值类型以及形参类型组成
///

#include "FunctionType.h"

/// @brief 构造函数类型
/// @param _retType 返回值类型
/// @param _argTypes 形参类型列表
FunctionType::FunctionType(Type * _retType, std::vector<Type *> _argTypes)
    : Type(FunctionTyID), retType{_retType}, argTypes{std::move(_argTypes)}
{}

/// @brief 转为 IR 文本形式
/// @return 函数类型字符串
std::string FunctionType::toString() const
{
    std::string typeStr = retType->toString() + " (*)(";

    bool first = true;
    for (Type * type: argTypes) {
        if (!first) {
            typeStr += ", ";
        }
        typeStr += type->toString();
        first = false;
    }

    typeStr += ")";
    return typeStr;
}

/// @brief 获取返回值类型
/// @return 返回值类型
Type * FunctionType::getReturnType() const
{
    return retType;
}

/// @brief 获取形参类型列表
/// @return 形参类型列表
const std::vector<Type *> & FunctionType::getArgTypes() const
{
    return argTypes;
}
