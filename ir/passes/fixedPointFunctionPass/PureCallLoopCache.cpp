///
/// @file PureCallLoopCache.cpp
/// @brief 循环内纯调用缓存 pass 实现。
///
/// 本 pass 对循环中实参不变的纯函数调用进行缓存优化。
/// 核心思路：若循环 latch 块中存在一个纯函数调用，且其所有实参在循环内不变，
/// 则该调用每轮迭代返回相同结果，可通过缓存避免重复计算。
///
/// 实现方式：
///   1. 在循环头插入 cache phi（缓存上一轮调用的返回值）和 valid phi（标记缓存是否有效）；
///   2. 将 latch 中的调用拆分为 check/call/reuse/cont 四个基本块：
///      - check 块：判断 valid phi 是否为真，若真则跳到 reuse，否则跳到 call；
///      - call 块：执行实际调用，结果进入 cont 块的 result phi；
///      - reuse 块：直接使用 cache phi 的值，进入 cont 块的 result phi；
///      - cont 块：通过 result phi 合并两条路径的结果。
///   3. valid phi 的语义：若循环体中存在 store 或非纯调用，则缓存失效（valid=false）；
///      否则 valid 沿着循环体传播，保持上一轮的有效性。
///
/// 这是一种通用的循环优化，不依赖特定函数名或输入模式。
///

#include "PureCallLoopCache.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "CondBranchInst.h"
#include "ConstFloat.h"
#include "ConstInteger.h"
#include "DominatorTree.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "GlobalVariable.h"
#include "Instruction.h"
#include "IntegerType.h"
#include "LoadInst.h"
#include "LoopInfo.h"
#include "MemoryLocation.h"
#include "Module.h"
#include "PhiInst.h"
#include "StoreInst.h"
#include "Type.h"
#include "Value.h"

namespace {

/// @brief 沿 GEP 链找到指针根对象。
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

/// @brief store 只写入非逃逸局部 alloca 时，不构成函数外可见副作用。
bool isNonEscapingLocalStore(StoreInst * store)
{
    if (!store) {
        return false;
    }

    auto * alloca = dynamic_cast<AllocaInst *>(getPointerRoot(store->getPointerOperand()));
    return alloca != nullptr && !doesPointerEscape(alloca);
}

/// @brief 函数纯度分析状态枚举
enum class PurityState {
    Unknown,   ///< 尚未分析
    Visiting,  ///< 正在访问（用于检测递归）
    Pure,      ///< 纯函数：无副作用，相同输入始终返回相同结果
    Impure,    ///< 非纯函数：有副作用或依赖外部状态
};

/// @brief 函数纯度分析器
///
/// 通过递归分析函数体中的指令来判断函数是否为纯函数。
/// 纯函数的判定条件：没有函数外可见写入，局部非逃逸 alloca 的 store
/// 可作为临时变量使用，所有调用也都满足同样条件。
class PurityAnalyzer {
public:
    /// @brief 判断函数是否为纯函数
    /// @param function 待分析的函数
    /// @return true 表示该函数是纯函数
    bool isPure(Function * function)
    {
        if (!function || function->isBuiltin() || function->getBlocks().empty()) {
            return false;
        }

        auto it = states.find(function);
        if (it != states.end()) {
            return it->second == PurityState::Pure;
        }

        states[function] = PurityState::Visiting;
        bool pure = true;
        for (auto * bb : function->getBlocks()) {
            for (auto * inst : bb->getInstructions()) {
                if (!isAllowed(inst)) {
                    pure = false;
                    break;
                }
            }
            if (!pure) {
                break;
            }
        }

        states[function] = pure ? PurityState::Pure : PurityState::Impure;
        return pure;
    }

private:
    /// @brief 判断单条指令是否允许出现在纯函数中
    /// @param inst 待检查的指令
    /// @return true 表示该指令不破坏纯性
    bool isAllowed(Instruction * inst)
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
            if (it != states.end() && it->second == PurityState::Visiting) {
                return false;
            }
            return isPure(call->getCallee());
        }

        return !inst->mayHaveSideEffects();
    }

    std::unordered_map<Function *, PurityState> states;  ///< 函数到纯度状态的映射
};

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

