///
/// @file RegVariable.h
/// @brief 寄存器变量类，用于后端
///

#pragma once

#include "Value.h"

class RegVariable : public Value {

public:
    explicit RegVariable(Type * _type, std::string _name, int32_t _reg_no) : Value(_type)
    {
        this->name = std::move(_name);
        regId = _reg_no;
    }

    int32_t getRegId() override
    {
        return regId;
    }

    [[nodiscard]] std::string getIRName() const override
    {
        return name;
    }

private:
    int32_t regId = -1;
};
