///
/// @file LoopTiling.cpp
/// @brief 保守的二维循环分块 pass 实现
///

#include "LoopTiling.h"

#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <vector>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "CondBranchInst.h"
#include "ConstInteger.h"
#include "DominatorTree.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "GlobalVariable.h"
#include "ICmpInst.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "LoopInfo.h"
#include "Module.h"
#include "PhiInst.h"
#include "ScalarEvolution.h"
#include "StoreInst.h"
#include "Value.h"

namespace {

constexpr int32_t kMinTileTripCount = 64;
constexpr int32_t kLargeNestTileSize = 128;
constexpr int32_t kLargeNestTripCount = 128;

using CanonicalLoop = ScalarEvolution::CanonicalLoop;

enum class RootKind {
    Unknown,
    Formal,
    Global,
    Alloca,
};

struct PointerRoot {
    RootKind kind = RootKind::Unknown;
    Value * value = nullptr;
};

ConstInteger * asConstInt(Value * value)
{
    return dynamic_cast<ConstInteger *>(value);
}

bool sameRoot(const PointerRoot & lhs, const PointerRoot & rhs);
bool isKnownRoot(const PointerRoot & root);
PointerRoot stripPointerRoot(Value * value);
bool isAddConstStep(Value * value, PhiInst * induction, int32_t expectedStep);

/// @brief 判断值是否定义在循环体内部
/// @param value 待判断的值
/// @param loopBody 循环体基本块集合
/// @return 若值是指令且定义在循环体内则返回true
bool isDefinedInLoop(Value * value, const std::unordered_set<BasicBlock *> & loopBody)
{
    auto * inst = dynamic_cast<Instruction *>(value);
    return inst && loopBody.find(inst->getParentBlock()) != loopBody.end();
}

/// @brief 递归判断value的操作数链中是否依赖needle
/// @param value 待判断的值
/// @param needle 目标依赖值
/// @param visiting 已访问集合，防止循环
/// @return 若value依赖needle则返回true
bool valueDependsOn(Value * value, Value * needle, std::unordered_set<Value *> & visiting)
{
    if (value == needle) {
        return true;
    }
    if (!value || !visiting.insert(value).second) {
        return false;
    }

    auto * inst = dynamic_cast<Instruction *>(value);
    if (!inst) {
        return false;
    }

    for (auto * operand : inst->getOperandsValue()) {
        if (valueDependsOn(operand, needle, visiting)) {
            return true;
        }
    }

    return false;
}

/// @brief valueDependsOn的无状态包装，内部创建visiting集合
bool valueDependsOn(Value * value, Value * needle)
{
    std::unordered_set<Value *> visiting;
    return valueDependsOn(value, needle, visiting);
}

/// @brief 获取值在函数形参列表中的索引
/// @param func 所属函数
/// @param value 待查找的值
/// @return 形参索引，若不是形参则返回-1
int32_t formalParamIndex(Function * func, Value * value)
{
    if (!func || !value) {
        return -1;
    }

    auto & params = func->getParams();
    for (std::size_t index = 0; index < params.size(); ++index) {
        if (params[index] == value) {
            return static_cast<int32_t>(index);
        }
    }

    return -1;
}

/// @brief 获取指针的形参根在函数形参列表中的索引
/// @param func 所属函数
/// @param pointer 待分析的指针
/// @return 形参索引，若指针根不是形参则返回-1
int32_t pointerFormalIndex(Function * func, Value * pointer)
{
    PointerRoot root = stripPointerRoot(pointer);
    if (root.kind != RootKind::Formal) {
        return -1;
    }
    return formalParamIndex(func, root.value);
}

/// @brief 被调用函数的内存访问摘要，记录读写操作涉及的形参索引
struct CalleeMemorySummary {
    bool valid = false;                          // 摘要是否有效
    std::unordered_set<int32_t> readArgs;        // 被读取的形参索引集合
    std::unordered_set<int32_t> writtenArgs;     // 被写入的形参索引集合
};

/// @brief 分析被调用函数的内存访问模式，生成读写形参摘要
/// 遍历被调用函数体中的load/store指令，记录涉及的形参索引
/// 若存在嵌套调用、store目标不是形参、或有其他内存写操作则标记为无效
/// @param callee 被调用函数
/// @return 内存访问摘要
CalleeMemorySummary summarizeCalleeMemory(Function * callee)
{
    CalleeMemorySummary summary;
    if (!callee || callee->isBuiltin() || callee->getBlocks().empty()) {
        return summary;
    }

    summary.valid = true;
    for (auto * bb : callee->getBlocks()) {
        for (auto * inst : bb->getInstructions()) {
            if (dynamic_cast<CallInst *>(inst)) {
                summary.valid = false;
                return summary;
            }

            if (auto * load = dynamic_cast<LoadInst *>(inst)) {
                int32_t index = pointerFormalIndex(callee, load->getPointerOperand());
                if (index >= 0) {
                    summary.readArgs.insert(index);
                }
                continue;
            }

            if (auto * store = dynamic_cast<StoreInst *>(inst)) {
                int32_t index = pointerFormalIndex(callee, store->getPointerOperand());
                if (index < 0) {
                    summary.valid = false;
                    return summary;
                }
                summary.writtenArgs.insert(index);
                continue;
            }

            if (inst->mayWriteMemory()) {
                summary.valid = false;
                return summary;
            }
        }
    }

    summary.valid = summary.valid && !summary.writtenArgs.empty();
    return summary;
}

/// @brief 判断指针根是否在给定根集合中
bool rootInSet(const PointerRoot & root, const std::vector<PointerRoot> & roots)
{
    return std::any_of(roots.begin(), roots.end(), [&root](const PointerRoot & item) {
        return sameRoot(root, item);
    });
}

/// @brief 判断循环体在重复执行时是否保持不变性
/// 检查条件：store目标必须在writableRoots中且不依赖归纳变量，
/// 除preservedCall外无其他调用，条件分支不依赖归纳变量（循环条件除外）
/// @param loopBody 循环体基本块集合
/// @param induction 归纳变量
/// @param loopCmp 循环比较指令
/// @param inductionNext 归纳变量步进指令
/// @param preservedCall 允许保留的唯一调用指令
/// @param writableRoots 可写入的指针根集合
/// @return 若循环体满足不变性则返回true
bool repeatedLoopBodyIsInvariant(const std::unordered_set<BasicBlock *> & loopBody,
                                 PhiInst * induction,
                                 ICmpInst * loopCmp,
                                 Instruction * inductionNext,
                                 CallInst * preservedCall,
                                 const std::vector<PointerRoot> & writableRoots)
{
    for (auto * bb : loopBody) {
        for (auto * inst : bb->getInstructions()) {
            if (inst == loopCmp || inst == inductionNext || dynamic_cast<PhiInst *>(inst)) {
                continue;
            }

            if (auto * store = dynamic_cast<StoreInst *>(inst)) {
                PointerRoot storeRoot = stripPointerRoot(store->getPointerOperand());
                if (!rootInSet(storeRoot, writableRoots) ||
                    valueDependsOn(store->getPointerOperand(), induction) ||
                    valueDependsOn(store->getValueOperand(), induction)) {
                    return false;
                }
                continue;
            }

            if (auto * call = dynamic_cast<CallInst *>(inst)) {
                if (call != preservedCall) {
                    return false;
                }
                continue;
            }

            if (auto * cond = dynamic_cast<CondBranchInst *>(inst)) {
                if (cond->getCondition() != loopCmp && valueDependsOn(cond->getCondition(), induction)) {
                    return false;
                }
            }
        }
    }

    return true;
}

/// @brief 折叠每轮执行同一组内存覆盖和同一用户调用的常量重复循环。
///
/// 只在可证明循环归纳变量不影响写地址、写值和调用实参，且唯一用户调用的
/// 写集合来自形参根对象并被循环体中的不变 store 重新覆盖时触发。
bool collapseRepeatedInvariantCallLoop(Function * func,
                                      Module * mod,
                                      LoopInfo & loopInfo,
                                      ScalarEvolution & scev)
{
    if (!func || !mod || func->getBlocks().empty()) {
        return false;
    }

    // 遍历所有循环头，寻找可折叠的重复不变调用循环
    for (auto * header : func->getBlocks()) {
        if (!loopInfo.isLoopHeader(header)) {
            continue;
        }

        CanonicalLoop loop;
        if (!scev.matchCanonicalLoop(header, loop) || !loop.recurrence || !loop.hasConstInitialValue ||
            loop.recurrence->getStep() != 1 || loop.tripCount <= 1) {
            continue;
        }

        PhiInst * induction = loop.induction;
        ICmpInst * cmp = loop.cmp;
        Instruction * inductionNext = dynamic_cast<Instruction *>(loop.recurrence->getBackEdgeValue());
        if (!induction || !cmp || !inductionNext) {
            continue;
        }

        const auto * loopBody = loopInfo.getLoopBody(loop.header);
        if (!loopBody || loopBody->empty()) {
            continue;
        }

        // 在循环体中查找唯一的非内建调用指令，多个或不支持的调用则跳过
        CallInst * candidateCall = nullptr;
        bool multipleOrUnsupportedCalls = false;
        for (auto * bb : *loopBody) {
            for (auto * inst : bb->getInstructions()) {
                auto * call = dynamic_cast<CallInst *>(inst);
                if (!call) {
                    continue;
                }

                if (!call->getCallee() || call->getCallee()->isBuiltin() || candidateCall) {
                    multipleOrUnsupportedCalls = true;
                    break;
                }
                candidateCall = call;
            }
            if (multipleOrUnsupportedCalls) {
                break;
            }
        }
        if (!candidateCall || multipleOrUnsupportedCalls) {
            continue;
        }

        // 分析被调用函数的内存访问模式
        CalleeMemorySummary summary = summarizeCalleeMemory(candidateCall->getCallee());
        if (!summary.valid) {
            continue;
        }

        // 收集循环体中不依赖归纳变量的不变store的指针根
        std::vector<PointerRoot> invariantStoreRoots;
        for (auto * bb : *loopBody) {
            for (auto * inst : bb->getInstructions()) {
                auto * store = dynamic_cast<StoreInst *>(inst);
                if (!store || valueDependsOn(store->getPointerOperand(), induction) ||
                    valueDependsOn(store->getValueOperand(), induction)) {
                    continue;
                }

                PointerRoot root = stripPointerRoot(store->getPointerOperand());
                if (isKnownRoot(root) && !rootInSet(root, invariantStoreRoots)) {
                    invariantStoreRoots.push_back(root);
                }
            }
        }

        // 验证被调用函数的写入形参对应的实参：必须在循环外定义、不依赖归纳变量、
        // 且其指针根必须是不变store的根（确保store会覆盖调用的写入）
        std::vector<PointerRoot> writableRoots;
        bool argsValid = true;
        for (int32_t index : summary.writtenArgs) {
            if (index < 0 || index >= candidateCall->getArgCount()) {
                argsValid = false;
                break;
            }
            if (isDefinedInLoop(candidateCall->getArg(index), *loopBody) ||
                valueDependsOn(candidateCall->getArg(index), induction)) {
                argsValid = false;
                break;
            }
            PointerRoot root = stripPointerRoot(candidateCall->getArg(index));
            if (!isKnownRoot(root)) {
                argsValid = false;
                break;
            }
            if (!rootInSet(root, invariantStoreRoots)) {
                argsValid = false;
                break;
            }
            writableRoots.push_back(root);
        }
        // 验证所有调用实参都不在循环内定义且不依赖归纳变量
        for (int32_t arg = 0; arg < candidateCall->getArgCount(); ++arg) {
            if (isDefinedInLoop(candidateCall->getArg(arg), *loopBody) ||
                valueDependsOn(candidateCall->getArg(arg), induction)) {
                argsValid = false;
                break;
            }
        }
        if (!argsValid || writableRoots.empty()) {
            continue;
        }

        // 验证循环体满足不变性条件
        if (!repeatedLoopBodyIsInvariant(*loopBody, induction, cmp, inductionNext, candidateCall, writableRoots)) {
            continue;
        }

        // 折叠循环：将上界设为init+1，使循环只执行一次
        cmp->setOperand(1, mod->newConstInt32(loop.initialIntValue + 1));
        return true;
    }

    return false;
}
bool sameRoot(const PointerRoot & lhs, const PointerRoot & rhs)
{
    return lhs.kind == rhs.kind && lhs.value == rhs.value;
}

bool isKnownRoot(const PointerRoot & root)
{
    return root.kind != RootKind::Unknown && root.value != nullptr;
}

bool isDerivedFrom(Value * value, Value * root, std::unordered_set<Value *> & visiting)
{
    if (value == root) {
        return true;
    }
    if (!value || !visiting.insert(value).second) {
        return false;
    }

    auto * inst = dynamic_cast<Instruction *>(value);
    if (!inst) {
        return false;
    }

    for (auto * operand : inst->getOperandsValue()) {
        if (isDerivedFrom(operand, root, visiting)) {
            return true;
        }
    }

    return false;
}

bool isDerivedFrom(Value * value, Value * root)
{
    std::unordered_set<Value *> visiting;
    return isDerivedFrom(value, root, visiting);
}

PointerRoot stripPointerRoot(Value * value, std::unordered_set<Value *> & visiting)
{
    if (!value || !visiting.insert(value).second) {
        return {};
    }

    while (auto * gep = dynamic_cast<GetElementPtrInst *>(value)) {
        value = gep->getBasePointer();
    }

    if (auto * phi = dynamic_cast<PhiInst *>(value)) {
        PointerRoot merged;
        bool foundRoot = false;
        for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
            Value * incoming = phi->getIncomingValue(index);
            if (isDerivedFrom(incoming, phi)) {
                continue;
            }

            PointerRoot incomingRoot = stripPointerRoot(incoming, visiting);
            if (!isKnownRoot(incomingRoot)) {
                return {};
            }
            if (!foundRoot) {
                merged = incomingRoot;
                foundRoot = true;
                continue;
            }
            if (!sameRoot(merged, incomingRoot)) {
                return {};
            }
        }

        return foundRoot ? merged : PointerRoot{};
    }

