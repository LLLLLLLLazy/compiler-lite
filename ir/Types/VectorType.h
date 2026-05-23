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

    /// RVV vscale 类型运行期长度未知，栈槽按常见 VLEN 上界保守预留。
    [[nodiscard]] int32_t getSize() const override
    {
        return 256;
    }

    static VectorType * get(Type * elementType);

    [[nodiscard]] std::string toString() const override;

private:
    Type * elementType = nullptr;
};
