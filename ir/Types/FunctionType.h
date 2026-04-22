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
    FunctionType(Type * _retType, std::vector<Type *> _argTypes)
        : Type(FunctionTyID), retType{_retType}, argTypes{std::move(_argTypes)}
    {}

    [[nodiscard]] std::string toString() const override
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

    [[nodiscard]] Type * getReturnType() const
    {
        return retType;
    }

    [[nodiscard]] const std::vector<Type *> & getArgTypes() const
    {
        return argTypes;
    }

private:
    Type * retType;
    std::vector<Type *> argTypes;
};
