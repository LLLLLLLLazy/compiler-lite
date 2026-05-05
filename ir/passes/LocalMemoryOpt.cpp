///
/// @file LocalMemoryOpt.cpp
/// @brief 单基本块局部内存访问优化 pass 实现
///

#include "LocalMemoryOpt.h"

#include <unordered_map>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "Function.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "MemoryLocation.h"
#include "StoreInst.h"
#include "Value.h"

namespace {

using AvailableValueMap = std::unordered_map<MemoryLocation, Value *, MemoryLocationHash>;
using PendingStoreMap = std::unordered_map<MemoryLocation, StoreInst *, MemoryLocationHash>;

/// @brief 判断位点是否属于可跟踪的非逃逸局部对象
/// @param location 待检查位点
/// @param trackableAllocas 非逃逸 alloca 集合
/// @return true 表示底层对象可被本 pass 跟踪
bool isTrackableObject(const MemoryLocation & location,
                       const std::unordered_set<AllocaInst *> & trackableAllocas)
{
    return location.object != nullptr && trackableAllocas.find(location.object) != trackableAllocas.end();
}

/// @brief 删除某个对象上的全部可用值缓存
/// @param availableValues 当前已知可复用值
/// @param object 目标对象
void eraseAvailableValuesForObject(AvailableValueMap & availableValues, AllocaInst * object)
{
    for (auto it = availableValues.begin(); it != availableValues.end();) {
        if (it->first.object == object) {
            it = availableValues.erase(it);
            continue;
        }
        ++it;
    }
}

/// @brief 删除某个对象上的全部待定 store
/// @param pendingStores 当前候选 dead store 集合
/// @param object 目标对象
void erasePendingStoresForObject(PendingStoreMap & pendingStores, AllocaInst * object)
{
    for (auto it = pendingStores.begin(); it != pendingStores.end();) {
        if (it->first.object == object) {
            it = pendingStores.erase(it);
            continue;
        }
        ++it;
    }
}

/// @brief 清除某个对象上的全部局部内存状态
/// @param availableValues 当前已知可复用值
/// @param pendingStores 当前候选 dead store 集合
/// @param object 目标对象
void clearObjectState(AvailableValueMap & availableValues,
                      PendingStoreMap & pendingStores,
                      AllocaInst * object)
{
    eraseAvailableValuesForObject(availableValues, object);
    erasePendingStoresForObject(pendingStores, object);
}

/// @brief 记录一次潜在内存读取，避免将已被读取的 store 当作 dead store
/// @param location 被读取的位点
/// @param pendingStores 当前候选 dead store 集合
void observeRead(const MemoryLocation & location, PendingStoreMap & pendingStores)
{
    if (!location.isKnownObject()) {
        return;
    }

    if (!location.isPrecise()) {
        erasePendingStoresForObject(pendingStores, location.object);
        return;
    }

    pendingStores.erase(location);
}

/// @brief 从函数中清扫已标记为 dead 的指令
/// @param func 待清扫函数
/// @return true 表示至少移除了一条指令
bool sweepDeadInstructions(Function * func)
{
    if (func == nullptr) {
        return false;
    }

    bool removed = false;
    for (auto * bb : func->getBlocks()) {
        auto & insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end();) {
            Instruction * inst = *it;
            if (!inst->isDead()) {
                ++it;
                continue;
            }

            auto next = std::next(it);
            insts.erase(it);
            delete inst;
            it = next;
            removed = true;
        }
    }

    return removed;
}

} // namespace

/// @brief 构造局部内存优化器
/// @param _func 待优化函数
LocalMemoryOpt::LocalMemoryOpt(Function * _func) : func(_func)
{}

/// @brief 收集可参与局部内存优化的非逃逸 alloca
/// @return 非逃逸局部对象集合
std::unordered_set<AllocaInst *> LocalMemoryOpt::collectTrackableAllocas() const
{
    std::unordered_set<AllocaInst *> trackableAllocas;
    if (!func) {
        return trackableAllocas;
    }

    for (auto * bb : func->getBlocks()) {
        for (auto * inst : bb->getInstructions()) {
            auto * alloca = dynamic_cast<AllocaInst *>(inst);
            if (alloca == nullptr || doesPointerEscape(alloca)) {
                continue;
            }
            trackableAllocas.insert(alloca);
        }
    }

    return trackableAllocas;
}

/// @brief 清扫已标记为 dead 的 load/store
/// @return true 表示至少删除了一条指令
bool LocalMemoryOpt::sweepDeadInstructions() const
{
    return ::sweepDeadInstructions(func);
}

/// @brief 对函数原地执行局部 load/store 优化
/// @return 若本轮修改了 IR 则返回 true
bool LocalMemoryOpt::run()
{
    if (!func || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    const auto trackableAllocas = collectTrackableAllocas();
    if (trackableAllocas.empty()) {
        return false;
    }

    bool changed = false;

    for (auto * bb : func->getBlocks()) {

        // 记录当前可用于同址 store-to-load forwarding 的值
        AvailableValueMap availableValues;

        // 记录当前候选 dead store（即尚未被同址 load 读取过的 store）
        PendingStoreMap pendingStores;

        for (auto * inst : bb->getInstructions()) {
            if (auto * load = dynamic_cast<LoadInst *>(inst)) {
                MemoryLocation location = normalizeMemoryLocation(load->getPointerOperand());
                if (!isTrackableObject(location, trackableAllocas)) {
                    continue;
                }

                observeRead(location, pendingStores);
                if (!location.isPrecise()) {
                    continue;
                }

                // 同址 store-to-load forwarding
                auto availableIt = availableValues.find(location);
                if (availableIt != availableValues.end()) {
                    load->replaceAllUseWith(availableIt->second);
                    load->clearOperands();
                    load->setDead(true);
                    changed = true;
                    continue;
                }

                availableValues[location] = load;
                continue;
            }

            if (auto * store = dynamic_cast<StoreInst *>(inst)) {
                MemoryLocation location = normalizeMemoryLocation(store->getPointerOperand());
                if (!isTrackableObject(location, trackableAllocas)) {
                    continue;
                }

                if (!location.isPrecise()) {
                    clearObjectState(availableValues, pendingStores, location.object);
                    continue;
                }

                // 冗余 store 消除
                auto pendingIt = pendingStores.find(location);
                if (pendingIt != pendingStores.end() && !pendingIt->second->isDead()) {
                    pendingIt->second->clearOperands();
                    pendingIt->second->setDead(true);
                    changed = true;
                }

                availableValues[location] = store->getValueOperand();
                pendingStores[location] = store;
                continue;
            }

            if (inst->getOp() == IRInstOperator::IRINST_OP_CALL) {
                availableValues.clear();
                pendingStores.clear();
            }
        }
    }

    return sweepDeadInstructions() || changed;
}