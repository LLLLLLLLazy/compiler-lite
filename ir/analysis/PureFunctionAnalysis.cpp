///
/// @file PureFunctionAnalysis.cpp
/// @brief 共享的纯函数与内存独立性分析实现
///

#include "PureFunctionAnalysis.h"

#include "AllocaInst.h"
#include "CallInst.h"
#include "Function.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "MemoryAccess.h"
#include "StoreInst.h"

PureFunctionAnalysis::PureFunctionAnalysis(Module * module) : mod(module)
{}

bool PureFunctionAnalysis::isPure(Function * function)
{
    if (!function || function->isBuiltin() || function->getBlocks().empty()) {
        return false;
    }

    auto it = states.find(function);
    if (it != states.end()) {
        return it->second == PureFunctionState::Pure;
    }

    states[function] = PureFunctionState::Visiting;
    bool pure = true;
    for (auto * bb : function->getBlocks()) {
        for (auto * inst : bb->getInstructions()) {
            if (!isInstructionAllowed(inst)) {
                pure = false;
                break;
            }
        }
        if (!pure) {
            break;
        }
    }

    states[function] = pure ? PureFunctionState::Pure : PureFunctionState::Impure;
    return pure;
}

bool PureFunctionAnalysis::isMemoryIndependent(Function * function)
{
    if (!isPure(function)) {
        return false;
    }

    auto cached = memoryIndependent.find(function);
    if (cached != memoryIndependent.end()) {
        return cached->second;
    }

    bool independent = true;
    for (auto * bb : function->getBlocks()) {
        for (auto * inst : bb->getInstructions()) {
            if (auto * load = dynamic_cast<LoadInst *>(inst)) {
                if (dynamic_cast<AllocaInst *>(getPointerRoot(load->getPointerOperand())) == nullptr) {
                    independent = false;
                    break;
                }
                continue;
            }

            if (auto * call = dynamic_cast<CallInst *>(inst)) {
                if (!isMemoryIndependent(call->getCallee())) {
                    independent = false;
                    break;
                }
            }
        }

        if (!independent) {
            break;
        }
    }

    memoryIndependent[function] = independent;
    return independent;
}

bool PureFunctionAnalysis::isInstructionAllowed(Instruction * inst)
{
    if (!inst || inst->isDead() || inst->isTerminator()) {
        return true;
    }

    if (dynamic_cast<AllocaInst *>(inst) != nullptr) {
        return true;
    }

    if (auto * load = dynamic_cast<LoadInst *>(inst)) {
        return isAllowedReadPointer(mod, load->getPointerOperand());
    }

    if (auto * store = dynamic_cast<StoreInst *>(inst)) {
        return isLocalMemory(store->getPointerOperand());
    }

    if (auto * call = dynamic_cast<CallInst *>(inst)) {
        auto it = states.find(call->getCallee());
        if (it != states.end() && it->second == PureFunctionState::Visiting) {
            return false;
        }
        return isPure(call->getCallee());
    }

    return !inst->mayHaveSideEffects();
}