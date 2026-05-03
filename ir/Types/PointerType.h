///
/// @file PointerType.h
/// @brief 指针类型描述类
///

#pragma once

#include "StorageSet.h"
#include "Type.h"

class PointerType : public Type {

    struct PointerTypeHasher final {
        size_t operator()(const PointerType & type) const noexcept;
    };

    struct PointerTypeEqual final {
        size_t operator()(const PointerType & lhs, const PointerType & rhs) const noexcept;
    };

public:
    explicit PointerType(const Type * pointeeType);

    [[nodiscard]] const Type * getRootType() const;

    [[nodiscard]] const Type * getPointeeType() const;

    [[nodiscard]] int32_t getDepth() const;

    static const PointerType * get(Type * pointee);

    [[nodiscard]] int32_t getSize() const override;

    [[nodiscard]] std::string toString() const override;

private:
    const Type * pointeeType = nullptr;
    const Type * rootType = nullptr;
    int32_t depth = 1;
};
