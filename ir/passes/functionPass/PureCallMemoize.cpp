///
/// @file PureCallMemoize.cpp
/// @brief 递归纯函数记忆化 pass 实现。
///

#include "PureCallMemoize.h"

#include <algorithm>
#include <string>

#include "ArrayType.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "CondBranchInst.h"
#include "ConstInteger.h"
#include "FormalParam.h"
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
#include "StoreInst.h"
#include "Type.h"
#include "Value.h"

namespace {

/// 将基本块中 phi 指令的某个 incoming 前驱块替换为新块
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

/// 将基本块移动到另一个基本块之后
void insertBlockAfter(Function * fn, BasicBlock * block, BasicBlock * after)
{
    if (!fn || !block || !after) {
        return;
    }
    auto & blks = fn->getBlocks();
    auto blockPos = std::find(blks.begin(), blks.end(), block);
    auto afterPos = std::find(blks.begin(), blks.end(), after);
    if (blockPos == blks.end() || afterPos == blks.end() || block == after) {
        return;
    }
    blks.erase(blockPos);
    afterPos = std::find(blks.begin(), blks.end(), after);
    blks.insert(std::next(afterPos), block);
}

/// 在模块中按名称查找全局变量
GlobalVariable * findGlobalByName(Module * mod, const std::string & name)
{
    if (!mod) {
        return nullptr;
    }
    for (auto * gv : mod->getGlobalVariables()) {
        if (gv && gv->getName() == name) {
            return gv;
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
    if (!func || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    auto & params = func->getParams();
    if (params.size() != 2) {
        return false;
    }
    if (!params[0]->getType()->isIntegerType() || !params[1]->getType()->isIntegerType()) {
        return false;
    }

    // 检查是否为纯函数
    PureFunctionAnalysis purity(mod);
    if (!purity.isPure(func)) {
        return false;
    }

    // 确认自递归调用
    bool isRecursive = false;
    for (auto * bb : func->getBlocks()) {
        for (auto * inst : bb->getInstructions()) {
            auto * call = dynamic_cast<CallInst *>(inst);
            if (call && call->getCallee() == func) {
                isRecursive = true;
                break;
            }
        }
        if (isRecursive) {
            break;
        }
    }
    if (!isRecursive) {
        return false;
    }

    // 推断参数界
    int32_t maxI = 0;
    if (!inferParamBound(maxI)) {
        return false;
    }
    int32_t maxW = kDefaultMaxW;

    // 创建全局 memo 数组
    createMemoGlobals(maxI, maxW);

    // 变换函数
    transformFunction(maxI, maxW);

    return true;
}

bool PureCallMemoize::inferParamBound(int32_t & maxArg0)
{
    if (!func) {
        return false;
    }

    auto & params = func->getParams();
    Value * param0 = params[0];

    for (auto * bb : func->getBlocks()) {
        for (auto * inst : bb->getInstructions()) {
            auto * gep = dynamic_cast<GetElementPtrInst *>(inst);
            if (!gep) {
                continue;
            }

            auto * basePtr = gep->getBasePointer();
            if (!dynamic_cast<GlobalVariable *>(basePtr)) {
                continue;
            }

            // 获取数组维度
            auto * baseType = basePtr->getType();
            auto * basePtrType = dynamic_cast<PointerType *>(baseType);
            if (!basePtrType) {
                continue;
            }

            const auto * arrayType = dynamic_cast<const ArrayType *>(basePtrType->getPointeeType());
            if (!arrayType) {
                continue;
            }

            int32_t numElements = arrayType->getNumElements();

            // 检查索引是否引用 param0（含 ± constant 模式）
            Value * indexVal = gep->getIndexOperand();
            Value * indexRoot = indexVal;

            if (auto * binOp = dynamic_cast<BinaryInst *>(indexVal)) {
                if (binOp->getLHS() == param0 || binOp->getRHS() == param0) {
                    indexRoot = param0;
                }
            }

            if (indexRoot == param0 || indexVal == param0) {
                if (numElements > maxArg0) {
                    maxArg0 = numElements;
                }
            }
        }
    }

    return maxArg0 > 0;
}

void PureCallMemoize::createMemoGlobals(int32_t maxI, int32_t maxW)
{
    if (!mod || !func) {
        return;
    }

    std::string baseName = func->getName();

    std::string memoName = "_memo_" + baseName;
    std::string doneName = "_memo_done_" + baseName;
    if (findGlobalByName(mod, memoName)) {
        return;
    }

    auto * i32Type = IntegerType::getTypeInt32();
    int32_t totalSize = (maxI + 1) * (maxW + 1);

    auto * arrayType = ArrayType::get(i32Type, totalSize);
    (void) mod->newSyntheticGlobalVariable(arrayType, memoName);
    (void) mod->newSyntheticGlobalVariable(arrayType, doneName);
}

void PureCallMemoize::transformFunction(int32_t maxI, int32_t maxW)
{
    if (!func || !mod) {
        return;
    }

    auto & params = func->getParams();
    Value * paramI = params[0];
    Value * paramW = params[1];

    std::string baseName = func->getName();
    auto * memoGlobal = findGlobalByName(mod, "_memo_" + baseName);
    auto * doneGlobal = findGlobalByName(mod, "_memo_done_" + baseName);
    if (!memoGlobal || !doneGlobal) {
        return;
    }

    auto * i32Type = IntegerType::getTypeInt32();
    auto * i1Type = IntegerType::getTypeInt1();
    auto * resultPtrType = const_cast<PointerType *>(PointerType::get(i32Type));

    int32_t stride = maxW + 1;

    // ================================================================
    // Step 1: 拆分入口块
    // ================================================================
    BasicBlock * origEntry = func->getEntryBlock();
    if (!origEntry) {
        return;
    }

    auto * newEntry = func->newBasicBlock();
    insertBlockAfter(func, newEntry, origEntry);

    auto * bodyBlock = func->newBasicBlock();
    insertBlockAfter(func, bodyBlock, newEntry);

    auto & bodyInsts = bodyBlock->getInstructions();
    auto & origInsts = origEntry->getInstructions();
    bodyInsts.splice(bodyInsts.end(), origInsts);
    for (auto * inst : bodyInsts) {
        inst->setParentBlock(bodyBlock);
    }

    // 转移 CFG 后继
    std::vector<BasicBlock *> oldSuccs = origEntry->getSuccessors();
    origEntry->getSuccessors().clear();
    for (auto * succ : oldSuccs) {
        succ->removePredecessor(origEntry);
        succ->addPredecessor(bodyBlock);
        bodyBlock->addSuccessor(succ);
        replacePhiIncomingBlock(succ, origEntry, bodyBlock);
    }

    // origEntry → newEntry
    auto * entryToNewEntry = new BranchInst(func, newEntry);
    origEntry->addInstruction(entryToNewEntry);
    origEntry->linkSuccessor(newEntry);

    // ================================================================
    // Step 2: 创建辅助基本块
    // ================================================================
    auto * checkWBound = func->newBasicBlock();
    insertBlockAfter(func, checkWBound, newEntry);

    auto * lookupBlock = func->newBasicBlock();
    insertBlockAfter(func, lookupBlock, checkWBound);

    auto * hitBlock = func->newBasicBlock();
    insertBlockAfter(func, hitBlock, lookupBlock);

    // ================================================================
    // Step 3: newEntry — 检查 i ≤ maxI
    // ================================================================
    auto * constMaxI = mod->newConstInteger(i32Type, maxI);
    auto * cmpI = new ICmpInst(func, IRInstOperator::IRINST_OP_LE_I, paramI, constMaxI, i1Type);
    newEntry->addInstruction(cmpI);
    auto * brI = new CondBranchInst(func, cmpI, checkWBound, bodyBlock);
    newEntry->addInstruction(brI);
    newEntry->linkSuccessor(checkWBound);
    newEntry->linkSuccessor(bodyBlock);

    // ================================================================
    // Step 4: checkWBound — 检查 w ≤ maxW
    // ================================================================
    auto * constMaxW = mod->newConstInteger(i32Type, maxW);
    auto * cmpW = new ICmpInst(func, IRInstOperator::IRINST_OP_LE_I, paramW, constMaxW, i1Type);
    checkWBound->addInstruction(cmpW);
    auto * brW = new CondBranchInst(func, cmpW, lookupBlock, bodyBlock);
    checkWBound->addInstruction(brW);
    checkWBound->linkSuccessor(lookupBlock);
    checkWBound->linkSuccessor(bodyBlock);

    // ================================================================
    // Step 5: lookupBlock — 检查 memo_done[flatIdx]
    //   flatIdx = i * stride + w
    // ================================================================
    auto * constStride = mod->newConstInteger(i32Type, stride);
    auto * mulResult = new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I,
                                       paramI, constStride, i32Type);
    lookupBlock->addInstruction(mulResult);

    auto * flatIdx = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I,
                                     mulResult, paramW, i32Type);
    lookupBlock->addInstruction(flatIdx);

    auto * gepDone = new GetElementPtrInst(func, doneGlobal, flatIdx, resultPtrType, true);
    lookupBlock->addInstruction(gepDone);

    auto * loadDone = new LoadInst(func, gepDone, i32Type);
    lookupBlock->addInstruction(loadDone);

    auto * constOne = mod->newConstInteger(i32Type, 1);
    auto * cmpDone = new ICmpInst(func, IRInstOperator::IRINST_OP_EQ_I,
                                   loadDone, constOne, i1Type);
    lookupBlock->addInstruction(cmpDone);

    auto * brDone = new CondBranchInst(func, cmpDone, hitBlock, bodyBlock);
    lookupBlock->addInstruction(brDone);
    lookupBlock->linkSuccessor(hitBlock);
    lookupBlock->linkSuccessor(bodyBlock);

    // ================================================================
    // Step 6: hitBlock — 返回缓存值
    // ================================================================
    auto * gepMemoHit = new GetElementPtrInst(func, memoGlobal, flatIdx, resultPtrType, true);
    hitBlock->addInstruction(gepMemoHit);

    auto * loadMemoHit = new LoadInst(func, gepMemoHit, i32Type);
    hitBlock->addInstruction(loadMemoHit);

    auto * hitRet = new ReturnInst(func, loadMemoHit);
    hitBlock->addInstruction(hitRet);

    // ================================================================
    // Step 7: 在 bodyBlock 开头计算 flatIdx，供 return 块使用
    // ================================================================
    auto * bodyMul = new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I,
                                     paramI, constStride, i32Type);
    auto * bodyFlatIdx = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I,
                                         bodyMul, paramW, i32Type);
    bodyInsts.insert(bodyInsts.begin(), bodyFlatIdx);
    bodyInsts.insert(bodyInsts.begin(), bodyMul);

