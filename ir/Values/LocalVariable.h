///
/// @file LocalVariable.h
/// @brief 局部变量描述的类
///

#pragma once

#include "IRConstant.h"
#include "Value.h"

class LocalVariable : public Value {

    friend class Function;

private:
    explicit LocalVariable(Type * _type, std::string _name, int32_t _scope_level)
        : Value(_type), scope_level(_scope_level)
    {
        this->name = std::move(_name);
    }

public:
    int32_t getScopeLevel() override
    {
        return scope_level;
    }

    int32_t getRegId() override
    {
        return regId;
    }

    bool getMemoryAddr(int32_t * _regId = nullptr, int64_t * _offset = nullptr) override
    {
        if (this->baseRegNo == -1) {
            return false;
        }

        if (_regId) {
            *_regId = this->baseRegNo;
        }
        if (_offset) {
            *_offset = this->offset;
        }

        return true;
    }

    void setMemoryAddr(int32_t _regId, int64_t _offset)
    {
        baseRegNo = _regId;
        offset = _offset;
    }

    int32_t getLoadRegId() override
    {
        return this->loadRegNo;
    }

    void setLoadRegId(int32_t regId) override
    {
        this->loadRegNo = regId;
    }

private:
    int32_t scope_level = -1;
    int32_t regId = -1;
    int32_t offset = 0;
    int32_t baseRegNo = -1;
    std::string baseRegName;
    int32_t loadRegNo = -1;
};
