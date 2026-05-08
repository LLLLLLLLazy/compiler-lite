///
/// @file SmallFunctionInline.cpp
/// @brief 保守的小函数内联优化 pass 实现。
///
/// 对满足体积和结构约束的 callee 进行内联展开：
///   - 叶子函数（不调用其他用户函数）：最多 4 个基本块、18 条指令
///   - 优先名称函数（如 sigmoid/exp 等）：最多 64 个基本块、300 条指令
/// 内联后删除 call 指令，将 callee 体复制到 caller 中，
/// 用 phi 节点合并多个返回值，使后续 mem2reg/LICM/SCCP 能跨函数体优化。
///

#include "SmallFunctionInline.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "CondBranchInst.h"
#include "CopyInst.h"
#include "FCmpInst.h"
#include "FPToSIInst.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "ICmpInst.h"
#include "LoadInst.h"
#include "Module.h"
#include "PhiInst.h"
#include "ReturnInst.h"
#include "SIToFPInst.h"
#include "StoreInst.h"
#include "ZExtInst.h"

namespace {

/// @brief 内联最大轮次，防止无限循环
constexpr int32_t kMaxInlineRounds = 256;
/// @brief 优先名称函数允许的最大基本块数
constexpr int32_t kPriorityMaxBlocks = 64;
/// @brief 优先名称函数允许的最大指令数
constexpr int32_t kPriorityMaxInsts = 300;
/// @brief 叶子函数允许的最大基本块数
constexpr int32_t kLeafMaxBlocks = 4;
/// @brief 叶子函数允许的最大指令数
constexpr int32_t kLeafMaxInsts = 18;
/// @brief 被内联函数的 alloca 总字节数上限，防止栈帧膨胀
constexpr int32_t kMaxAllocaBytes = 128;

/// @brief 优先内联的函数名称集合，这些函数即使较大也允许内联
const std::unordered_set<std::string> kPriorityInlineNames = {
    "getNumPos",
    "min",
    "f",
    "loop",
    "max",
    "sigmoid",
    "exp",
};

/// @brief 判断函数名是否属于优先内联集合
bool isPriorityName(const std::string & name)
{
    return kPriorityInlineNames.find(name) != kPriorityInlineNames.end();
}

/// @brief 统计函数中的指令总数
/// @param func 目标函数
/// @return 指令条数
int32_t countInstructions(Function * func)
{
    int32_t count = 0;
    if (!func) {
        return count;
    }

    for (auto * bb : func->getBlocks()) {
        count += static_cast<int32_t>(bb->getInstructions().size());
    }

    return count;
}

/// @brief 判断函数是否包含自递归调用
/// @param func 目标函数
/// @return true 表示函数内部调用了自身
bool containsSelfCall(Function * func)
{
    if (!func) {
        return true;
    }

    for (auto * bb : func->getBlocks()) {
        for (auto * inst : bb->getInstructions()) {
            auto * call = dynamic_cast<CallInst *>(inst);
            if (call && call->getCallee() == func) {
                return true;
            }
        }
    }

    return false;
}

/// @brief 判断函数是否调用了非自身的用户函数（即非叶子函数）
/// @param func 目标函数
/// @return true 表示函数调用了其他用户函数
bool hasNonSelfUserCall(Function * func)
{
    if (!func) {
        return true;
    }

    for (auto * bb : func->getBlocks()) {
        for (auto * inst : bb->getInstructions()) {
            auto * call = dynamic_cast<CallInst *>(inst);
            if (call && call->getCallee() != func && !call->getCallee()->isBuiltin()) {
                return true;
            }
        }
    }

    return false;
}

/// @brief 判断指令是否属于内联支持的指令类型
/// @param inst 待检查的指令
/// @return true 表示该指令可以被安全地克隆和内联
bool isSupportedInlineInstruction(Instruction * inst)
{
    return dynamic_cast<AllocaInst *>(inst) != nullptr ||
           dynamic_cast<BinaryInst *>(inst) != nullptr ||
           dynamic_cast<BranchInst *>(inst) != nullptr ||
           dynamic_cast<CallInst *>(inst) != nullptr ||
           dynamic_cast<CondBranchInst *>(inst) != nullptr ||
           dynamic_cast<CopyInst *>(inst) != nullptr ||
           dynamic_cast<FCmpInst *>(inst) != nullptr ||
           dynamic_cast<FPToSIInst *>(inst) != nullptr ||
           dynamic_cast<GetElementPtrInst *>(inst) != nullptr ||
           dynamic_cast<ICmpInst *>(inst) != nullptr ||
           dynamic_cast<LoadInst *>(inst) != nullptr ||
           dynamic_cast<PhiInst *>(inst) != nullptr ||
           dynamic_cast<ReturnInst *>(inst) != nullptr ||
           dynamic_cast<SIToFPInst *>(inst) != nullptr ||
           dynamic_cast<StoreInst *>(inst) != nullptr ||
           dynamic_cast<ZExtInst *>(inst) != nullptr;
}

/// @brief 计算函数中所有 alloca 指令分配的总字节数
/// @param func 目标函数
/// @return alloca 总字节数，超过上限时返回 kMaxAllocaBytes + 1
int32_t getAllocaBytes(Function * func)
{
    if (!func) {
        return 0;
    }

    int32_t bytes = 0;
    for (auto * bb : func->getBlocks()) {
        for (auto * inst : bb->getInstructions()) {
            auto * alloca = dynamic_cast<AllocaInst *>(inst);
            if (!alloca) {
                continue;
            }

            int32_t size = alloca->getAllocaType()->getSize();
            if (size < 0) {
                return kMaxAllocaBytes + 1;
            }
            bytes += size;
        }
    }

    return bytes;
}

/// @brief 将基本块中 phi 指令的某个 incoming 前驱块替换为新块
/// @param bb 包含 phi 指令的基本块
/// @param oldBlock 旧前驱块
/// @param newBlock 新前驱块
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

} // namespace

