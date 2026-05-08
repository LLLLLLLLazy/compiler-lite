///
/// @file ArrayScalarize.cpp
/// @brief 局部数组标量化 pass 实现
///

#include "ArrayScalarize.h"

#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AllocaInst.h"
#include "ArrayType.h"
#include "BasicBlock.h"
#include "ConstInteger.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "MemoryLocation.h"
#include "StoreInst.h"
#include "Type.h"
#include "Use.h"
#include "Value.h"

namespace {

struct ArrayCandidate {
    /// @brief 数组对象的 alloca 指令，比如 %a = alloca [4 x i32] 这条指令
    AllocaInst * arrayAlloca = nullptr;

    /// @brief 要拆出来的数组元素位置，比如访问了 a[0], a[2]，那么这里记录的就是 a[0], a[2] 这两个位置 (以 MemoryLocation 形式记录)
    std::vector<MemoryLocation> slotLocations;

    /// @brief 每个槽位的类型
    std::unordered_map<MemoryLocation, Type *, MemoryLocationHash> slotTypes;

    /// @brief 新建出来的标量 alloca
    std::unordered_map<MemoryLocation, AllocaInst *, MemoryLocationHash> slotAllocas;

    /// @brief 从数组中派生出来的 GEP，用于后续清理死 GEP
    std::unordered_set<GetElementPtrInst *> derivedGeps;
};

/// @brief 按索引路径稳定排序，避免标量槽位生成顺序不确定
/// @param lhs 左侧位点
/// @param rhs 右侧位点
/// @return true 表示 lhs 应排在 rhs 前面
bool compareMemoryLocation(const MemoryLocation & lhs, const MemoryLocation & rhs)
{
    return std::lexicographical_compare(lhs.indices.begin(),
                                        lhs.indices.end(),
                                        rhs.indices.begin(),
                                        rhs.indices.end());
}

/// @brief 沿索引路径解析数组中的目标类型
/// @param baseType 数组对象类型
/// @param indices 常量下标路径
/// @return 对应位置的类型，失败返回空指针
Type * resolveIndexedType(Type * baseType, const std::vector<int32_t> & indices)
{
    Type * current = baseType;
    for (int32_t index : indices) {
        auto * arrayType = dynamic_cast<ArrayType *>(current);
        if (arrayType == nullptr || index < 0 || index >= arrayType->getNumElements()) {
            return nullptr;
        }
        current = arrayType->getElementType();
    }

    return current;
}

/// @brief 记录一个可标量化的精确数组元素位点
/// @param candidate 当前候选数组
/// @param location 精确内存位点
/// @param accessType 本次 load/store 对应的标量类型
/// @return true 表示位点可被当前候选接纳
bool recordScalarSlot(ArrayCandidate & candidate, const MemoryLocation & location, Type * accessType)
{
    if (!location.isPrecise() || !location.isKnownObject()) {
        return false;
    }

    // 解析并拒绝不支持的类型
    Type * slotType = resolveIndexedType(candidate.arrayAlloca->getAllocaType(), location.indices);
    if (slotType == nullptr || slotType->isArrayType() || slotType->isPointerType() || slotType != accessType) {
        return false;
    }

    auto [it, inserted] = candidate.slotTypes.emplace(location, slotType);
    if (!inserted) {
        return it->second == slotType;
    }

    candidate.slotLocations.push_back(location);
    return true;
}

/// @brief 判断一条 GEP 是否是当前 pass 能理解的形式
/// @param arrayAlloca 底层数组对象
/// @param gep 待检查的 GEP
/// @return true 表示可继续沿该 GEP 链递归标量化
bool isSupportedGEP(AllocaInst * arrayAlloca, GetElementPtrInst * gep)
{
    if (gep == nullptr) {
        return false;
    }

    auto * constIndex = dynamic_cast<ConstInteger *>(gep->getIndexOperand());
    if (gep->isArrayDecayGEP()) {
        if (constIndex == nullptr) {
            return false;
        }
    } else if (constIndex == nullptr || constIndex->getVal() != 0) {
        return false;
    }

    MemoryLocation location = normalizeMemoryLocation(gep);
    if (location.object != arrayAlloca) {
        return false;
    }

    return resolveIndexedType(arrayAlloca->getAllocaType(), location.indices) != nullptr;
}

/// @brief 递归检查数组对象及其派生 GEP 是否都属于安全标量化范围：
///        从数组 alloca 出发，沿着 use-list 递归检查所有使用者，确认整个数组是否都能安全标量化 
/// @param arrayAlloca 底层数组对象
/// @param pointer 当前检查的地址值
/// @param candidate 输出候选信息
/// @param visited DFS 去重集合
/// @return true 表示整个 use 图都可安全标量化
bool collectScalarizableUses(AllocaInst * arrayAlloca,
                             Value * pointer,
                             ArrayCandidate & candidate,
                             std::unordered_set<Value *> & visited)
{
    if (pointer == nullptr) {
        return false;
    }

    if (!visited.insert(pointer).second) {
        return true;
    }

    for (auto * use : pointer->getUseList()) {
        auto * userInst = dynamic_cast<Instruction *>(use->getUser());
        if (userInst == nullptr) {
            return false;
        }

        if (auto * gep = dynamic_cast<GetElementPtrInst *>(userInst)) {
            if (gep->getBasePointer() != pointer || !isSupportedGEP(arrayAlloca, gep)) {
                return false;
            }

            candidate.derivedGeps.insert(gep);
            if (!collectScalarizableUses(arrayAlloca, gep, candidate, visited)) {
                return false;
            }
            continue;
        }

        if (auto * load = dynamic_cast<LoadInst *>(userInst)) {
            if (load->getPointerOperand() != pointer) {
                return false;
            }

            MemoryLocation location = normalizeMemoryLocation(load->getPointerOperand());
            if (location.object != arrayAlloca || !recordScalarSlot(candidate, location, load->getType())) {
                return false;
            }
            continue;
        }

        if (auto * store = dynamic_cast<StoreInst *>(userInst)) {
            if (store->getPointerOperand() != pointer) {
                return false;
            }

            MemoryLocation location = normalizeMemoryLocation(store->getPointerOperand());
            if (location.object != arrayAlloca ||
                !recordScalarSlot(candidate, location, store->getValueOperand()->getType())) {
                return false;
            }
            continue;
        }

        return false;
    }

    return true;
}

/// @brief 判断某个数组 alloca 是否可整对象标量化
/// @param alloca 待检查数组对象
/// @param candidate 输出候选信息
/// @return true 表示该数组对象可以进入第二阶段标量化
bool analyzeCandidate(AllocaInst * alloca, ArrayCandidate & candidate)
{
    if (alloca == nullptr || !alloca->getAllocaType()->isArrayType() || doesPointerEscape(alloca)) {
        return false;
    }

    candidate.arrayAlloca = alloca;

    std::unordered_set<Value *> visited;
    if (!collectScalarizableUses(alloca, alloca, candidate, visited) || candidate.slotLocations.empty()) {
        return false;
    }

    std::sort(candidate.slotLocations.begin(), candidate.slotLocations.end(), compareMemoryLocation);
    return true;
}

/// @brief 在入口块顶部插入新的标量槽位 alloca
/// @param func 待优化函数
/// @param candidate 待重写的数组候选
void createScalarAllocas(Function * func, ArrayCandidate & candidate)
{
    BasicBlock * entry = func == nullptr ? nullptr : func->getEntryBlock();
    if (entry == nullptr) {
        return;
    }

    auto & entryInsts = entry->getInstructions();
    auto insertIt = entryInsts.begin();
    while (insertIt != entryInsts.end() && dynamic_cast<AllocaInst *>(*insertIt) != nullptr) {
        ++insertIt;
    }

    for (const MemoryLocation & location : candidate.slotLocations) {
        auto typeIt = candidate.slotTypes.find(location);
        if (typeIt == candidate.slotTypes.end()) {
            continue;
        }

        auto * slotAlloca = new AllocaInst(func, typeIt->second);
        slotAlloca->setParentBlock(entry);
        entryInsts.insert(insertIt, slotAlloca);
        candidate.slotAllocas.emplace(location, slotAlloca);
    }
}

/// @brief 将精确数组元素的 load/store 改写到新的标量槽位
/// @param func 待优化函数
/// @param candidate 待重写的数组候选
/// @return true 表示至少改写了一条内存访问
bool rewriteArrayAccesses(Function * func, const ArrayCandidate & candidate)
{
    if (func == nullptr) {
        return false;
    }

    bool changed = false;
    for (auto * bb : func->getBlocks()) {
        for (auto * inst : bb->getInstructions()) {
            if (auto * load = dynamic_cast<LoadInst *>(inst)) {
                MemoryLocation location = normalizeMemoryLocation(load->getPointerOperand());
                if (location.object != candidate.arrayAlloca || !location.isPrecise()) {
                    continue;
                }

                auto slotIt = candidate.slotAllocas.find(location);
                if (slotIt == candidate.slotAllocas.end()) {
                    continue;
                }

                if (load->getPointerOperand() != static_cast<Value *>(slotIt->second)) {
                    load->setOperand(0, slotIt->second);
                    changed = true;
                }
                continue;
            }

            auto * store = dynamic_cast<StoreInst *>(inst);
            if (store == nullptr) {
                continue;
            }

            MemoryLocation location = normalizeMemoryLocation(store->getPointerOperand());
            if (location.object != candidate.arrayAlloca || !location.isPrecise()) {
                continue;
            }

            auto slotIt = candidate.slotAllocas.find(location);
            if (slotIt == candidate.slotAllocas.end()) {
                continue;
            }

            if (store->getPointerOperand() != static_cast<Value *>(slotIt->second)) {
                store->setOperand(1, slotIt->second);
                changed = true;
            }
        }
    }

    return changed;
}

/// @brief 清理因重写而失去全部用途的派生 GEP
/// @param func 待优化函数
/// @param candidate 待清理的数组候选
/// @return true 表示至少删除了一条 GEP
bool eraseDeadGeps(Function * func, const ArrayCandidate & candidate)
{
    if (func == nullptr) {
        return false;
    }

    bool changed = false;
    bool removedInRound = false;
    do {
        removedInRound = false;
        for (auto * bb : func->getBlocks()) {
            auto & insts = bb->getInstructions();
            for (auto it = insts.begin(); it != insts.end();) {
                auto * gep = dynamic_cast<GetElementPtrInst *>(*it);
                if (gep == nullptr || candidate.derivedGeps.find(gep) == candidate.derivedGeps.end() ||
                    !gep->getUseList().empty()) {
                    ++it;
                    continue;
                }

                gep->clearOperands();
                auto next = std::next(it);
                insts.erase(it);
                delete gep;
                it = next;
                removedInRound = true;
                changed = true;
            }
        }
    } while (removedInRound);

    return changed;
}

/// @brief 删除已经没有剩余用途的原数组 alloca
/// @param alloca 待删除的数组对象
/// @return true 表示数组 alloca 已被删除
bool eraseDeadArrayAlloca(AllocaInst * alloca)
{
    if (alloca == nullptr || !alloca->getUseList().empty()) {
        return false;
    }

    BasicBlock * parent = alloca->getParentBlock();
    if (parent == nullptr) {
        return false;
    }

    auto & insts = parent->getInstructions();
    for (auto it = insts.begin(); it != insts.end(); ++it) {
        if (*it != alloca) {
            continue;
        }

        alloca->clearOperands();
        insts.erase(it);
        delete alloca;
        return true;
    }

    return false;
}

} // namespace

/// @brief 构造数组标量化 pass
/// @param _func 待优化函数
ArrayScalarize::ArrayScalarize(Function * _func) : func(_func)
{}

/// @brief 将局部数组的常量下标访问拆成独立标量槽位
/// @return true 表示本轮修改了 IR
bool ArrayScalarize::run()
{
    if (!func || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    std::vector<AllocaInst *> arrayAllocas;
    for (auto * bb : func->getBlocks()) {
        for (auto * inst : bb->getInstructions()) {
            auto * alloca = dynamic_cast<AllocaInst *>(inst);
            if (alloca == nullptr || !alloca->getAllocaType()->isArrayType()) {
                continue;
            }
            arrayAllocas.push_back(alloca);
        }
    }

    if (arrayAllocas.empty()) {
        return false;
    }

    bool changed = false;
    for (auto * alloca : arrayAllocas) {
        ArrayCandidate candidate;
        if (!analyzeCandidate(alloca, candidate)) {
            continue;
        }

        createScalarAllocas(func, candidate);
        changed = rewriteArrayAccesses(func, candidate) || changed;
        changed = eraseDeadGeps(func, candidate) || changed;
        changed = eraseDeadArrayAlloca(alloca) || changed;
    }

    return changed;
}