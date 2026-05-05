///
/// @file MemoryLocation.cpp
/// @brief 局部内存位点归一化与逃逸分析工具实现
///

#include "MemoryLocation.h"

#include <functional>
#include <unordered_set>

#include "AllocaInst.h"
#include "CallInst.h"
#include "ConstInteger.h"
#include "GetElementPtrInst.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "PointerType.h"
#include "ReturnInst.h"
#include "StoreInst.h"
#include "Type.h"
#include "Value.h"

namespace {

/// @brief 组合哈希值
/// @param seed 当前种子
/// @param value 新值哈希
void hashCombine(std::size_t & seed, std::size_t value)
{
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
}

/// @brief 递归归一化指针值
/// @param pointer 待处理指针
/// @param location 输出位点
/// @return true 表示成功定位到底层对象
bool collectMemoryLocation(Value * pointer, MemoryLocation & location)
{
    if (pointer == nullptr) {
        return false;
    }

    if (auto * alloca = dynamic_cast<AllocaInst *>(pointer)) {
        location.object = alloca;
        location.indices.clear();
        location.precise = !alloca->getAllocaType()->isArrayType();
        return true;
    }

    auto * gep = dynamic_cast<GetElementPtrInst *>(pointer);
    if (gep == nullptr) {
        return false;
    }

    MemoryLocation baseLocation;
    if (!collectMemoryLocation(gep->getBasePointer(), baseLocation)) {
        return false;
    }

    location = baseLocation;

    auto * constIndex = dynamic_cast<ConstInteger *>(gep->getIndexOperand());
    if (gep->isArrayDecayGEP()) {
        if (constIndex == nullptr) {
            location.precise = false;
            return true;
        }

        location.indices.push_back(constIndex->getVal());
        auto * resultPtrType = dynamic_cast<const PointerType *>(gep->getType());
        const Type * pointeeType = resultPtrType == nullptr ? nullptr : resultPtrType->getPointeeType();
        location.precise = pointeeType != nullptr && !pointeeType->isArrayType();
        return true;
    }

    if (constIndex == nullptr || constIndex->getVal() != 0) {
        location.precise = false;
    }

    return true;
}

/// @brief 递归判断指针值是否逃逸
/// @param pointer 待检查的指针或派生地址
/// @param visited 去重集合
/// @return true 表示发生逃逸
bool valueEscapes(Value * pointer, std::unordered_set<Value *> & visited)
{
    if (pointer == nullptr) {
        return false;
    }

    if (!visited.insert(pointer).second) {
        return false;
    }

    for (auto * use : pointer->getUseList()) {
        auto * user = use->getUser();
        auto * inst = dynamic_cast<Instruction *>(user);
        if (inst == nullptr) {
            return true;
        }

        if (auto * load = dynamic_cast<LoadInst *>(inst)) {
            if (load->getPointerOperand() != pointer) {
                return true;
            }
            continue;
        }

        if (auto * store = dynamic_cast<StoreInst *>(inst)) {
            if (store->getPointerOperand() == pointer) {
                continue;
            }
            return true;
        }

        if (auto * gep = dynamic_cast<GetElementPtrInst *>(inst)) {
            if (gep->getBasePointer() != pointer) {
                return true;
            }
            if (valueEscapes(gep, visited)) {
                return true;
            }
            continue;
        }

        if (dynamic_cast<CallInst *>(inst) != nullptr || dynamic_cast<ReturnInst *>(inst) != nullptr) {
            return true;
        }

        return true;
    }

    return false;
}

} // namespace

std::size_t MemoryLocationHash::operator()(const MemoryLocation & location) const noexcept
{
    std::size_t seed = std::hash<AllocaInst *>{}(location.object);
    hashCombine(seed, std::hash<bool>{}(location.precise));
    for (int32_t index : location.indices) {
        hashCombine(seed, std::hash<int32_t>{}(index));
    }
    return seed;
}

MemoryLocation normalizeMemoryLocation(Value * pointer)
{
    MemoryLocation location;
    if (!collectMemoryLocation(pointer, location)) {
        return {};
    }
    return location;
}

MemoryAliasResult classifyMemoryAlias(const MemoryLocation & lhs, const MemoryLocation & rhs)
{
    if (!lhs.isKnownObject() || !rhs.isKnownObject()) {
        return MemoryAliasResult::MayAlias;
    }

    if (lhs.object != rhs.object) {
        return MemoryAliasResult::NoAlias;
    }

    if (!lhs.isPrecise() || !rhs.isPrecise()) {
        return MemoryAliasResult::MayAlias;
    }

    return lhs.indices == rhs.indices ? MemoryAliasResult::MustAlias : MemoryAliasResult::NoAlias;
}

bool doesPointerEscape(AllocaInst * alloca)
{
    std::unordered_set<Value *> visited;
    return valueEscapes(alloca, visited);
}