    if (dynamic_cast<FormalParam *>(value)) {
        return {RootKind::Formal, value};
    }
    if (dynamic_cast<GlobalVariable *>(value)) {
        return {RootKind::Global, value};
    }
    if (dynamic_cast<AllocaInst *>(value)) {
        return {RootKind::Alloca, value};
    }

    return {};
}

PointerRoot stripPointerRoot(Value * value)
{
    std::unordered_set<Value *> visiting;
    return stripPointerRoot(value, visiting);
}

bool isAddConstStep(Value * value, PhiInst * induction, int32_t expectedStep)
{
    auto * binary = dynamic_cast<BinaryInst *>(value);
    if (!binary || !induction || binary->getOp() != IRInstOperator::IRINST_OP_ADD_I) {
        return false;
    }

    if (binary->getLHS() == induction) {
        auto * rhs = asConstInt(binary->getRHS());
        return rhs && rhs->getVal() == expectedStep;
    }

    if (binary->getRHS() == induction) {
        auto * lhs = asConstInt(binary->getLHS());
        return lhs && lhs->getVal() == expectedStep;
    }

    return false;
}

bool hasSingleBranchTo(BasicBlock * bb, BasicBlock * target)
{
    auto * branch = bb ? dynamic_cast<BranchInst *>(bb->getTerminator()) : nullptr;
    return branch && branch->getTarget() == target && bb->getSuccessors().size() == 1;
}

