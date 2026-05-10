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
#include "StoreInst.h"
#include "Value.h"

namespace {

constexpr int32_t kMinTileTripCount = 64;
constexpr int32_t kLargeNestTileSize = 128;
constexpr int32_t kLargeNestTripCount = 128;

struct CanonicalLoop {
    BasicBlock * header = nullptr;
    BasicBlock * preheader = nullptr;
    BasicBlock * body = nullptr;
    BasicBlock * latch = nullptr;
    BasicBlock * exit = nullptr;
    PhiInst * induction = nullptr;
    BinaryInst * next = nullptr;
    ICmpInst * cmp = nullptr;
    CondBranchInst * branch = nullptr;
    ConstInteger * bound = nullptr;
};

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

bool matchCanonicalLoop(BasicBlock * header, CanonicalLoop & loop)
{
    if (!header) {
        return false;
    }

    auto * condBr = dynamic_cast<CondBranchInst *>(header->getTerminator());
    if (!condBr) {
        return false;
    }

    auto * cmp = dynamic_cast<ICmpInst *>(condBr->getCondition());
    if (!cmp || cmp->getParentBlock() != header || cmp->getOp() != IRInstOperator::IRINST_OP_LT_I) {
        return false;
    }

    auto * induction = dynamic_cast<PhiInst *>(cmp->getLHS());
    auto * bound = asConstInt(cmp->getRHS());
    if (!induction || induction->getParentBlock() != header || !bound ||
        !induction->getType()->isInt32Type() || induction->getIncomingCount() != 2) {
        return false;
    }

    BasicBlock * preheader = nullptr;
    BasicBlock * latch = nullptr;
    BinaryInst * next = nullptr;
    for (int32_t index = 0; index < induction->getIncomingCount(); ++index) {
        BasicBlock * incomingBlock = induction->getIncomingBlock(index);
        Value * incomingValue = induction->getIncomingValue(index);
        auto * initConst = asConstInt(incomingValue);
        if (initConst && initConst->getVal() == 0) {
            preheader = incomingBlock;
            continue;
        }

        if (isAddConstStep(incomingValue, induction, 1)) {
            latch = incomingBlock;
            next = dynamic_cast<BinaryInst *>(incomingValue);
        }
    }

    if (!preheader || !latch || !next || !next->getParentBlock() ||
        !hasSingleBranchTo(preheader, header) || !hasSingleBranchTo(latch, header)) {
        return false;
    }

    if (header->getPredecessors().size() != 2 || !hasPred(header, preheader) || !hasPred(header, latch)) {
        return false;
    }

    const int32_t tripCount = bound->getVal();
    if (tripCount < kMinTileTripCount) {
        return false;
    }

    loop.header = header;
    loop.preheader = preheader;
    loop.body = condBr->getTrueDest();
    loop.latch = latch;
    loop.exit = condBr->getFalseDest();
    loop.induction = induction;
    loop.next = next;
    loop.cmp = cmp;
    loop.branch = condBr;
    loop.bound = bound;
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

bool isGepStepFromPhi(Value * value, PhiInst * phi)
{
    auto * gep = dynamic_cast<GetElementPtrInst *>(value);
    if (!gep || gep->getBasePointer() != phi || gep->isArrayDecayGEP()) {
        return false;
    }

    auto * step = asConstInt(gep->getIndexOperand());
    return step && step->getVal() == 1;
}

bool isAdjustableHeaderPhi(PhiInst * phi, const CanonicalLoop & loop)
{
    if (!phi || phi->getParentBlock() != loop.header || phi->getIncomingCount() != 2) {
        return false;
    }
    if (phi == loop.induction) {
        return true;
    }

    Value * initValue = nullptr;
    Value * latchValue = nullptr;
    for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
        if (phi->getIncomingBlock(index) == loop.preheader) {
            initValue = phi->getIncomingValue(index);
        } else if (phi->getIncomingBlock(index) == loop.latch) {
            latchValue = phi->getIncomingValue(index);
        }
    }

    return initValue && latchValue && (isGepStepFromPhi(latchValue, phi) || isAddConstStep(latchValue, phi, 1));
}

bool headerPhisAreAdjustable(const CanonicalLoop & loop)
{
    for (auto * inst : loop.header->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        if (!isAdjustableHeaderPhi(phi, loop)) {
            return false;
        }
    }

    return true;
}

bool valueDependsOnLoopIndex(Value * value,
                             const CanonicalLoop & loop,
                             std::unordered_set<Value *> & visiting)
{
    if (value == loop.induction) {
        return true;
    }
    if (!value || !visiting.insert(value).second) {
        return false;
    }

    if (auto * phi = dynamic_cast<PhiInst *>(value)) {
        if (phi->getParentBlock() == loop.header && isAdjustableHeaderPhi(phi, loop)) {
            return true;
        }

        for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
            Value * incoming = phi->getIncomingValue(index);
            if (isDerivedFrom(incoming, phi)) {
                continue;
            }
            if (valueDependsOnLoopIndex(incoming, loop, visiting)) {
                return true;
            }
        }
        return false;
    }

