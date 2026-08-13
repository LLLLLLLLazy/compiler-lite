///
/// @file VectorType.h
/// @brief RVV 可伸缩向量类型
///

#pragma once

#include "StorageSet.h"
#include "Type.h"

class VectorType final : public Type {

    struct VectorTypeHasher final {
        size_t operator()(const VectorType & type) const noexcept;
    };

    struct VectorTypeEqual final {
        bool operator()(const VectorType & lhs, const VectorType & rhs) const noexcept;
    };

public:
    explicit VectorType(Type * elementType);

    [[nodiscard]] Type * getElementType() const
    {
        return elementType;
    }

    /// @brief 返回单个 RVV 寄存器在架构允许的最大 VLEN 下所需的 spill 字节数
    /// @return RVV 1.0 最大 65,536 bit 对应的 8,192 字节
    [[nodiscard]] int32_t getSize() const override
    {
        return 8192;
    }

    static VectorType * get(Type * elementType);

    [[nodiscard]] std::string toString() const override;

private:
    Type * elementType = nullptr;
};