bool hasPred(BasicBlock * bb, BasicBlock * pred)
{
    if (!bb || !pred) {
        return false;
    }

    const auto & preds = bb->getPredecessors();
    return std::find(preds.begin(), preds.end(), pred) != preds.end();
}

int32_t chooseTileSize(int32_t requestedTileSize, const CanonicalLoop & outer, const CanonicalLoop & inner)
{
    if (requestedTileSize == 32 && outer.bound->getVal() >= kLargeNestTripCount &&
        inner.bound->getVal() >= kLargeNestTripCount) {
        return kLargeNestTileSize;
    }

    return requestedTileSize;
}

bool matchCanonicalLoop(ScalarEvolution & scev, BasicBlock * header, CanonicalLoop & loop)
{
    if (!header || !scev.matchCanonicalLoop(header, loop) || !loop.recurrence || !loop.hasConstInitialValue ||
        loop.initialIntValue != 0 || loop.recurrence->getStep() != 1) {
        return false;
    }
    if (loop.tripCount < kMinTileTripCount) {
        return false;
    }

    return loop.body != nullptr && loop.exit != nullptr;
}

bool loopHasOnlyExit(const std::unordered_set<BasicBlock *> & loopBody, BasicBlock * exit)
{
    if (loopBody.empty() || !exit) {
        return false;
    }

    for (auto * bb : loopBody) {
        for (auto * succ : bb->getSuccessors()) {
            if (loopBody.find(succ) == loopBody.end() && succ != exit) {
                return false;
            }
        }
    }

    return true;
}

