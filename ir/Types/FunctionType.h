///
/// @file FunctionType.h
/// @brief 函数类型类，主要有函数返回值类型以及各个形参类型组成
///

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "Type.h"

class FunctionType final : public Type {
public:
    FunctionType(Type * _retType, std::vector<Type *> _argTypes);

    [[nodiscard]] std::string toString() const override;

    [[nodiscard]] Type * getReturnType() const;

    [[nodiscard]] const std::vector<Type *> & getArgTypes() const;

private:
    Type * retType;
    std::vector<Type *> argTypes;
};
