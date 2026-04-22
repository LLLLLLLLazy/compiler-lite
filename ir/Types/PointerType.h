///
/// @file PointerType.h
/// @brief 指针类型描述类
///

#pragma once

#include "StorageSet.h"
#include "Type.h"

class PointerType : public Type {

    struct PointerTypeHasher final {
        size_t operator()(const PointerType & type) const noexcept
        {
            return std::hash<const Type *>{}(type.getPointeeType());
        }
    };

    struct PointerTypeEqual final {
        size_t operator()(const PointerType & lhs, const PointerType & rhs) const noexcept
        {
            return lhs.getPointeeType() == rhs.getPointeeType();
        }
    };

public:
    explicit PointerType(const Type * pointeeType) : Type(PointerTyID)
    {
        this->pointeeType = pointeeType;

        if (pointeeType->isPointerType()) {
            Instanceof(pType, const PointerType *, pointeeType);
            this->rootType = pType->getRootType();
            this->depth = pType->getDepth() + 1;
        } else {
            this->rootType = pointeeType;
            this->depth = 1;
        }
    }

    [[nodiscard]] const Type * getRootType() const
    {
        return rootType;
    }

    [[nodiscard]] const Type * getPointeeType() const
    {
        return pointeeType;
    }

    [[nodiscard]] int32_t getDepth() const
    {
        return depth;
    }

    static const PointerType * get(Type * pointee)
    {
        static StorageSet<PointerType, PointerTypeHasher, PointerTypeEqual> storageSet;
        return storageSet.get(pointee);
    }

    [[nodiscard]] std::string toString() const override
    {
        return pointeeType->toString() + "*";
    }

private:
    const Type * pointeeType = nullptr;
    const Type * rootType = nullptr;
    int32_t depth = 1;
};