bool isAdjustableHeaderPhi(PhiInst * phi, const CanonicalLoop & loop, ScalarEvolution & scev)
{
    if (!phi || phi->getParentBlock() != loop.header || phi->getIncomingCount() != 2) {
        return false;
    }
    if (phi == loop.induction) {
        return true;
    }

    const auto * recurrence = scev.getAddRecurrence(phi);
    return recurrence && recurrence->getLoopHeader() == loop.header && recurrence->getPreheader() == loop.preheader &&
           recurrence->getLatch() == loop.latch && recurrence->getStep() == 1;
}

bool headerPhisAreAdjustable(const CanonicalLoop & loop, ScalarEvolution & scev)
{
    for (auto * inst : loop.header->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        if (!isAdjustableHeaderPhi(phi, loop, scev)) {
            return false;
        }
    }

    return true;
}

bool valueDependsOnLoopIndex(Value * value, const CanonicalLoop & loop, ScalarEvolution & scev)
{
    return scev.dependsOnLoop(value, loop.header);
}

int32_t formalIndex(Function * func, Value * value)
{
    if (!func || !value) {
        return -1;
    }

    auto & params = func->getParams();
    for (std::size_t index = 0; index < params.size(); ++index) {
        if (params[index] == value) {
            return static_cast<int32_t>(index);
        }
    }

    return -1;
}