/// @brief 构造小函数内联 pass
/// @param _mod 待优化的模块
SmallFunctionInline::SmallFunctionInline(Module * _mod) : mod(_mod)
{}

/// @brief 执行小函数内联，反复内联直到没有新的内联机会
/// @return 若 IR 被修改则返回 true
bool SmallFunctionInline::run()
{
    if (!mod) {
        return false;
    }

    bool changed = false;
    for (int32_t round = 0; round < kMaxInlineRounds; ++round) {
        if (!inlineFirstCall()) {
            break;
        }
        changed = true;
    }

    return changed;
}

/// @brief 查找并内联第一个满足条件的调用点
/// @return true 表示成功内联了一个调用
bool SmallFunctionInline::inlineFirstCall()
{
    for (auto * caller : mod->getFunctionList()) {
        if (!caller || caller->isBuiltin() || caller->getBlocks().empty()) {
            continue;
        }

        std::vector<BasicBlock *> blocks = caller->getBlocks();
        for (auto * bb : blocks) {
            std::vector<Instruction *> insts(bb->getInstructions().begin(), bb->getInstructions().end());
            for (auto * inst : insts) {
                auto * call = dynamic_cast<CallInst *>(inst);
                if (!call || call->getParentBlock() != bb) {
                    continue;
                }

                if (shouldInlineCallee(caller, call->getCallee())) {
                    return inlineCall(call);
                }
            }
        }
    }

    return false;
}

/// @brief 判断 callee 是否满足内联条件
/// @param caller 调用方函数
/// @param callee 被调用方函数
/// @return true 表示可以内联该 callee
bool SmallFunctionInline::shouldInlineCallee(Function * caller, Function * callee) const
{
    if (!caller || !callee || callee->isBuiltin() || callee == caller || callee->getBlocks().empty()) {
        return false;
    }

    if (callee->getParams().size() > 8 || containsSelfCall(callee)) {
        return false;
    }

    if (getAllocaBytes(callee) > kMaxAllocaBytes) {
        return false;
    }

    for (auto * bb : callee->getBlocks()) {
        for (auto * inst : bb->getInstructions()) {
            if (!isSupportedInlineInstruction(inst)) {
                return false;
            }
        }
    }

    int32_t blockCount = static_cast<int32_t>(callee->getBlocks().size());
    int32_t instCount = countInstructions(callee);
    if (isPriorityName(callee->getName())) {
        return blockCount <= kPriorityMaxBlocks && instCount <= kPriorityMaxInsts;
    }

    return !hasNonSelfUserCall(callee) && blockCount <= kLeafMaxBlocks && instCount <= kLeafMaxInsts;
}

