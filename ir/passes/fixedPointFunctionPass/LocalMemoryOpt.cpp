///
/// @file LocalMemoryOpt.cpp
/// @brief 局部内存访问优化 pass 实现
///

#include "LocalMemoryOpt.h"

#include <algorithm>
#include <iterator>
#include <unordered_map>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "DominatorTree.h"
#include "Function.h"
#include "Instruction.h"
#include "LocalMemoryAnalysis.h"
#include "LoadInst.h"
#include "MemoryLocation.h"
#include "StoreInst.h"
#include "Value.h"

namespace {

using AvailableValueMap = std::unordered_map<MemoryLocation, Value *, MemoryLocationHash>;
using PendingStoreMap = std::unordered_map<MemoryLocation, StoreInst *, MemoryLocationHash>;
using LiveMemoryKeySet = std::unordered_set<LocalMemoryKey, LocalMemoryKeyHash>;
using ReachableBlockSet = std::unordered_set<BasicBlock *>;
using BlockAvailableStateMap = std::unordered_map<BasicBlock *, AvailableValueMap>;
using BlockLiveStateMap = std::unordered_map<BasicBlock *, LiveMemoryKeySet>;

/// @brief 判断位点是否属于可跟踪的非逃逸局部对象
/// @param location 待检查位点
/// @param trackableAllocas 非逃逸 alloca 集合
/// @return true 表示底层对象可被本 pass 跟踪
bool isTrackableObject(const MemoryLocation & location,
                       const std::unordered_set<AllocaInst *> & trackableAllocas)
{
    return location.object != nullptr && trackableAllocas.find(location.object) != trackableAllocas.end();
}

/// @brief 删除状态表中与给定位点可能别名的全部条目
/// @tparam MapT 以 MemoryLocation 为键的状态表类型
/// @param state 待更新的状态表
/// @param location 当前访问位点
template <typename MapT>
void eraseMayAliasEntries(MapT & state, const MemoryLocation & location)
{
    if (!location.isKnownObject()) {
        state.clear();
        return;
    }

    for (auto it = state.begin(); it != state.end();) {
        if (classifyMemoryAlias(location, it->first) == MemoryAliasResult::NoAlias) {
            ++it;
            continue;
        }

        it = state.erase(it);
    }
}

/// @brief 删除对象上全部待定 store 候选而不视为 dead
/// @param object 目标对象
/// @param pendingStores 当前候选 dead store 集合
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

/// @brief 比较两个可用值状态是否完全一致
/// @param lhs 左状态
/// @param rhs 右状态
/// @return true 表示两者等价
bool isSameAvailableValueMap(const AvailableValueMap & lhs, const AvailableValueMap & rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (const auto & [location, value] : lhs) {
        auto it = rhs.find(location);
        if (it == rhs.end() || it->second != value) {
            return false;
        }
    }

    return true;
}

/// @brief 比较两个活跃内存键集合是否完全一致
/// @param lhs 左集合
/// @param rhs 右集合
/// @return true 表示两者等价
bool isSameLiveMemoryKeySet(const LiveMemoryKeySet & lhs, const LiveMemoryKeySet & rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (const auto & location : lhs) {
        if (rhs.find(location) == rhs.end()) {
            return false;
        }
    }

    return true;
}

/// @brief 判断对象上是否存在对象级不精确读取活跃摘要
/// @param liveKeys 当前活跃内存键集合
/// @param object 目标对象
/// @return true 表示后续仍有不精确读取可能观测到该对象上的写入
bool hasObjectReadSummary(const LiveMemoryKeySet & liveKeys, AllocaInst * object)
{
    return liveKeys.find(makeObjectReadSummaryLocalMemoryKey(object)) != liveKeys.end();
}

/// @brief 判断对象上是否存在任意精确槽位活跃读取
/// @param liveKeys 当前活跃内存键集合
/// @param object 目标对象
/// @return true 表示后续仍有精确读取可能观测到该对象上的写入
bool hasAnyPreciseLiveKeyForObject(const LiveMemoryKeySet & liveKeys, AllocaInst * object)
{
    for (const auto & key : liveKeys) {
        if (key.kind == LocalMemoryKeyKind::PreciseLocation && localMemoryKeyBelongsToObject(key, object)) {
            return true;
        }
    }

    return false;
}

/// @brief 判断一次 store 在 whole-function DSE 中是否仍然活跃
/// @param location 当前写入位点
/// @param liveKeys 当前活跃内存键集合
/// @return true 表示该 store 可能被后续读取观测到
bool isWholeFunctionStoreLive(const MemoryLocation & location, const LiveMemoryKeySet & liveKeys)
{
    if (!location.isKnownObject()) {
        return true;
    }

    if (hasObjectReadSummary(liveKeys, location.object)) {
        return true;
    }

    if (!location.isPrecise()) {
        return hasAnyPreciseLiveKeyForObject(liveKeys, location.object);
    }

    return liveKeys.find(makePreciseLocalMemoryKey(location)) != liveKeys.end();
}

/// @brief 将一次读取加入 whole-function DSE 的活跃集合
/// @param location 被读取位点
/// @param liveKeys 当前活跃内存键集合
void observeWholeFunctionRead(const MemoryLocation & location, LiveMemoryKeySet & liveKeys)
{
    if (!location.isKnownObject()) {
        return;
    }

    if (!location.isPrecise()) {
        liveKeys.insert(makeObjectReadSummaryLocalMemoryKey(location.object));
        return;
    }

    liveKeys.insert(makePreciseLocalMemoryKey(location));
}

/// @brief 应用一次 store 对 whole-function DSE 活跃集合的 kill 影响
/// @param location 当前写入位点
/// @param liveKeys 当前活跃内存键集合
void applyWholeFunctionStoreKill(const MemoryLocation & location, LiveMemoryKeySet & liveKeys)
{
    if (!location.isPrecise()) {
        return;
    }

    liveKeys.erase(makePreciseLocalMemoryKey(location));
}

/// @brief 记录一次 local conservative DSE 视角下的内存读取
/// @param location 被读取的位点
/// @param pendingStores 当前候选 dead store 集合
void observeLocalDeadStoreRead(const MemoryLocation & location, PendingStoreMap & pendingStores)
{
    eraseMayAliasEntries(pendingStores, location);
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

/// @brief 提升只有一次入口 store 的非逃逸 alloca，覆盖 mem2reg 不处理的指针临时槽
///
/// 对于只在入口基本块被 store 一次、且所有 load 都在该 store 之后的非逃逸 alloca，
/// 直接将所有 load 替换为 store 的值操作数，并删除 store 和 alloca。
/// 这能处理 mem2reg 无法提升的指针临时槽场景。
///
/// @param func 待优化函数
/// @param trackableAllocas 非逃逸 alloca 集合
/// @return true 表示至少替换了一条 load 或删除了一条 store/alloca
bool promoteSingleEntryStoreAllocas(Function * func, const std::unordered_set<AllocaInst *> & trackableAllocas)
{
    if (!func || !func->getEntryBlock()) {
        return false;
    }

    bool changed = false;
    BasicBlock * entry = func->getEntryBlock();
    for (auto * alloca : trackableAllocas) {
        if (!alloca || alloca->isDead()) {
            continue;
        }

        StoreInst * onlyStore = nullptr;
        std::vector<LoadInst *> loads;
        bool valid = true;

        for (auto * use : alloca->getUseList()) {
            auto * inst = dynamic_cast<Instruction *>(use->getUser());
            if (!inst || inst->isDead()) {
                continue;
            }

            if (auto * load = dynamic_cast<LoadInst *>(inst)) {
                if (load->getPointerOperand() != alloca) {
                    valid = false;
                    break;
                }
                loads.push_back(load);
                continue;
            }

            if (auto * store = dynamic_cast<StoreInst *>(inst)) {
                if (store->getPointerOperand() != alloca || store->getValueOperand() == alloca) {
                    valid = false;
                    break;
                }
                if (onlyStore != nullptr) {
                    valid = false;
                    break;
                }
                onlyStore = store;
                continue;
            }

            valid = false;
            break;
        }

        if (!valid || !onlyStore || onlyStore->getParentBlock() != entry || loads.empty()) {
            continue;
        }

        auto & entryInsts = entry->getInstructions();
        auto storeIt = std::find(entryInsts.begin(), entryInsts.end(), static_cast<Instruction *>(onlyStore));
        if (storeIt == entryInsts.end()) {
            continue;
        }

        bool storeDominatesLoads = true;
        for (auto * load : loads) {
            if (load->getParentBlock() != entry) {
                continue;
            }

            auto loadIt = std::find(entryInsts.begin(), entryInsts.end(), static_cast<Instruction *>(load));
            if (loadIt != entryInsts.end() && std::distance(entryInsts.begin(), loadIt) < std::distance(entryInsts.begin(), storeIt)) {
                storeDominatesLoads = false;
                break;
            }
        }

        if (!storeDominatesLoads) {
            continue;
        }

        Value * replacement = onlyStore->getValueOperand();
        for (auto * load : loads) {
            load->replaceAllUseWith(replacement);
            load->clearOperands();
            load->setDead(true);
            changed = true;
        }

        onlyStore->clearOperands();
        onlyStore->setDead(true);
        alloca->clearOperands();
        alloca->setDead(true);
        changed = true;
    }

    return changed;
}

/// @brief 收集从入口块可达的基本块集合
/// @param dt 当前函数的支配树分析
/// @return 可达基本块集合
ReachableBlockSet collectReachableBlocks(const DominatorTree & dt)
{
    ReachableBlockSet reachableBlocks;
    for (auto * bb : dt.getRPO()) {
        reachableBlocks.insert(bb);
    }
    return reachableBlocks;
}

/// @brief 计算块入口的可用值状态 meet
/// @param bb 目标基本块
/// @param outStates 前驱块出口状态
/// @param reachableBlocks 当前函数的可达块集合
/// @return 仅保留所有可达前驱一致的可用值
AvailableValueMap meetAvailableValuesAtBlock(
    BasicBlock * bb,
    const BlockAvailableStateMap & outStates,
    const ReachableBlockSet & reachableBlocks)
{
    AvailableValueMap mergedState;
    bool hasReachablePred = false;

    for (auto * pred : bb->getPredecessors()) {
        if (reachableBlocks.find(pred) == reachableBlocks.end()) {
            continue;
        }

        auto predIt = outStates.find(pred);
        const AvailableValueMap * predState = predIt == outStates.end() ? nullptr : &predIt->second;
        if (!hasReachablePred) {
            if (predState != nullptr) {
                mergedState = *predState;
            }
            hasReachablePred = true;
            continue;
        }

        for (auto it = mergedState.begin(); it != mergedState.end();) {
            if (predState == nullptr) {
                it = mergedState.erase(it);
                continue;
            }

            auto valueIt = predState->find(it->first);
            if (valueIt == predState->end() || valueIt->second != it->second) {
                it = mergedState.erase(it);
                continue;
            }

            ++it;
        }
    }

    return mergedState;
}

/// @brief 对一个基本块应用前向可用值 transfer
/// @param bb 目标基本块
/// @param trackableAllocas 非逃逸对象集合
/// @param availableValues 入口状态，返回时为出口状态
void applyAvailableValueTransfer(BasicBlock * bb,
                                 const std::unordered_set<AllocaInst *> & trackableAllocas,
                                 AvailableValueMap & availableValues)
{
    for (auto * inst : bb->getInstructions()) {
        if (inst == nullptr || inst->isDead()) {
            continue;
        }

        if (auto * load = dynamic_cast<LoadInst *>(inst)) {
            MemoryLocation location = normalizeMemoryLocation(load->getPointerOperand());
            if (!isTrackableObject(location, trackableAllocas) || !location.isPrecise()) {
                continue;
            }

            if (availableValues.find(location) == availableValues.end()) {
                availableValues[location] = load;
            }
            continue;
        }

        if (auto * store = dynamic_cast<StoreInst *>(inst)) {
            MemoryLocation location = normalizeMemoryLocation(store->getPointerOperand());
            if (!isTrackableObject(location, trackableAllocas)) {
                continue;
            }

            eraseMayAliasEntries(availableValues, location);
            if (!location.isPrecise()) {
                continue;
            }

            availableValues[location] = store->getValueOperand();
        }
    }
}

/// @brief 计算 whole-function 可用值的块入口/出口状态
/// @param rpo CFG 逆后序列表
/// @param reachableBlocks 可达块集合
/// @param trackableAllocas 非逃逸对象集合
/// @param inStates 输出块入口状态
/// @param outStates 输出块出口状态
void solveAvailableValueDataflow(const std::vector<BasicBlock *> & rpo,
                                 const ReachableBlockSet & reachableBlocks,
                                 const std::unordered_set<AllocaInst *> & trackableAllocas,
                                 BlockAvailableStateMap & inStates,
                                 BlockAvailableStateMap & outStates)
{
    bool changed = false;
    do {
        changed = false;

        for (auto * bb : rpo) {
            AvailableValueMap inState = meetAvailableValuesAtBlock(bb, outStates, reachableBlocks);
            AvailableValueMap outState = inState;
            applyAvailableValueTransfer(bb, trackableAllocas, outState);

            auto inIt = inStates.find(bb);
            if (inIt == inStates.end() || !isSameAvailableValueMap(inIt->second, inState)) {
                inStates[bb] = inState;
                changed = true;
            }

            auto outIt = outStates.find(bb);
            if (outIt == outStates.end() || !isSameAvailableValueMap(outIt->second, outState)) {
                outStates[bb] = outState;
                changed = true;
            }
        }
    } while (changed);
}

/// @brief 按 whole-function 可用值状态重写 load
/// @param func 待优化函数
/// @param trackableAllocas 非逃逸对象集合
/// @param inStates 每个基本块的入口状态
/// @return true 表示至少替换了一条 load
bool rewriteLoadsFromAvailableValues(Function * func,
                                    const std::unordered_set<AllocaInst *> & trackableAllocas,
                                    const BlockAvailableStateMap & inStates)
{
    if (func == nullptr) {
        return false;
    }

    bool changed = false;
    // 转发映射：记录已被替换的 load 指令到其替换值的对应关系，
    // 用于后续 store 的可用值也通过转发链解析到最终源值
    std::unordered_map<Value *, Value *> forwardedValues;
    // 解析转发链：沿 forwardedValues 递归查找最终源值，避免使用已被删除的 load 作为可用值
    auto resolveForwardedValue = [&forwardedValues](Value * value) {
        Value * current = value;
        std::unordered_set<Value *> visited;
        while (current != nullptr && visited.insert(current).second) {
            auto it = forwardedValues.find(current);
            if (it == forwardedValues.end()) {
                break;
            }
            current = it->second;
        }
        return current == nullptr ? value : current;
    };

    for (auto * bb : func->getBlocks()) {
        AvailableValueMap availableValues;
        auto inIt = inStates.find(bb);
        if (inIt != inStates.end()) {
            availableValues = inIt->second;
        }

        for (auto * inst : bb->getInstructions()) {
            if (inst == nullptr || inst->isDead()) {
                continue;
            }

            if (auto * load = dynamic_cast<LoadInst *>(inst)) {
                MemoryLocation location = normalizeMemoryLocation(load->getPointerOperand());
                if (!isTrackableObject(location, trackableAllocas)) {
                    continue;
                }

                if (!location.isPrecise()) {
                    continue;
                }

                auto availableIt = availableValues.find(location);
                if (availableIt != availableValues.end()) {
                    // 将 load 替换为转发解析后的最终可用值
                    Value * replacement = resolveForwardedValue(availableIt->second);
                    load->replaceAllUseWith(replacement);
                    // 记录转发关系，后续 store 遇到该 load 作为可用值时也能解析到最终源值
                    forwardedValues[load] = replacement;
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

                eraseMayAliasEntries(availableValues, location);
                if (!location.isPrecise()) {
                    continue;
                }

                // store 更新可用值时，也通过转发链解析，确保可用值指向最终源值而非已删除的 load
                availableValues[location] = resolveForwardedValue(store->getValueOperand());
            }
        }
    }

    return changed;
}

/// @brief 按 whole-function 可用值状态删除写回同值的冗余 store
/// @param func 待优化函数
/// @param trackableAllocas 非逃逸对象集合
/// @param inStates 每个基本块的入口状态
/// @return true 表示至少删除了一条 store
bool eliminateSameValueStores(Function * func,
                              const std::unordered_set<AllocaInst *> & trackableAllocas,
                              const BlockAvailableStateMap & inStates)
{
    if (func == nullptr) {
        return false;
    }

    bool changed = false;
    for (auto * bb : func->getBlocks()) {
        AvailableValueMap availableValues;
        auto inIt = inStates.find(bb);
        if (inIt != inStates.end()) {
            availableValues = inIt->second;
        }

        for (auto * inst : bb->getInstructions()) {
            if (inst == nullptr || inst->isDead()) {
                continue;
            }

            if (auto * load = dynamic_cast<LoadInst *>(inst)) {
                MemoryLocation location = normalizeMemoryLocation(load->getPointerOperand());
                if (!isTrackableObject(location, trackableAllocas) || !location.isPrecise()) {
                    continue;
                }

                if (availableValues.find(location) == availableValues.end()) {
                    availableValues[location] = load;
                }
                continue;
            }

            if (auto * store = dynamic_cast<StoreInst *>(inst)) {
                MemoryLocation location = normalizeMemoryLocation(store->getPointerOperand());
                if (!isTrackableObject(location, trackableAllocas)) {
                    continue;
                }

                auto availableIt = availableValues.find(location);
                if (location.isPrecise() && availableIt != availableValues.end() &&
                    availableIt->second == store->getValueOperand()) {
                    store->clearOperands();
                    store->setDead(true);
                    changed = true;
                    continue;
                }

                eraseMayAliasEntries(availableValues, location);
                if (!location.isPrecise()) {
                    continue;
                }

                availableValues[location] = store->getValueOperand();
            }
        }
    }

    return changed;
}

/// @brief 块内 conservative dead store 消除
/// @param func 待优化函数
/// @param trackableAllocas 非逃逸对象集合
/// @return true 表示至少删除了一条 store
bool eliminateLocalConservativeDeadStores(Function * func,
                                          const std::unordered_set<AllocaInst *> & trackableAllocas)
{
    if (func == nullptr) {
        return false;
    }

    bool changed = false;
    for (auto * bb : func->getBlocks()) {
        PendingStoreMap pendingStores;

        for (auto * inst : bb->getInstructions()) {
            if (inst == nullptr || inst->isDead()) {
                continue;
            }

            if (auto * load = dynamic_cast<LoadInst *>(inst)) {
                MemoryLocation location = normalizeMemoryLocation(load->getPointerOperand());
                if (!isTrackableObject(location, trackableAllocas)) {
                    continue;
                }

                observeLocalDeadStoreRead(location, pendingStores);
                continue;
            }

            if (auto * store = dynamic_cast<StoreInst *>(inst)) {
                MemoryLocation location = normalizeMemoryLocation(store->getPointerOperand());
                if (!isTrackableObject(location, trackableAllocas)) {
                    continue;
                }

                if (!location.isPrecise()) {
                    erasePendingStoresForObject(pendingStores, location.object);
                    continue;
                }

                auto pendingIt = pendingStores.find(location);
                if (pendingIt != pendingStores.end() && !pendingIt->second->isDead()) {
                    pendingIt->second->clearOperands();
                    pendingIt->second->setDead(true);
                    changed = true;
                }

                pendingStores[location] = store;
            }
        }
    }

    return changed;
}

/// @brief 计算块出口的活跃内存键并集
/// @param bb 目标基本块
/// @param reachableBlocks 可达块集合
/// @param liveInStates 后继块入口活跃键集合
/// @return 块出口活跃集合
LiveMemoryKeySet computeLiveOutAtBlock(BasicBlock * bb,
                                       const ReachableBlockSet & reachableBlocks,
                                       const BlockLiveStateMap & liveInStates)
{
    LiveMemoryKeySet liveOut;
    for (auto * succ : bb->getSuccessors()) {
        if (reachableBlocks.find(succ) == reachableBlocks.end()) {
            continue;
        }

        auto succIt = liveInStates.find(succ);
        if (succIt == liveInStates.end()) {
            continue;
        }

        liveOut.insert(succIt->second.begin(), succIt->second.end());
    }

    return liveOut;
}

/// @brief 对一个基本块应用逆向活跃内存键 transfer
/// @param bb 目标基本块
/// @param trackableAllocas 可做 whole-function DSE 的对象集合
/// @param liveKeys 出口活跃集合，返回时为入口活跃集合
void applyLiveMemoryTransfer(BasicBlock * bb,
                             const std::unordered_set<AllocaInst *> & trackableAllocas,
                             LiveMemoryKeySet & liveKeys)
{
    auto & insts = bb->getInstructions();
    for (auto it = insts.rbegin(); it != insts.rend(); ++it) {
        Instruction * inst = *it;
        if (inst == nullptr || inst->isDead()) {
            continue;
        }

        if (auto * load = dynamic_cast<LoadInst *>(inst)) {
            MemoryLocation location = normalizeMemoryLocation(load->getPointerOperand());
            if (!isTrackableObject(location, trackableAllocas)) {
                continue;
            }

            observeWholeFunctionRead(location, liveKeys);
            continue;
        }

        if (auto * store = dynamic_cast<StoreInst *>(inst)) {
            MemoryLocation location = normalizeMemoryLocation(store->getPointerOperand());
            if (!isTrackableObject(location, trackableAllocas)) {
                continue;
            }

            applyWholeFunctionStoreKill(location, liveKeys);
        }
    }
}

/// @brief 计算 whole-function dead store 所需的逆向活跃集合
/// @param rpo CFG 逆后序列表
/// @param reachableBlocks 可达块集合
/// @param trackableAllocas 可做 whole-function DSE 的对象集合
/// @param liveInStates 输出块入口活跃集合
/// @param liveOutStates 输出块出口活跃集合
void solveLiveMemoryDataflow(const std::vector<BasicBlock *> & rpo,
                             const ReachableBlockSet & reachableBlocks,
                             const std::unordered_set<AllocaInst *> & trackableAllocas,
                             BlockLiveStateMap & liveInStates,
                             BlockLiveStateMap & liveOutStates)
{
    bool changed = false;
    do {
        changed = false;

        for (auto it = rpo.rbegin(); it != rpo.rend(); ++it) {
            BasicBlock * bb = *it;
            LiveMemoryKeySet liveOut = computeLiveOutAtBlock(bb, reachableBlocks, liveInStates);
            LiveMemoryKeySet liveIn = liveOut;
            applyLiveMemoryTransfer(bb, trackableAllocas, liveIn);

            auto outIt = liveOutStates.find(bb);
            if (outIt == liveOutStates.end() || !isSameLiveMemoryKeySet(outIt->second, liveOut)) {
                liveOutStates[bb] = liveOut;
                changed = true;
            }

            auto inIt = liveInStates.find(bb);
            if (inIt == liveInStates.end() || !isSameLiveMemoryKeySet(inIt->second, liveIn)) {
                liveInStates[bb] = liveIn;
                changed = true;
            }
        }
    } while (changed);
}

/// @brief 依据 whole-function 活跃集合删除跟踪对象上的 dead store
/// @param func 待优化函数
/// @param reachableBlocks 可达块集合
/// @param trackableAllocas 可做 whole-function DSE 的对象集合
/// @param liveOutStates 每个基本块的出口活跃集合
/// @return true 表示至少删除了一条 store
bool eliminateWholeFunctionDeadStores(Function * func,
                                      const ReachableBlockSet & reachableBlocks,
                                      const std::unordered_set<AllocaInst *> & trackableAllocas,
                                      const BlockLiveStateMap & liveOutStates)
{
    if (func == nullptr) {
        return false;
    }

    bool changed = false;
    for (auto * bb : func->getBlocks()) {
        if (reachableBlocks.find(bb) == reachableBlocks.end()) {
            continue;
        }

        LiveMemoryKeySet liveKeys;
        auto outIt = liveOutStates.find(bb);
        if (outIt != liveOutStates.end()) {
            liveKeys = outIt->second;
        }

        auto & insts = bb->getInstructions();
        for (auto it = insts.rbegin(); it != insts.rend(); ++it) {
            Instruction * inst = *it;
            if (inst == nullptr || inst->isDead()) {
                continue;
            }

            if (auto * load = dynamic_cast<LoadInst *>(inst)) {
                MemoryLocation location = normalizeMemoryLocation(load->getPointerOperand());
                if (!isTrackableObject(location, trackableAllocas)) {
                    continue;
                }

                observeWholeFunctionRead(location, liveKeys);
                continue;
            }

            if (auto * store = dynamic_cast<StoreInst *>(inst)) {
                MemoryLocation location = normalizeMemoryLocation(store->getPointerOperand());
                if (!isTrackableObject(location, trackableAllocas)) {
                    continue;
                }

                if (!isWholeFunctionStoreLive(location, liveKeys)) {
                    store->clearOperands();
                    store->setDead(true);
                    changed = true;
                    continue;
                }

                applyWholeFunctionStoreKill(location, liveKeys);
            }
        }
    }

    return changed;
}

/// @brief 执行基于 whole-function 可用值的 load/store 化简
/// @param func 待优化函数
/// @param rpo CFG 逆后序列表
/// @param reachableBlocks 可达块集合
/// @param trackableAllocas 非逃逸对象集合
/// @return true 表示至少删除或替换了一条 load/store
bool runAvailableValueOptimizations(Function * func,
                                    const std::vector<BasicBlock *> & rpo,
                                    const ReachableBlockSet & reachableBlocks,
                                    const std::unordered_set<AllocaInst *> & trackableAllocas)
{
    BlockAvailableStateMap inAvailableStates;
    BlockAvailableStateMap outAvailableStates;
    solveAvailableValueDataflow(rpo, reachableBlocks, trackableAllocas, inAvailableStates, outAvailableStates);

    bool changed = false;
    changed = rewriteLoadsFromAvailableValues(func, trackableAllocas, inAvailableStates) || changed;
    changed = eliminateSameValueStores(func, trackableAllocas, inAvailableStates) || changed;
    return changed;
}

/// @brief 按 local/global 两层职责执行 dead store elimination
/// @param func 待优化函数
/// @param rpo CFG 逆后序列表
/// @param reachableBlocks 可达块集合
/// @param trackableAllocas 可安全参与 local/global DSE 的对象集合
/// @return true 表示至少删除了一条 store
bool eliminateDeadStores(Function * func,
                         const std::vector<BasicBlock *> & rpo,
                         const ReachableBlockSet & reachableBlocks,
                         const std::unordered_set<AllocaInst *> & trackableAllocas)
{
    bool changed = false;

    if (trackableAllocas.empty()) {
        return changed;
    }

    changed = eliminateLocalConservativeDeadStores(func, trackableAllocas) || changed;

    BlockLiveStateMap liveInStates;
    BlockLiveStateMap liveOutStates;
    solveLiveMemoryDataflow(rpo, reachableBlocks, trackableAllocas, liveInStates, liveOutStates);
    changed = eliminateWholeFunctionDeadStores(func, reachableBlocks, trackableAllocas, liveOutStates)
              || changed;
    return changed;
}

} // namespace

/// @brief 构造局部内存优化器
/// @param _func 待优化函数
LocalMemoryOpt::LocalMemoryOpt(Function * _func) : func(_func)
{}

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

    LocalMemoryAnalysis localMemory(func);
    const auto & trackableAllocas = localMemory.getTrackableAllocas();
    if (trackableAllocas.empty()) {
        return false;
    }

    DominatorTree dt(func);
    const auto & rpo = dt.getRPO();
    const ReachableBlockSet reachableBlocks = collectReachableBlocks(dt);
    bool changed = false;
    // 先提升只有一次入口 store 的 alloca，覆盖 mem2reg 不处理的指针临时槽
    changed = promoteSingleEntryStoreAllocas(func, trackableAllocas) || changed;

    changed = runAvailableValueOptimizations(func, rpo, reachableBlocks, trackableAllocas) || changed;
    changed = eliminateDeadStores(func, rpo, reachableBlocks, trackableAllocas) || changed;

    return sweepDeadInstructions() || changed;
}