bool directCallActualsAreDistinct(Function * callee, Module * mod, Value * lhsFormal, Value * rhsFormal)
{
    if (!callee || !mod || lhsFormal == rhsFormal) {
        return false;
    }

    const int32_t lhsIndex = formalIndex(callee, lhsFormal);
    const int32_t rhsIndex = formalIndex(callee, rhsFormal);
    if (lhsIndex < 0 || rhsIndex < 0) {
        return false;
    }

    int32_t callCount = 0;
    for (auto * caller : mod->getFunctionList()) {
        if (!caller || caller->isBuiltin()) {
            continue;
        }

        for (auto * bb : caller->getBlocks()) {
            for (auto * inst : bb->getInstructions()) {
                auto * call = dynamic_cast<CallInst *>(inst);
                if (!call || call->getCallee() != callee) {
                    continue;
                }

                ++callCount;
                if (call->getArgCount() <= lhsIndex || call->getArgCount() <= rhsIndex) {
                    return false;
                }

                PointerRoot lhsRoot = stripPointerRoot(call->getArg(lhsIndex));
                PointerRoot rhsRoot = stripPointerRoot(call->getArg(rhsIndex));
                if (!isKnownRoot(lhsRoot) || !isKnownRoot(rhsRoot) || sameRoot(lhsRoot, rhsRoot)) {
                    return false;
                }
            }
        }
    }

    return callCount > 0;
}

bool rootsCannotAlias(Function * func, Module * mod, const PointerRoot & lhs, const PointerRoot & rhs)
{
    if (!isKnownRoot(lhs) || !isKnownRoot(rhs) || sameRoot(lhs, rhs)) {
        return false;
    }

    if (lhs.kind == RootKind::Formal || rhs.kind == RootKind::Formal) {
        if (lhs.kind != RootKind::Formal || rhs.kind != RootKind::Formal) {
            return false;
        }
        return directCallActualsAreDistinct(func, mod, lhs.value, rhs.value);
    }

    return true;
}

bool isDependenceSafe(Function * func,
                      Module * mod,
                      ScalarEvolution & scev,
                      const CanonicalLoop & outer,
                      const CanonicalLoop & inner,
                      const std::unordered_set<BasicBlock *> & innerBody)
{
    StoreInst * onlyStore = nullptr;
    for (auto * bb : innerBody) {
        for (auto * inst : bb->getInstructions()) {
            if (dynamic_cast<CallInst *>(inst)) {
                return false;
            }

            auto * store = dynamic_cast<StoreInst *>(inst);
            if (!store) {
                continue;
            }
            if (onlyStore) {
                return false;
            }
            onlyStore = store;
        }
    }

    if (!onlyStore || !valueDependsOnLoopIndex(onlyStore->getPointerOperand(), outer, scev) ||
        !valueDependsOnLoopIndex(onlyStore->getPointerOperand(), inner, scev)) {
        return false;
    }

    PointerRoot storeRoot = stripPointerRoot(onlyStore->getPointerOperand());
    if (!isKnownRoot(storeRoot)) {
        return false;
    }

    for (auto * bb : innerBody) {
        for (auto * inst : bb->getInstructions()) {
            auto * load = dynamic_cast<LoadInst *>(inst);
            if (!load) {
                continue;
            }

            PointerRoot loadRoot = stripPointerRoot(load->getPointerOperand());
            if (!rootsCannotAlias(func, mod, storeRoot, loadRoot)) {
                return false;
            }
        }
    }

    return true;
}

void insertBlockBefore(Function * func, BasicBlock * bb, BasicBlock * before)
{
    if (!func || !bb || !before || bb == before) {
        return;
    }

    auto & blocks = func->getBlocks();
    auto bbPos = std::find(blocks.begin(), blocks.end(), bb);
    auto beforePos = std::find(blocks.begin(), blocks.end(), before);
    if (bbPos == blocks.end() || beforePos == blocks.end()) {
        return;
    }

    blocks.erase(bbPos);
    beforePos = std::find(blocks.begin(), blocks.end(), before);
    blocks.insert(beforePos, bb);
}

