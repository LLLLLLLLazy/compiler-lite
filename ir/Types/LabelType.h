///
/// @file LabelType.h
/// @brief Label名称符号类
///

#pragma once

#include "Type.h"

class LabelType final : public Type {

public:
    static LabelType * getType();

    [[nodiscard]] std::string toString() const override
    {
        return "void";
    }

private:
    explicit LabelType() : Type(Type::LabelTyID)
    {}
};
