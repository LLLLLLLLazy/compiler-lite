///
/// @file MemVariable.h
/// @brief 内存变量，用于栈内变量的描述。用于后端处理
///

#pragma once

#include "Value.h"

class MemVariable : public Value {

    friend class Function;

private:
    explicit MemVariable(Type * _type) : Value(_type)
    {}

public:
    bool getMemoryAddr(int32_t * _regId = nullptr, int64_t * _offset = nullptr) override
    {
        if (_regId) {
            *_regId = this->baseRegNo;
        }
        if (_offset) {
            *_offset = this->offset;
        }

        return true;
    }

    int32_t getLoadRegId() override
    {
        return this->loadRegNo;
    }

    void setLoadRegId(int32_t regId) override
    {
        this->loadRegNo = regId;
    }

    void setMemoryAddr(int32_t _regId, int64_t _offset)
    {
        baseRegNo = _regId;
        offset = _offset;
    }

private:
    int32_t offset = 0;
    int32_t baseRegNo = -1;
    std::string baseRegName;
    int32_t loadRegNo = -1;
};
