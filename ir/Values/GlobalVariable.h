///
/// @file GlobalVariable.h
/// @brief 全局变量描述类
///

#pragma once

#include <cstdint>

#include "GlobalValue.h"

class GlobalVariable : public GlobalValue {

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

    int32_t getLoadRegId()
    {
        return this->loadRegNo;
    }

    void setLoadRegId(int32_t regId)
    {
        this->loadRegNo = regId;
    }

    void setInitIntValue(int32_t value)
    {
        initIntValue = value;
        inBSSSection = (value == 0);
    }

    int32_t getInitIntValue() const
    {
        return initIntValue;
    }

    void toDeclareString(std::string & str)
    {
        str = "declare " + getType()->toString() + " " + getIRName();
    }

private:
    int32_t loadRegNo = -1;
    bool inBSSSection = true;
    int32_t initIntValue = 0;
};