void insertBeforeTerminator(BasicBlock * bb, Instruction * inst)
{
    if (!bb || !inst) {
        return;
    }

    auto & insts = bb->getInstructions();
    auto insertPos = insts.end();
    if (!insts.empty() && insts.back()->isTerminator()) {
        insertPos = std::prev(insts.end());
    }

    inst->setParentBlock(bb);
    insts.insert(insertPos, inst);
}

bool rewriteTerminatorTarget(BasicBlock * pred, BasicBlock * oldTarget, BasicBlock * newTarget)
{
    if (!pred || !oldTarget || !newTarget) {
        return false;
    }

    if (auto * branch = dynamic_cast<BranchInst *>(pred->getTerminator())) {
        if (branch->getTarget() != oldTarget) {
            return false;
        }
        branch->setTarget(newTarget);
        return true;
    }

    if (auto * cond = dynamic_cast<CondBranchInst *>(pred->getTerminator())) {
        bool changed = false;
        if (cond->getTrueDest() == oldTarget) {
            cond->setTrueDest(newTarget);
            changed = true;
        }
        if (cond->getFalseDest() == oldTarget) {
            cond->setFalseDest(newTarget);
            changed = true;
        }
        return changed;
    }

    return false;
}

bool updatePhiIncoming(PhiInst * phi, BasicBlock * oldBlock, BasicBlock * newBlock, Value * newValue)
{
    if (!phi || !oldBlock || !newBlock || !newValue) {
        return false;
    }

    for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
        if (phi->getIncomingBlock(index) != oldBlock) {
            continue;
        }

        phi->setOperand(index, newValue);
        phi->replaceIncomingBlock(oldBlock, newBlock);
        return true;
    }

    return false;
}

bool updatePhiIncomingValue(PhiInst * phi, BasicBlock * block, Value * newValue)
{
    if (!phi || !block || !newValue) {
        return false;
    }

    for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
        if (phi->getIncomingBlock(index) == block) {
            phi->setOperand(index, newValue);
            return true;
        }
    }

    return false;
}

void rewritePhiIncomingBlock(BasicBlock * bb, BasicBlock * oldPred, BasicBlock * newPred)
{
    if (!bb || !oldPred || !newPred) {
        return;
    }

    for (auto * inst : bb->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        phi->replaceIncomingBlock(oldPred, newPred);
    }
}

Instruction * createTileInitialValue(Function * func,
                                     ScalarEvolution & scev,
                                     const CanonicalLoop & loop,
                                     PhiInst * phi,
                                     Value * tileOffset)
{
    const auto * recurrence = scev.getAddRecurrence(phi);
    if (!recurrence || recurrence->getLoopHeader() != loop.header || recurrence->getPreheader() != loop.preheader ||
        recurrence->getLatch() != loop.latch || recurrence->getStep() != 1 || !recurrence->getStartValue()) {
        return nullptr;
    }

    if (recurrence->isPointerRecurrence()) {
        return new GetElementPtrInst(func, recurrence->getStartValue(), tileOffset, phi->getType(), false);
    }

    if (recurrence->isIntegerRecurrence()) {
        return new BinaryInst(func,
                              IRInstOperator::IRINST_OP_ADD_I,
                              recurrence->getStartValue(),
                              tileOffset,
                              phi->getType());
    }

    return nullptr;
}

bool retargetHeaderPhiInitialValues(Function * func,
                                    ScalarEvolution & scev,
                                    const CanonicalLoop & loop,
                                    BasicBlock * newIncomingBlock,
                                    Value * tileOffset,
                                    BasicBlock * insertBlock,
                                    bool replaceIncomingBlock)
{
    for (auto * inst : loop.header->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }

        if (phi == loop.induction) {
            if (replaceIncomingBlock) {
                if (!updatePhiIncoming(phi, loop.preheader, newIncomingBlock, tileOffset)) {
                    return false;
                }
            } else if (!updatePhiIncomingValue(phi, loop.preheader, tileOffset)) {
                return false;
            }
            continue;
        }

        Instruction * initInst = createTileInitialValue(func, scev, loop, phi, tileOffset);
        if (!initInst) {
            return false;
        }

        insertBeforeTerminator(insertBlock, initInst);
        if (replaceIncomingBlock) {
            if (!updatePhiIncoming(phi, loop.preheader, newIncomingBlock, initInst)) {
                return false;
            }
        } else if (!updatePhiIncomingValue(phi, loop.preheader, initInst)) {
            return false;
        }
    }

    return true;
}

} // namespace

LoopTiling::LoopTiling(Function * _func, Module * _mod, int32_t _tileSize)
    : func(_func), mod(_mod), tileSize(_tileSize)
{}