/// @brief 判断 loop 内是否可能改写某个可识别 load 的地址。
bool loopMayClobberLoad(Value * pointer, const std::unordered_set<BasicBlock *> & loopBody)
{
    MemoryLocation location = normalizeMemoryLocation(pointer);
    if (location.isPrecise() && !doesPointerEscape(location.object)) {
        for (auto * bb : loopBody) {
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

    PurityAnalyzer purity;
    for (auto * bb : loopBody) {
        for (auto * inst : bb->getInstructions()) {
            if (auto * store = dynamic_cast<StoreInst *>(inst)) {
                Value * storeRoot = getPointerRoot(store->getPointerOperand());
                if (storeRoot == global || (!dynamic_cast<AllocaInst *>(storeRoot) &&
                                            !dynamic_cast<GlobalVariable *>(storeRoot))) {
                    return true;
                }
                continue;
            }

            auto * call = dynamic_cast<CallInst *>(inst);
            if (call && !purity.isPure(call->getCallee())) {
                return true;
            }
        }
    }

    return false;
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

/// @brief 判断调用指令的所有操作数是否循环不变
/// @param call 待检查的调用指令
/// @param loopBody 循环体基本块集合
/// @return true 表示所有实参在循环各轮中保持相同
bool operandsAreLoopInvariant(CallInst * call, const std::unordered_set<BasicBlock *> & loopBody)
{
    if (!call) {
        return false;
    }

    std::unordered_map<Value *, bool> memo;
    std::unordered_set<Value *> visiting;
    for (auto * operand : call->getOperandsValue()) {
        if (!valueIsLoopInvariant(operand, loopBody, memo, visiting)) {
            return false;
        }
    }
    return true;
}

/// @brief 判断基本块是否会使缓存失效
///
/// 若基本块中包含 store 指令或非纯函数调用，则该块可能修改内存状态，
/// 导致缓存的纯调用结果不再有效。
/// @param bb 待检查的基本块
/// @return true 表示该块会使缓存失效
bool blockInvalidatesCache(BasicBlock * bb)
{
    if (!bb) {
        return true;
    }

    for (auto * inst : bb->getInstructions()) {
        if (!inst || inst->isDead()) {
            continue;
        }
        if (dynamic_cast<StoreInst *>(inst)) {
            return true;
        }
        auto * call = dynamic_cast<CallInst *>(inst);
        if (call) {
            PurityAnalyzer purity;
            if (!purity.isPure(call->getCallee())) {
                return true;
            }
        }
    }
    return false;
}

/// @brief 为给定类型创建零值默认值，用于缓存 phi 的初始值
/// @param mod 所属模块
/// @param type 值类型
/// @return 对应类型的零值常量，不支持的类型返回 nullptr
Value * makeDefaultValue(Module * mod, Type * type)
{
    if (!mod || !type) {
        return nullptr;
    }
    if (type->isFloatType()) {
        return mod->newConstFloat(0.0f);
    }
    if (type->isIntegerType()) {
        return mod->newConstInteger(type, 0);
    }
    return nullptr;
}

/// @brief 在基本块开头插入 phi 指令
/// @param bb 目标基本块
/// @param phi 待插入的 phi 指令
void insertPhiAtBlockStart(BasicBlock * bb, PhiInst * phi)
{
    if (!bb || !phi) {
        return;
    }
    phi->setParentBlock(bb);
    bb->getInstructions().insert(bb->getInstructions().begin(), phi);
}

/// @brief 将基本块中所有 phi 指令的某个 incoming 前驱块替换为新块
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

/// @brief 判断循环头是否具有唯一的回边（即唯一来自循环内部的 latch 前驱）
/// @param header 循环头基本块
/// @param latch 循环 latch 块
/// @param loopBody 循环体基本块集合
/// @return true 表示循环头有且仅有一条来自 latch 的回边
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

/// @brief 在 latch 块中查找唯一可缓存的纯函数调用
///
/// 可缓存的条件：调用是纯函数调用、有返回值、实参循环不变，
/// 且 latch 块中最多只有一个满足条件的调用（避免多个调用的缓存冲突）。
/// @param latch 循环 latch 块
/// @param loopBody 循环体基本块集合
/// @param purity 纯度分析器
/// @return 找到的可缓存调用，不存在或多个则返回 nullptr
CallInst * findCacheableLatchCall(BasicBlock * latch,
                                  const std::unordered_set<BasicBlock *> & loopBody,
                                  PurityAnalyzer & purity)
{
    if (!latch || !latch->getTerminator()) {
        return nullptr;
    }

    CallInst * candidate = nullptr;
    for (auto * inst : latch->getInstructions()) {
        auto * call = dynamic_cast<CallInst *>(inst);
        if (!call || !call->hasResultValue()) {
            continue;
        }
        if (!purity.isPure(call->getCallee()) || !operandsAreLoopInvariant(call, loopBody)) {
            continue;
        }
        if (candidate != nullptr) {
            return nullptr;
        }
        candidate = call;
    }
    return candidate;
}

/// @brief 调用块拆分结果：将包含调用的块拆分为 check/call/reuse/cont 四个块
struct SplitCallBlocks {
    BasicBlock * checkBlock = nullptr;  ///< 检查缓存有效性的块
    BasicBlock * callBlock = nullptr;   ///< 执行实际调用的块
    BasicBlock * reuseBlock = nullptr;  ///< 复用缓存值的块
    BasicBlock * contBlock = nullptr;   ///< 合并两条路径结果的块
    PhiInst * resultPhi = nullptr;      ///< 合并调用结果与缓存值的 phi 节点
};

/// @brief 将包含纯调用的基本块拆分为 check/call/reuse/cont 四个块
///
/// 拆分后的控制流：
///   check → [valid? reuse : call]
///   call → cont (携带实际调用结果)
///   reuse → cont (携带缓存值)
///   cont 中的 result_phi 合并两条路径的结果
/// @param func 所属函数
/// @param call 待拆分的调用指令
/// @param cachedValue 缓存值（cache phi）
/// @param validAtCall 调用点处的缓存有效性标记
/// @return 拆分结果，失败时各指针为 nullptr
SplitCallBlocks splitCallBlock(Function * func, CallInst * call, Value * cachedValue, Value * validAtCall)
{
    SplitCallBlocks result;
    if (!func || !call || !cachedValue || !validAtCall || !call->getParentBlock()) {
        return result;
    }

    BasicBlock * checkBlock = call->getParentBlock();
    auto & checkInsts = checkBlock->getInstructions();
    auto callPos = std::find(checkInsts.begin(), checkInsts.end(), static_cast<Instruction *>(call));
    if (callPos == checkInsts.end()) {
        return result;
    }

    std::vector<BasicBlock *> oldSuccessors = checkBlock->getSuccessors();
    if (oldSuccessors.empty()) {
        return result;
    }

    auto * callBlock = func->newBasicBlock();
    auto * reuseBlock = func->newBasicBlock();
    auto * contBlock = func->newBasicBlock();

    auto afterCall = std::next(callPos);
    callBlock->getInstructions().splice(callBlock->getInstructions().end(), checkInsts, callPos);
    call->setParentBlock(callBlock);
    contBlock->getInstructions().splice(contBlock->getInstructions().end(), checkInsts, afterCall, checkInsts.end());
    for (auto * inst : contBlock->getInstructions()) {
        inst->setParentBlock(contBlock);
    }

    checkBlock->getSuccessors().clear();
    for (auto * succ : oldSuccessors) {
        succ->removePredecessor(checkBlock);
        succ->addPredecessor(contBlock);
        contBlock->addSuccessor(succ);
        replacePhiIncomingBlock(succ, checkBlock, contBlock);
    }

    auto * branchToCache = new CondBranchInst(func, validAtCall, reuseBlock, callBlock);
    checkBlock->addInstruction(branchToCache);
    checkBlock->linkSuccessor(reuseBlock);
    checkBlock->linkSuccessor(callBlock);

    auto * callToCont = new BranchInst(func, contBlock);
    callBlock->addInstruction(callToCont);
    callBlock->linkSuccessor(contBlock);

    auto * reuseToCont = new BranchInst(func, contBlock);
    reuseBlock->addInstruction(reuseToCont);
    reuseBlock->linkSuccessor(contBlock);

    auto * resultPhi = new PhiInst(func, call->getType());
    resultPhi->setParentBlock(contBlock);
    contBlock->getInstructions().insert(contBlock->getInstructions().begin(), resultPhi);
    call->replaceAllUseWith(resultPhi);
    resultPhi->addIncoming(cachedValue, reuseBlock);
    resultPhi->addIncoming(call, callBlock);

    result.checkBlock = checkBlock;
    result.callBlock = callBlock;
    result.reuseBlock = reuseBlock;
    result.contBlock = contBlock;
    result.resultPhi = resultPhi;
    return result;
}

/// @brief 对循环内的纯函数调用执行缓存优化
///
/// 在循环头插入 cache phi 和 valid phi，将 latch 中的调用拆分为
/// 条件分支（缓存有效则复用，否则执行调用），并设置 valid phi 的
/// incoming 值：若某基本块会使缓存失效则 valid=false，否则传播上一轮的 valid。
/// @param func 所属函数
/// @param mod 所属模块
/// @param header 循环头基本块
/// @param loopBody 循环体基本块集合
/// @param latch 循环 latch 块
/// @param call 待缓存的纯函数调用
/// @return true 表示成功执行缓存优化
bool cacheCallInLoop(Function * func,
                     Module * mod,
                     BasicBlock * header,
                     const std::unordered_set<BasicBlock *> & loopBody,
                     BasicBlock * latch,
                     CallInst * call)
{
    if (!func || !mod || !header || !latch || !call) {
        return false;
    }

    Value * defaultCache = makeDefaultValue(mod, call->getType());
    if (!defaultCache) {
        return false;
    }

    auto * falseValue = mod->newConstInteger(IntegerType::getTypeInt1(), 0);
    auto * trueValue = mod->newConstInteger(IntegerType::getTypeInt1(), 1);

    std::unordered_map<BasicBlock *, PhiInst *> validIn;
    std::unordered_map<BasicBlock *, bool> invalidates;
    for (auto * bb : loopBody) {
        auto * phi = new PhiInst(func, IntegerType::getTypeInt1());
        insertPhiAtBlockStart(bb, phi);
        validIn[bb] = phi;
        invalidates[bb] = blockInvalidatesCache(bb);
    }

    Value * validAtCall = invalidates[latch] ? static_cast<Value *>(falseValue) : static_cast<Value *>(validIn[latch]);
    auto split = splitCallBlock(func, call, defaultCache, validAtCall);
    if (!split.contBlock || !split.resultPhi) {
        return false;
    }

    auto * cachePhi = new PhiInst(func, call->getType());
    insertPhiAtBlockStart(header, cachePhi);

    const bool contInvalidates = blockInvalidatesCache(split.contBlock);
    for (auto * bb : loopBody) {
        auto * phi = validIn[bb];
        if (!phi) {
            return false;
        }

        for (auto * pred : bb->getPredecessors()) {
            Value * incoming = falseValue;
            if (bb == header && pred == split.contBlock) {
                incoming = contInvalidates ? static_cast<Value *>(falseValue) : static_cast<Value *>(trueValue);
            } else if (loopBody.find(pred) != loopBody.end()) {
                incoming = invalidates[pred] ? static_cast<Value *>(falseValue) : static_cast<Value *>(validIn[pred]);
            }
            phi->addIncoming(incoming, pred);
        }
    }

    for (auto * pred : header->getPredecessors()) {
        if (pred == split.contBlock) {
            cachePhi->addIncoming(split.resultPhi, pred);
        } else {
            cachePhi->addIncoming(defaultCache, pred);
        }
    }

    split.resultPhi->replaceOperand(defaultCache, cachePhi);
    return true;
}

} // namespace

PureCallLoopCache::PureCallLoopCache(Function * _func, Module * _mod) : func(_func), mod(_mod)
{}

bool PureCallLoopCache::run()
{
    if (!func || !mod || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    DominatorTree domTree(func);
    LoopInfo loopInfo(func, &domTree);
    PurityAnalyzer purity;

    std::vector<BasicBlock *> headers;
    for (auto * bb : func->getBlocks()) {
        if (loopInfo.isLoopHeader(bb)) {
            headers.push_back(bb);
        }
    }

    std::stable_sort(headers.begin(),
                     headers.end(),
                     [&loopInfo](BasicBlock * lhs, BasicBlock * rhs) {
                         return loopInfo.getLoopDepth(lhs) < loopInfo.getLoopDepth(rhs);
                     });

    for (auto * header : headers) {
        const auto * loopBody = loopInfo.getLoopBody(header);
        if (!loopBody || loopBody->empty()) {
            continue;
        }

        for (auto * pred : header->getPredecessors()) {
            if (loopBody->find(pred) == loopBody->end()) {
                continue;
            }
            auto * term = pred->getTerminator();
            auto * branch = dynamic_cast<BranchInst *>(term);
            if (!branch || branch->getTarget() != header || !hasUniqueLoopBackedge(header, pred, *loopBody)) {
                continue;
            }

            auto * call = findCacheableLatchCall(pred, *loopBody, purity);
            if (!call) {
                continue;
            }

            if (cacheCallInLoop(func, mod, header, *loopBody, pred, call)) {
                return true;
            }
        }
    }

    return false;
}
