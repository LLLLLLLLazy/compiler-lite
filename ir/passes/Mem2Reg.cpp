///
/// @file Mem2Reg.cpp
/// @brief mem2reg pass 实现
///
/// 参考：
///   Cytron et al., "Efficiently Computing Static Single Assignment Form and
///   the Control Dependence Graph", TOPLAS 1991.
///   Cooper & Torczon, "Engineering a Compiler", 2nd ed., Chapter 9.
///

#include "Mem2Reg.h"

#include <list>
#include <set>
#include <unordered_set>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "ConstInt.h"
#include "DominanceFrontier.h"
#include "DominatorTree.h"
#include "Function.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "Module.h"
#include "PhiInst.h"
#include "StoreInst.h"
#include "Types/IntegerType.h"
#include "Value.h"

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

Mem2Reg::Mem2Reg(Function * _func, Module * _mod) : func(_func), mod(_mod)
{}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

void Mem2Reg::run()
{
    if (!func || func->isBuiltin() || func->getBlocks().empty()) {
        return;
    }

    // Step 1: collect promotable allocas
    std::vector<AllocaInst *> allocas = findPromotableAllocas();
    if (allocas.empty()) {
        return;
    }

    // Steps 2–3 require dominator information
    DominatorTree dt(func);
    DominanceFrontier df(func, dt);

    // Step 2: insert phi nodes at IDF of def-blocks
    // allocaPhis[alloca][block] = phi inserted at top of block for that alloca
    std::unordered_map<AllocaInst *, std::unordered_map<BasicBlock *, PhiInst *>> allocaPhis;
    std::unordered_map<PhiInst *, AllocaInst *> phiToAlloca;
    insertPhiNodes(allocas, df, allocaPhis, phiToAlloca);

    // Step 3: rename – initialise reaching-def stacks
    std::unordered_map<AllocaInst *, std::vector<Value *>> reachingDefs;
    for (auto * alloca : allocas) {
        reachingDefs[alloca]; // create entry with empty stack
    }

    std::unordered_set<BasicBlock *> visited;
    rename(func->getEntryBlock(), dt, reachingDefs, allocaPhis, phiToAlloca, visited);

    // Step 4: remove dead instructions and the allocas themselves
    cleanup(allocas);
}

// ---------------------------------------------------------------------------
// Step 1: find promotable allocas
// ---------------------------------------------------------------------------

std::vector<AllocaInst *> Mem2Reg::findPromotableAllocas()
{
    std::vector<AllocaInst *> result;

    // IRGenerator always places allocas in the entry block
    BasicBlock * entry = func->getEntryBlock();
    if (!entry) {
        return result;
    }

    for (auto * inst : entry->getInstructions()) {
        if (auto * alloca = dynamic_cast<AllocaInst *>(inst)) {
            if (isPromotable(alloca)) {
                result.push_back(alloca);
            }
        }
    }

    return result;
}