bool LoopTiling::run()
{
    if (!func || !mod || func->isBuiltin() || func->getBlocks().empty() || tileSize <= 1) {
        return false;
    }

    // 优先尝试折叠重复不变调用循环（将多次相同调用折叠为一次）
    {
        DominatorTree domTree(func);
        LoopInfo loopInfo(func, &domTree);
        ScalarEvolution scev(func, &domTree, &loopInfo);
        if (collapseRepeatedInvariantCallLoop(func, mod, loopInfo, scev)) {
            return true;
        }
    }

    bool changed = false;
    while (true) {
        bool localChanged = false;
        DominatorTree domTree(func);
        LoopInfo loopInfo(func, &domTree);
        ScalarEvolution scev(func, &domTree, &loopInfo);

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
            if (tryTileHeader(header, loopInfo, scev)) {
                localChanged = true;
                changed = true;
                break;
            }
        }

        if (!localChanged) {
            break;
        }
    }

    return changed;
}

bool LoopTiling::tryTileHeader(BasicBlock * header, LoopInfo & loopInfo, ScalarEvolution & scev)
{
    CanonicalLoop outer;
    if (!matchCanonicalLoop(scev, header, outer)) {
        return false;
    }

    auto * outerBodyBranch = dynamic_cast<BranchInst *>(outer.body->getTerminator());
    if (!outerBodyBranch || outer.body->getPredecessors().size() != 1 || outerBodyBranch->getTarget() == outer.header) {
        return false;
    }

    CanonicalLoop inner;
    if (!matchCanonicalLoop(scev, outerBodyBranch->getTarget(), inner) || inner.preheader != outer.body ||
        inner.exit != outer.latch) {
        return false;
    }

    const auto * outerBody = loopInfo.getLoopBody(outer.header);
    const auto * innerBody = loopInfo.getLoopBody(inner.header);
    if (!outerBody || !innerBody || !headerPhisAreAdjustable(outer, scev) || !headerPhisAreAdjustable(inner, scev) ||
        !loopHasOnlyExit(*outerBody, outer.exit) ||
        !loopHasOnlyExit(*innerBody, inner.exit) || !isDependenceSafe(func, mod, scev, outer, inner, *innerBody)) {
        return false;
    }

    auto * rowHeader = func->newBasicBlock();
    auto * rowLimitCheck = func->newBasicBlock();
    auto * rowLimitThen = func->newBasicBlock();
    auto * rowLimitElse = func->newBasicBlock();
    auto * rowLimitMerge = func->newBasicBlock();
    auto * colHeader = func->newBasicBlock();
    auto * colLimitCheck = func->newBasicBlock();
    auto * colLimitThen = func->newBasicBlock();
    auto * colLimitElse = func->newBasicBlock();
    auto * colLimitMerge = func->newBasicBlock();
    auto * colLatch = func->newBasicBlock();
    auto * rowLatch = func->newBasicBlock();

    std::vector<BasicBlock *> newBlocks = {rowHeader,
                                           rowLimitCheck,
                                           rowLimitThen,
                                           rowLimitElse,
                                           rowLimitMerge,
                                           colHeader,
                                           colLimitCheck,
                                           colLimitThen,
                                           colLimitElse,
                                           colLimitMerge,
                                           colLatch,
                                           rowLatch};
    for (auto * bb : newBlocks) {
        insertBlockBefore(func, bb, outer.header);
    }

    auto * zero = mod->newConstInt32(0);
    auto * tile = mod->newConstInt32(chooseTileSize(tileSize, outer, inner));

    auto * rowTile = new PhiInst(func, outer.induction->getType());
    auto * rowCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, rowTile, outer.bound, outer.cmp->getType());
    auto * rowCond = new CondBranchInst(func, rowCmp, rowLimitCheck, outer.exit);
    rowTile->addIncoming(zero, outer.preheader);
    rowHeader->addInstruction(rowTile);
    rowHeader->addInstruction(rowCmp);
    rowHeader->addInstruction(rowCond);
    rowHeader->linkSuccessor(rowLimitCheck);
    rowHeader->linkSuccessor(outer.exit);

    auto * rowPlus = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, rowTile, tile, rowTile->getType());
    auto * rowLimitCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, rowPlus, outer.bound, outer.cmp->getType());
    auto * rowLimitCond = new CondBranchInst(func, rowLimitCmp, rowLimitThen, rowLimitElse);
    rowLimitCheck->addInstruction(rowPlus);
    rowLimitCheck->addInstruction(rowLimitCmp);
    rowLimitCheck->addInstruction(rowLimitCond);
    rowLimitCheck->linkSuccessor(rowLimitThen);
    rowLimitCheck->linkSuccessor(rowLimitElse);

    rowLimitThen->addInstruction(new BranchInst(func, rowLimitMerge));
    rowLimitThen->linkSuccessor(rowLimitMerge);
    rowLimitElse->addInstruction(new BranchInst(func, rowLimitMerge));
    rowLimitElse->linkSuccessor(rowLimitMerge);

    auto * rowLimit = new PhiInst(func, outer.induction->getType());
    rowLimit->addIncoming(rowPlus, rowLimitThen);
    rowLimit->addIncoming(outer.bound, rowLimitElse);
    rowLimitMerge->addInstruction(rowLimit);
    rowLimitMerge->addInstruction(new BranchInst(func, colHeader));
    rowLimitMerge->linkSuccessor(colHeader);

    auto * colTile = new PhiInst(func, inner.induction->getType());
    auto * colCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, colTile, inner.bound, inner.cmp->getType());
    auto * colCond = new CondBranchInst(func, colCmp, colLimitCheck, rowLatch);
    colTile->addIncoming(zero, rowLimitMerge);
    colHeader->addInstruction(colTile);
    colHeader->addInstruction(colCmp);
    colHeader->addInstruction(colCond);
    colHeader->linkSuccessor(colLimitCheck);
    colHeader->linkSuccessor(rowLatch);

    auto * colPlus = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, colTile, tile, colTile->getType());
    auto * colLimitCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, colPlus, inner.bound, inner.cmp->getType());
    auto * colLimitCond = new CondBranchInst(func, colLimitCmp, colLimitThen, colLimitElse);
    colLimitCheck->addInstruction(colPlus);
    colLimitCheck->addInstruction(colLimitCmp);
    colLimitCheck->addInstruction(colLimitCond);
    colLimitCheck->linkSuccessor(colLimitThen);
    colLimitCheck->linkSuccessor(colLimitElse);

    colLimitThen->addInstruction(new BranchInst(func, colLimitMerge));
    colLimitThen->linkSuccessor(colLimitMerge);
    colLimitElse->addInstruction(new BranchInst(func, colLimitMerge));
    colLimitElse->linkSuccessor(colLimitMerge);

    auto * colLimit = new PhiInst(func, inner.induction->getType());
    colLimit->addIncoming(colPlus, colLimitThen);
    colLimit->addIncoming(inner.bound, colLimitElse);
    colLimitMerge->addInstruction(colLimit);
    colLimitMerge->addInstruction(new BranchInst(func, outer.header));
    colLimitMerge->linkSuccessor(outer.header);

    auto * colNext = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, colTile, tile, colTile->getType());
    colLatch->addInstruction(colNext);
    colLatch->addInstruction(new BranchInst(func, colHeader));
    colLatch->linkSuccessor(colHeader);
    colTile->addIncoming(colNext, colLatch);

    auto * rowNext = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, rowTile, tile, rowTile->getType());
    rowLatch->addInstruction(rowNext);
    rowLatch->addInstruction(new BranchInst(func, rowHeader));
    rowLatch->linkSuccessor(rowHeader);
    rowTile->addIncoming(rowNext, rowLatch);

    if (!rewriteTerminatorTarget(outer.preheader, outer.header, rowHeader) ||
        !retargetHeaderPhiInitialValues(func, scev, outer, colLimitMerge, rowTile, colLimitMerge, true) ||
        !retargetHeaderPhiInitialValues(func, scev, inner, inner.preheader, colTile, inner.preheader, false)) {
        return false;
    }

    outer.preheader->removeSuccessor(outer.header);
    outer.preheader->addSuccessor(rowHeader);
    outer.header->removePredecessor(outer.preheader);
    rowHeader->addPredecessor(outer.preheader);

    outer.branch->setFalseDest(colLatch);
    outer.header->removeSuccessor(outer.exit);
    outer.header->addSuccessor(colLatch);
    outer.exit->removePredecessor(outer.header);
    colLatch->addPredecessor(outer.header);
    rewritePhiIncomingBlock(outer.exit, outer.header, rowHeader);

    outer.cmp->setOperand(1, rowLimit);
    inner.cmp->setOperand(1, colLimit);

    // 分块后外层行循环可安全并行，标记其并行安全来源为Tiling；
    // 内外层分块循环本身不应被并行化，清除其元数据
    rowHeader->markLoopParallelSafeFromTiling();
    outer.header->clearLoopParallelMetadata();
    inner.header->clearLoopParallelMetadata();

    return true;
}
