///
/// @file GlobalVariable.h
/// @brief 全局变量描述类
///

#pragma once

#include <cstdint>

#include "GlobalValue.h"

class GlobalVariable : public GlobalValue {

public:
    enum class InitKind {
        Zero,
        Int,
        Float,
    };

public:
    explicit GlobalVariable(Type * _type, std::string _name) : GlobalValue(_type, std::move(_name))
    {
        setAlignment(4);
    }

    [[nodiscard]] bool isGlobalVarible() const override
    {
        return true;
    }

    [[nodiscard]] bool isInBSSSection() const
    {
        return this->inBSSSection;
    }

    int32_t getScopeLevel() override
    {
        return 0;
    }

    void setInitIntValue(int32_t value)
    {
        initIntValue = value;
        inBSSSection = (value == 0);
        initKind = (value == 0) ? InitKind::Zero : InitKind::Int;
    }

    int32_t getInitIntValue() const
    {
        return initIntValue;
    }

    void setInitFloatValue(float value)
    {
        initFloatValue = value;
        inBSSSection = (value == 0.0f);
        initKind = (value == 0.0f) ? InitKind::Zero : InitKind::Float;
    }

    float getInitFloatValue() const
    {
        return initFloatValue;
    }

    [[nodiscard]] InitKind getInitKind() const
    {
        return initKind;
    }

    void toDeclareString(std::string & str)
    {
        str = "declare " + getType()->toString() + " " + getIRName();
    }

private:
    bool inBSSSection = true;
    int32_t initIntValue = 0;
    float initFloatValue = 0.0f;
    InitKind initKind = InitKind::Zero;
};
