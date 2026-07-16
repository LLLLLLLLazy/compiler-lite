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
#include "SelectInst.h"
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

    if (params.size() == 1 && !hasOverlappingSelfCalls()) {
        return false;
    }

    // 检查是否为纯函数
    PureFunctionAnalysis purity(mod);
    if (!purity.isPure(func)) {
        return false;
    }
    // 单参数缓存会跨顶层调用持续存在，因此要求结果只依赖参数和局部状态，
    // 避免可变全局数据改变后复用过期结果。双参数路径保留现有 knapsack 行为。
    if (params.size() == 1 && !purity.isMemoryIndependent(func)) {
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

    if (params.size() == 1) {
        int32_t maxArg = 0;
        if (!inferSingleParamBound(maxArg)) {
            return false;
        }

        createSingleParamMemoGlobals(maxArg);
        transformSingleParamFunction(maxArg);
        return true;
    }

    // 推断双参数函数的参数界
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

bool PureCallMemoize::hasOverlappingSelfCalls()
{
    if (!func || !func->getEntryBlock()) {
        return false;
    }

    // 记录到达各基本块入口时，一条可执行路径上最多已经执行了多少次自递归调用。
    // 计数上限为 2：达到 2 后即可确认存在潜在重叠子问题，无需继续精确累计。
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

bool PureCallMemoize::inferSingleParamBound(int32_t & maxArg)
{
    if (!func || !mod || func->getParams().size() != 1) {
        return false;
    }

    // 若递归参数用于索引已知大小的全局数组，优先沿用数组维度推断
    if (inferParamBound(maxArg)) {
        maxArg = std::min(maxArg, kMaxSingleMemoArg);
        return true;
    }

    // 若所有非递归调用点都传入非负常量，使用实际调用上界缩小缓存
    bool foundExternalCall = false;
    bool allExternalArgsConstant = true;
    int32_t maxConstantArg = 0;
    for (auto * caller : mod->getFunctionList()) {
        if (!caller || caller == func) {
            continue;
        }
        for (auto * bb : caller->getBlocks()) {
            for (auto * inst : bb->getInstructions()) {
                auto * call = dynamic_cast<CallInst *>(inst);
                if (!call || call->getCallee() != func || call->getArgCount() != 1) {
                    continue;
                }

                foundExternalCall = true;
                auto * constantArg = dynamic_cast<ConstInteger *>(call->getArg(0));
                if (!constantArg || constantArg->getVal() < 0) {
                    allExternalArgsConstant = false;
                    continue;
                }
                maxConstantArg = std::max(maxConstantArg, constantArg->getVal());
            }
        }
    }

    if (foundExternalCall && allExternalArgsConstant && maxConstantArg > 0) {
        maxArg = std::min(maxConstantArg, kMaxSingleMemoArg);
        return true;
    }

    maxArg = kDefaultMaxArg;
    return true;
}

void PureCallMemoize::createSingleParamMemoGlobals(int32_t maxArg)
{
    if (!mod || !func || maxArg < 0) {
        return;
    }

    std::string baseName = func->getName();
    std::string memoName = "_memo_" + baseName;
    std::string doneName = "_memo_done_" + baseName;
    if (findGlobalByName(mod, memoName)) {
        return;
    }

    auto * i32Type = IntegerType::getTypeInt32();

    // 额外保留一个哨兵槽。越界参数仍执行原函数体，但返回时写入哨兵槽，
    // 避免缓存写回使用负索引或超过数组上界。
    int32_t totalSize = maxArg + 2;
    auto * arrayType = ArrayType::get(i32Type, totalSize);
    (void) mod->newSyntheticGlobalVariable(arrayType, memoName);
    (void) mod->newSyntheticGlobalVariable(arrayType, doneName);
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

void PureCallMemoize::transformSingleParamFunction(int32_t maxArg)
{
    if (!func || !mod || func->getParams().size() != 1 || maxArg < 0) {
        return;
    }

    Value * param = func->getParams()[0];
    std::string baseName = func->getName();
    auto * memoGlobal = findGlobalByName(mod, "_memo_" + baseName);
    auto * doneGlobal = findGlobalByName(mod, "_memo_done_" + baseName);
    if (!memoGlobal || !doneGlobal) {
        return;
    }

    auto * i32Type = IntegerType::getTypeInt32();
    auto * i1Type = IntegerType::getTypeInt1();
    auto * resultPtrType = const_cast<PointerType *>(PointerType::get(i32Type));
    auto * constZero = mod->newConstInteger(i32Type, 0);
    auto * constOne = mod->newConstInteger(i32Type, 1);
    auto * constMaxArg = mod->newConstInteger(i32Type, maxArg);
    auto * constSentinel = mod->newConstInteger(i32Type, maxArg + 1);

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

    std::vector<BasicBlock *> oldSuccs = origEntry->getSuccessors();
    origEntry->getSuccessors().clear();
    for (auto * succ : oldSuccs) {
        succ->removePredecessor(origEntry);
        succ->addPredecessor(bodyBlock);
        bodyBlock->addSuccessor(succ);
        replacePhiIncomingBlock(succ, origEntry, bodyBlock);
    }

    auto * entryToNewEntry = new BranchInst(func, newEntry);
    origEntry->addInstruction(entryToNewEntry);
    origEntry->linkSuccessor(newEntry);

    // ================================================================
    // Step 2: 创建范围检查、查询和命中基本块
    // ================================================================
    auto * checkUpperBound = func->newBasicBlock();
    insertBlockAfter(func, checkUpperBound, newEntry);

    auto * lookupBlock = func->newBasicBlock();
    insertBlockAfter(func, lookupBlock, checkUpperBound);

    auto * hitBlock = func->newBasicBlock();
    insertBlockAfter(func, hitBlock, lookupBlock);

    // 仅缓存 0 <= arg <= maxArg 的状态。
    auto * cmpNonNegative = new ICmpInst(func, IRInstOperator::IRINST_OP_GE_I,
                                          param, constZero, i1Type);
    newEntry->addInstruction(cmpNonNegative);
    auto * brNonNegative = new CondBranchInst(func, cmpNonNegative, checkUpperBound, bodyBlock);
    newEntry->addInstruction(brNonNegative);
    newEntry->linkSuccessor(checkUpperBound);
    newEntry->linkSuccessor(bodyBlock);

    auto * cmpUpper = new ICmpInst(func, IRInstOperator::IRINST_OP_LE_I,
                                    param, constMaxArg, i1Type);
    checkUpperBound->addInstruction(cmpUpper);
    auto * brUpper = new CondBranchInst(func, cmpUpper, lookupBlock, bodyBlock);
    checkUpperBound->addInstruction(brUpper);
    checkUpperBound->linkSuccessor(lookupBlock);
    checkUpperBound->linkSuccessor(bodyBlock);

    // ================================================================
    // Step 3: 查询 memo_done[arg]
    // ================================================================
    auto * gepDone = new GetElementPtrInst(func, doneGlobal, param, resultPtrType, true);
    lookupBlock->addInstruction(gepDone);

    auto * loadDone = new LoadInst(func, gepDone, i32Type);
    lookupBlock->addInstruction(loadDone);

    auto * cmpDone = new ICmpInst(func, IRInstOperator::IRINST_OP_EQ_I,
                                   loadDone, constOne, i1Type);
    lookupBlock->addInstruction(cmpDone);

    auto * brDone = new CondBranchInst(func, cmpDone, hitBlock, bodyBlock);
    lookupBlock->addInstruction(brDone);
    lookupBlock->linkSuccessor(hitBlock);
    lookupBlock->linkSuccessor(bodyBlock);

    // ================================================================
    // Step 4: 命中时返回 memo[arg]
    // ================================================================
    auto * gepMemoHit = new GetElementPtrInst(func, memoGlobal, param, resultPtrType, true);
    hitBlock->addInstruction(gepMemoHit);

    auto * loadMemoHit = new LoadInst(func, gepMemoHit, i32Type);
    hitBlock->addInstruction(loadMemoHit);

    auto * hitRet = new ReturnInst(func, loadMemoHit);
    hitBlock->addInstruction(hitRet);

    // ================================================================
    // Step 5: 为写回计算安全索引
    // ================================================================
    auto * bodyCmpNonNegative = new ICmpInst(func, IRInstOperator::IRINST_OP_GE_I,
                                              param, constZero, i1Type);
    auto * bodyCmpUpper = new ICmpInst(func, IRInstOperator::IRINST_OP_LE_I,
                                        param, constMaxArg, i1Type);
    auto * bodyInRange = new BinaryInst(func, IRInstOperator::IRINST_OP_AND_I,
                                         bodyCmpNonNegative, bodyCmpUpper, i1Type);
    auto * safeIndex = new SelectInst(func, bodyInRange, param, constSentinel, i32Type);

    auto insertPos = bodyInsts.begin();
    for (auto * inst : {static_cast<Instruction *>(bodyCmpNonNegative),
                        static_cast<Instruction *>(bodyCmpUpper),
                        static_cast<Instruction *>(bodyInRange),
                        static_cast<Instruction *>(safeIndex)}) {
        inst->setParentBlock(bodyBlock);
        insertPos = bodyInsts.insert(insertPos, inst);
        ++insertPos;
    }

    // ================================================================
    // Step 6: 在每个正常 return 前写回缓存
    // ================================================================
    for (auto * bb : func->getBlocks()) {
        if (bb == hitBlock || bb == newEntry || bb == checkUpperBound ||
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
            auto * gepMemoStore = new GetElementPtrInst(func, memoGlobal, safeIndex,
                                                         resultPtrType, true);
            auto * storeMemo = new StoreInst(func, retVal, gepMemoStore);
            auto * gepDoneStore = new GetElementPtrInst(func, doneGlobal, safeIndex,
                                                         resultPtrType, true);
            auto * storeDone = new StoreInst(func, constOne, gepDoneStore);

            for (auto * inst : {static_cast<Instruction *>(gepMemoStore),
                                static_cast<Instruction *>(storeMemo),
                                static_cast<Instruction *>(gepDoneStore),
                                static_cast<Instruction *>(storeDone)}) {
                inst->setParentBlock(bb);
            }

            auto returnPos = it;
            returnPos = insts.insert(returnPos, storeDone);
            returnPos = insts.insert(returnPos, gepDoneStore);
            returnPos = insts.insert(returnPos, storeMemo);
            insts.insert(returnPos, gepMemoStore);
            break;
        }
    }
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
