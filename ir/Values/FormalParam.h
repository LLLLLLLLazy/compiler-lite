///
/// @file FormalParam.h
/// @brief 函数形参描述类
///

#pragma once

#include "Value.h"

class FormalParam : public Value {

public:
    FormalParam(Type * _type, std::string _name) : Value(_type)
    {
        this->name = std::move(_name);
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

    void setRegId(int32_t _regId)
    {
        this->regId = _regId;
    }

private:
    int32_t regId = -1;
    int32_t offset = 0;
    int32_t baseRegNo = -1;
    std::string baseRegName;
    int32_t loadRegNo = -1;
};
