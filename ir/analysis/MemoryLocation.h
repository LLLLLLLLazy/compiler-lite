///
/// @file MemoryLocation.h
/// @brief 局部内存位点归一化与逃逸分析工具
///

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class AllocaInst;
class Value;

enum class MemoryAliasResult : std::int8_t {
    NoAlias,
    MayAlias,
    MustAlias,
};

struct MemoryLocation {
    /// @brief 底层对象，通常为 alloca 指令
    AllocaInst * object = nullptr;

    /// @brief 沿着数组层级走到哪个常量下标路径. 比如二维数组 arr[2][3] 的 arr[1][2] 位点的 indices 就是 {1, 2}
    std::vector<int32_t> indices;

    /// @brief 是否精确识别到一个具体内存位点
    bool precise = false;

    [[nodiscard]] bool isKnownObject() const
    {
        return object != nullptr;
    }

    [[nodiscard]] bool isPrecise() const
    {
        return object != nullptr && precise;
    }

    [[nodiscard]] bool operator==(const MemoryLocation & other) const
    {
        return object == other.object && precise == other.precise && indices == other.indices;
    }
};

struct MemoryLocationHash final {
    std::size_t operator()(const MemoryLocation & location) const noexcept;
};

/// @brief 将指针值规范化成可复用的局部内存位点描述
/// @param pointer 待归一化的指针值
/// @return 归一化后的内存位点. 若无法精确识别，则返回 object 为空或 precise 为 false 的保守结果
MemoryLocation normalizeMemoryLocation(Value * pointer);

/// @brief 对两个局部内存位点做保守别名分类
/// @param lhs 左侧位点
/// @param rhs 右侧位点
/// @return MustAlias / NoAlias / MayAlias
MemoryAliasResult classifyMemoryAlias(const MemoryLocation & lhs, const MemoryLocation & rhs);

/// @brief 判断某个 alloca 或其派生地址是否发生逃逸
/// @param alloca 待检查的局部对象
/// @return true 表示地址逃逸，false 表示仅被局部 load/store/GEP 使用
bool doesPointerEscape(AllocaInst * alloca);