    auto * inst = dynamic_cast<Instruction *>(value);
    if (!inst) {
        return false;
    }

    for (auto * operand : inst->getOperandsValue()) {
        if (valueDependsOnLoopIndex(operand, loop, visiting)) {
            return true;
        }
    }

    return false;
}

bool valueDependsOnLoopIndex(Value * value, const CanonicalLoop & loop)
{
    std::unordered_set<Value *> visiting;
    return valueDependsOnLoopIndex(value, loop, visiting);
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

    if (!onlyStore || !valueDependsOnLoopIndex(onlyStore->getPointerOperand(), outer) ||
        !valueDependsOnLoopIndex(onlyStore->getPointerOperand(), inner)) {
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
                                     const CanonicalLoop & loop,
                                     PhiInst * phi,
                                     Value * tileOffset)
{
    Value * initValue = nullptr;
    Value * latchValue = nullptr;
    for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
        if (phi->getIncomingBlock(index) == loop.preheader) {
            initValue = phi->getIncomingValue(index);
        } else if (phi->getIncomingBlock(index) == loop.latch) {
            latchValue = phi->getIncomingValue(index);
        }
    }

    if (!initValue || !latchValue) {
        return nullptr;
    }

    if (isGepStepFromPhi(latchValue, phi)) {
        return new GetElementPtrInst(func, initValue, tileOffset, phi->getType(), false);
    }

    if (isAddConstStep(latchValue, phi, 1)) {
        return new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, initValue, tileOffset, phi->getType());
    }

    return nullptr;
}

bool retargetHeaderPhiInitialValues(Function * func,
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

        Instruction * initInst = createTileInitialValue(func, loop, phi, tileOffset);
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

    bool changed = false;
    while (true) {
        bool localChanged = false;
        DominatorTree domTree(func);
        LoopInfo loopInfo(func, &domTree);

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
            if (tryTileHeader(header, loopInfo)) {
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

bool LoopTiling::tryTileHeader(BasicBlock * header, LoopInfo & loopInfo)
{
    CanonicalLoop outer;
    if (!matchCanonicalLoop(header, outer)) {
        return false;
    }

    auto * outerBodyBranch = dynamic_cast<BranchInst *>(outer.body->getTerminator());
    if (!outerBodyBranch || outer.body->getPredecessors().size() != 1 || outerBodyBranch->getTarget() == outer.header) {
        return false;
    }

    CanonicalLoop inner;
    if (!matchCanonicalLoop(outerBodyBranch->getTarget(), inner) || inner.preheader != outer.body ||
        inner.exit != outer.latch) {
        return false;
    }

    const auto * outerBody = loopInfo.getLoopBody(outer.header);
    const auto * innerBody = loopInfo.getLoopBody(inner.header);
    if (!outerBody || !innerBody || !headerPhisAreAdjustable(outer) || !headerPhisAreAdjustable(inner) ||
        !loopHasOnlyExit(*outerBody, outer.exit) ||
        !loopHasOnlyExit(*innerBody, inner.exit) || !isDependenceSafe(func, mod, outer, inner, *innerBody)) {
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
        !retargetHeaderPhiInitialValues(func, outer, colLimitMerge, rowTile, colLimitMerge, true) ||
        !retargetHeaderPhiInitialValues(func, inner, inner.preheader, colTile, inner.preheader, false)) {
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

    return true;
}
