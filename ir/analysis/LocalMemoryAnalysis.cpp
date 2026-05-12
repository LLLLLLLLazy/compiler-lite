///
/// @file LocalMemoryAnalysis.cpp
/// @brief 非逃逸局部内存对象分析与统一键抽象实现
///

#include "LocalMemoryAnalysis.h"

#include <functional>

#include "AllocaInst.h"
#include "Function.h"
#include "Instruction.h"
#include "MemoryLocation.h"

namespace {

/// @brief 组合哈希值
/// @param seed 当前种子
/// @param value 新值哈希
void hashCombine(std::size_t & seed, std::size_t value)
{
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
}

} // namespace

std::size_t LocalMemoryKeyHash::operator()(const LocalMemoryKey & key) const noexcept
{
    std::size_t seed = std::hash<AllocaInst *>{}(key.object);
    hashCombine(seed, std::hash<int32_t>{}(static_cast<int32_t>(key.kind)));
    for (int32_t index : key.indices) {
        hashCombine(seed, std::hash<int32_t>{}(index));
    }
    return seed;
}

LocalMemoryKey makePreciseLocalMemoryKey(const MemoryLocation & location)
{
    if (!location.isPrecise()) {
        return {};
    }

    return {location.object, location.indices, LocalMemoryKeyKind::PreciseLocation};
}

LocalMemoryKey makeObjectAnyStoreLocalMemoryKey(AllocaInst * object)
{
    return {object, {}, LocalMemoryKeyKind::ObjectAnyStore};
}

LocalMemoryKey makeObjectImpreciseStoreLocalMemoryKey(AllocaInst * object)
{
    return {object, {}, LocalMemoryKeyKind::ObjectImpreciseStore};
}

LocalMemoryKey makeObjectReadSummaryLocalMemoryKey(AllocaInst * object)
{
    return {object, {}, LocalMemoryKeyKind::ObjectReadSummary};
}

bool localMemoryKeyBelongsToObject(const LocalMemoryKey & key, AllocaInst * object)
{
    return key.object == object;
}

LocalMemoryAnalysis::LocalMemoryAnalysis(Function * function)
{
    if (function == nullptr) {
        return;
    }

    for (auto * bb : function->getBlocks()) {
        for (auto * inst : bb->getInstructions()) {
            auto * alloca = dynamic_cast<AllocaInst *>(inst);
            if (alloca == nullptr || doesPointerEscape(alloca)) {
                continue;
            }
            trackableAllocas.insert(alloca);
        }
    }
}

const std::unordered_set<AllocaInst *> & LocalMemoryAnalysis::getTrackableAllocas() const
{
    return trackableAllocas;
}

bool LocalMemoryAnalysis::isTrackableLocation(const MemoryLocation & location) const
{
    return location.object != nullptr && trackableAllocas.find(location.object) != trackableAllocas.end();
}