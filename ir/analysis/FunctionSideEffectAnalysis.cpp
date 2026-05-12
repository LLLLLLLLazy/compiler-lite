///
/// @file FunctionSideEffectAnalysis.cpp
/// @brief 共享的函数外可见副作用分析实现
///

#include "FunctionSideEffectAnalysis.h"

#include "AllocaInst.h"
#include "CallInst.h"
#include "Function.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "MemoryAccess.h"
#include "StoreInst.h"

bool FunctionSideEffectAnalysis::isSideEffectFree(Function * function)
{
    if (!function || function->isBuiltin() || function->getBlocks().empty()) {
        return false;
    }

    auto it = states.find(function);
    if (it != states.end()) {
        return it->second == FunctionSideEffectState::SideEffectFree;
    }

    states[function] = FunctionSideEffectState::Visiting;
    bool sideEffectFree = true;
    for (auto * bb : function->getBlocks()) {
        for (auto * inst : bb->getInstructions()) {
            if (!isInstructionAllowed(inst)) {
                sideEffectFree = false;
                break;
            }
        }
        if (!sideEffectFree) {
            break;
        }
    }

    states[function] = sideEffectFree ? FunctionSideEffectState::SideEffectFree
                                      : FunctionSideEffectState::HasSideEffect;
    return sideEffectFree;
}

bool FunctionSideEffectAnalysis::isInstructionAllowed(Instruction * inst)
{
    if (!inst || inst->isDead() || inst->isTerminator()) {
        return true;
    }

    if (dynamic_cast<AllocaInst *>(inst) || dynamic_cast<LoadInst *>(inst)) {
        return true;
    }

    if (auto * store = dynamic_cast<StoreInst *>(inst)) {
        return isNonEscapingLocalStore(store);
    }

    if (auto * call = dynamic_cast<CallInst *>(inst)) {
        auto it = states.find(call->getCallee());
        if (it != states.end() && it->second == FunctionSideEffectState::Visiting) {
            return false;
        }
        return isSideEffectFree(call->getCallee());
    }

    return !inst->mayHaveSideEffects();
}