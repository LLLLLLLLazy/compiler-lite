///
/// @file PureCallMemoize.cpp
/// @brief 递归纯函数记忆化 pass 实现。
///

#include "PureCallMemoize.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "ArrayType.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "CondBranchInst.h"
#include "ConstInteger.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "GlobalVariable.h"
#include "ICmpInst.h"
#include "Instruction.h"
#include "IntegerType.h"
#include "LoadInst.h"
#include "Module.h"
#include "PhiInst.h"
#include "PointerType.h"
#include "PureFunctionAnalysis.h"
#include "ReturnInst.h"
#include "SelectInst.h"
#include "StoreInst.h"
#include "Type.h"
#include "Value.h"

namespace {

void replacePhiIncomingBlock(BasicBlock * bb, BasicBlock * oldBlock, BasicBlock * newBlock)
{
    if (!bb || !oldBlock || !newBlock) {
        return;
    }
    for (auto * inst : bb->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        phi->replaceIncomingBlock(oldBlock, newBlock);
    }
}

void insertBlockAfter(Function * fn, BasicBlock * block, BasicBlock * after)
{
    if (!fn || !block || !after || block == after) {
        return;
    }

    auto & blocks = fn->getBlocks();
    auto blockPos = std::find(blocks.begin(), blocks.end(), block);
    auto afterPos = std::find(blocks.begin(), blocks.end(), after);
    if (blockPos == blocks.end() || afterPos == blocks.end()) {
        return;
    }

    blocks.erase(blockPos);
    afterPos = std::find(blocks.begin(), blocks.end(), after);
    blocks.insert(std::next(afterPos), block);
}

GlobalVariable * findGlobalByName(Module * mod, const std::string & name)
{
    if (!mod) {
        return nullptr;
    }
    for (auto * global : mod->getGlobalVariables()) {
        if (global && global->getName() == name) {
            return global;
        }
    }
    return nullptr;
}

} // namespace

PureCallMemoize::PureCallMemoize(Function * _func, Module * _mod)
    : func(_func), mod(_mod)
{}

