///
/// @file LocalMemoryAnalysis.h
/// @brief 非逃逸局部内存对象分析与统一键抽象
///

#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

class AllocaInst;
class Function;
struct MemoryLocation;

enum class LocalMemoryKeyKind : std::int8_t {
    /// @brief 精确到常量索引槽位的键
    PreciseLocation,
    /// @brief 对象上任意写入都会更新的摘要键
    ObjectAnyStore,
    /// @brief 仅不精确写入会更新的摘要键
    ObjectImpreciseStore,
    /// @brief 表示后续存在对象级不精确读取的活跃摘要键
    ObjectReadSummary,
};

struct LocalMemoryKey {
    /// @brief 被跟踪的局部对象
    AllocaInst * object = nullptr;

    /// @brief 精确槽位的常量下标路径
    std::vector<int32_t> indices;

    /// @brief 当前键代表的局部内存语义类别
    LocalMemoryKeyKind kind = LocalMemoryKeyKind::PreciseLocation;

    [[nodiscard]] bool isValid() const
    {
        return object != nullptr;
    }

    [[nodiscard]] bool operator==(const LocalMemoryKey & other) const
    {
        return object == other.object && kind == other.kind && indices == other.indices;
    }
};

struct LocalMemoryKeyHash final {
    std::size_t operator()(const LocalMemoryKey & key) const noexcept;
};

/// @brief 由精确内存位点构造统一键
/// @param location 已归一化的精确内存位点
/// @return 对应的精确槽位键，若位点不精确则返回无效键
LocalMemoryKey makePreciseLocalMemoryKey(const MemoryLocation & location);

/// @brief 构造对象级任意写入摘要键
/// @param object 被跟踪的局部对象
/// @return 对象任意 store 都会更新的摘要键
LocalMemoryKey makeObjectAnyStoreLocalMemoryKey(AllocaInst * object);

/// @brief 构造对象级不精确写入摘要键
/// @param object 被跟踪的局部对象
/// @return 仅 imprecise store 会更新的摘要键
LocalMemoryKey makeObjectImpreciseStoreLocalMemoryKey(AllocaInst * object);

/// @brief 构造对象级读取活跃摘要键
/// @param object 被跟踪的局部对象
/// @return 表示后续存在不精确读取的活跃摘要键
LocalMemoryKey makeObjectReadSummaryLocalMemoryKey(AllocaInst * object);

/// @brief 判断统一键是否属于指定对象
/// @param key 待检查键
/// @param object 目标对象
/// @return true 表示键属于该对象
bool localMemoryKeyBelongsToObject(const LocalMemoryKey & key, AllocaInst * object);

class LocalMemoryAnalysis {
public:
    /// @brief 收集函数中可被局部内存优化安全跟踪的非逃逸 alloca
    /// @param function 待分析函数
    explicit LocalMemoryAnalysis(Function * function);

    /// @brief 取得所有可跟踪的非逃逸局部对象
    /// @return trackable alloca 集合
    [[nodiscard]] const std::unordered_set<AllocaInst *> & getTrackableAllocas() const;

    /// @brief 判断内存位点是否属于可跟踪对象
    /// @param location 待检查位点
    /// @return true 表示该位点属于非逃逸局部对象
    [[nodiscard]] bool isTrackableLocation(const MemoryLocation & location) const;

private:
    std::unordered_set<AllocaInst *> trackableAllocas;
};