///
/// @file ArrayType.h
/// @brief 数组类型描述类
///

#pragma once

#include "StorageSet.h"
#include "Type.h"

class ArrayType : public Type {

    struct ArrayTypeHasher final {
        size_t operator()(const ArrayType & type) const noexcept
        {
            size_t h1 = std::hash<const Type *>{}(type.getElementType());
            size_t h2 = std::hash<int32_t>{}(type.getNumElements());
            return h1 ^ (h2 << 1U);
        }
    };

    struct ArrayTypeEqual final {
        bool operator()(const ArrayType & lhs, const ArrayType & rhs) const noexcept
        {
            return lhs.getElementType() == rhs.getElementType() &&
                   lhs.getNumElements() == rhs.getNumElements();
        }
    };

public:
    ArrayType(Type * elementType, int32_t numElements)
        : Type(ArrayTyID), elementType(elementType), numElements(numElements)
    {}

    [[nodiscard]] Type * getElementType() const
    {
        return elementType;
    }

    [[nodiscard]] int32_t getNumElements() const
    {
        return numElements;
    }

    [[nodiscard]] int32_t getSize() const override
    {
        return elementType->getSize() * numElements;
    }

    static ArrayType * get(Type * elementType, int32_t numElements)
    {
        static StorageSet<ArrayType, ArrayTypeHasher, ArrayTypeEqual> storageSet;
        return const_cast<ArrayType *>(storageSet.get(elementType, numElements));
    }

    [[nodiscard]] std::string toString() const override
    {
        return "[" + std::to_string(numElements) + " x " + elementType->toString() + "]";
    }

private:
    Type * elementType = nullptr;
    int32_t numElements = 0;
};
