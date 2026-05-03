///
/// @file GlobalVariable.h
/// @brief 全局变量描述类
///

#pragma once

#include <vector>
#include <cstdint>
#include <vector>

#include "GlobalValue.h"
#include "PointerType.h"

class GlobalVariable : public GlobalValue {

public:
    enum class InitKind {
        Zero,
        Int,
        Float,
        IntArray,
        FloatArray,
    };

public:
    explicit GlobalVariable(Type * _type, std::string _name);

    [[nodiscard]] Type * getValueType() const;

    [[nodiscard]] bool isGlobalVarible() const override;

    [[nodiscard]] bool isInBSSSection() const;

    int32_t getScopeLevel() override;

    void setInitIntValue(int32_t value);

    int32_t getInitIntValue() const;

    void setInitFloatValue(float value);

    float getInitFloatValue() const;

    void setInitIntArray(const std::vector<int32_t> & values);

    const std::vector<int32_t> & getInitIntArray() const;

    void setInitFloatArray(const std::vector<float> & values);

    const std::vector<float> & getInitFloatArray() const;

    [[nodiscard]] InitKind getInitKind() const;

    void toDeclareString(std::string & str);

private:
    Type * valueType = nullptr;
    bool inBSSSection = true;
    int32_t initIntValue = 0;
    float initFloatValue = 0.0f;
    std::vector<int32_t> initIntArray;
    std::vector<float> initFloatArray;
    InitKind initKind = InitKind::Zero;
};