/// @brief 克隆指令的外壳（不填充操作数），用于内联时复制 callee 指令
/// @param inst 待克隆的指令
/// @param caller 目标 caller 函数
/// @return 克隆出的新指令，不支持的指令类型返回 nullptr
Instruction * SmallFunctionInline::cloneInstructionShell(Instruction * inst, Function * caller)
{
    if (!inst || !caller) {
        return nullptr;
    }

    if (auto * alloca = dynamic_cast<AllocaInst *>(inst)) {
        return new AllocaInst(caller, alloca->getAllocaType());
    }

    if (auto * binary = dynamic_cast<BinaryInst *>(inst)) {
        return new BinaryInst(caller, binary->getOp(), binary->getLHS(), binary->getRHS(), binary->getType());
    }

    if (auto * icmp = dynamic_cast<ICmpInst *>(inst)) {
        return new ICmpInst(caller, icmp->getOp(), icmp->getLHS(), icmp->getRHS(), icmp->getType());
    }

    if (auto * fcmp = dynamic_cast<FCmpInst *>(inst)) {
        return new FCmpInst(caller, fcmp->getOp(), fcmp->getLHS(), fcmp->getRHS(), fcmp->getType());
    }

    if (auto * load = dynamic_cast<LoadInst *>(inst)) {
        return new LoadInst(caller, load->getPointerOperand(), load->getType());
    }

    if (auto * store = dynamic_cast<StoreInst *>(inst)) {
        return new StoreInst(caller, store->getValueOperand(), store->getPointerOperand());
    }

    if (auto * gep = dynamic_cast<GetElementPtrInst *>(inst)) {
        return new GetElementPtrInst(caller,
                                     gep->getBasePointer(),
                                     gep->getIndexOperand(),
                                     gep->getType(),
                                     gep->isArrayDecayGEP());
    }

    if (auto * call = dynamic_cast<CallInst *>(inst)) {
        return new CallInst(caller, call->getCallee(), call->getOperandsValue(), call->getType());
    }

    if (auto * zext = dynamic_cast<ZExtInst *>(inst)) {
        return new ZExtInst(caller, zext->getSource(), zext->getType());
    }

    if (auto * sitofp = dynamic_cast<SIToFPInst *>(inst)) {
        return new SIToFPInst(caller, sitofp->getSource(), sitofp->getType());
    }

    if (auto * fptosi = dynamic_cast<FPToSIInst *>(inst)) {
        return new FPToSIInst(caller, fptosi->getSource(), fptosi->getType());
    }

    if (auto * copy = dynamic_cast<CopyInst *>(inst)) {
        return copy->getDst() ? new CopyInst(caller, copy->getSource(), copy->getDst())
                              : new CopyInst(caller, copy->getSource());
    }

    if (dynamic_cast<PhiInst *>(inst)) {
        return new PhiInst(caller, inst->getType());
    }

    return nullptr;
}

