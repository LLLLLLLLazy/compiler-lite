///
/// @file ArrayType.h
/// @brief 数组类型描述类
///

#pragma once

#include "StorageSet.h"
#include "Type.h"

class ArrayType : public Type {

    struct ArrayTypeHasher final {
        size_t operator()(const ArrayType & type) const noexcept;
    };

    struct ArrayTypeEqual final {
        bool operator()(const ArrayType & lhs, const ArrayType & rhs) const noexcept;
    };

public:
    ArrayType(Type * elementType, int32_t numElements);

    [[nodiscard]] Type * getElementType() const;

    [[nodiscard]] int32_t getNumElements() const;

    [[nodiscard]] int32_t getSize() const override;

    static ArrayType * get(Type * elementType, int32_t numElements);

    [[nodiscard]] std::string toString() const override;

private:
    Type * elementType = nullptr;
    int32_t numElements = 0;
};
