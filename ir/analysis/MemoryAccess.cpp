///
/// @file MemoryAccess.cpp
/// @brief 共享的指针根对象与内存访问查询工具实现
///

#include "MemoryAccess.h"

#include <unordered_set>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "CallInst.h"
#include "FormalParam.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "GlobalVariable.h"
#include "Instruction.h"
#include "MemoryLocation.h"
#include "Module.h"
#include "StoreInst.h"
#include "Value.h"

Value * getPointerRoot(Value * value)
{
    Value * current = value;
    std::unordered_set<Value *> visited;
    while (current && visited.insert(current).second) {
        auto * gep = dynamic_cast<GetElementPtrInst *>(current);
        if (!gep) {
            break;
        }
        current = gep->getBasePointer();
    }
    return current;
}

bool isReadOnlyGlobal(Module * mod, GlobalVariable * global)
{
    if (!mod || !global) {
        return false;
    }

    for (auto * function : mod->getFunctionList()) {
        if (!function || function->isBuiltin()) {
            continue;
        }

        for (auto * bb : function->getBlocks()) {
            for (auto * inst : bb->getInstructions()) {
                auto * store = dynamic_cast<StoreInst *>(inst);
                if (store && getPointerRoot(store->getPointerOperand()) == global) {
                    return false;
                }
            }
        }
    }

    return true;
}

bool isLocalMemory(Value * ptr)
{
    return dynamic_cast<AllocaInst *>(getPointerRoot(ptr)) != nullptr;
}

bool isAllowedReadPointer(Module * mod, Value * ptr)
{
    Value * root = getPointerRoot(ptr);
    if (dynamic_cast<AllocaInst *>(root) != nullptr || dynamic_cast<FormalParam *>(root) != nullptr) {
        return true;
    }

    auto * global = dynamic_cast<GlobalVariable *>(root);
    return global != nullptr && isReadOnlyGlobal(mod, global);
}

bool isNonEscapingLocalStore(StoreInst * store)
{
    if (!store) {
        return false;
    }

    auto * alloca = dynamic_cast<AllocaInst *>(getPointerRoot(store->getPointerOperand()));
    return alloca != nullptr && !doesPointerEscape(alloca);
}

bool storeMayAliasLocation(StoreInst * store, const MemoryLocation & location)
{
    if (!store || !location.isPrecise()) {
        return true;
    }

    MemoryLocation storeLocation = normalizeMemoryLocation(store->getPointerOperand());
    if (!storeLocation.isKnownObject()) {
        return true;
    }

    return classifyMemoryAlias(location, storeLocation) != MemoryAliasResult::NoAlias;
}

bool blocksMayClobberLoad(Value * pointer,
                          const std::unordered_set<BasicBlock *> & blocks,
                          const std::function<bool(CallInst *)> & callMayClobber)
{
    MemoryLocation location = normalizeMemoryLocation(pointer);
    if (location.isPrecise() && !doesPointerEscape(location.object)) {
        for (auto * bb : blocks) {
            for (auto * inst : bb->getInstructions()) {
                auto * store = dynamic_cast<StoreInst *>(inst);
                if (store && storeMayAliasLocation(store, location)) {
                    return true;
                }
            }
        }
        return false;
    }

    auto * global = dynamic_cast<GlobalVariable *>(getPointerRoot(pointer));
    if (!global) {
        return true;
    }

    for (auto * bb : blocks) {
        for (auto * inst : bb->getInstructions()) {
            if (auto * store = dynamic_cast<StoreInst *>(inst)) {
                Value * storeRoot = getPointerRoot(store->getPointerOperand());
                if (storeRoot == global ||
                    (!dynamic_cast<AllocaInst *>(storeRoot) && !dynamic_cast<GlobalVariable *>(storeRoot))) {
                    return true;
                }
                continue;
            }

            auto * call = dynamic_cast<CallInst *>(inst);
            if (call && callMayClobber(call)) {
                return true;
            }
        }
    }

    return false;
}