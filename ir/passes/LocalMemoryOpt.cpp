///
/// @file LocalMemoryOpt.cpp
/// @brief 局部内存访问优化 pass 实现
///

#include "LocalMemoryOpt.h"

#include <iterator>
#include <unordered_map>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "DominatorTree.h"
#include "Function.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "MemoryLocation.h"
#include "StoreInst.h"
#include "Value.h"

namespace {

using AvailableValueMap = std::unordered_map<MemoryLocation, Value *, MemoryLocationHash>;
using PendingStoreMap = std::unordered_map<MemoryLocation, StoreInst *, MemoryLocationHash>;
using LiveLocationSet = std::unordered_set<MemoryLocation, MemoryLocationHash>;
using ReachableBlockSet = std::unordered_set<BasicBlock *>;
using BlockAvailableStateMap = std::unordered_map<BasicBlock *, AvailableValueMap>;
using BlockLiveStateMap = std::unordered_map<BasicBlock *, LiveLocationSet>;

/// @brief 判断位点是否属于可跟踪的非逃逸局部对象
/// @param location 待检查位点
/// @param trackableAllocas 非逃逸 alloca 集合
/// @return true 表示底层对象可被本 pass 跟踪
bool isTrackableObject(const MemoryLocation & location,
                       const std::unordered_set<AllocaInst *> & trackableAllocas)
{
    return location.object != nullptr && trackableAllocas.find(location.object) != trackableAllocas.end();
}

/// @brief 判断位点是否属于可做跨块 dead store 分析的精确槽位
/// @param location 待检查位点
/// @param preciseOnlyAllocas 没有任何不精确访问的对象集合
/// @return true 表示该位点可以参与 whole-function DSE
bool isWholeFunctionDSELocation(const MemoryLocation & location,
                                const std::unordered_set<AllocaInst *> & preciseOnlyAllocas)
{
    return location.isPrecise() && preciseOnlyAllocas.find(location.object) != preciseOnlyAllocas.end();
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

/// @brief 比较两个活跃槽位集合是否完全一致
/// @param lhs 左集合
/// @param rhs 右集合
/// @return true 表示两者等价
bool isSameLiveLocationSet(const LiveLocationSet & lhs, const LiveLocationSet & rhs)
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

/// @brief 记录一次 local conservative DSE 视角下的内存读取
/// @param location 被读取的位点
/// @param pendingStores 当前候选 dead store 集合
void observeLocalDeadStoreRead(const MemoryLocation & location, PendingStoreMap & pendingStores)
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

/// @brief 收集可安全参与 whole-function DSE 的对象
/// @param func 待分析函数
/// @param trackableAllocas 非逃逸对象集合
/// @return 没有任何不精确访问的对象集合
std::unordered_set<AllocaInst *> collectPreciseOnlyAllocas(
    Function * func,
    const std::unordered_set<AllocaInst *> & trackableAllocas)
{
    std::unordered_set<AllocaInst *> preciseOnlyAllocas = trackableAllocas;
    if (func == nullptr) {
        return preciseOnlyAllocas;
    }

    for (auto * bb : func->getBlocks()) {
        for (auto * inst : bb->getInstructions()) {
            if (inst == nullptr || inst->isDead()) {
                continue;
            }

            Value * pointerOperand = nullptr;
            if (auto * load = dynamic_cast<LoadInst *>(inst)) {
                pointerOperand = load->getPointerOperand();
            } else if (auto * store = dynamic_cast<StoreInst *>(inst)) {
                pointerOperand = store->getPointerOperand();
            } else {
                continue;
            }

            MemoryLocation location = normalizeMemoryLocation(pointerOperand);
            if (!isTrackableObject(location, trackableAllocas) || location.isPrecise()) {
                continue;
            }

            preciseOnlyAllocas.erase(location.object);
        }
    }

    return preciseOnlyAllocas;
}

/// @brief 收集需要交给 local conservative DSE 的对象
/// @param trackableAllocas 非逃逸对象集合
/// @param preciseOnlyAllocas 可安全参与 whole-function DSE 的对象集合
/// @return 仅能依赖 local DSE 的对象集合
std::unordered_set<AllocaInst *> collectLocalDSEAllocas(
    const std::unordered_set<AllocaInst *> & trackableAllocas,
    const std::unordered_set<AllocaInst *> & preciseOnlyAllocas)
{
    std::unordered_set<AllocaInst *> localDSEAllocas;
    for (auto * alloca : trackableAllocas) {
        if (preciseOnlyAllocas.find(alloca) == preciseOnlyAllocas.end()) {
            localDSEAllocas.insert(alloca);
        }
    }
    return localDSEAllocas;
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

            if (!location.isPrecise()) {
                eraseAvailableValuesForObject(availableValues, location.object);
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
                    eraseAvailableValuesForObject(availableValues, location.object);
                    continue;
                }

                availableValues[location] = store->getValueOperand();
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

                if (!location.isPrecise()) {
                    eraseAvailableValuesForObject(availableValues, location.object);
                    continue;
                }

                auto availableIt = availableValues.find(location);
                if (availableIt != availableValues.end() && availableIt->second == store->getValueOperand()) {
                    store->clearOperands();
                    store->setDead(true);
                    changed = true;
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
/// @param localDSEAllocas 仅能依赖 local DSE 的对象集合
/// @return true 表示至少删除了一条 store
bool eliminateLocalConservativeDeadStores(Function * func,
                                          const std::unordered_set<AllocaInst *> & localDSEAllocas)
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
                if (!isTrackableObject(location, localDSEAllocas)) {
                    continue;
                }

                observeLocalDeadStoreRead(location, pendingStores);
                continue;
            }

            if (auto * store = dynamic_cast<StoreInst *>(inst)) {
                MemoryLocation location = normalizeMemoryLocation(store->getPointerOperand());
                if (!isTrackableObject(location, localDSEAllocas)) {
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

/// @brief 计算块出口的活跃精确槽位并集
/// @param bb 目标基本块
/// @param reachableBlocks 可达块集合
/// @param liveInStates 后继块入口活跃集合
/// @return 块出口活跃集合
LiveLocationSet computeLiveOutAtBlock(BasicBlock * bb,
                                      const ReachableBlockSet & reachableBlocks,
                                      const BlockLiveStateMap & liveInStates)
{
    LiveLocationSet liveOut;
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

/// @brief 对一个基本块应用逆向活跃槽位 transfer
/// @param bb 目标基本块
/// @param preciseOnlyAllocas 可做 whole-function DSE 的对象集合
/// @param liveLocations 出口活跃集合，返回时为入口活跃集合
void applyLiveLocationTransfer(BasicBlock * bb,
                               const std::unordered_set<AllocaInst *> & preciseOnlyAllocas,
                               LiveLocationSet & liveLocations)
{
    auto & insts = bb->getInstructions();
    for (auto it = insts.rbegin(); it != insts.rend(); ++it) {
        Instruction * inst = *it;
        if (inst == nullptr || inst->isDead()) {
            continue;
        }

        if (auto * load = dynamic_cast<LoadInst *>(inst)) {
            MemoryLocation location = normalizeMemoryLocation(load->getPointerOperand());
            if (!isWholeFunctionDSELocation(location, preciseOnlyAllocas)) {
                continue;
            }

            liveLocations.insert(location);
            continue;
        }

        if (auto * store = dynamic_cast<StoreInst *>(inst)) {
            MemoryLocation location = normalizeMemoryLocation(store->getPointerOperand());
            if (!isWholeFunctionDSELocation(location, preciseOnlyAllocas)) {
                continue;
            }

            liveLocations.erase(location);
        }
    }
}

/// @brief 计算 whole-function dead store 所需的逆向活跃集合
/// @param rpo CFG 逆后序列表
/// @param reachableBlocks 可达块集合
/// @param preciseOnlyAllocas 可做 whole-function DSE 的对象集合
/// @param liveInStates 输出块入口活跃集合
/// @param liveOutStates 输出块出口活跃集合
void solveLiveLocationDataflow(const std::vector<BasicBlock *> & rpo,
                               const ReachableBlockSet & reachableBlocks,
                               const std::unordered_set<AllocaInst *> & preciseOnlyAllocas,
                               BlockLiveStateMap & liveInStates,
                               BlockLiveStateMap & liveOutStates)
{
    bool changed = false;
    do {
        changed = false;

        for (auto it = rpo.rbegin(); it != rpo.rend(); ++it) {
            BasicBlock * bb = *it;
            LiveLocationSet liveOut = computeLiveOutAtBlock(bb, reachableBlocks, liveInStates);
            LiveLocationSet liveIn = liveOut;
            applyLiveLocationTransfer(bb, preciseOnlyAllocas, liveIn);

            auto outIt = liveOutStates.find(bb);
            if (outIt == liveOutStates.end() || !isSameLiveLocationSet(outIt->second, liveOut)) {
                liveOutStates[bb] = liveOut;
                changed = true;
            }

            auto inIt = liveInStates.find(bb);
            if (inIt == liveInStates.end() || !isSameLiveLocationSet(inIt->second, liveIn)) {
                liveInStates[bb] = liveIn;
                changed = true;
            }
        }
    } while (changed);
}

/// @brief 依据 whole-function 活跃集合删除 precise dead store
/// @param func 待优化函数
/// @param reachableBlocks 可达块集合
/// @param preciseOnlyAllocas 可做 whole-function DSE 的对象集合
/// @param liveOutStates 每个基本块的出口活跃集合
/// @return true 表示至少删除了一条 store
bool eliminateGlobalPreciseDeadStores(Function * func,
                                      const ReachableBlockSet & reachableBlocks,
                                      const std::unordered_set<AllocaInst *> & preciseOnlyAllocas,
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

        LiveLocationSet liveLocations;
        auto outIt = liveOutStates.find(bb);
        if (outIt != liveOutStates.end()) {
            liveLocations = outIt->second;
        }

        auto & insts = bb->getInstructions();
        for (auto it = insts.rbegin(); it != insts.rend(); ++it) {
            Instruction * inst = *it;
            if (inst == nullptr || inst->isDead()) {
                continue;
            }

            if (auto * load = dynamic_cast<LoadInst *>(inst)) {
                MemoryLocation location = normalizeMemoryLocation(load->getPointerOperand());
                if (!isWholeFunctionDSELocation(location, preciseOnlyAllocas)) {
                    continue;
                }

                liveLocations.insert(location);
                continue;
            }

            if (auto * store = dynamic_cast<StoreInst *>(inst)) {
                MemoryLocation location = normalizeMemoryLocation(store->getPointerOperand());
                if (!isWholeFunctionDSELocation(location, preciseOnlyAllocas)) {
                    continue;
                }

                if (liveLocations.find(location) == liveLocations.end()) {
                    store->clearOperands();
                    store->setDead(true);
                    changed = true;
                    continue;
                }

                liveLocations.erase(location);
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
/// @param localDSEAllocas 仅能依赖 local DSE 的对象集合
/// @param preciseOnlyAllocas 可安全参与 whole-function DSE 的对象集合
/// @return true 表示至少删除了一条 store
bool eliminateDeadStores(Function * func,
                         const std::vector<BasicBlock *> & rpo,
                         const ReachableBlockSet & reachableBlocks,
                         const std::unordered_set<AllocaInst *> & localDSEAllocas,
                         const std::unordered_set<AllocaInst *> & preciseOnlyAllocas)
{
    bool changed = false;

    if (!localDSEAllocas.empty()) {
        changed = eliminateLocalConservativeDeadStores(func, localDSEAllocas) || changed;
    }

    if (preciseOnlyAllocas.empty()) {
        return changed;
    }

    BlockLiveStateMap liveInStates;
    BlockLiveStateMap liveOutStates;
    solveLiveLocationDataflow(rpo, reachableBlocks, preciseOnlyAllocas, liveInStates, liveOutStates);
    changed = eliminateGlobalPreciseDeadStores(func, reachableBlocks, preciseOnlyAllocas, liveOutStates)
              || changed;
    return changed;
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

    DominatorTree dt(func);
    const auto & rpo = dt.getRPO();
    const ReachableBlockSet reachableBlocks = collectReachableBlocks(dt);
    const auto preciseOnlyAllocas = collectPreciseOnlyAllocas(func, trackableAllocas);
    const auto localDSEAllocas = collectLocalDSEAllocas(trackableAllocas, preciseOnlyAllocas);

    bool changed = false;

    changed = runAvailableValueOptimizations(func, rpo, reachableBlocks, trackableAllocas) || changed;
    changed = eliminateDeadStores(func, rpo, reachableBlocks, localDSEAllocas, preciseOnlyAllocas) || changed;

    return sweepDeadInstructions() || changed;
}