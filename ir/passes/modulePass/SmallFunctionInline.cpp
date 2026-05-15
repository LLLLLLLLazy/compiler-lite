///
/// @file SmallFunctionInline.cpp
/// @brief 保守的小函数内联优化 pass 实现。
///
/// 对满足体积和结构约束的 callee 进行内联展开：
///   - 叶子函数（不调用其他用户函数）：最多 4 个基本块、18 条指令
///   - 非叶子小函数（调用其他用户函数）：最多 8 个基本块、40 条指令
///   - 循环内热点调用点上的单层叶子循环函数：要求无 alloca、调用点数量受限
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
#include "DominatorTree.h"
#include "FCmpInst.h"
#include "FPToSIInst.h"
#include "FunctionSideEffectAnalysis.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "GlobalVariable.h"
#include "ICmpInst.h"
#include "LoadInst.h"
#include "LoopInfo.h"
#include "MemoryAccess.h"
#include "MemoryLocation.h"
#include "Module.h"
#include "PhiInst.h"
#include "ReturnInst.h"
#include "SIToFPInst.h"
#include "StoreInst.h"
#include "ZExtInst.h"

namespace {

/// @brief 内联最大轮次，防止无限循环
constexpr int32_t kMaxInlineRounds = 256;
/// @brief 叶子函数允许的最大基本块数
constexpr int32_t kLeafMaxBlocks = 8;
/// @brief 叶子函数允许的最大指令数
constexpr int32_t kLeafMaxInsts = 36;
/// @brief 非叶子小函数允许的最大基本块数
constexpr int32_t kSmallMaxBlocks = 8;
/// @brief 非叶子小函数允许的最大指令数
constexpr int32_t kSmallMaxInsts = 40;
/// @brief 循环内热点调用点允许的叶子函数最大基本块数
constexpr int32_t kHotLeafMaxBlocks = 64;
/// @brief 循环内热点调用点允许的叶子函数最大指令数
constexpr int32_t kHotLeafMaxInsts = 300;
/// @brief 循环内热点调用点允许的非叶子函数最大基本块数
constexpr int32_t kHotSmallMaxBlocks = 32;
/// @brief 循环内热点调用点允许的非叶子函数最大指令数
constexpr int32_t kHotSmallMaxInsts = 180;
/// @brief 循环内热点调用点允许的单层叶子循环函数最大基本块数
constexpr int32_t kHotLoopLeafMaxBlocks = 12;
/// @brief 循环内热点调用点允许的单层叶子循环函数最大指令数
constexpr int32_t kHotLoopLeafMaxInsts = 64;
/// @brief 循环内热点调用点允许的单层叶子循环函数最大循环体基本块数
constexpr int32_t kHotLoopLeafMaxBodyBlocks = 8;
/// @brief 循环内热点调用点允许的单层叶子循环函数最大直接调用点数
constexpr int32_t kHotLoopLeafMaxCallSites = 8;
/// @brief 被内联函数的 alloca 总字节数上限，防止栈帧膨胀
constexpr int32_t kMaxAllocaBytes = 128;
/// @brief 循环内热点调用点的 alloca 总字节数上限
constexpr int32_t kHotMaxAllocaBytes = 256;

/// @brief 自然循环摘要
struct NaturalLoopSummary {
    int32_t loopCount = 0;
    int32_t maxDepth = 0;
    int32_t maxLoopBodyBlocks = 0;
};

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

/// @brief 分析函数中的自然循环规模与嵌套情况
/// @param func 目标函数
/// @return 自然循环摘要
NaturalLoopSummary analyzeNaturalLoops(Function * func)
{
    NaturalLoopSummary summary;
    if (!func || func->getBlocks().empty()) {
        return summary;
    }

    DominatorTree domTree(func);
    LoopInfo loopInfo(func, &domTree);
    for (auto * bb : func->getBlocks()) {
        summary.maxDepth = std::max(summary.maxDepth, loopInfo.getLoopDepth(bb));
        if (loopInfo.isLoopHeader(bb)) {
            ++summary.loopCount;
            if (const auto * body = loopInfo.getLoopBody(bb)) {
                summary.maxLoopBodyBlocks =
                    std::max(summary.maxLoopBodyBlocks, static_cast<int32_t>(body->size()));
            }
        }
    }
    return summary;
}

/// @brief 统计模块中直接调用某个函数的调用点数量
///
/// 遍历模块中所有非内置函数，统计直接调用 callee 的调用点个数。
/// 用于判断 callee 是否为单调用点函数
/// @param mod 所属模块
/// @param callee 被调用函数
/// @return 直接调用点个数
int32_t countDirectCallSites(Module * mod, Function * callee)
{
    if (!mod || !callee) {
        return 0;
    }

    int32_t count = 0;
    for (auto * function : mod->getFunctionList()) {
        if (!function || function->isBuiltin()) {
            continue;
        }
        for (auto * bb : function->getBlocks()) {
            for (auto * inst : bb->getInstructions()) {
                auto * call = dynamic_cast<CallInst *>(inst);
                if (call && call->getCallee() == callee) {
                    ++count;
                }
            }
        }
    }
    return count;
}

/// @brief 判断热点调用点上的单层叶子循环函数是否值得内联
/// @param mod 所属模块
/// @param callee 被调函数
/// @param loopSummary 循环摘要
/// @param allocaBytes alloca 总字节数
/// @param blockCount 基本块数量
/// @param instCount 指令数量
/// @return true 表示允许在热点调用点内联
bool isEligibleHotLoopLeafCallee(Module * mod,
                                 Function * callee,
                                 const NaturalLoopSummary & loopSummary,
                                 int32_t allocaBytes,
                                 int32_t blockCount,
                                 int32_t instCount)
{
    if (!mod || !callee || loopSummary.loopCount != 1 || loopSummary.maxDepth > 1) {
        return false;
    }

    if (allocaBytes != 0) {
        return false;
    }

    if (countDirectCallSites(mod, callee) > kHotLoopLeafMaxCallSites) {
        return false;
    }

    return blockCount <= kHotLoopLeafMaxBlocks && instCount <= kHotLoopLeafMaxInsts &&
           loopSummary.maxLoopBodyBlocks <= kHotLoopLeafMaxBodyBlocks;
}

using SideEffectAnalyzer = FunctionSideEffectAnalysis;

/// @brief 判断 loop 内是否可能改写某个可识别 load 的地址。
bool loopMayClobberLoad(Value * pointer, const std::unordered_set<BasicBlock *> & loopBody)
{
    SideEffectAnalyzer sideEffects;
    return blocksMayClobberLoad(pointer,
                                loopBody,
                                [&sideEffects](CallInst * call) {
                                    return call != nullptr && !sideEffects.isSideEffectFree(call->getCallee());
                                });
}

bool valueIsLoopInvariant(Value * value,
                          const std::unordered_set<BasicBlock *> & loopBody,
                          std::unordered_map<Value *, bool> & memo,
                          std::unordered_set<Value *> & visiting)
{
    if (!value) {
        return false;
    }

    auto * inst = dynamic_cast<Instruction *>(value);
    if (!inst) {
        return true;
    }

    BasicBlock * block = inst->getParentBlock();
    if (!block || loopBody.find(block) == loopBody.end()) {
        return true;
    }

    auto cached = memo.find(value);
    if (cached != memo.end()) {
        return cached->second;
    }

    if (!visiting.insert(value).second) {
        memo[value] = false;
        return false;
    }

    bool invariant = false;
    if (auto * gep = dynamic_cast<GetElementPtrInst *>(inst)) {
        invariant = valueIsLoopInvariant(gep->getBasePointer(), loopBody, memo, visiting) &&
                    valueIsLoopInvariant(gep->getIndexOperand(), loopBody, memo, visiting);
    } else if (auto * load = dynamic_cast<LoadInst *>(inst)) {
        invariant = valueIsLoopInvariant(load->getPointerOperand(), loopBody, memo, visiting) &&
                    !loopMayClobberLoad(load->getPointerOperand(), loopBody);
    } else if (!dynamic_cast<PhiInst *>(inst) && !dynamic_cast<CallInst *>(inst) &&
               !dynamic_cast<StoreInst *>(inst) && !inst->mayHaveSideEffects() &&
               !inst->mayReadMemory() && !inst->mayWriteMemory()) {
        invariant = true;
        for (auto * operand : inst->getOperandsValue()) {
            if (!valueIsLoopInvariant(operand, loopBody, memo, visiting)) {
                invariant = false;
                break;
            }
        }
    }

    visiting.erase(value);
    memo[value] = invariant;
    return invariant;
}

bool operandsAreLoopInvariant(Instruction * inst, const std::unordered_set<BasicBlock *> & loopBody)
{
    if (!inst) {
        return false;
    }

    std::unordered_map<Value *, bool> memo;
    std::unordered_set<Value *> visiting;
    for (auto * operand : inst->getOperandsValue()) {
        if (!valueIsLoopInvariant(operand, loopBody, memo, visiting)) {
            return false;
        }
    }
    return true;
}

/// @brief 判断循环头是否只有来自 latch 的唯一回边。
bool hasUniqueLoopBackedge(BasicBlock * header,
                           BasicBlock * latch,
                           const std::unordered_set<BasicBlock *> & loopBody)
{
    if (!header || !latch) {
        return false;
    }

    int insidePreds = 0;
    for (auto * pred : header->getPredecessors()) {
        if (loopBody.find(pred) == loopBody.end()) {
            continue;
        }
        ++insidePreds;
        if (pred != latch) {
            return false;
        }
    }
    return insidePreds == 1;
}

bool isCacheableLoopCall(CallInst * call,
                         const std::unordered_set<BasicBlock *> & loopBody,
                         SideEffectAnalyzer & sideEffects)
{
    return call != nullptr && call->hasResultValue() &&
           (call->getType()->isIntegerType() || call->getType()->isFloatType()) &&
           sideEffects.isSideEffectFree(call->getCallee()) &&
           operandsAreLoopInvariant(call, loopBody);
}

/// @brief 判断基本块是否会让 PureCallLoopCache 的跨迭代缓存失效
/// @param bb 待检查的基本块
/// @param ignoredCall 允许忽略的候选纯调用本身
/// @param sideEffects 共享副作用分析器
/// @return true 表示该块中存在 store 或非纯调用
bool blockInvalidatesPureCallCache(BasicBlock * bb,
                                   CallInst * ignoredCall,
                                   SideEffectAnalyzer & sideEffects)
{
    if (!bb) {
        return true;
    }

    for (auto * inst : bb->getInstructions()) {
        if (!inst || inst->isDead() || inst == ignoredCall) {
            continue;
        }
        if (dynamic_cast<StoreInst *>(inst)) {
            return true;
        }
        if (auto * call = dynamic_cast<CallInst *>(inst)) {
            if (!sideEffects.isSideEffectFree(call->getCallee())) {
                return true;
            }
        }
    }
    return false;
}

/// @brief 若调用正好是 PureCallLoopCache 可处理的 latch 纯调用，保留给缓存 pass。
bool shouldPreserveForPureCallLoopCache(CallInst * call)
{
    if (!call || !call->getFunction() || !call->getParentBlock() || !call->hasResultValue()) {
        return false;
    }

    Function * caller = call->getFunction();
    DominatorTree domTree(caller);
    LoopInfo loopInfo(caller, &domTree);

    BasicBlock * latch = call->getParentBlock();
    auto * branch = dynamic_cast<BranchInst *>(latch->getTerminator());
    if (!branch) {
        return false;
    }

    BasicBlock * header = branch->getTarget();
    if (!loopInfo.isLoopHeader(header)) {
        return false;
    }

    const auto * loopBody = loopInfo.getLoopBody(header);
    if (!loopBody || loopBody->find(latch) == loopBody->end() ||
        !hasUniqueLoopBackedge(header, latch, *loopBody)) {
        return false;
    }

    SideEffectAnalyzer sideEffects;
    CallInst * candidate = nullptr;
    for (auto * inst : latch->getInstructions()) {
        auto * currentCall = dynamic_cast<CallInst *>(inst);
        if (!isCacheableLoopCall(currentCall, *loopBody, sideEffects)) {
            continue;
        }
        if (candidate != nullptr) {
            return false;
        }
        candidate = currentCall;
    }

    if (candidate != call) {
        return false;
    }

    for (auto * bb : *loopBody) {
        if (blockInvalidatesPureCallCache(bb, bb == latch ? candidate : nullptr, sideEffects)) {
            return false;
        }
    }

    return true;
}

/// @brief 判断调用点是否位于自然循环内。
bool isCallInLoop(CallInst * call)
{
    if (!call || !call->getFunction() || !call->getParentBlock()) {
        return false;
    }

    Function * caller = call->getFunction();
    if (caller->getBlocks().empty()) {
        return false;
    }

    DominatorTree domTree(caller);
    LoopInfo loopInfo(caller, &domTree);
    return loopInfo.getLoopDepth(call->getParentBlock()) > 0;
}

/// @brief 判断函数体是否包含自然循环。
/// 通过构建支配树和循环信息，检查是否存在循环头基本块。
/// 用于避免将包含循环的"循环密集型"函数内联到循环内的热点调用点，
/// 防止内联后循环嵌套加深导致性能退化。
/// @param func 待检查的函数
/// @return 若函数体包含自然循环则返回true
bool containsNaturalLoop(Function * func)
{
    if (!func || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    DominatorTree domTree(func);
    LoopInfo loopInfo(func, &domTree);
    for (auto * bb : func->getBlocks()) {
        if (loopInfo.isLoopHeader(bb)) {
            return true;
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

/// @brief 将内联产生的 alloca 放回 caller 入口块，便于后续 mem2reg 提升。
void insertAllocaIntoEntry(Function * caller, AllocaInst * alloca)
{
    if (!caller || !alloca || !caller->getEntryBlock()) {
        return;
    }

    BasicBlock * entry = caller->getEntryBlock();
    auto & insts = entry->getInstructions();
    auto insertPos = insts.end();
    if (!insts.empty() && insts.back()->isTerminator()) {
        insertPos = std::prev(insts.end());
    }

    alloca->setParentBlock(entry);
    insts.insert(insertPos, alloca);
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

        // 为当前 caller 构建支配树和循环信息，用于判断调用点是否在循环内
        DominatorTree domTree(caller);
        LoopInfo loopInfo(caller, &domTree);
        std::vector<BasicBlock *> blocks = caller->getBlocks();
        for (auto * bb : blocks) {
            std::vector<Instruction *> insts(bb->getInstructions().begin(), bb->getInstructions().end());
            for (auto * inst : insts) {
                auto * call = dynamic_cast<CallInst *>(inst);
                if (!call || call->getParentBlock() != bb) {
                    continue;
                }

                if (shouldInlineCallee(caller, call, loopInfo.getLoopDepth(bb))) {
                    return inlineCall(call);
                }
            }
        }
    }

    return false;
}

/// @brief 判断 callee 是否满足内联条件
/// @param caller 调用方函数
/// @param call 调用点
/// @param callLoopDepth 调用点所在循环深度
/// @return true 表示可以内联该 callee
bool SmallFunctionInline::shouldInlineCallee(Function * caller, CallInst * call, int32_t callLoopDepth) const
{
    Function * callee = call ? call->getCallee() : nullptr;
    if (!caller || !callee || callee->isBuiltin() || callee == caller || callee->getBlocks().empty()) {
        return false;
    }

    if (callee->getParams().size() > 8 || containsSelfCall(callee)) {
        return false;
    }

    if (shouldPreserveForPureCallLoopCache(call)) {
        return false;
    }

    const bool hotCallSite = callLoopDepth > 0;
    const bool leafCallee = !hasNonSelfUserCall(callee);
    const int32_t maxAllocaBytes = hotCallSite ? kHotMaxAllocaBytes : kMaxAllocaBytes;
    const int32_t allocaBytes = getAllocaBytes(callee);
    if (allocaBytes > maxAllocaBytes) {
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
    const NaturalLoopSummary loopSummary = analyzeNaturalLoops(callee);

    if (loopSummary.loopCount > 0) {
        if (!hotCallSite || !leafCallee) {
            return false;
        }
        return isEligibleHotLoopLeafCallee(mod, callee, loopSummary, allocaBytes, blockCount, instCount);
    }

    // 叶子函数：普通调用点保守，循环内热点调用点使用更宽松阈值。
    if (leafCallee) {
        const int32_t maxBlocks = hotCallSite ? kHotLeafMaxBlocks : kLeafMaxBlocks;
        const int32_t maxInsts = hotCallSite ? kHotLeafMaxInsts : kLeafMaxInsts;
        return blockCount <= maxBlocks && instCount <= maxInsts;
    }

    // 非叶子小函数：仍限制体积，热点调用点允许更大的非递归小函数。
    const int32_t maxBlocks = hotCallSite ? kHotSmallMaxBlocks : kSmallMaxBlocks;
    const int32_t maxInsts = hotCallSite ? kHotSmallMaxInsts : kSmallMaxInsts;
    return blockCount <= maxBlocks && instCount <= maxInsts;
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

            if (auto * alloca = dynamic_cast<AllocaInst *>(cloned)) {
                insertAllocaIntoEntry(caller, alloca);
            } else {
                cloneBB->addInstruction(cloned);
            }
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