bool Mem2Reg::isPromotable(AllocaInst * alloca) const
{
    // Only promote scalar (non-pointer) element types
    Type * elemType = alloca->getAllocaType();
    if (!elemType || elemType->isPointerType()) {
        return false;
    }

    for (auto * use : alloca->getUseList()) {
        auto * u = use->getUser();

        if (auto * load = dynamic_cast<LoadInst *>(u)) {
            // The alloca must be the pointer operand of the load
            if (load->getPointerOperand() != static_cast<Value *>(alloca)) {
                return false;
            }
        } else if (auto * store = dynamic_cast<StoreInst *>(u)) {
            // The alloca must be the pointer operand, NOT the stored value
            if (store->getValueOperand() == static_cast<Value *>(alloca)) {
                // Address escapes – alloca is being stored as a value
                return false;
            }
            if (store->getPointerOperand() != static_cast<Value *>(alloca)) {
                return false;
            }
        } else {
            // Any other use (e.g., passed to a call) – not promotable
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Step 2: phi placement (Cytron et al. IDF algorithm)
// ---------------------------------------------------------------------------

void Mem2Reg::insertPhiNodes(
    const std::vector<AllocaInst *> & allocas,
    const DominanceFrontier & df,
    std::unordered_map<AllocaInst *, std::unordered_map<BasicBlock *, PhiInst *>> & allocaPhis,
    std::unordered_map<PhiInst *, AllocaInst *> & phiToAlloca)
{
    for (auto * alloca : allocas) {
        // Collect blocks that contain a store to this alloca (definition sites)
        std::vector<BasicBlock *> defBlocks;
        for (auto * bb : func->getBlocks()) {
            for (auto * inst : bb->getInstructions()) {
                if (auto * store = dynamic_cast<StoreInst *>(inst)) {
                    if (store->getPointerOperand() == static_cast<Value *>(alloca)) {
                        defBlocks.push_back(bb);
                        break; // one store in this block is enough
                    }
                }
            }
        }

        if (defBlocks.empty()) {
            // No stores: all loads are of an undef value – will be replaced with 0 in rename
            continue;
        }

        // IDF computation (Cytron et al.)
        std::unordered_set<BasicBlock *> placed;       // blocks where phi was inserted
        std::unordered_set<BasicBlock *> everOnWorklist; // blocks ever enqueued
        std::vector<BasicBlock *> worklist;

        for (auto * bb : defBlocks) {
            everOnWorklist.insert(bb);
            worklist.push_back(bb);
        }

        while (!worklist.empty()) {
            BasicBlock * bb = worklist.back();
            worklist.pop_back();

            for (BasicBlock * y : df.getFrontier(bb)) {
                if (placed.count(y)) {
                    continue;
                }

                // Insert phi for alloca at top of y
                auto * phi = new PhiInst(func, alloca->getAllocaType());
                // Push phi to the front of y's instruction list (phi nodes live at the top)
                auto & insts = y->getInstructions();
                insts.push_front(phi);
                phi->setParentBlock(y);

                placed.insert(y);
                allocaPhis[alloca][y] = phi;
                phiToAlloca[phi] = alloca;

                if (!everOnWorklist.count(y)) {
                    everOnWorklist.insert(y);
                    worklist.push_back(y);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Step 3: rename pass (DFS over dominator tree)
// ---------------------------------------------------------------------------

void Mem2Reg::rename(
    BasicBlock * bb,
    const DominatorTree & dt,
    std::unordered_map<AllocaInst *, std::vector<Value *>> & reachingDefs,
    const std::unordered_map<AllocaInst *, std::unordered_map<BasicBlock *, PhiInst *>> & allocaPhis,
    const std::unordered_map<PhiInst *, AllocaInst *> & phiToAlloca,
    std::unordered_set<BasicBlock *> & visited)
{
    if (visited.count(bb)) {
        return;
    }
    visited.insert(bb);

    // Track how many values we pushed per alloca so we can pop them later
    std::unordered_map<AllocaInst *, int> pushCount;

    // ---- Process instructions in this block ----
    for (auto * inst : bb->getInstructions()) {
        if (auto * phi = dynamic_cast<PhiInst *>(inst)) {
            // Phi nodes at the top of the block define new values for mem2reg allocas
            auto it = phiToAlloca.find(phi);
            if (it != phiToAlloca.end()) {
                AllocaInst * alloca = it->second;
                reachingDefs[alloca].push_back(phi);
                pushCount[alloca]++;
            }

        } else if (auto * load = dynamic_cast<LoadInst *>(inst)) {
            // Replace load from promotable alloca with the current reaching definition
            Value * ptr = load->getPointerOperand();
            if (auto * alloca = dynamic_cast<AllocaInst *>(ptr)) {
                auto it = reachingDefs.find(alloca);
                if (it != reachingDefs.end()) {
                    // Get the reaching definition (or 0 if undefined)
                    Value * reaching =
                        it->second.empty() ? static_cast<Value *>(mod->newConstInt(0)) : it->second.back();
                    load->replaceAllUseWith(reaching);
                    load->setDead(true);
                }
            }

        } else if (auto * store = dynamic_cast<StoreInst *>(inst)) {
            // Record the stored value as the new reaching definition for this alloca
            Value * ptr = store->getPointerOperand();
            if (auto * alloca = dynamic_cast<AllocaInst *>(ptr)) {
                auto it = reachingDefs.find(alloca);
                if (it != reachingDefs.end()) {
                    Value * val = store->getValueOperand();
                    it->second.push_back(val);
                    pushCount[alloca]++;
                    store->setDead(true);
                }
            }
        }
    }

    // ---- Fill phi incoming values in immediate successors ----
    for (auto * succ : bb->getSuccessors()) {
        for (auto * inst : succ->getInstructions()) {
            auto * phi = dynamic_cast<PhiInst *>(inst);
            if (!phi) {
                break; // Phi nodes are always at the top; once we see a non-phi, stop
            }

            auto it = phiToAlloca.find(phi);
            if (it == phiToAlloca.end()) {
                continue; // Not a mem2reg phi – skip
            }

            AllocaInst * alloca = it->second;
            auto defIt = reachingDefs.find(alloca);
            Value * reaching = (defIt == reachingDefs.end() || defIt->second.empty())
                                    ? static_cast<Value *>(mod->newConstInt(0))
                                    : defIt->second.back();
            phi->addIncoming(reaching, bb);
        }
    }

    // ---- Recurse on dominator-tree children ----
    for (auto * child : dt.getDomChildren(bb)) {
        rename(child, dt, reachingDefs, allocaPhis, phiToAlloca, visited);
    }

    // ---- Restore reaching-def stacks (pop values pushed in this block) ----
    for (auto & [alloca, count] : pushCount) {
        auto & stack = reachingDefs[alloca];
        for (int i = 0; i < count; ++i) {
            if (!stack.empty()) {
                stack.pop_back();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Step 4: cleanup – remove dead instructions and promoted allocas
// ---------------------------------------------------------------------------

void Mem2Reg::cleanup(const std::vector<AllocaInst *> & allocas)
{
    std::unordered_set<AllocaInst *> allocaSet(allocas.begin(), allocas.end());

    // Pass 1: remove dead load/store instructions across all blocks.
    // This must happen before removing allocas so that the alloca's use list
    // is fully cleared before we delete the alloca itself.
    for (auto * bb : func->getBlocks()) {
        auto & insts = bb->getInstructions();
        auto it = insts.begin();
        while (it != insts.end()) {
            Instruction * inst = *it;
            if (inst->isDead()) {
                inst->clearOperands(); // removes this instruction from all operand use-lists
                auto next = std::next(it);
                insts.erase(it);
                delete inst;
                it = next;
            } else {
                ++it;
            }
        }
    }

    // Pass 2: remove promotable allocas from the entry block.
    // Their use lists should now be empty (all users were removed above).
    BasicBlock * entry = func->getEntryBlock();
    if (!entry) {
        return;
    }

    auto & entryInsts = entry->getInstructions();
    auto it = entryInsts.begin();
    while (it != entryInsts.end()) {
        if (auto * alloca = dynamic_cast<AllocaInst *>(*it)) {
            if (allocaSet.count(alloca)) {
                alloca->clearOperands();
                auto next = std::next(it);
                entryInsts.erase(it);
                delete alloca;
                it = next;
                continue;
            }
        }
        ++it;
    }
}