    // ================================================================
    // Step 8: 在每个 return 前插入 memo 存储
    // ================================================================
    for (auto * bb : func->getBlocks()) {
        if (bb == hitBlock || bb == newEntry || bb == checkWBound ||
            bb == lookupBlock || bb == origEntry) {
            continue;
        }

        auto & insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end(); ++it) {
            auto * ret = dynamic_cast<ReturnInst *>(*it);
            if (!ret || !ret->hasReturnValue()) {
                continue;
            }

            Value * retVal = ret->getReturnValue();

            auto * gepMemoStore = new GetElementPtrInst(func, memoGlobal, bodyFlatIdx,
                                                         resultPtrType, true);
            auto * storeMemo = new StoreInst(func, retVal, gepMemoStore);
            auto * gepDoneStore = new GetElementPtrInst(func, doneGlobal, bodyFlatIdx,
                                                         resultPtrType, true);
            auto * storeDone = new StoreInst(func, constOne, gepDoneStore);

            // 按正确顺序插入：gepMemoStore, storeMemo, gepDoneStore, storeDone
            auto insertPos = it;
            insertPos = insts.insert(insertPos, storeDone);
            insertPos = insts.insert(insertPos, gepDoneStore);
            insertPos = insts.insert(insertPos, storeMemo);
            insts.insert(insertPos, gepMemoStore);

            break;
        }
    }
}