bool PureCallMemoize::run()
{
    if (!func || !mod || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    auto & params = func->getParams();
    if (params.size() != 1 && params.size() != 2) {
        return false;
    }
    if (!func->getReturnType()->isInt32Type()) {
        return false;
    }
    for (auto * param : params) {
        if (!param || !param->getType()->isInt32Type()) {
            return false;
        }
    }

    // 只有同一次函数激活中可能执行多个自递归调用时，才值得引入 memo。
    if (!hasOverlappingSelfCalls()) {
        return false;
    }

    PureFunctionAnalysis purity(mod);
    // 每次顶层调用都会切换 epoch，缓存只在本次无外部写入的递归闭包内复用
    if (!purity.isPure(func)) {
        return false;
    }

    MemoGlobals globals = createMemoGlobals(static_cast<int32_t>(params.size()));
    if (!globals.key0 || !globals.value || !globals.epochTag ||
        !globals.currentEpoch || !globals.recursionDepth ||
        (params.size() == 2 && !globals.key1)) {
        return false;
    }

    return transformFunction(globals, static_cast<int32_t>(params.size()));
}

bool PureCallMemoize::hasOverlappingSelfCalls()
{
    if (!func || !func->getEntryBlock()) {
        return false;
    }

    // 传播“到达块入口时，一条执行路径上最多执行过多少次自递归调用”。
    // 计数饱和于 2，既能区分互斥分支中的单次递归，也能识别 Fibonacci/
    // knapsack 这类同一路径会顺序执行多个递归调用的结构。
    std::unordered_map<BasicBlock *, int32_t> maxCallsAtEntry;
    std::vector<BasicBlock *> worklist;
    maxCallsAtEntry[func->getEntryBlock()] = 0;
    worklist.push_back(func->getEntryBlock());

    for (std::size_t index = 0; index < worklist.size(); ++index) {
        BasicBlock * bb = worklist[index];
        if (!bb) {
            continue;
        }

        int32_t callsOnPath = maxCallsAtEntry[bb];
        for (auto * inst : bb->getInstructions()) {
            auto * call = dynamic_cast<CallInst *>(inst);
            if (call && call->getCallee() == func) {
                callsOnPath = std::min<int32_t>(2, callsOnPath + 1);
            }
        }
        if (callsOnPath >= 2) {
            return true;
        }

        for (auto * succ : bb->getSuccessors()) {
            if (!succ) {
                continue;
            }
            auto it = maxCallsAtEntry.find(succ);
            if (it == maxCallsAtEntry.end() || callsOnPath > it->second) {
                maxCallsAtEntry[succ] = callsOnPath;
                worklist.push_back(succ);
            }
        }
    }

    return false;
}

PureCallMemoize::MemoGlobals PureCallMemoize::createMemoGlobals(int32_t paramCount)
{
    MemoGlobals globals;
    if (!func || !mod || (paramCount != 1 && paramCount != 2)) {
        return globals;
    }

    const std::string suffix = func->getName();
    auto * i32Type = IntegerType::getTypeInt32();
    auto * tableType = ArrayType::get(i32Type, kHashCapacity);

    auto getOrCreate = [this](Type * type, const std::string & name) {
        if (auto * existing = findGlobalByName(mod, name)) {
            return existing;
        }
        return mod->newSyntheticGlobalVariable(type, name);
    };

    globals.key0 = getOrCreate(tableType, "_memo_hash_key0_" + suffix);
    if (paramCount == 2) {
        globals.key1 = getOrCreate(tableType, "_memo_hash_key1_" + suffix);
    }
    globals.value = getOrCreate(tableType, "_memo_hash_value_" + suffix);
    globals.epochTag = getOrCreate(tableType, "_memo_hash_epoch_" + suffix);
    globals.currentEpoch = getOrCreate(i32Type, "_memo_epoch_" + suffix);
    globals.recursionDepth = getOrCreate(i32Type, "_memo_depth_" + suffix);
    return globals;
}

bool PureCallMemoize::transformFunction(const MemoGlobals & globals, int32_t paramCount)
{
    if (!func || !mod || (paramCount != 1 && paramCount != 2)) {
        return false;
    }

    auto & params = func->getParams();
    Value * key0 = params[0];
    Value * key1 = paramCount == 2 ? static_cast<Value *>(params[1]) : nullptr;

    auto * i32Type = IntegerType::getTypeInt32();
    auto * i1Type = IntegerType::getTypeInt1();
    auto * i32PtrType = const_cast<PointerType *>(PointerType::get(i32Type));

    auto * zero = mod->newConstInteger(i32Type, 0);
    auto * one = mod->newConstInteger(i32Type, 1);
    auto * hashMul0 = mod->newConstInteger(i32Type, 0x045d9f3b);
    auto * hashMul1 = mod->newConstInteger(i32Type, 0x119de1f3);
    auto * hashMask = mod->newConstInteger(i32Type, kHashMask);

    // ================================================================
    // Step 1: 拆分原入口。所有调用先经过 epoch/depth 和哈希查询。
    // ================================================================
    BasicBlock * origEntry = func->getEntryBlock();
    if (!origEntry) {
        return false;
    }

    auto * lookupBlock = func->newBasicBlock();
    insertBlockAfter(func, lookupBlock, origEntry);

    auto * hitBlock = func->newBasicBlock();
    insertBlockAfter(func, hitBlock, lookupBlock);

    auto * bodyBlock = func->newBasicBlock();
    insertBlockAfter(func, bodyBlock, hitBlock);

    auto & bodyInsts = bodyBlock->getInstructions();
    auto & origInsts = origEntry->getInstructions();
    bodyInsts.splice(bodyInsts.end(), origInsts);
    for (auto * inst : bodyInsts) {
        inst->setParentBlock(bodyBlock);
    }

    std::vector<BasicBlock *> oldSuccs = origEntry->getSuccessors();
    origEntry->getSuccessors().clear();
    for (auto * succ : oldSuccs) {
        succ->removePredecessor(origEntry);
        succ->addPredecessor(bodyBlock);
        bodyBlock->addSuccessor(succ);
        replacePhiIncomingBlock(succ, origEntry, bodyBlock);
    }

    auto * entryBranch = new BranchInst(func, lookupBlock);
    origEntry->addInstruction(entryBranch);
    origEntry->linkSuccessor(lookupBlock);

    // ================================================================
    // Step 2: 管理顶层调用 epoch 和递归深度。
    // ================================================================
    auto * oldDepth = new LoadInst(func, globals.recursionDepth, i32Type);
    lookupBlock->addInstruction(oldDepth);

    auto * isRoot = new ICmpInst(func, IRInstOperator::IRINST_OP_EQ_I,
                                  oldDepth, zero, i1Type);
    lookupBlock->addInstruction(isRoot);

    auto * oldEpoch = new LoadInst(func, globals.currentEpoch, i32Type);
    lookupBlock->addInstruction(oldEpoch);

    auto * incrementedEpoch = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I,
                                              oldEpoch, one, i32Type);
    lookupBlock->addInstruction(incrementedEpoch);

    auto * epochWrapped = new ICmpInst(func, IRInstOperator::IRINST_OP_EQ_I,
                                       incrementedEpoch, zero, i1Type);
    lookupBlock->addInstruction(epochWrapped);

    auto * nonZeroEpoch = new SelectInst(func, epochWrapped, one, incrementedEpoch, i32Type);
    lookupBlock->addInstruction(nonZeroEpoch);

    auto * activeEpoch = new SelectInst(func, isRoot, nonZeroEpoch, oldEpoch, i32Type);
    lookupBlock->addInstruction(activeEpoch);

    auto * storeEpoch = new StoreInst(func, activeEpoch, globals.currentEpoch);
    lookupBlock->addInstruction(storeEpoch);

    auto * activeDepth = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I,
                                         oldDepth, one, i32Type);
    lookupBlock->addInstruction(activeDepth);

    auto * storeDepth = new StoreInst(func, activeDepth, globals.recursionDepth);
    lookupBlock->addInstruction(storeDepth);

    // ================================================================
    // Step 3: 计算完整参数键的直接映射哈希槽。
    // 冲突时执行原函数并覆盖该槽，不会返回错误结果。
    // ================================================================
    auto * key0Hash = new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I,
                                      key0, hashMul0, i32Type);
    lookupBlock->addInstruction(key0Hash);

    Value * combinedHash = key0Hash;
    if (paramCount == 2) {
        auto * key1Hash = new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I,
                                          key1, hashMul1, i32Type);
        lookupBlock->addInstruction(key1Hash);

        auto * hashXor = new BinaryInst(func, IRInstOperator::IRINST_OP_XOR_I,
                                        key0Hash, key1Hash, i32Type);
        lookupBlock->addInstruction(hashXor);
        combinedHash = hashXor;
    }

    auto * slotIndex = new BinaryInst(func, IRInstOperator::IRINST_OP_AND_I,
                                       combinedHash, hashMask, i32Type);
    lookupBlock->addInstruction(slotIndex);

    auto * tagPtr = new GetElementPtrInst(func, globals.epochTag, slotIndex, i32PtrType, true);
    lookupBlock->addInstruction(tagPtr);
    auto * storedEpoch = new LoadInst(func, tagPtr, i32Type);
    lookupBlock->addInstruction(storedEpoch);
    auto * epochMatches = new ICmpInst(func, IRInstOperator::IRINST_OP_EQ_I,
                                       storedEpoch, activeEpoch, i1Type);
    lookupBlock->addInstruction(epochMatches);

    auto * key0Ptr = new GetElementPtrInst(func, globals.key0, slotIndex, i32PtrType, true);
    lookupBlock->addInstruction(key0Ptr);
    auto * storedKey0 = new LoadInst(func, key0Ptr, i32Type);
    lookupBlock->addInstruction(storedKey0);
    auto * key0Matches = new ICmpInst(func, IRInstOperator::IRINST_OP_EQ_I,
                                      storedKey0, key0, i1Type);
    lookupBlock->addInstruction(key0Matches);

    auto * cacheMatches = new BinaryInst(func, IRInstOperator::IRINST_OP_AND_I,
                                          epochMatches, key0Matches, i1Type);
    lookupBlock->addInstruction(cacheMatches);

    if (paramCount == 2) {
        auto * key1Ptr = new GetElementPtrInst(func, globals.key1, slotIndex, i32PtrType, true);
        lookupBlock->addInstruction(key1Ptr);
        auto * storedKey1 = new LoadInst(func, key1Ptr, i32Type);
        lookupBlock->addInstruction(storedKey1);
        auto * key1Matches = new ICmpInst(func, IRInstOperator::IRINST_OP_EQ_I,
                                          storedKey1, key1, i1Type);
        lookupBlock->addInstruction(key1Matches);

        auto * fullMatch = new BinaryInst(func, IRInstOperator::IRINST_OP_AND_I,
                                           cacheMatches, key1Matches, i1Type);
        lookupBlock->addInstruction(fullMatch);
        cacheMatches = fullMatch;
    }

    auto * cacheBranch = new CondBranchInst(func, cacheMatches, hitBlock, bodyBlock);
    lookupBlock->addInstruction(cacheBranch);
    lookupBlock->linkSuccessor(hitBlock);
    lookupBlock->linkSuccessor(bodyBlock);

    // ================================================================
    // Step 4: 命中时返回缓存值，并恢复递归深度。
    // ================================================================
    auto * valuePtr = new GetElementPtrInst(func, globals.value, slotIndex, i32PtrType, true);
    hitBlock->addInstruction(valuePtr);
    auto * cachedValue = new LoadInst(func, valuePtr, i32Type);
    hitBlock->addInstruction(cachedValue);

    auto * hitReturnDepth = new BinaryInst(func, IRInstOperator::IRINST_OP_SUB_I,
                                            activeDepth, one, i32Type);
    hitBlock->addInstruction(hitReturnDepth);
    auto * restoreHitDepth = new StoreInst(func, hitReturnDepth, globals.recursionDepth);
    hitBlock->addInstruction(restoreHitDepth);

    auto * hitReturn = new ReturnInst(func, cachedValue);
    hitBlock->addInstruction(hitReturn);

    // ================================================================
    // Step 5: 正常返回前写回完整键、值和 epoch，最后恢复 depth。
    // epochTag 最后写入，使部分写回不会被视为有效缓存。
    // ================================================================
    for (auto * bb : func->getBlocks()) {
        if (!bb || bb == origEntry || bb == lookupBlock || bb == hitBlock) {
            continue;
        }

        auto & insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end(); ++it) {
            auto * ret = dynamic_cast<ReturnInst *>(*it);
            if (!ret || !ret->hasReturnValue()) {
                continue;
            }

            Value * returnValue = ret->getReturnValue();
            std::vector<Instruction *> writeback;

            auto * key0StorePtr = new GetElementPtrInst(func, globals.key0, slotIndex,
                                                         i32PtrType, true);
            auto * storeKey0 = new StoreInst(func, key0, key0StorePtr);
            writeback.push_back(key0StorePtr);
            writeback.push_back(storeKey0);

            if (paramCount == 2) {
                auto * key1StorePtr = new GetElementPtrInst(func, globals.key1, slotIndex,
                                                             i32PtrType, true);
                auto * storeKey1 = new StoreInst(func, key1, key1StorePtr);
                writeback.push_back(key1StorePtr);
                writeback.push_back(storeKey1);
            }

            auto * valueStorePtr = new GetElementPtrInst(func, globals.value, slotIndex,
                                                          i32PtrType, true);
            auto * storeValue = new StoreInst(func, returnValue, valueStorePtr);
            writeback.push_back(valueStorePtr);
            writeback.push_back(storeValue);

            auto * tagStorePtr = new GetElementPtrInst(func, globals.epochTag, slotIndex,
                                                        i32PtrType, true);
            auto * storeTag = new StoreInst(func, activeEpoch, tagStorePtr);
            writeback.push_back(tagStorePtr);
            writeback.push_back(storeTag);

            auto * returnDepth = new BinaryInst(func, IRInstOperator::IRINST_OP_SUB_I,
                                                 activeDepth, one, i32Type);
            auto * restoreDepth = new StoreInst(func, returnDepth, globals.recursionDepth);
            writeback.push_back(returnDepth);
            writeback.push_back(restoreDepth);

            for (auto * inst : writeback) {
                inst->setParentBlock(bb);
                insts.insert(it, inst);
            }
            break;
        }
    }

    return true;
}