/// @brief 对一个调用点执行内联展开
/// @param call 待内联的调用指令
/// @return true 表示内联成功
bool SmallFunctionInline::inlineCall(CallInst * call)
{
    if (!call || !call->getParentBlock() || !call->getCallee()) {
        return false;
    }

    Function * caller = call->getFunction();
    Function * callee = call->getCallee();
    BasicBlock * callBlock = call->getParentBlock();
    auto & callInsts = callBlock->getInstructions();
    auto callPos = std::find(callInsts.begin(), callInsts.end(), static_cast<Instruction *>(call));
    if (callPos == callInsts.end()) {
        return false;
    }

    std::unordered_map<Value *, Value *> valueMap;
    std::unordered_map<BasicBlock *, BasicBlock *> blockMap;
    for (int32_t i = 0; i < call->getArgCount() && i < static_cast<int32_t>(callee->getParams().size()); ++i) {
        valueMap[callee->getParams()[i]] = call->getArg(i);
    }

    auto mapValue = [&valueMap](Value * value) -> Value * {
        auto it = valueMap.find(value);
        return it == valueMap.end() ? value : it->second;
    };

    BasicBlock * continuation = caller->newBasicBlock();
    auto afterCall = std::next(callPos);
    continuation->getInstructions().splice(continuation->getInstructions().end(), callInsts, afterCall, callInsts.end());
    for (auto * inst : continuation->getInstructions()) {
        inst->setParentBlock(continuation);
    }

    std::vector<BasicBlock *> oldSuccessors = callBlock->getSuccessors();
    callBlock->getSuccessors().clear();
    for (auto * succ : oldSuccessors) {
        succ->removePredecessor(callBlock);
        succ->addPredecessor(continuation);
        continuation->addSuccessor(succ);
        replacePhiIncomingBlock(succ, callBlock, continuation);
    }

    callInsts.erase(callPos);

    for (auto * calleeBB : callee->getBlocks()) {
        auto * cloneBB = caller->newBasicBlock();
        blockMap[calleeBB] = cloneBB;
    }

    std::vector<std::pair<Instruction *, Instruction *>> clonedInsts;
    for (auto * calleeBB : callee->getBlocks()) {
        BasicBlock * cloneBB = blockMap[calleeBB];
        for (auto * inst : calleeBB->getInstructions()) {
            if (inst->isTerminator()) {
                continue;
            }

            Instruction * cloned = cloneInstructionShell(inst, caller);
            if (!cloned) {
                return false;
            }

            cloneBB->addInstruction(cloned);
            clonedInsts.push_back({inst, cloned});
            if (inst->hasResultValue()) {
                valueMap[inst] = cloned;
            }
        }
    }

    for (auto & [orig, cloned] : clonedInsts) {
        if (auto * origPhi = dynamic_cast<PhiInst *>(orig)) {
            auto * clonedPhi = dynamic_cast<PhiInst *>(cloned);
            if (!clonedPhi) {
                return false;
            }

            for (int32_t i = 0; i < origPhi->getIncomingCount(); ++i) {
                clonedPhi->addIncoming(mapValue(origPhi->getIncomingValue(i)), blockMap[origPhi->getIncomingBlock(i)]);
            }
            continue;
        }

        for (int32_t i = 0; i < cloned->getOperandsNum(); ++i) {
            cloned->setOperand(i, mapValue(cloned->getOperand(i)));
        }
    }

    std::vector<std::pair<BasicBlock *, Value *>> returns;
    for (auto * calleeBB : callee->getBlocks()) {
        BasicBlock * cloneBB = blockMap[calleeBB];
        Instruction * term = calleeBB->getTerminator();
        if (auto * branch = dynamic_cast<BranchInst *>(term)) {
            auto * clonedBranch = new BranchInst(caller, blockMap[branch->getTarget()]);
            cloneBB->addInstruction(clonedBranch);
            cloneBB->linkSuccessor(blockMap[branch->getTarget()]);
            continue;
        }

        if (auto * condBranch = dynamic_cast<CondBranchInst *>(term)) {
            auto * clonedCond = new CondBranchInst(caller,
                                                   mapValue(condBranch->getCondition()),
                                                   blockMap[condBranch->getTrueDest()],
                                                   blockMap[condBranch->getFalseDest()]);
            cloneBB->addInstruction(clonedCond);
            cloneBB->linkSuccessor(blockMap[condBranch->getTrueDest()]);
            if (condBranch->getFalseDest() != condBranch->getTrueDest()) {
                cloneBB->linkSuccessor(blockMap[condBranch->getFalseDest()]);
            }
            continue;
        }

        if (auto * ret = dynamic_cast<ReturnInst *>(term)) {
            returns.push_back({cloneBB, ret->hasReturnValue() ? mapValue(ret->getReturnValue()) : nullptr});
            auto * clonedBranch = new BranchInst(caller, continuation);
            cloneBB->addInstruction(clonedBranch);
            cloneBB->linkSuccessor(continuation);
            continue;
        }

        return false;
    }

    auto * entryBranch = new BranchInst(caller, blockMap[callee->getEntryBlock()]);
    callBlock->addInstruction(entryBranch);
    callBlock->linkSuccessor(blockMap[callee->getEntryBlock()]);

    if (call->hasResultValue()) {
        Value * replacement = nullptr;
        if (returns.size() == 1) {
            replacement = returns.front().second;
        } else {
            auto * resultPhi = new PhiInst(caller, call->getType());
            for (auto & [retBlock, retValue] : returns) {
                if (!retValue) {
                    return false;
                }
                resultPhi->addIncoming(retValue, retBlock);
            }

            resultPhi->setParentBlock(continuation);
            continuation->getInstructions().insert(continuation->getInstructions().begin(), resultPhi);
            replacement = resultPhi;
        }

        if (!replacement) {
            return false;
        }
        call->replaceAllUseWith(replacement);
    }

    call->clearOperands();
    delete call;
    return true;
}
