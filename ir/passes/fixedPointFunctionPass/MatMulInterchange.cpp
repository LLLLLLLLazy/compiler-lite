///
/// @file MatMulInterchange.cpp
/// @brief 矩阵乘法 j-k 循环交换 pass 实现
///

#include "MatMulInterchange.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <unordered_set>
#include <vector>

#include "AllocaInst.h"
#include "AnalysisCache.h"
#include "ArrayType.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CondBranchInst.h"
#include "ConstInteger.h"
#include "CostModel.h"
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
#include "PointerType.h"
#include "ScalarEvolution.h"
#include "StoreInst.h"
#include "Use.h"
#include "Value.h"

namespace {

using CanonicalLoop = ScalarEvolution::CanonicalLoop;

constexpr int32_t kMinTripCount = 16;   ///< 已知 trip count 低于此值不交换（额外控制流摊不开）
constexpr int32_t kMinRowBytes = 64;    ///< 行宽不足一条 cache line 时列访问无明显惩罚
constexpr int32_t kRegisterBlockCols = 8; ///< 原地 A=C*A 的标量回退路径一次累加的列数
constexpr int32_t kConstantTailBlockCols = 8; ///< 常量后缀列和路径的标量列块，控制寄存器压力

enum class RootKind {
    Unknown,
    Global,
    Alloca,
};

struct PointerRoot {
    RootKind kind = RootKind::Unknown;
    Value * value = nullptr;
};

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

ConstInteger * asConstInt(Value * value)
{
    return dynamic_cast<ConstInteger *>(value);
}

Value * getPhiIncomingFrom(PhiInst * phi, BasicBlock * block)
{
    if (!phi || !block) {
        return nullptr;
    }
    for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
        if (phi->getIncomingBlock(i) == block) {
            return phi->getIncomingValue(i);
        }
    }
    return nullptr;
}

bool getLoopPhiIncoming(PhiInst * phi,
                        const std::unordered_set<BasicBlock *> & loopBody,
                        Value *& entryValue,
                        BasicBlock *& entryBlock,
                        Value *& backedgeValue,
                        BasicBlock *& backedgeBlock)
{
    entryValue = nullptr;
    entryBlock = nullptr;
    backedgeValue = nullptr;
    backedgeBlock = nullptr;
    if (!phi) {
        return false;
    }

    for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
        BasicBlock * block = phi->getIncomingBlock(i);
        Value * value = phi->getIncomingValue(i);
        if (loopBody.find(block) == loopBody.end()) {
            if (entryValue) {
                return false;
            }
            entryValue = value;
            entryBlock = block;
        } else {
            if (backedgeValue) {
                return false;
            }
            backedgeValue = value;
            backedgeBlock = block;
        }
    }

    return entryValue != nullptr && entryBlock != nullptr && backedgeValue != nullptr && backedgeBlock != nullptr;
}

/// @brief 单位步长元素访问（X 的 load 地址或 Z 的 store 地址）
/// 覆盖两种形态：LSR 收敛后的游标 phi（latch 边为 gep(phi,1)）；
/// 或未削减的 gep(base, iv) 直接寻址。
struct ElemAccess {
    PhiInst * cursorPhi = nullptr;          // 游标形态的 phi，gep 形态为空
    GetElementPtrInst * advanceGEP = nullptr; // 游标形态的步进 gep
    GetElementPtrInst * indexGEP = nullptr;   // gep 形态的寻址指令
    Value * baseAtZero = nullptr;             // 迭代起点处的元素地址（可能需要物化）
    bool needMaterializeBase = false;         // gep(decay) 形态需要在 preheader 物化 base
};

/// @brief 条件归约信息，处理 if(cond) acc += X*Y 模式
struct CondReduceInfo {
    bool active = false;
    BinaryInst * condMod = nullptr;
    ICmpInst * condCmp = nullptr;
    CondBranchInst * condBr = nullptr;
    BasicBlock * condTrueBlock = nullptr;
    PhiInst * mergePhi = nullptr;
    LoadInst * trueLoadX = nullptr;
    LoadInst * trueLoadY = nullptr;
    GetElementPtrInst * trueYElemGEP = nullptr;
    BinaryInst * trueMul = nullptr;
    BinaryInst * trueAcc = nullptr;
    bool trueMulXFirst = true;
    PhiInst * secondXCursorPhi = nullptr;
    GetElementPtrInst * secondXAdvanceGEP = nullptr;
    Value * secondXBaseAtZero = nullptr;
    PhiInst * secondYRowPhi = nullptr;
    GetElementPtrInst * secondYRowAdvance = nullptr;
    Value * secondYRowInit = nullptr;
    PhiInst * secondYColumnPhi = nullptr;
    GetElementPtrInst * secondYColumnAdvance = nullptr;
    Value * secondYColumnInit = nullptr;
    Value * secondYBaseAtZero = nullptr;
    bool secondYUsesColumn = false;
    bool secondYBaseNeedMaterialize = false;
    PointerRoot rootX;
    PointerRoot rootY;
};

/// @brief 匹配到的 (j,k) 列访问归约嵌套
struct MatMulPattern {
    CanonicalLoop jLoop;
    CanonicalLoop kLoop;
    BasicBlock * forwardBlock = nullptr; // j.body 为 k preheader/setup 块时非空

    PhiInst * sumPhi = nullptr;
    Value * sumInit = nullptr;
    BasicBlock * sumBackedgeBlock = nullptr;
    BasicBlock * reductionBlock = nullptr;
    LoadInst * loadX = nullptr;
    LoadInst * loadY = nullptr;
    BinaryInst * mul = nullptr;
    BinaryInst * acc = nullptr;
    bool mulXFirst = true;  // mul 操作数序：X 在前
    bool accSumFirst = true; // acc 操作数序：sum 在前
    StoreInst * store = nullptr;

    ElemAccess x; // k 维单位步长
    ElemAccess z; // j 维单位步长

    PhiInst * yRowPhi = nullptr;
    GetElementPtrInst * yRowAdvance = nullptr;
    GetElementPtrInst * yElemGEP = nullptr;
    Value * yRowInit = nullptr;
    PhiInst * yColumnPhi = nullptr;
    GetElementPtrInst * yColumnAdvance = nullptr;
    Value * yColumnInit = nullptr; // 原 j 列上的 A[0][j] 起点
    PhiInst * yColumnStartPhi = nullptr;
    Value * yBaseAtZero = nullptr; // A[0][0]，交换后每个 k 行扫描的起点
    bool yBaseNeedMaterialize = false;
    bool yUsesColumnCursor = false;

    Type * elemType = nullptr;
    Type * elemPtrType = nullptr;
    int32_t rowWidth = 0;

    PointerRoot rootX;
    PointerRoot rootY;
    PointerRoot rootZ;
    bool inPlace = false;   // Z 与 Y 同根，需要临时行缓冲
    bool needGuard = false; // 运行期 j 上界需 guard Nj<=W 的双版本

    /// @brief 条件归约 — 处理 if(a[i][k]*b[k][j]%2==0) temp+=b[i][k]*a[k][j] 模式
    CondReduceInfo cond;
};

struct OuterInPlaceContext {
    PhiInst * zRowPhi = nullptr;
    PhiInst * xRowPhi = nullptr;
    BasicBlock * header = nullptr;
    BasicBlock * preheader = nullptr;
    BasicBlock * latch = nullptr;
    Value * zStart = nullptr;
    Value * xStart = nullptr;
};

/// @brief 取指针类型的指向类型
const Type * pointeeOf(const Type * type)
{
    auto * ptrType = dynamic_cast<const PointerType *>(type);
    return ptrType ? ptrType->getPointeeType() : nullptr;
}

/// @brief 判断值是否为标量元素指针（指向 i32/f32）
bool isElemPointer(const Type * type, const Type * elemType)
{
    const Type * pointee = pointeeOf(type);
    return pointee != nullptr && pointee == elemType;
}

/// @brief 行指针的来源是否全程行对齐：
/// 链条上只允许整行步进 gep、外层数组 decay、全局/alloca 根与同型 phi，
/// 元素级偏移一旦混入即失败。原地（Z 与 Y 同根）改写依赖该性质保证
/// Z 行基址与 Y 行基址差为行宽整数倍。
bool isRowAlignedPointer(Value * value, const Type * rowType, std::unordered_set<Value *> & visiting)
{
    if (!value || !visiting.insert(value).second) {
        return false;
    }

    if (dynamic_cast<GlobalVariable *>(value) || dynamic_cast<AllocaInst *>(value)) {
        return true;
    }

    if (auto * gep = dynamic_cast<GetElementPtrInst *>(value)) {
        if (gep->isArrayDecayGEP()) {
            // 外层数组 decay：任意下标都选中整行
            return isRowAlignedPointer(gep->getBasePointer(), rowType, visiting);
        }
        // 非 decay 步进必须发生在行指针类型上（整行步进）
        if (gep->getType() != rowType || gep->getBasePointer()->getType() != rowType) {
            return false;
        }
        return isRowAlignedPointer(gep->getBasePointer(), rowType, visiting);
    }

    if (auto * phi = dynamic_cast<PhiInst *>(value)) {
        if (phi->getType() != rowType) {
            return false;
        }
        for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
            Value * incoming = phi->getIncomingValue(i);
            if (isDerivedFrom(incoming, phi)) {
                continue;
            }
            if (!isRowAlignedPointer(incoming, rowType, visiting)) {
                return false;
            }
        }
        return true;
    }

    return false;
}

bool isRowAlignedPointer(Value * value, const Type * rowType)
{
    std::unordered_set<Value *> visiting;
    return isRowAlignedPointer(value, rowType, visiting);
}

bool isMultipleOfRowWidth(Value * value, int32_t rowWidth)
{
    auto * constant = asConstInt(value);
    return constant && rowWidth > 0 && constant->getVal() % rowWidth == 0;
}

/// @brief 扁平 i32*/f32* 指针是否可证位于某一整行开头。
///
/// LSR 可能把 [N x T]* 行游标进一步化为 T* 标量游标。原地 matmul
/// 合法性仍然只需要证明 Z 起点和 Y 的零列起点都按 rowWidth 对齐。
bool isFlatRowAlignedPointer(Value * value,
                             const Type * elemPtrType,
                             int32_t rowWidth,
                             std::unordered_set<Value *> & visiting)
{
    if (!value || rowWidth <= 0 || !visiting.insert(value).second) {
        return false;
    }

    if (dynamic_cast<GlobalVariable *>(value) || dynamic_cast<AllocaInst *>(value)) {
        return true;
    }

    if (auto * gep = dynamic_cast<GetElementPtrInst *>(value)) {
        if (gep->isArrayDecayGEP()) {
            const Type * pointee = pointeeOf(gep->getType());
            if (dynamic_cast<const ArrayType *>(pointee)) {
                // 外层数组 decay 选中整行，任意行号仍保持行对齐。
                return isFlatRowAlignedPointer(gep->getBasePointer(), elemPtrType, rowWidth, visiting);
            }

            // 从一行数组 decay 到标量元素指针时，只有第 0 列仍是行首。
            auto * index = asConstInt(gep->getIndexOperand());
            if (!index || index->getVal() % rowWidth != 0) {
                return false;
            }
            return isFlatRowAlignedPointer(gep->getBasePointer(), elemPtrType, rowWidth, visiting);
        }

        if (gep->getType() != elemPtrType || !isMultipleOfRowWidth(gep->getIndexOperand(), rowWidth)) {
            return false;
        }
        return isFlatRowAlignedPointer(gep->getBasePointer(), elemPtrType, rowWidth, visiting);
    }

    if (auto * phi = dynamic_cast<PhiInst *>(value)) {
        if (phi->getType() != elemPtrType) {
            return false;
        }

        for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
            Value * incoming = phi->getIncomingValue(i);
            if (auto * advance = dynamic_cast<GetElementPtrInst *>(incoming)) {
                if (!advance->isArrayDecayGEP() && advance->getBasePointer() == phi &&
                    advance->getType() == elemPtrType &&
                    isMultipleOfRowWidth(advance->getIndexOperand(), rowWidth)) {
                    continue;
                }
            }

            if (isDerivedFrom(incoming, phi) ||
                !isFlatRowAlignedPointer(incoming, elemPtrType, rowWidth, visiting)) {
                return false;
            }
        }
        return true;
    }

    return false;
}

bool isFlatRowAlignedPointer(Value * value, const Type * elemPtrType, int32_t rowWidth)
{
    std::unordered_set<Value *> visiting;
    return isFlatRowAlignedPointer(value, elemPtrType, rowWidth, visiting);
}

bool sameValue(Value * lhs, Value * rhs)
{
    if (lhs == rhs) {
        return true;
    }
    auto * lhsConst = asConstInt(lhs);
    auto * rhsConst = asConstInt(rhs);
    return lhsConst && rhsConst && lhsConst->getVal() == rhsConst->getVal();
}

bool isZeroArrayDecayFrom(Value * value, Value * base)
{
    auto * gep = dynamic_cast<GetElementPtrInst *>(value);
    auto * index = gep ? asConstInt(gep->getIndexOperand()) : nullptr;
    return gep && gep->isArrayDecayGEP() && gep->getBasePointer() == base && index && index->getVal() == 0;
}

bool sameOrZeroDecay(Value * lhs, Value * rhs)
{
    return sameValue(lhs, rhs) || isZeroArrayDecayFrom(lhs, rhs) || isZeroArrayDecayFrom(rhs, lhs);
}

/// @brief 原地常量后缀折叠要求 Y 的外层行游标从矩阵首行开始并按整行步进。
bool hasCanonicalInPlaceRowTraversal(const MatMulPattern & pat)
{
    if (!pat.inPlace || !pat.yUsesColumnCursor || !pat.yColumnPhi || !pat.yColumnAdvance ||
        !pat.yBaseAtZero || pat.rowWidth <= 0) {
        return false;
    }

    auto * step = asConstInt(pat.yColumnAdvance->getIndexOperand());
    if (!step || step->getVal() != pat.rowWidth) {
        return false;
    }

    if (!sameRoot(stripPointerRoot(pat.yBaseAtZero), pat.rootY)) {
        return false;
    }

    if (pat.yBaseNeedMaterialize) {
        return isRowAlignedPointer(pat.yBaseAtZero, pat.yBaseAtZero->getType());
    }
    return isFlatRowAlignedPointer(pat.yBaseAtZero, pat.elemPtrType, pat.rowWidth);
}

bool matchRowStepGEP(Value * value, PhiInst * phi, int32_t rowWidth)
{
    auto * gep = dynamic_cast<GetElementPtrInst *>(value);
    auto * step = gep ? asConstInt(gep->getIndexOperand()) : nullptr;
    return gep && !gep->isArrayDecayGEP() && gep->getBasePointer() == phi && step && step->getVal() == rowWidth;
}

bool getPhiIncomingFromBlock(PhiInst * phi, BasicBlock * block, Value *& value)
{
    value = nullptr;
    if (!phi || !block) {
        return false;
    }
    for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
        if (phi->getIncomingBlock(i) == block) {
            value = phi->getIncomingValue(i);
            return true;
        }
    }
    return false;
}

bool matchOuterInPlaceContext(const MatMulPattern & pat, OuterInPlaceContext & ctx)
{
    auto * zPhi = dynamic_cast<PhiInst *>(pat.z.baseAtZero);
    auto * xPhi = dynamic_cast<PhiInst *>(pat.x.baseAtZero);
    if (!zPhi || !xPhi || zPhi->getParentBlock() != xPhi->getParentBlock() ||
        zPhi->getIncomingCount() != 2 || xPhi->getIncomingCount() != 2) {
        return false;
    }

    BasicBlock * preheader = nullptr;
    BasicBlock * latch = nullptr;
    Value * zStart = nullptr;
    for (int32_t i = 0; i < zPhi->getIncomingCount(); ++i) {
        Value * incoming = zPhi->getIncomingValue(i);
        BasicBlock * block = zPhi->getIncomingBlock(i);
        if (matchRowStepGEP(incoming, zPhi, pat.rowWidth)) {
            if (latch) {
                return false;
            }
            latch = block;
        } else {
            if (preheader) {
                return false;
            }
            preheader = block;
            zStart = incoming;
        }
    }

    if (!preheader || !latch || !sameOrZeroDecay(zStart, pat.yBaseAtZero)) {
        return false;
    }

    Value * xStart = nullptr;
    Value * xLatch = nullptr;
    if (!getPhiIncomingFromBlock(xPhi, preheader, xStart) ||
        !getPhiIncomingFromBlock(xPhi, latch, xLatch) ||
        !matchRowStepGEP(xLatch, xPhi, pat.rowWidth)) {
        return false;
    }

    ctx.zRowPhi = zPhi;
    ctx.xRowPhi = xPhi;
    ctx.header = zPhi->getParentBlock();
    ctx.preheader = preheader;
    ctx.latch = latch;
    ctx.zStart = zStart;
    ctx.xStart = xStart;
    return true;
}

bool needsRuntimeUpperBoundGuard(const CanonicalLoop & loop, int32_t rowWidth)
{
    return !loop.hasConstBoundValue || loop.boundIntValue > rowWidth;
}

bool staticallyExceedsRowWidth(const CanonicalLoop & loop, int32_t rowWidth)
{
    return loop.hasConstBoundValue && loop.boundIntValue > rowWidth;
}

void addBoundsGuard(Function * func,
                    Module * mod,
                    BasicBlock * guard,
                    Value * bound,
                    int32_t rowWidth,
                    BasicBlock * trueTarget,
                    BasicBlock * falseTarget,
                    Type * cmpType)
{
    auto * limit = mod->newConstInt32(rowWidth);
    auto * cmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LE_I, bound, limit, cmpType);
    auto * br = new CondBranchInst(func, cmp, trueTarget, falseTarget);
    guard->addInstruction(cmp);
    guard->addInstruction(br);
    guard->linkSuccessor(trueTarget);
    guard->linkSuccessor(falseTarget);
}

/// @brief 值在 j 循环内是否不变（非循环体内定义的指令即视为不变）
bool isInvariantOutside(Value * value, const std::unordered_set<BasicBlock *> & loopBody)
{
    auto * inst = dynamic_cast<Instruction *>(value);
    return !inst || loopBody.find(inst->getParentBlock()) == loopBody.end();
}

/// @brief 匹配游标 phi：latch 边为 gep(phi, +1, 非 decay)
bool matchUnitCursor(PhiInst * phi, BasicBlock * preheader, BasicBlock * latch, ElemAccess & access)
{
    if (!phi || !phi->getType() || !phi->getType()->isPointerType()) {
        return false;
    }

    Value * init = getPhiIncomingFrom(phi, preheader);
    auto * advance = dynamic_cast<GetElementPtrInst *>(getPhiIncomingFrom(phi, latch));
    if (!init || !advance || advance->isArrayDecayGEP() || advance->getBasePointer() != phi) {
        return false;
    }

    auto * step = asConstInt(advance->getIndexOperand());
    if (!step || step->getVal() != 1) {
        return false;
    }

    access.cursorPhi = phi;
    access.advanceGEP = advance;
    access.baseAtZero = init;
    return true;
}

/// @brief 规范计数循环匹配的公共附加条件：步长 1、小于比较
bool matchUnitCountedLoop(ScalarEvolution & scev, BasicBlock * header, CanonicalLoop & loop)
{
    return header && scev.matchCanonicalLoop(header, loop) && loop.recurrence &&
           loop.recurrence->getStep() == 1 && loop.compareKind == ScalarEvolution::CompareKind::LessThan &&
           loop.boundValue && loop.induction && loop.body && loop.exit;
}

} // namespace

MatMulInterchange::MatMulInterchange(Function * _func, Module * _mod) : func(_func), mod(_mod)
{}

namespace {

/// @brief 在 j 循环头上尝试匹配完整模式
bool matchPattern(ScalarEvolution & scev, LoopInfo & loopInfo, BasicBlock * jHeader, MatMulPattern & pat)
{
    const bool debug = std::getenv("MINIC_DEBUG_MATMUL") != nullptr;

    if (!matchUnitCountedLoop(scev, jHeader, pat.jLoop)) {
        return false;
    }

    if (debug) {
        std::cerr << "[MatMul] Checking loop header: " << jHeader->getName()
                  << ", hasConstInit=" << pat.jLoop.hasConstInitialValue
                  << ", initVal=" << pat.jLoop.initialIntValue
                  << ", hasConstBound=" << pat.jLoop.hasConstBoundValue
                  << ", j.body=" << pat.jLoop.body
                  << ", j.exit=" << pat.jLoop.exit
                  << ", j.latch=" << pat.jLoop.latch << std::endl;
    }

    // guard 回落版本不再二次版本化
    if (jHeader->isMatMulInterchangeFallback()) {
        if (debug) std::cerr << "  -> Fallback marked, skip" << std::endl;
        return false;
    }

    // j 下界必须为 0：缓冲/新循环的下标都从 0 起
    if (!pat.jLoop.hasConstInitialValue || pat.jLoop.initialIntValue != 0) {
        if (debug) std::cerr << "  -> Failed: j initial value not 0" << std::endl;
        return false;
    }

    const auto * jBodySetPtr = loopInfo.getLoopBody(jHeader);
    if (!jBodySetPtr) {
        if (debug) std::cerr << "  -> Failed: no loop body info" << std::endl;
        return false;
    }
    const auto & jBodySet = *jBodySetPtr;

    // 定位 k 循环头：j.body 直接是 k.header，或经一个 setup/转发块。
    // LSR 会把 A[k][j] 的列访问收敛为 preheader 中的 A[0][j] 起点，
    // 再在 k 循环内用 +rowWidth 的标量指针 phi 递推。
    BasicBlock * kHeader = pat.jLoop.body;
    if (!loopInfo.isLoopHeader(kHeader)) {
        if (auto * fwd = dynamic_cast<BranchInst *>(kHeader->getTerminator())) {
            pat.forwardBlock = kHeader;
            kHeader = fwd->getTarget();
        }
    }

    if (!matchUnitCountedLoop(scev, kHeader, pat.kLoop)) {
        if (debug) std::cerr << "  -> Failed: k loop not unit counted or k.body="
                            << (pat.jLoop.body ? pat.jLoop.body->getName() : "null") << std::endl;
        return false;
    }

    BasicBlock * expectedKPreheader = pat.forwardBlock ? pat.forwardBlock : jHeader;

    if (debug) {
        std::cerr << "  -> Found k loop at " << kHeader
                  << ", k.preheader=" << pat.kLoop.preheader
                  << ", expectedKPreheader=" << expectedKPreheader
                  << ", k.exit=" << pat.kLoop.exit
                  << ", k.body=" << pat.kLoop.body
                  << ", k.latch=" << pat.kLoop.latch
                  << ", j.latch=" << pat.jLoop.latch;
        if (pat.kLoop.exit != pat.jLoop.latch) {
            std::cerr << " [exit mismatch]";
        }
        if (pat.kLoop.body != pat.kLoop.latch) {
            std::cerr << " [body!=latch, relaxing requirement]";
        }
        if (pat.kLoop.preheader != expectedKPreheader) {
            std::cerr << " [preheader mismatch]";
        }
        std::cerr << std::endl;
    }

    // 放宽要求：允许 k.body != k.latch（单独的 latch 块）
    if (pat.kLoop.preheader != expectedKPreheader || pat.kLoop.exit != pat.jLoop.latch) {
        if (debug) std::cerr << "  -> Failed: k loop structure mismatch" << std::endl;
        return false;
    }

    BasicBlock * kBody = pat.kLoop.body;
    BasicBlock * kLatch = pat.kLoop.latch;
    BasicBlock * jLatch = pat.jLoop.latch;
    const auto * kBodySetPtr = loopInfo.getLoopBody(kHeader);
    if (!kBodySetPtr) {
        return false;
    }
    const auto & kBodySet = *kBodySetPtr;

    // j 出口块不能有 phi（改写后出口新增前驱）
    for (auto * inst : pat.jLoop.exit->getInstructions()) {
        if (dynamic_cast<PhiInst *>(inst)) {
            if (debug) std::cerr << "  -> Failed: j.exit has phi" << std::endl;
            return false;
        }
        break;
    }

    // ---- 识别 k.header 的 phi：归纳变量、sum、X 游标、Y 行游标 ----
    std::vector<PhiInst *> kPhis;
    for (auto * inst : kHeader->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        kPhis.push_back(phi);
    }

    if (debug) {
        std::cerr << "  -> k.header has " << kPhis.size() << " phi(s), kLoop.induction=" << pat.kLoop.induction << std::endl;
    }

    for (auto * phi : kPhis) {
        if (phi == pat.kLoop.induction) {
            continue;
        }

        if (debug) {
            std::cerr << "    Checking phi at " << phi << ", isPointer=" << phi->getType()->isPointerType() << std::endl;
        }

        if (phi->getType()->isPointerType()) {
            Value * init = nullptr;
            Value * backedge = nullptr;
            BasicBlock * initBlock = nullptr;
            BasicBlock * advanceBlock = nullptr;
            if (!getLoopPhiIncoming(phi, kBodySet, init, initBlock, backedge, advanceBlock)) {
                if (debug) std::cerr << "  -> Failed: pointer phi incoming split" << std::endl;
                return false;
            }
            if (initBlock != pat.kLoop.preheader || advanceBlock != kLatch) {
                if (debug) std::cerr << "  -> Failed: pointer phi edge mismatch" << std::endl;
                return false;
            }
            auto * advance = dynamic_cast<GetElementPtrInst *>(backedge);

            if (debug) {
                std::cerr << "      init=" << init << ", advance=" << advance;
                if (advance) {
                    std::cerr << ", isDecay=" << advance->isArrayDecayGEP()
                              << ", base=" << advance->getBasePointer()
                              << ", baseIsPhi=" << (advance->getBasePointer() == phi);
                }
                std::cerr << std::endl;
            }

            if (!init || !advance || !advanceBlock || advance->isArrayDecayGEP() || advance->getBasePointer() != phi) {
                if (debug) std::cerr << "  -> Failed: pointer phi pattern mismatch" << std::endl;
                return false;
            }
            auto * step = asConstInt(advance->getIndexOperand());
            if (!step || step->getVal() <= 0) {
                if (debug) std::cerr << "  -> Failed: pointer phi step not positive const" << std::endl;
                return false;
            }

            const Type * pointee = pointeeOf(phi->getType());
            if (dynamic_cast<const ArrayType *>(pointee)) {
                if (step->getVal() != 1) {
                    if (debug) std::cerr << "  -> Failed: Y row phi step != 1" << std::endl;
                    return false;
                }
                if (pat.yRowPhi) {
                    // 条件归约允许第二个 Y row phi
                    if (pat.cond.secondYRowPhi) {
                        if (debug) std::cerr << "  -> Failed: multiple Y row phi" << std::endl;
                        return false;
                    }
                    pat.cond.secondYRowPhi = phi;
                    pat.cond.secondYRowAdvance = advance;
                    pat.cond.secondYRowInit = init;
                    if (debug) std::cerr << "      -> Identified as second Y row phi (conditional)" << std::endl;
                } else {
                    pat.yRowPhi = phi;
                    pat.yRowAdvance = advance;
                    pat.yRowInit = init;
                    if (debug) std::cerr << "      -> Identified as Y row phi" << std::endl;
                }
            } else {
                if (step->getVal() == 1) {
                    if (pat.x.cursorPhi) {
                        // 条件归约模式允许第二个单位步长指针 phi (如 b[i][k])
                        if (pat.cond.secondXCursorPhi) {
                            if (debug) std::cerr << "  -> Failed: >2 X cursor phi" << std::endl;
                            return false;
                        }
                        pat.cond.secondXCursorPhi = phi;
                        pat.cond.secondXAdvanceGEP = advance;
                        pat.cond.secondXBaseAtZero = init;
                        if (debug) std::cerr << "      -> Identified as second X cursor phi (conditional)" << std::endl;
                    } else {
                        pat.x.cursorPhi = phi;
                        pat.x.advanceGEP = advance;
                        pat.x.baseAtZero = init;
                        if (debug) std::cerr << "      -> Identified as X cursor phi" << std::endl;
                    }
                } else {
                    if (pat.yColumnPhi) {
                        // 条件归约允许第二个 Y column phi (如 a[k][j])
                        if (pat.cond.secondYColumnPhi) {
                            if (debug) std::cerr << "  -> Failed: >2 Y column phi" << std::endl;
                            return false;
                        }
                        pat.cond.secondYColumnPhi = phi;
                        pat.cond.secondYColumnAdvance = advance;
                        pat.cond.secondYColumnInit = init;
                        if (debug) std::cerr << "      -> Identified as second Y column phi (conditional), rowWidth="
                                              << step->getVal() << std::endl;
                    } else {
                        pat.yColumnPhi = phi;
                        pat.yColumnAdvance = advance;
                        pat.yColumnInit = init;
                        pat.rowWidth = step->getVal();
                        if (debug) std::cerr << "      -> Identified as Y column phi, rowWidth="
                                              << pat.rowWidth << std::endl;
                    }
                }
            }
            continue;
        }

        // 标量 phi：唯一的 sum 归约
        if (pat.sumPhi) {
            if (debug) std::cerr << "  -> Failed: multiple sum phi" << std::endl;
            return false;
        }
        pat.sumPhi = phi;
        if (debug) std::cerr << "      -> Identified as sum phi" << std::endl;
    }

    if (!pat.sumPhi || (!pat.yRowPhi && !pat.yColumnPhi)) {
        if (debug) std::cerr << "  -> Failed: missing sumPhi=" << pat.sumPhi << " or Y phi" << std::endl;
        return false;
    }

    pat.elemType = pat.sumPhi->getType();
    if (!pat.elemType->isIntegerType() && !pat.elemType->isFloatType()) {
        if (debug) std::cerr << "  -> Failed: elem type not int/float" << std::endl;
        return false;
    }

    if (pat.yRowPhi) {
        const auto * yRowType = dynamic_cast<const ArrayType *>(pointeeOf(pat.yRowPhi->getType()));
        if (!yRowType || yRowType->getElementType() != pat.elemType) {
            if (debug) std::cerr << "  -> Failed: Y row type mismatch" << std::endl;
            return false;
        }
        pat.rowWidth = yRowType->getNumElements();
    }
    if (pat.yColumnPhi) {
        if (!isElemPointer(pat.yColumnPhi->getType(), pat.elemType)) {
            if (debug) std::cerr << "  -> Failed: Y column phi type mismatch" << std::endl;
            return false;
        }
        if (pat.rowWidth <= 0) {
            if (debug) std::cerr << "  -> Failed: Y column rowWidth invalid" << std::endl;
            return false;
        }
    }

    Value * sumBackedgeValue = nullptr;
    BasicBlock * sumInitBlock = nullptr;
    if (!getLoopPhiIncoming(pat.sumPhi, kBodySet, pat.sumInit, sumInitBlock, sumBackedgeValue, pat.sumBackedgeBlock)) {
        if (debug) std::cerr << "  -> Failed: sum phi incoming split" << std::endl;
        return false;
    }
    if (sumInitBlock != pat.kLoop.preheader || pat.sumBackedgeBlock != kLatch) {
        if (debug) std::cerr << "  -> Failed: sum phi edge mismatch" << std::endl;
        return false;
    }
    pat.acc = dynamic_cast<BinaryInst *>(sumBackedgeValue);
    const bool isFloat = pat.elemType->isFloatType();

    // ---- 条件归约检测：sum 回边为 phi 时，尝试匹配 if(cond) acc+=X*Y 模式 ----
    auto * sumBackedgePhi = !pat.acc ? dynamic_cast<PhiInst *>(sumBackedgeValue) : nullptr;
    if (sumBackedgePhi && sumBackedgePhi->getIncomingCount() == 2) {
        Value * trueUpdate = nullptr;
        BasicBlock * trueBlock = nullptr;
        Value * unchangedValue = nullptr;
        BasicBlock * unchangedBlock = nullptr;
        for (int32_t i = 0; i < sumBackedgePhi->getIncomingCount(); ++i) {
            Value * incoming = sumBackedgePhi->getIncomingValue(i);
            BasicBlock * block = sumBackedgePhi->getIncomingBlock(i);
            if (incoming == pat.sumPhi) {
                if (unchangedValue) {
                    return false;
                }
                unchangedValue = incoming;
                unchangedBlock = block;
            } else {
                if (trueUpdate) {
                    return false;
                }
                trueUpdate = incoming;
                trueBlock = block;
            }
        }
        if (!trueUpdate || !trueBlock || !unchangedValue || unchangedBlock != kBody ||
            sumBackedgePhi->getParentBlock() != pat.sumBackedgeBlock) {
            if (debug) std::cerr << "  -> Failed: malformed conditional merge phi" << std::endl;
            return false;
        }
        auto * trueAcc = dynamic_cast<BinaryInst *>(trueUpdate);
        if (!pat.elemType->isInt32Type() || isFloat || !trueAcc ||
            trueAcc->getParentBlock() != trueBlock ||
            trueAcc->getOp() != IRInstOperator::IRINST_OP_ADD_I) {
            if (debug) std::cerr << "  -> Failed: conditional trueAcc not BinaryInst add" << std::endl;
            return false;
        }

        // 仅接受 true 边执行乘加、false 边直接保留 sum 的规范 CFG
        auto * condBr = dynamic_cast<CondBranchInst *>(kBody->getTerminator());
        auto * trueBr = dynamic_cast<BranchInst *>(trueBlock->getTerminator());
        if (!condBr || condBr->getTrueDest() != trueBlock ||
            condBr->getFalseDest() != pat.sumBackedgeBlock ||
            !trueBr || trueBr->getTarget() != pat.sumBackedgeBlock ||
            trueBlock->getPredecessors().size() != 1 ||
            trueBlock->getPredecessors().front() != kBody ||
            trueBlock->getSuccessors().size() != 1 ||
            trueBlock->getSuccessors().front() != pat.sumBackedgeBlock ||
            pat.sumBackedgeBlock->getPredecessors().size() != 2 ||
            std::find(pat.sumBackedgeBlock->getPredecessors().begin(),
                      pat.sumBackedgeBlock->getPredecessors().end(),
                      kBody) == pat.sumBackedgeBlock->getPredecessors().end() ||
            std::find(pat.sumBackedgeBlock->getPredecessors().begin(),
                      pat.sumBackedgeBlock->getPredecessors().end(),
                      trueBlock) == pat.sumBackedgeBlock->getPredecessors().end()) {
            if (debug) std::cerr << "  -> Failed: conditional CFG polarity mismatch" << std::endl;
            return false;
        }
        auto * condCmp = dynamic_cast<ICmpInst *>(condBr->getCondition());
        if (!condCmp || condCmp->getParentBlock() != kBody ||
            condCmp->getOp() != IRInstOperator::IRINST_OP_EQ_I) {
            if (debug) std::cerr << "  -> Failed: condition is not integer equality" << std::endl;
            return false;
        }
        auto * condMod = dynamic_cast<BinaryInst *>(condCmp->getLHS());
        auto * cmpZero = asConstInt(condCmp->getRHS());
        if (!condMod || condMod->getParentBlock() != kBody ||
            condMod->getOp() != IRInstOperator::IRINST_OP_MOD_I ||
            !cmpZero || cmpZero->getVal() != 0) {
            if (debug) std::cerr << "  -> Failed: condition is not srem == 0" << std::endl;
            return false;
        }
        auto * condMul = dynamic_cast<BinaryInst *>(condMod->getLHS());
        auto * modTwo = asConstInt(condMod->getRHS());
        if (!condMul || condMul->getParentBlock() != kBody ||
            condMul->getOp() != IRInstOperator::IRINST_OP_MUL_I ||
            !modTwo || modTwo->getVal() != 2) {
            if (debug) std::cerr << "  -> Failed: condition is not product modulo two" << std::endl;
            return false;
        }

        BinaryInst * trueMul = nullptr;
        if (trueAcc->getLHS() == pat.sumPhi) {
            trueMul = dynamic_cast<BinaryInst *>(trueAcc->getRHS());
            pat.accSumFirst = true;
        } else if (trueAcc->getRHS() == pat.sumPhi) {
            trueMul = dynamic_cast<BinaryInst *>(trueAcc->getLHS());
            pat.accSumFirst = false;
        }
        if (!trueMul || trueMul->getParentBlock() != trueBlock ||
            trueMul->getOp() != IRInstOperator::IRINST_OP_MUL_I) {
            if (debug) std::cerr << "  -> Failed: trueMul not found" << std::endl;
            return false;
        }

        pat.mul = condMul;
        pat.cond.active = true;
        pat.cond.condMod = condMod;
        pat.cond.condCmp = condCmp;
        pat.cond.condBr = condBr;
        pat.cond.condTrueBlock = trueBlock;
        pat.cond.mergePhi = sumBackedgePhi;
        pat.cond.trueMul = trueMul;
        pat.cond.trueAcc = trueAcc;
        pat.cond.trueLoadX = dynamic_cast<LoadInst *>(trueMul->getLHS());
        pat.cond.trueLoadY = dynamic_cast<LoadInst *>(trueMul->getRHS());
        if (!pat.cond.trueLoadX || !pat.cond.trueLoadY ||
            pat.cond.trueLoadX == pat.cond.trueLoadY ||
            pat.cond.trueLoadX->getParentBlock() != trueBlock ||
            pat.cond.trueLoadY->getParentBlock() != trueBlock) {
            if (debug) std::cerr << "  -> Failed: true block loads not found" << std::endl;
            return false;
        }
        pat.reductionBlock = trueBlock;

        if (debug) std::cerr << "      -> Conditional matmul detected, trueBlock="
                           << trueBlock->getName() << std::endl;
    }

    if (!pat.sumInit) {
        if (debug) std::cerr << "  -> Failed: sumInit not found" << std::endl;
        return false;
    }

    if (!pat.cond.active && !pat.acc) {
        if (debug) std::cerr << "  -> Failed: acc not found" << std::endl;
        return false;
    }

    if (!pat.cond.active) {
        pat.reductionBlock = pat.acc->getParentBlock();
        if (!pat.sumBackedgeBlock || !pat.reductionBlock || kBodySet.find(pat.reductionBlock) == kBodySet.end()) {
            return false;
        }

        const IRInstOperator accOp = pat.acc->getOp();
        if (accOp != (isFloat ? IRInstOperator::IRINST_OP_ADD_F : IRInstOperator::IRINST_OP_ADD_I)) {
            return false;
        }

        if (pat.acc->getLHS() == pat.sumPhi) {
            pat.mul = dynamic_cast<BinaryInst *>(pat.acc->getRHS());
            pat.accSumFirst = true;
        } else if (pat.acc->getRHS() == pat.sumPhi) {
            pat.mul = dynamic_cast<BinaryInst *>(pat.acc->getLHS());
            pat.accSumFirst = false;
        } else {
            return false;
        }

        if (!pat.mul || pat.mul->getOp() != (isFloat ? IRInstOperator::IRINST_OP_MUL_F : IRInstOperator::IRINST_OP_MUL_I)) {
            return false;
        }
    }

    // ---- 识别两个 load 并区分 X / Y ----
    auto * lhsLoad = dynamic_cast<LoadInst *>(pat.mul->getLHS());
    auto * rhsLoad = dynamic_cast<LoadInst *>(pat.mul->getRHS());
    if (!lhsLoad || !rhsLoad || lhsLoad == rhsLoad ||
        lhsLoad->getParentBlock() != kBody || rhsLoad->getParentBlock() != kBody) {
        return false;
    }

    auto classifyY = [&pat](LoadInst * load, bool & usesColumnCursor) -> GetElementPtrInst * {
        usesColumnCursor = false;
        if (pat.yColumnPhi && load->getPointerOperand() == pat.yColumnPhi) {
            usesColumnCursor = true;
            return nullptr;
        }

        auto * gep = dynamic_cast<GetElementPtrInst *>(load->getPointerOperand());
        if (pat.yRowPhi && gep && gep->isArrayDecayGEP() && gep->getBasePointer() == pat.yRowPhi &&
            gep->getIndexOperand() == pat.jLoop.induction) {
            return gep;
        }
        return nullptr;
    };

    bool rhsUsesYColumn = false;
    bool lhsUsesYColumn = false;
    if (auto * gep = classifyY(rhsLoad, rhsUsesYColumn); gep || rhsUsesYColumn) {
        pat.loadY = rhsLoad;
        pat.yElemGEP = gep;
        pat.yUsesColumnCursor = rhsUsesYColumn;
        pat.loadX = lhsLoad;
        pat.mulXFirst = true;
    } else if (auto * gepL = classifyY(lhsLoad, lhsUsesYColumn); gepL || lhsUsesYColumn) {
        pat.loadY = lhsLoad;
        pat.yElemGEP = gepL;
        pat.yUsesColumnCursor = lhsUsesYColumn;
        pat.loadX = rhsLoad;
        pat.mulXFirst = false;
    } else {
        return false;
    }

    if (pat.loadX->getType() != pat.elemType || pat.loadY->getType() != pat.elemType) {
        if (debug) std::cerr << "  -> Failed: load type mismatch" << std::endl;
        return false;
    }

    if (debug && pat.cond.active) std::cerr << "  -> Loads classified OK, checking X addr..." << std::endl;

    // X 地址：游标 phi 直接寻址，或 gep(base, kIV)
    Value * xPtr = pat.loadX->getPointerOperand();
    if (pat.x.cursorPhi) {
        if (xPtr != pat.x.cursorPhi) {
            return false;
        }
    } else {
        auto * gep = dynamic_cast<GetElementPtrInst *>(xPtr);
        if (!gep || gep->getIndexOperand() != pat.kLoop.induction ||
            !isInvariantOutside(gep->getBasePointer(), jBodySet)) {
            return false;
        }
        // gep 形态要求 k 下界为 0，使迭代起点地址即为 base
        if (!pat.kLoop.hasConstInitialValue || pat.kLoop.initialIntValue != 0) {
            return false;
        }
        pat.x.indexGEP = gep;
        pat.x.baseAtZero = gep->getBasePointer();
        pat.x.needMaterializeBase = gep->isArrayDecayGEP();
        if (!isElemPointer(gep->getType(), pat.elemType)) {
            return false;
        }
    }

    // 条件乘加的第二组 load 必须精确对应第二把单位步长 X 游标和 Y 行/列游标
    if (pat.cond.active) {
        if (!pat.cond.secondXCursorPhi ||
            (!pat.cond.secondYRowPhi && !pat.cond.secondYColumnPhi)) {
            if (debug) std::cerr << "  -> Failed: missing conditional X/Y cursor" << std::endl;
            return false;
        }

        auto matchSecondY = [&pat](LoadInst * load, bool & usesColumn,
                                   GetElementPtrInst *& elemGEP) {
            usesColumn = false;
            elemGEP = nullptr;
            if (pat.cond.secondYColumnPhi &&
                load->getPointerOperand() == pat.cond.secondYColumnPhi) {
                usesColumn = true;
                return true;
            }

            auto * gep = dynamic_cast<GetElementPtrInst *>(load->getPointerOperand());
            if (pat.cond.secondYRowPhi && gep && gep->isArrayDecayGEP() &&
                gep->getBasePointer() == pat.cond.secondYRowPhi &&
                gep->getIndexOperand() == pat.jLoop.induction) {
                elemGEP = gep;
                return true;
            }
            return false;
        };

        LoadInst * lhs = pat.cond.trueLoadX;
        LoadInst * rhs = pat.cond.trueLoadY;
        bool rhsUsesColumn = false;
        bool lhsUsesColumn = false;
        GetElementPtrInst * rhsElemGEP = nullptr;
        GetElementPtrInst * lhsElemGEP = nullptr;
        if (lhs->getPointerOperand() == pat.cond.secondXCursorPhi &&
            matchSecondY(rhs, rhsUsesColumn, rhsElemGEP)) {
            pat.cond.trueLoadX = lhs;
            pat.cond.trueLoadY = rhs;
            pat.cond.trueMulXFirst = true;
            pat.cond.secondYUsesColumn = rhsUsesColumn;
            pat.cond.trueYElemGEP = rhsElemGEP;
        } else if (rhs->getPointerOperand() == pat.cond.secondXCursorPhi &&
                   matchSecondY(lhs, lhsUsesColumn, lhsElemGEP)) {
            pat.cond.trueLoadX = rhs;
            pat.cond.trueLoadY = lhs;
            pat.cond.trueMulXFirst = false;
            pat.cond.secondYUsesColumn = lhsUsesColumn;
            pat.cond.trueYElemGEP = lhsElemGEP;
        } else {
            if (debug) std::cerr << "  -> Failed: conditional loads do not match cursors" << std::endl;
            return false;
        }

        if (pat.cond.trueLoadX->getType() != pat.elemType ||
            pat.cond.trueLoadY->getType() != pat.elemType ||
            pat.cond.secondXCursorPhi->getType() != pat.cond.trueLoadX->getPointerOperand()->getType()) {
            return false;
        }
    }

    // ---- j.header 的 phi：归纳变量 + 可选 Z 游标 + LSR 后的 Y 列起点游标 ----
    for (auto * inst : jHeader->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        if (phi == pat.jLoop.induction) {
            continue;
        }

        if (debug) {
            std::cerr << "    j.header phi at " << phi
                      << ", isPointer=" << phi->getType()->isPointerType()
                      << ", yColumnInit=" << pat.yColumnInit
                      << ", condActive=" << pat.cond.active << std::endl;
        }

        ElemAccess access;
        if (!matchUnitCursor(phi, pat.jLoop.preheader, jLatch, access)) {
            if (debug) {
                std::cerr << "    -> Failed: j.header non-IV phi not unit cursor";
                if (!phi->getType()->isPointerType()) {
                    std::cerr << " (not pointer type)";
                }
                Value * init = getPhiIncomingFrom(phi, pat.jLoop.preheader);
                Value * backedge = getPhiIncomingFrom(phi, jLatch);
                std::cerr << ", init=" << init
                          << ", backedge=" << backedge;
                if (auto * gep = dynamic_cast<GetElementPtrInst *>(backedge)) {
                    std::cerr << " (gep, isDecay=" << gep->isArrayDecayGEP()
                              << ", base=" << gep->getBasePointer() << ")";
                }
                std::cerr << std::endl;
            }
            return false;
        }

        if (pat.yUsesColumnCursor && phi == pat.yColumnInit) {
            pat.yColumnStartPhi = phi;
            pat.yBaseAtZero = access.baseAtZero;
            continue;
        }

        if (pat.z.cursorPhi) {
            return false;
        }
        pat.z = access;
    }

    if (pat.yUsesColumnCursor && !pat.yBaseAtZero) {
        auto * initGEP = dynamic_cast<GetElementPtrInst *>(pat.yColumnInit);
        if (initGEP && initGEP->getIndexOperand() == pat.jLoop.induction &&
            isInvariantOutside(initGEP->getBasePointer(), jBodySet)) {
            pat.yBaseAtZero = initGEP->getBasePointer();
            pat.yBaseNeedMaterialize = initGEP->isArrayDecayGEP();
        } else if (isInvariantOutside(pat.yColumnInit, jBodySet)) {
            pat.yBaseAtZero = pat.yColumnInit;
        }
        if (!pat.yBaseAtZero) {
            if (debug) std::cerr << "  -> Failed: cannot determine yBaseAtZero" << std::endl;
            return false;
        }
    }

    if (pat.yUsesColumnCursor && pat.yBaseNeedMaterialize) {
        const auto * yBaseRowType = dynamic_cast<const ArrayType *>(pointeeOf(pat.yBaseAtZero->getType()));
        if (!yBaseRowType || yBaseRowType->getElementType() != pat.elemType ||
            yBaseRowType->getNumElements() != pat.rowWidth) {
            if (debug) std::cerr << "  -> Failed: yBaseMaterialize check" << std::endl;
            return false;
        }
    }

    if (pat.cond.active) {
        if (!pat.jLoop.hasConstBoundValue || pat.jLoop.boundIntValue <= 0 ||
            pat.jLoop.boundIntValue > pat.rowWidth) {
            if (debug) std::cerr << "  -> Failed: conditional j bound is not statically safe" << std::endl;
            return false;
        }
        if (!pat.cond.secondXBaseAtZero ||
            !isInvariantOutside(pat.cond.secondXBaseAtZero, jBodySet) ||
            !isElemPointer(pat.cond.secondXCursorPhi->getType(), pat.elemType)) {
            if (debug) std::cerr << "  -> Failed: conditional X base is not loop invariant" << std::endl;
            return false;
        }

        if (pat.cond.secondYUsesColumn) {
            auto * step = pat.cond.secondYColumnAdvance
                              ? asConstInt(pat.cond.secondYColumnAdvance->getIndexOperand())
                              : nullptr;
            auto * initGEP = dynamic_cast<GetElementPtrInst *>(pat.cond.secondYColumnInit);
            if (!step || step->getVal() != pat.rowWidth || !initGEP ||
                initGEP->getIndexOperand() != pat.jLoop.induction ||
                !isInvariantOutside(initGEP->getBasePointer(), jBodySet) ||
                !isElemPointer(pat.cond.secondYColumnPhi->getType(), pat.elemType)) {
                if (debug) std::cerr << "  -> Failed: conditional Y column base mismatch" << std::endl;
                return false;
            }
            pat.cond.secondYBaseAtZero = initGEP->getBasePointer();
            pat.cond.secondYBaseNeedMaterialize = initGEP->isArrayDecayGEP();
            if (pat.cond.secondYBaseNeedMaterialize) {
                const auto * rowType = dynamic_cast<const ArrayType *>(
                    pointeeOf(pat.cond.secondYBaseAtZero->getType()));
                if (!rowType || rowType->getElementType() != pat.elemType ||
                    rowType->getNumElements() != pat.rowWidth) {
                    return false;
                }
            }
        } else {
            const auto * rowType = pat.cond.secondYRowPhi
                                       ? dynamic_cast<const ArrayType *>(
                                             pointeeOf(pat.cond.secondYRowPhi->getType()))
                                       : nullptr;
            if (!rowType || rowType->getElementType() != pat.elemType ||
                rowType->getNumElements() != pat.rowWidth ||
                !pat.cond.secondYRowInit ||
                !isInvariantOutside(pat.cond.secondYRowInit, jBodySet)) {
                if (debug) std::cerr << "  -> Failed: conditional Y row base mismatch" << std::endl;
                return false;
            }
            pat.cond.secondYBaseAtZero = pat.cond.secondYRowInit;
        }
    }

    if (debug && pat.cond.active) std::cerr << "  -> j.header phi OK, checking j.latch..." << std::endl;

    // ---- j.latch：store + j 步进 + 可选 Z 寻址 ----
    pat.store = nullptr;
    for (auto * inst : jLatch->getInstructions()) {
        if (auto * store = dynamic_cast<StoreInst *>(inst)) {
            if (pat.store) {
                return false;
            }
            pat.store = store;
        }
    }
    if (!pat.store || pat.store->getValueOperand() != pat.sumPhi) {
        if (debug) {
            std::cerr << "  -> Failed: store mismatch, store=" << pat.store
                      << " storeVal=" << (pat.store ? pat.store->getValueOperand() : nullptr)
                      << " sumPhi=" << pat.sumPhi << std::endl;
        }
        return false;
    }

    Value * zPtr = pat.store->getPointerOperand();
    if (pat.z.cursorPhi) {
        if (zPtr != pat.z.cursorPhi) {
            return false;
        }
    } else {
        auto * gep = dynamic_cast<GetElementPtrInst *>(zPtr);
        if (!gep || gep->getIndexOperand() != pat.jLoop.induction ||
            !isInvariantOutside(gep->getBasePointer(), jBodySet)) {
            return false;
        }
        pat.z.indexGEP = gep;
        pat.z.baseAtZero = gep->getBasePointer();
        pat.z.needMaterializeBase = gep->isArrayDecayGEP();
        if (!isElemPointer(gep->getType(), pat.elemType)) {
            return false;
        }
    }

    if (!isElemPointer(pat.z.cursorPhi ? pat.z.cursorPhi->getType() : pat.z.indexGEP->getType(), pat.elemType)) {
        if (debug) std::cerr << "  -> Failed: Z elemType mismatch" << std::endl;
        return false;
    }
    pat.elemPtrType = pat.z.cursorPhi ? pat.z.cursorPhi->getType() : pat.z.indexGEP->getType();
    if (debug && pat.cond.active) std::cerr << "  -> j.latch OK, checking whitelist..." << std::endl;
    if (pat.x.cursorPhi && pat.x.cursorPhi->getType() != pat.elemPtrType) {
        return false;
    }
    if (pat.yUsesColumnCursor && pat.yColumnPhi->getType() != pat.elemPtrType) {
        return false;
    }
    if (pat.cond.active &&
        (pat.cond.secondXCursorPhi->getType() != pat.elemPtrType ||
         (pat.cond.secondYUsesColumn &&
          pat.cond.secondYColumnPhi->getType() != pat.elemPtrType))) {
        return false;
    }

    // ---- 白名单校验：四个块中不允许出现任何模式之外的指令 ----
    std::unordered_set<Instruction *> allowed;
    auto allow = [&allowed](Instruction * inst) {
        if (inst) {
            allowed.insert(inst);
        }
    };

    // j.header / k.header：phi + cmp + 分支
    for (auto * phi : kPhis) {
        allow(phi);
    }
    allow(pat.kLoop.cmp);
    allow(pat.kLoop.branch);
    for (auto * inst : jHeader->getInstructions()) {
        if (dynamic_cast<PhiInst *>(inst)) {
            allow(inst);
        } else {
            break;
        }
    }
    allow(pat.jLoop.cmp);
    allow(pat.jLoop.branch);

    // k reduction block
    allow(pat.loadX);
    allow(pat.loadY);
    allow(pat.mul);
    allow(pat.acc);
    allow(pat.yElemGEP);
    allow(pat.yRowAdvance);
    allow(pat.yColumnAdvance);
    allow(dynamic_cast<Instruction *>(pat.yColumnInit));
    allow(pat.x.advanceGEP);
    allow(pat.x.indexGEP);
    allow(dynamic_cast<Instruction *>(pat.kLoop.recurrence->getBackEdgeValue()));
    allow(pat.reductionBlock->getTerminator());

    // k.body may be only a forwarding block after loop canonicalization.
    allow(kBody->getTerminator());

    // k.latch（如果与 k.body 不同）
    if (kLatch != kBody) {
        // k.latch 应该只包含 k 归纳变量递增和跳转到 k.header
        allow(dynamic_cast<Instruction *>(pat.kLoop.recurrence->getBackEdgeValue()));
        allow(pat.yRowAdvance);
        allow(pat.yColumnAdvance);
        allow(pat.x.advanceGEP);
        allow(kLatch->getTerminator());
    }

    // ---- 条件归约：白名单扩展 ----
    if (pat.cond.active) {
        // k.body 中的条件指令
        allow(pat.cond.condMod);
        allow(pat.cond.condCmp);
        allow(pat.cond.condBr);
        // 第二组 X/Y 游标步进
        allow(pat.cond.secondXAdvanceGEP);
        allow(pat.cond.secondYRowAdvance);
        allow(pat.cond.secondYColumnAdvance);
        allow(pat.cond.trueLoadX);
        allow(pat.cond.trueLoadY);
        allow(pat.cond.trueYElemGEP);
        allow(pat.cond.trueMul);
        allow(pat.cond.trueAcc);
        allow(pat.cond.mergePhi);
        if (pat.cond.condTrueBlock) {
            allow(pat.cond.condTrueBlock->getTerminator());
        }
    }

    // j.latch
    allow(pat.store);
    allow(pat.z.advanceGEP);
    allow(pat.z.indexGEP);
    allow(dynamic_cast<Instruction *>(pat.jLoop.recurrence->getBackEdgeValue()));
    allow(jLatch->getTerminator());

    std::vector<BasicBlock *> nestBlocks = {jHeader, kHeader, kBody, jLatch};
    if (pat.reductionBlock && pat.reductionBlock != kBody) {
        nestBlocks.push_back(pat.reductionBlock);
    }
    if (kLatch != kBody) {
        nestBlocks.push_back(kLatch);
    }
    if (pat.forwardBlock) {
        nestBlocks.push_back(pat.forwardBlock);
        allow(pat.forwardBlock->getTerminator());
        for (auto * inst : pat.forwardBlock->getInstructions()) {
            if (dynamic_cast<GetElementPtrInst *>(inst)) {
                allow(inst);
            }
        }
    }

    for (auto * bb : nestBlocks) {
        for (auto * inst : bb->getInstructions()) {
            if (allowed.find(inst) == allowed.end()) {
                if (debug) {
                    std::cerr << "  -> Failed: whitelist - inst op=" << static_cast<int>(inst->getOp())
                              << " in block " << bb << " not allowed" << std::endl;
                }
                return false;
            }
        }
    }

    // ---- 嵌套内定义的值不得在嵌套外使用（旧循环将被整体删除/版本化） ----
    std::unordered_set<BasicBlock *> nestSet(nestBlocks.begin(), nestBlocks.end());
    for (auto * bb : nestBlocks) {
        for (auto * inst : bb->getInstructions()) {
            for (auto * use : inst->getUseList()) {
                auto * user = dynamic_cast<Instruction *>(use->getUser());
                if (!user || nestSet.find(user->getParentBlock()) == nestSet.end()) {
                    if (debug) {
                        std::cerr << "  -> Failed: external use - inst op=" << static_cast<int>(inst->getOp())
                                  << " in block " << bb << " used outside nest" << std::endl;
                    }
                    return false;
                }
            }
        }
    }

    // ---- 外部输入必须在 j 循环外可用 ----
    Value * yExternalBase = pat.yUsesColumnCursor ? pat.yBaseAtZero : pat.yRowInit;
    std::vector<Value *> externalInputs = {pat.sumInit,
                                           pat.x.baseAtZero,
                                           yExternalBase,
                                           pat.z.baseAtZero,
                                           pat.jLoop.boundValue,
                                           pat.kLoop.boundValue,
                                           pat.kLoop.initialValue};
    if (pat.cond.active) {
        externalInputs.push_back(pat.cond.secondXBaseAtZero);
        externalInputs.push_back(pat.cond.secondYBaseAtZero);
    }
    for (auto * value : externalInputs) {
        if (!value || !isInvariantOutside(value, jBodySet)) {
            return false;
        }
    }

    // ---- 别名/根对象合法性 ----
    pat.rootX = stripPointerRoot(pat.x.baseAtZero);
    pat.rootY = stripPointerRoot(yExternalBase);
    pat.rootZ = stripPointerRoot(pat.z.baseAtZero);
    if (!isKnownRoot(pat.rootX) || !isKnownRoot(pat.rootY) || !isKnownRoot(pat.rootZ)) {
        return false;
    }
    if (sameRoot(pat.rootX, pat.rootZ)) {
        // 原序读到的是被本轮 j 迭代逐步覆盖的 X，交换会改变语义
        return false;
    }

    if (pat.cond.active) {
        pat.cond.rootX = stripPointerRoot(pat.cond.secondXBaseAtZero);
        pat.cond.rootY = stripPointerRoot(pat.cond.secondYBaseAtZero);
        if (!isKnownRoot(pat.cond.rootX) || !isKnownRoot(pat.cond.rootY) ||
            sameRoot(pat.rootY, pat.rootZ) ||
            sameRoot(pat.cond.rootX, pat.rootZ) ||
            sameRoot(pat.cond.rootY, pat.rootZ)) {
            // 条件版本尚不处理输入与输出重叠，避免交换改变条件 load 观察到的值
            return false;
        }
    }

    pat.inPlace = sameRoot(pat.rootY, pat.rootZ);
    if (pat.inPlace) {
        // 原地情形要求 Y 行来源与 Z 基址都可证行对齐
        if (pat.yUsesColumnCursor) {
            bool yAligned = false;
            if (pat.yBaseNeedMaterialize) {
                yAligned = isRowAlignedPointer(pat.yBaseAtZero, pat.yBaseAtZero->getType());
            } else {
                yAligned = isFlatRowAlignedPointer(pat.yBaseAtZero, pat.elemPtrType, pat.rowWidth);
            }
            if (!yAligned || !isFlatRowAlignedPointer(pat.z.baseAtZero, pat.elemPtrType, pat.rowWidth)) {
                return false;
            }
        } else {
            if (!isRowAlignedPointer(pat.yRowInit, pat.yRowPhi->getType())) {
                return false;
            }
            auto * zBaseGEP = dynamic_cast<GetElementPtrInst *>(pat.z.baseAtZero);
            auto * zIdx = zBaseGEP ? asConstInt(zBaseGEP->getIndexOperand()) : nullptr;
            if (!zBaseGEP || !zBaseGEP->isArrayDecayGEP() || !zIdx || zIdx->getVal() != 0 ||
                zBaseGEP->getBasePointer()->getType() != pat.yRowPhi->getType() ||
                !isRowAlignedPointer(zBaseGEP->getBasePointer(), pat.yRowPhi->getType())) {
                return false;
            }
        }

        if (pat.jLoop.hasConstBoundValue) {
            if (pat.jLoop.boundIntValue > pat.rowWidth) {
                // 原程序跨行访问，保守放弃
                return false;
            }
            pat.needGuard = false;
        } else {
            pat.needGuard = true;
        }
    }

    // ---- 收益判断 ----
    if (CostModel::profitabilityEnabled()) {
        if ((pat.jLoop.hasConstTripCount && pat.jLoop.tripCount < kMinTripCount) ||
            (pat.kLoop.hasConstTripCount && pat.kLoop.tripCount < kMinTripCount)) {
            CostModel::remark("matmul-interchange", false, "trip count below threshold");
            return false;
        }
        const long rowBytes = static_cast<long>(pat.rowWidth) * 4;
        if (rowBytes < kMinRowBytes) {
            CostModel::remark("matmul-interchange", false, "row narrower than cache line");
            return false;
        }
    }

    if (debug) std::cerr << "[MatMul] PATTERN MATCHED, conditional=" << pat.cond.active
                         << " inPlace=" << pat.inPlace << std::endl;
    return true;
}

/// @brief 物化迭代起点元素地址（gep decay 形态需要新建 gep(base, 0)）
Value * materializeBase(Function * func, Module * mod, const ElemAccess & access, Type * elemPtrType, BasicBlock * insertBlock)
{
    if (!access.needMaterializeBase) {
        return access.baseAtZero;
    }
    auto * gep = new GetElementPtrInst(func, access.baseAtZero, mod->newConstInt32(0), elemPtrType, true);
    insertBeforeTerminator(insertBlock, gep);
    return gep;
}

Value * materializeYBase(Function * func, Module * mod, MatMulPattern & pat, BasicBlock * insertBlock)
{
    if (!pat.yUsesColumnCursor || !pat.yBaseNeedMaterialize) {
        return pat.yUsesColumnCursor ? pat.yBaseAtZero : pat.yRowInit;
    }

    auto * gep = new GetElementPtrInst(func, pat.yBaseAtZero, mod->newConstInt32(0), pat.elemPtrType, true);
    insertBeforeTerminator(insertBlock, gep);
    return gep;
}

/// @brief 在旧循环之外物化条件乘加使用的第二组 Y 基址
Value * materializeConditionalYBase(Function * func,
                                     Module * mod,
                                     MatMulPattern & pat,
                                     BasicBlock * insertBlock)
{
    if (!pat.cond.secondYUsesColumn || !pat.cond.secondYBaseNeedMaterialize) {
        return pat.cond.secondYBaseAtZero;
    }

    auto * gep = new GetElementPtrInst(func,
                                       pat.cond.secondYBaseAtZero,
                                       mod->newConstInt32(0),
                                       pat.elemPtrType,
                                       true);
    insertBeforeTerminator(insertBlock, gep);
    return gep;
}

void replaceOldNest(Function * func, MatMulPattern & pat, BasicBlock * entryBlock, BasicBlock * guard)
{
    BasicBlock * jPre = pat.jLoop.preheader;
    BasicBlock * oldJHeader = pat.jLoop.header;

    rewriteTerminatorTarget(jPre, oldJHeader, entryBlock);
    jPre->removeSuccessor(oldJHeader);
    jPre->addSuccessor(entryBlock);
    entryBlock->addPredecessor(jPre);
    oldJHeader->removePredecessor(jPre);

    if (guard) {
        oldJHeader->markMatMulInterchangeFallback();
        for (auto * inst : oldJHeader->getInstructions()) {
            auto * phi = dynamic_cast<PhiInst *>(inst);
            if (!phi) {
                break;
            }
            phi->replaceIncomingBlock(jPre, guard);
        }
        return;
    }

    std::vector<BasicBlock *> deadBlocks = {oldJHeader, pat.kLoop.header, pat.kLoop.body, pat.jLoop.latch};
    if (pat.kLoop.latch != pat.kLoop.body) {
        deadBlocks.push_back(pat.kLoop.latch);
    }
    if (pat.reductionBlock && pat.reductionBlock != pat.kLoop.body && pat.reductionBlock != pat.kLoop.latch) {
        deadBlocks.push_back(pat.reductionBlock);
    }
    if (pat.forwardBlock) {
        deadBlocks.push_back(pat.forwardBlock);
    }
    std::unordered_set<BasicBlock *> deadSet(deadBlocks.begin(), deadBlocks.end());

    for (auto * deadBB : deadBlocks) {
        for (auto * succ : deadBB->getSuccessors()) {
            if (deadSet.count(succ)) {
                continue;
            }
            succ->removePredecessor(deadBB);
            for (auto * inst : succ->getInstructions()) {
                auto * phi = dynamic_cast<PhiInst *>(inst);
                if (!phi) {
                    break;
                }
                phi->removeIncomingBlock(deadBB);
            }
        }
    }

    for (auto * deadBB : deadBlocks) {
        for (auto * inst : deadBB->getInstructions()) {
            inst->clearOperands();
        }
    }

    auto & blocks = func->getBlocks();
    blocks.erase(std::remove_if(blocks.begin(),
                                blocks.end(),
                                [&deadSet](BasicBlock * bb) { return deadSet.count(bb) != 0; }),
                 blocks.end());

    for (auto * deadBB : deadBlocks) {
        delete deadBB;
    }
}

BinaryInst * createProduct(Function * func,
                           IRInstOperator mulOp,
                           Value * xValue,
                           Value * yValue,
                           Type * elemType,
                           bool mulXFirst)
{
    return mulXFirst ? new BinaryInst(func, mulOp, xValue, yValue, elemType)
                     : new BinaryInst(func, mulOp, yValue, xValue, elemType);
}

BinaryInst * createAccum(Function * func,
                         IRInstOperator addOp,
                         Value * sumValue,
                         Value * productValue,
                         Type * elemType,
                         bool accSumFirst)
{
    return accSumFirst ? new BinaryInst(func, addOp, sumValue, productValue, elemType)
                       : new BinaryInst(func, addOp, productValue, sumValue, elemType);
}

/// @brief 原地 A=C*A 的寄存器分块版本：一次计算 4 个 j 列，sum 保持为 SSA 标量。
void rewriteInPlaceRegisterBlocked(Function * func, Module * mod, MatMulPattern & pat)
{
    BasicBlock * jPre = pat.jLoop.preheader;
    BasicBlock * oldJHeader = pat.jLoop.header;
    BasicBlock * jExit = pat.jLoop.exit;
    Type * idxType = pat.jLoop.induction->getType();
    Type * cmpType = pat.jLoop.cmp->getType();
    Value * ubj = pat.jLoop.boundValue;
    Value * ubk = pat.kLoop.boundValue;
    auto * zero = mod->newConstInteger(idxType, 0);
    auto * one = mod->newConstInteger(idxType, 1);
    auto * three = mod->newConstInteger(idxType, kRegisterBlockCols - 1);
    auto * blockCols = mod->newConstInteger(idxType, kRegisterBlockCols);
    auto * gepOne = mod->newConstInt32(1);
    auto * gepFour = mod->newConstInt32(kRegisterBlockCols);
    auto * rowStep = mod->newConstInt32(pat.rowWidth);
    const bool isFloat = pat.elemType->isFloatType();
    const IRInstOperator addOp = isFloat ? IRInstOperator::IRINST_OP_ADD_F : IRInstOperator::IRINST_OP_ADD_I;
    const IRInstOperator mulOp = isFloat ? IRInstOperator::IRINST_OP_MUL_F : IRInstOperator::IRINST_OP_MUL_I;

    Value * zBase = materializeBase(func, mod, pat.z, pat.elemPtrType, jPre);
    Value * xBase = materializeBase(func, mod, pat.x, pat.elemPtrType, jPre);
    Value * yBase = materializeYBase(func, mod, pat, jPre);

    BasicBlock * guard = pat.needGuard ? func->newBasicBlock() : nullptr;
    BasicBlock * mainJHeader = func->newBasicBlock();
    BasicBlock * mainKHeader = func->newBasicBlock();
    BasicBlock * mainKBody = func->newBasicBlock();
    BasicBlock * mainStore = func->newBasicBlock();
    BasicBlock * tailJHeader = func->newBasicBlock();
    BasicBlock * tailKHeader = func->newBasicBlock();
    BasicBlock * tailKBody = func->newBasicBlock();
    BasicBlock * tailStore = func->newBasicBlock();

    std::vector<BasicBlock *> newBlocks;
    if (guard) {
        newBlocks.push_back(guard);
    }
    newBlocks.insert(newBlocks.end(),
                     {mainJHeader, mainKHeader, mainKBody, mainStore, tailJHeader, tailKHeader, tailKBody, tailStore});
    for (auto * bb : newBlocks) {
        insertBlockBefore(func, bb, oldJHeader);
    }

    BasicBlock * entryBlock = guard ? guard : mainJHeader;
    BasicBlock * mainEntryPred = guard ? guard : jPre;

    if (guard) {
        auto * wConst = mod->newConstInt32(pat.rowWidth);
        auto * guardCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LE_I, ubj, wConst, cmpType);
        auto * guardBr = new CondBranchInst(func, guardCmp, mainJHeader, oldJHeader);
        guard->addInstruction(guardCmp);
        guard->addInstruction(guardBr);
        guard->linkSuccessor(mainJHeader);
        guard->linkSuccessor(oldJHeader);
    }

    // ---- 主 j-block 循环：处理 j, j+1, j+2, j+3 ----
    auto * mainJ = new PhiInst(func, idxType);
    auto * mainZ = new PhiInst(func, pat.elemPtrType);
    auto * mainY = new PhiInst(func, pat.elemPtrType);
    auto * mainJLast = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, mainJ, three, idxType);
    auto * mainJCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, mainJLast, ubj, cmpType);
    auto * mainJBr = new CondBranchInst(func, mainJCmp, mainKHeader, tailJHeader);
    mainJ->addIncoming(zero, mainEntryPred);
    mainZ->addIncoming(zBase, mainEntryPred);
    mainY->addIncoming(yBase, mainEntryPred);
    mainJHeader->addInstruction(mainJ);
    mainJHeader->addInstruction(mainZ);
    mainJHeader->addInstruction(mainY);
    mainJHeader->addInstruction(mainJLast);
    mainJHeader->addInstruction(mainJCmp);
    mainJHeader->addInstruction(mainJBr);
    mainJHeader->linkSuccessor(mainKHeader);
    mainJHeader->linkSuccessor(tailJHeader);

    auto * kIV = new PhiInst(func, pat.kLoop.induction->getType());
    auto * kX = new PhiInst(func, pat.elemPtrType);
    auto * kY = new PhiInst(func, pat.elemPtrType);
    std::vector<PhiInst *> sums;
    for (int32_t lane = 0; lane < kRegisterBlockCols; ++lane) {
        sums.push_back(new PhiInst(func, pat.elemType));
    }
    auto * kCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, kIV, ubk, cmpType);
    auto * kBr = new CondBranchInst(func, kCmp, mainKBody, mainStore);
    kIV->addIncoming(pat.kLoop.initialValue, mainJHeader);
    kX->addIncoming(xBase, mainJHeader);
    kY->addIncoming(mainY, mainJHeader);
    mainKHeader->addInstruction(kIV);
    mainKHeader->addInstruction(kX);
    mainKHeader->addInstruction(kY);
    for (auto * sum : sums) {
        sum->addIncoming(pat.sumInit, mainJHeader);
        mainKHeader->addInstruction(sum);
    }
    mainKHeader->addInstruction(kCmp);
    mainKHeader->addInstruction(kBr);
    mainKHeader->linkSuccessor(mainKBody);
    mainKHeader->linkSuccessor(mainStore);

    auto * xLoad = new LoadInst(func, kX, pat.elemType);
    mainKBody->addInstruction(xLoad);
    std::vector<BinaryInst *> nextSums;
    for (int32_t lane = 0; lane < kRegisterBlockCols; ++lane) {
        Value * yPtr = kY;
        if (lane != 0) {
            yPtr = new GetElementPtrInst(func, kY, mod->newConstInt32(lane), pat.elemPtrType, false);
            mainKBody->addInstruction(dynamic_cast<Instruction *>(yPtr));
        }
        auto * yLoad = new LoadInst(func, yPtr, pat.elemType);
        auto * product = createProduct(func, mulOp, xLoad, yLoad, pat.elemType, pat.mulXFirst);
        auto * nextSum = createAccum(func, addOp, sums[lane], product, pat.elemType, pat.accSumFirst);
        mainKBody->addInstruction(yLoad);
        mainKBody->addInstruction(product);
        mainKBody->addInstruction(nextSum);
        nextSums.push_back(nextSum);
    }
    auto * kNext = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, kIV, one, kIV->getType());
    auto * xNext = new GetElementPtrInst(func, kX, gepOne, pat.elemPtrType, false);
    auto * yNext = new GetElementPtrInst(func, kY, rowStep, pat.elemPtrType, false);
    auto * mainKBodyBr = new BranchInst(func, mainKHeader);
    mainKBody->addInstruction(kNext);
    mainKBody->addInstruction(xNext);
    mainKBody->addInstruction(yNext);
    mainKBody->addInstruction(mainKBodyBr);
    mainKBody->linkSuccessor(mainKHeader);
    kIV->addIncoming(kNext, mainKBody);
    kX->addIncoming(xNext, mainKBody);
    kY->addIncoming(yNext, mainKBody);
    for (int32_t lane = 0; lane < kRegisterBlockCols; ++lane) {
        sums[lane]->addIncoming(nextSums[lane], mainKBody);
    }

    for (int32_t lane = 0; lane < kRegisterBlockCols; ++lane) {
        Value * zPtr = mainZ;
        if (lane != 0) {
            zPtr = new GetElementPtrInst(func, mainZ, mod->newConstInt32(lane), pat.elemPtrType, false);
            mainStore->addInstruction(dynamic_cast<Instruction *>(zPtr));
        }
        auto * store = new StoreInst(func, sums[lane], zPtr);
        mainStore->addInstruction(store);
    }
    auto * mainJNext = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, mainJ, blockCols, idxType);
    auto * mainZNext = new GetElementPtrInst(func, mainZ, gepFour, pat.elemPtrType, false);
    auto * mainYNext = new GetElementPtrInst(func, mainY, gepFour, pat.elemPtrType, false);
    auto * mainStoreBr = new BranchInst(func, mainJHeader);
    mainStore->addInstruction(mainJNext);
    mainStore->addInstruction(mainZNext);
    mainStore->addInstruction(mainYNext);
    mainStore->addInstruction(mainStoreBr);
    mainStore->linkSuccessor(mainJHeader);
    mainJ->addIncoming(mainJNext, mainStore);
    mainZ->addIncoming(mainZNext, mainStore);
    mainY->addIncoming(mainYNext, mainStore);

    // ---- 尾部：处理剩余 0..3 列，仍用标量 sum phi 保持原地安全 ----
    auto * tailJ = new PhiInst(func, idxType);
    auto * tailZ = new PhiInst(func, pat.elemPtrType);
    auto * tailY = new PhiInst(func, pat.elemPtrType);
    auto * tailCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, tailJ, ubj, cmpType);
    auto * tailBr = new CondBranchInst(func, tailCmp, tailKHeader, jExit);
    tailJ->addIncoming(mainJ, mainJHeader);
    tailZ->addIncoming(mainZ, mainJHeader);
    tailY->addIncoming(mainY, mainJHeader);
    tailJHeader->addInstruction(tailJ);
    tailJHeader->addInstruction(tailZ);
    tailJHeader->addInstruction(tailY);
    tailJHeader->addInstruction(tailCmp);
    tailJHeader->addInstruction(tailBr);
    tailJHeader->linkSuccessor(tailKHeader);
    tailJHeader->linkSuccessor(jExit);

    auto * tailKIV = new PhiInst(func, pat.kLoop.induction->getType());
    auto * tailX = new PhiInst(func, pat.elemPtrType);
    auto * tailYCol = new PhiInst(func, pat.elemPtrType);
    auto * tailSum = new PhiInst(func, pat.elemType);
    auto * tailKCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, tailKIV, ubk, cmpType);
    auto * tailKBr = new CondBranchInst(func, tailKCmp, tailKBody, tailStore);
    tailKIV->addIncoming(pat.kLoop.initialValue, tailJHeader);
    tailX->addIncoming(xBase, tailJHeader);
    tailYCol->addIncoming(tailY, tailJHeader);
    tailSum->addIncoming(pat.sumInit, tailJHeader);
    tailKHeader->addInstruction(tailKIV);
    tailKHeader->addInstruction(tailX);
    tailKHeader->addInstruction(tailYCol);
    tailKHeader->addInstruction(tailSum);
    tailKHeader->addInstruction(tailKCmp);
    tailKHeader->addInstruction(tailKBr);
    tailKHeader->linkSuccessor(tailKBody);
    tailKHeader->linkSuccessor(tailStore);

    auto * tailXLoad = new LoadInst(func, tailX, pat.elemType);
    auto * tailYLoad = new LoadInst(func, tailYCol, pat.elemType);
    auto * tailProduct = createProduct(func, mulOp, tailXLoad, tailYLoad, pat.elemType, pat.mulXFirst);
    auto * tailSumNext = createAccum(func, addOp, tailSum, tailProduct, pat.elemType, pat.accSumFirst);
    auto * tailKNext = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, tailKIV, one, tailKIV->getType());
    auto * tailXNext = new GetElementPtrInst(func, tailX, gepOne, pat.elemPtrType, false);
    auto * tailYNextRow = new GetElementPtrInst(func, tailYCol, rowStep, pat.elemPtrType, false);
    auto * tailKBodyBr = new BranchInst(func, tailKHeader);
    tailKBody->addInstruction(tailXLoad);
    tailKBody->addInstruction(tailYLoad);
    tailKBody->addInstruction(tailProduct);
    tailKBody->addInstruction(tailSumNext);
    tailKBody->addInstruction(tailKNext);
    tailKBody->addInstruction(tailXNext);
    tailKBody->addInstruction(tailYNextRow);
    tailKBody->addInstruction(tailKBodyBr);
    tailKBody->linkSuccessor(tailKHeader);
    tailKIV->addIncoming(tailKNext, tailKBody);
    tailX->addIncoming(tailXNext, tailKBody);
    tailYCol->addIncoming(tailYNextRow, tailKBody);
    tailSum->addIncoming(tailSumNext, tailKBody);

    auto * tailOutStore = new StoreInst(func, tailSum, tailZ);
    auto * tailJNext = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, tailJ, one, idxType);
    auto * tailZNext = new GetElementPtrInst(func, tailZ, gepOne, pat.elemPtrType, false);
    auto * tailYNext = new GetElementPtrInst(func, tailY, gepOne, pat.elemPtrType, false);
    auto * tailStoreBr = new BranchInst(func, tailJHeader);
    tailStore->addInstruction(tailOutStore);
    tailStore->addInstruction(tailJNext);
    tailStore->addInstruction(tailZNext);
    tailStore->addInstruction(tailYNext);
    tailStore->addInstruction(tailStoreBr);
    tailStore->linkSuccessor(tailJHeader);
    tailJ->addIncoming(tailJNext, tailStore);
    tailZ->addIncoming(tailZNext, tailStore);
    tailY->addIncoming(tailYNext, tailStore);

    replaceOldNest(func, pat, entryBlock, guard);
}

/// @brief 原地 A = X * A 的常量后缀折叠版本。
///
/// 对每个 X 行运行期识别最长常量后缀 X[k] == c：
///   sum_k X[k] * A[k][j] = c * colSum[j] + sum_{k<p} (X[k] - c) * A[k][j]
///
/// colSum[j] 维护当前原地右矩阵每一列的和。每写完当前输出行后，用
/// new-old 增量更新对应列和，保证下一行仍读到原程序语义下的矩阵状态。
bool rewriteInPlaceConstantTailBlocked(Function * func, Module * mod, MatMulPattern & pat)
{
    const bool debug = std::getenv("MINIC_DEBUG_MATMUL") != nullptr;
    auto fail = [debug](const char * reason) {
        if (debug) {
            std::cerr << "  -> Constant-tail skipped: " << reason << std::endl;
        }
        return false;
    };

    if (!func || !mod) {
        return fail("missing func/mod");
    }
    if (!pat.elemType->isInt32Type()) {
        return fail("element type is not i32");
    }
    if (!pat.inPlace) {
        return fail("not in-place");
    }
    if (!hasCanonicalInPlaceRowTraversal(pat)) {
        if (debug) {
            std::cerr << "     inPlace=" << pat.inPlace
                      << " yUsesColumnCursor=" << pat.yUsesColumnCursor
                      << " yColumnPhi=" << pat.yColumnPhi
                      << " yColumnAdvance=" << pat.yColumnAdvance
                      << " yBaseAtZero=" << pat.yBaseAtZero
                      << " yBaseNeedMaterialize=" << pat.yBaseNeedMaterialize
                      << " rowWidth=" << pat.rowWidth << std::endl;
            if (pat.yColumnAdvance) {
                auto * step = asConstInt(pat.yColumnAdvance->getIndexOperand());
                std::cerr << "     yColumnStep=" << (step ? step->getVal() : -1) << std::endl;
            }
        }
        return fail("non-canonical Y row traversal");
    }

    OuterInPlaceContext outer;
    if (!matchOuterInPlaceContext(pat, outer)) {
        return fail("outer row context not matched");
    }

    Type * idxType = pat.jLoop.induction->getType();
    Type * cmpType = pat.jLoop.cmp->getType();
    Value * ubj = pat.jLoop.boundValue;
    Value * ubk = pat.kLoop.boundValue;
    auto * zero = mod->newConstInteger(idxType, 0);
    auto * one = mod->newConstInteger(idxType, 1);
    auto * blockLast = mod->newConstInteger(idxType, kConstantTailBlockCols - 1);
    auto * blockCols = mod->newConstInteger(idxType, kConstantTailBlockCols);
    auto * rowStep = mod->newConstInteger(idxType, pat.rowWidth);
    auto * i32Zero = mod->newConstInt32(0);
    auto * i32One = mod->newConstInt32(1);

    if (!sameValue(pat.kLoop.initialValue, zero) ||
        (pat.kLoop.hasConstBoundValue && pat.kLoop.boundIntValue <= 0) ||
        staticallyExceedsRowWidth(pat.jLoop, pat.rowWidth) ||
        staticallyExceedsRowWidth(pat.kLoop, pat.rowWidth)) {
        return fail("static bounds not suitable");
    }

    BasicBlock * jPre = pat.jLoop.preheader;
    BasicBlock * oldJHeader = pat.jLoop.header;
    BasicBlock * jExit = pat.jLoop.exit;
    const bool needsGuard = needsRuntimeUpperBoundGuard(pat.jLoop, pat.rowWidth) ||
                            needsRuntimeUpperBoundGuard(pat.kLoop, pat.rowWidth);

    Value * zBase = materializeBase(func, mod, pat.z, pat.elemPtrType, jPre);
    Value * xBase = materializeBase(func, mod, pat.x, pat.elemPtrType, jPre);
    Value * yBase = materializeYBase(func, mod, pat, jPre);

    auto * colAlloca = new AllocaInst(func, ArrayType::get(pat.elemType, pat.rowWidth));
    auto * flagAlloca = new AllocaInst(func, pat.elemType);
    BasicBlock * entry = func->getEntryBlock();
    auto & entryInsts = entry->getInstructions();
    entryInsts.insert(entryInsts.begin(), flagAlloca);
    flagAlloca->setParentBlock(entry);
    entryInsts.insert(entryInsts.begin(), colAlloca);
    colAlloca->setParentBlock(entry);

    auto * colBase = new GetElementPtrInst(func, colAlloca, i32Zero, pat.elemPtrType, true);
    insertBeforeTerminator(outer.preheader, colBase);
    insertBeforeTerminator(outer.preheader, new StoreInst(func, i32Zero, flagAlloca));

    BasicBlock * guardJ = needsGuard ? func->newBasicBlock() : nullptr;
    BasicBlock * guardKUpper = needsGuard ? func->newBasicBlock() : nullptr;
    BasicBlock * guardKPositive = needsGuard ? func->newBasicBlock() : nullptr;
    BasicBlock * fallback = needsGuard ? func->newBasicBlock() : nullptr;
    BasicBlock * flagCheck = func->newBasicBlock();
    BasicBlock * initJHeader = func->newBasicBlock();
    BasicBlock * initJBody = func->newBasicBlock();
    BasicBlock * initKHeader = func->newBasicBlock();
    BasicBlock * initKBody = func->newBasicBlock();
    BasicBlock * initStore = func->newBasicBlock();
    BasicBlock * initDone = func->newBasicBlock();
    BasicBlock * tailSetup = func->newBasicBlock();
    BasicBlock * tailHeader = func->newBasicBlock();
    BasicBlock * tailCheck = func->newBasicBlock();
    BasicBlock * tailAdvance = func->newBasicBlock();
    BasicBlock * mainJHeader = func->newBasicBlock();
    BasicBlock * mainInit = func->newBasicBlock();
    BasicBlock * mainKHeader = func->newBasicBlock();
    BasicBlock * mainKBody = func->newBasicBlock();
    BasicBlock * mainStore = func->newBasicBlock();
    BasicBlock * scalarJHeader = func->newBasicBlock();
    BasicBlock * scalarInit = func->newBasicBlock();
    BasicBlock * scalarKHeader = func->newBasicBlock();
    BasicBlock * scalarKBody = func->newBasicBlock();
    BasicBlock * scalarStore = func->newBasicBlock();

    std::vector<BasicBlock *> newBlocks;
    if (needsGuard) {
        newBlocks.insert(newBlocks.end(), {guardJ, guardKUpper, guardKPositive, fallback});
    }
    newBlocks.insert(newBlocks.end(),
                     {flagCheck,
                      initJHeader,
                      initJBody,
                      initKHeader,
                      initKBody,
                      initStore,
                      initDone,
                      tailSetup,
                      tailHeader,
                      tailCheck,
                      tailAdvance,
                      mainJHeader,
                      mainInit,
                      mainKHeader,
                      mainKBody,
                      mainStore,
                      scalarJHeader,
                      scalarInit,
                      scalarKHeader,
                      scalarKBody,
                      scalarStore});
    for (auto * bb : newBlocks) {
        insertBlockBefore(func, bb, oldJHeader);
    }

    BasicBlock * entryBlock = needsGuard ? guardJ : flagCheck;
    if (needsGuard) {
        addBoundsGuard(func, mod, guardJ, ubj, pat.rowWidth, guardKUpper, fallback, cmpType);
        addBoundsGuard(func, mod, guardKUpper, ubk, pat.rowWidth, guardKPositive, fallback, cmpType);
        auto * positiveCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_GT_I, ubk, zero, cmpType);
        auto * positiveBr = new CondBranchInst(func, positiveCmp, flagCheck, fallback);
        guardKPositive->addInstruction(positiveCmp);
        guardKPositive->addInstruction(positiveBr);
        guardKPositive->linkSuccessor(flagCheck);
        guardKPositive->linkSuccessor(fallback);
        fallback->addInstruction(new BranchInst(func, oldJHeader));
        fallback->linkSuccessor(oldJHeader);
    }

    // ---- 一次性初始化 colSum[j] = sum_k A[k][j] ----
    auto * flagLoad = new LoadInst(func, flagAlloca, pat.elemType);
    auto * flagIsZero = new ICmpInst(func, IRInstOperator::IRINST_OP_EQ_I, flagLoad, i32Zero, cmpType);
    auto * flagBr = new CondBranchInst(func, flagIsZero, initJHeader, tailSetup);
    flagCheck->addInstruction(flagLoad);
    flagCheck->addInstruction(flagIsZero);
    flagCheck->addInstruction(flagBr);
    flagCheck->linkSuccessor(initJHeader);
    flagCheck->linkSuccessor(tailSetup);

    auto * initJ = new PhiInst(func, idxType);
    auto * initCol = new PhiInst(func, pat.elemPtrType);
    auto * initJCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, initJ, ubj, cmpType);
    auto * initJBr = new CondBranchInst(func, initJCmp, initJBody, initDone);
    initJ->addIncoming(zero, flagCheck);
    initCol->addIncoming(colBase, flagCheck);
    initJHeader->addInstruction(initJ);
    initJHeader->addInstruction(initCol);
    initJHeader->addInstruction(initJCmp);
    initJHeader->addInstruction(initJBr);
    initJHeader->linkSuccessor(initJBody);
    initJHeader->linkSuccessor(initDone);

    auto * initYStart = new GetElementPtrInst(func, yBase, initJ, pat.elemPtrType, false);
    initJBody->addInstruction(initYStart);
    initJBody->addInstruction(new BranchInst(func, initKHeader));
    initJBody->linkSuccessor(initKHeader);

    auto * initK = new PhiInst(func, idxType);
    auto * initY = new PhiInst(func, pat.elemPtrType);
    auto * initSum = new PhiInst(func, pat.elemType);
    auto * initKCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, initK, ubk, cmpType);
    auto * initKBr = new CondBranchInst(func, initKCmp, initKBody, initStore);
    initK->addIncoming(zero, initJBody);
    initY->addIncoming(initYStart, initJBody);
    initSum->addIncoming(i32Zero, initJBody);
    initKHeader->addInstruction(initK);
    initKHeader->addInstruction(initY);
    initKHeader->addInstruction(initSum);
    initKHeader->addInstruction(initKCmp);
    initKHeader->addInstruction(initKBr);
    initKHeader->linkSuccessor(initKBody);
    initKHeader->linkSuccessor(initStore);

    auto * initLoad = new LoadInst(func, initY, pat.elemType);
    auto * initSumNext = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, initSum, initLoad, pat.elemType);
    auto * initKNext = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, initK, one, idxType);
    auto * initYNext = new GetElementPtrInst(func, initY, rowStep, pat.elemPtrType, false);
    initKBody->addInstruction(initLoad);
    initKBody->addInstruction(initSumNext);
    initKBody->addInstruction(initKNext);
    initKBody->addInstruction(initYNext);
    initKBody->addInstruction(new BranchInst(func, initKHeader));
    initKBody->linkSuccessor(initKHeader);
    initK->addIncoming(initKNext, initKBody);
    initY->addIncoming(initYNext, initKBody);
    initSum->addIncoming(initSumNext, initKBody);

    auto * initColStore = new StoreInst(func, initSum, initCol);
    auto * initJNext = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, initJ, one, idxType);
    auto * initColNext = new GetElementPtrInst(func, initCol, one, pat.elemPtrType, false);
    initStore->addInstruction(initColStore);
    initStore->addInstruction(initJNext);
    initStore->addInstruction(initColNext);
    initStore->addInstruction(new BranchInst(func, initJHeader));
    initStore->linkSuccessor(initJHeader);
    initJ->addIncoming(initJNext, initStore);
    initCol->addIncoming(initColNext, initStore);

    initDone->addInstruction(new StoreInst(func, i32One, flagAlloca));
    initDone->addInstruction(new BranchInst(func, tailSetup));
    initDone->linkSuccessor(tailSetup);

    // ---- 扫描当前 X 行的最长常量后缀 ----
    auto * lastIndex = new BinaryInst(func, IRInstOperator::IRINST_OP_SUB_I, ubk, one, idxType);
    auto * lastPtr = new GetElementPtrInst(func, xBase, lastIndex, pat.elemPtrType, false);
    auto * tailValue = new LoadInst(func, lastPtr, pat.elemType);
    tailSetup->addInstruction(lastIndex);
    tailSetup->addInstruction(lastPtr);
    tailSetup->addInstruction(tailValue);
    tailSetup->addInstruction(new BranchInst(func, tailHeader));
    tailSetup->linkSuccessor(tailHeader);

    auto * tail = new PhiInst(func, idxType);
    auto * tailPositive = new ICmpInst(func, IRInstOperator::IRINST_OP_GT_I, tail, zero, cmpType);
    auto * tailBr = new CondBranchInst(func, tailPositive, tailCheck, mainJHeader);
    tail->addIncoming(lastIndex, tailSetup);
    tailHeader->addInstruction(tail);
    tailHeader->addInstruction(tailPositive);
    tailHeader->addInstruction(tailBr);
    tailHeader->linkSuccessor(tailCheck);
    tailHeader->linkSuccessor(mainJHeader);

    auto * tailPrev = new BinaryInst(func, IRInstOperator::IRINST_OP_SUB_I, tail, one, idxType);
    auto * tailPrevPtr = new GetElementPtrInst(func, xBase, tailPrev, pat.elemPtrType, false);
    auto * tailPrevValue = new LoadInst(func, tailPrevPtr, pat.elemType);
    auto * tailEq = new ICmpInst(func, IRInstOperator::IRINST_OP_EQ_I, tailPrevValue, tailValue, cmpType);
    auto * tailCheckBr = new CondBranchInst(func, tailEq, tailAdvance, mainJHeader);
    tailCheck->addInstruction(tailPrev);
    tailCheck->addInstruction(tailPrevPtr);
    tailCheck->addInstruction(tailPrevValue);
    tailCheck->addInstruction(tailEq);
    tailCheck->addInstruction(tailCheckBr);
    tailCheck->linkSuccessor(tailAdvance);
    tailCheck->linkSuccessor(mainJHeader);
    tailAdvance->addInstruction(new BranchInst(func, tailHeader));
    tailAdvance->linkSuccessor(tailHeader);
    tail->addIncoming(tailPrev, tailAdvance);

    // ---- 主列块：一次处理 8 列 ----
    auto * mainJ = new PhiInst(func, idxType);
    auto * mainZ = new PhiInst(func, pat.elemPtrType);
    auto * mainCol = new PhiInst(func, pat.elemPtrType);
    auto * mainY = new PhiInst(func, pat.elemPtrType);
    auto * mainJLast = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, mainJ, blockLast, idxType);
    auto * mainJCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, mainJLast, ubj, cmpType);
    auto * mainJBr = new CondBranchInst(func, mainJCmp, mainInit, scalarJHeader);
    for (auto * pred : {tailHeader, tailCheck}) {
        mainJ->addIncoming(zero, pred);
        mainZ->addIncoming(zBase, pred);
        mainCol->addIncoming(colBase, pred);
        mainY->addIncoming(yBase, pred);
    }
    mainJHeader->addInstruction(mainJ);
    mainJHeader->addInstruction(mainZ);
    mainJHeader->addInstruction(mainCol);
    mainJHeader->addInstruction(mainY);
    mainJHeader->addInstruction(mainJLast);
    mainJHeader->addInstruction(mainJCmp);
    mainJHeader->addInstruction(mainJBr);
    mainJHeader->linkSuccessor(mainInit);
    mainJHeader->linkSuccessor(scalarJHeader);

    std::vector<Value *> mainColPtrs;
    std::vector<LoadInst *> mainColLoads;
    std::vector<BinaryInst *> mainInitialSums;
    for (int32_t lane = 0; lane < kConstantTailBlockCols; ++lane) {
        Value * colPtr = mainCol;
        if (lane != 0) {
            auto * gep = new GetElementPtrInst(func, mainCol, mod->newConstInt32(lane), pat.elemPtrType, false);
            mainInit->addInstruction(gep);
            colPtr = gep;
        }
        auto * colLoad = new LoadInst(func, colPtr, pat.elemType);
        auto * initial = new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I, tailValue, colLoad, pat.elemType);
        mainInit->addInstruction(colLoad);
        mainInit->addInstruction(initial);
        mainColPtrs.push_back(colPtr);
        mainColLoads.push_back(colLoad);
        mainInitialSums.push_back(initial);
    }
    mainInit->addInstruction(new BranchInst(func, mainKHeader));
    mainInit->linkSuccessor(mainKHeader);

    auto * mainK = new PhiInst(func, idxType);
    auto * mainX = new PhiInst(func, pat.elemPtrType);
    auto * mainYRow = new PhiInst(func, pat.elemPtrType);
    std::vector<PhiInst *> mainSums;
    for (int32_t lane = 0; lane < kConstantTailBlockCols; ++lane) {
        mainSums.push_back(new PhiInst(func, pat.elemType));
    }
    auto * mainKCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, mainK, tail, cmpType);
    auto * mainKBr = new CondBranchInst(func, mainKCmp, mainKBody, mainStore);
    mainK->addIncoming(zero, mainInit);
    mainX->addIncoming(xBase, mainInit);
    mainYRow->addIncoming(mainY, mainInit);
    mainKHeader->addInstruction(mainK);
    mainKHeader->addInstruction(mainX);
    mainKHeader->addInstruction(mainYRow);
    for (int32_t lane = 0; lane < kConstantTailBlockCols; ++lane) {
        mainSums[lane]->addIncoming(mainInitialSums[lane], mainInit);
        mainKHeader->addInstruction(mainSums[lane]);
    }
    mainKHeader->addInstruction(mainKCmp);
    mainKHeader->addInstruction(mainKBr);
    mainKHeader->linkSuccessor(mainKBody);
    mainKHeader->linkSuccessor(mainStore);

    auto * mainXLoad = new LoadInst(func, mainX, pat.elemType);
    auto * mainDiff = new BinaryInst(func, IRInstOperator::IRINST_OP_SUB_I, mainXLoad, tailValue, pat.elemType);
    mainKBody->addInstruction(mainXLoad);
    mainKBody->addInstruction(mainDiff);
    std::vector<BinaryInst *> mainNextSums;
    for (int32_t lane = 0; lane < kConstantTailBlockCols; ++lane) {
        Value * yPtr = mainYRow;
        if (lane != 0) {
            auto * gep = new GetElementPtrInst(func, mainYRow, mod->newConstInt32(lane), pat.elemPtrType, false);
            mainKBody->addInstruction(gep);
            yPtr = gep;
        }
        auto * yLoad = new LoadInst(func, yPtr, pat.elemType);
        auto * product = new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I, mainDiff, yLoad, pat.elemType);
        auto * nextSum = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, mainSums[lane], product, pat.elemType);
        mainKBody->addInstruction(yLoad);
        mainKBody->addInstruction(product);
        mainKBody->addInstruction(nextSum);
        mainNextSums.push_back(nextSum);
    }
    auto * mainKNext = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, mainK, one, idxType);
    auto * mainXNext = new GetElementPtrInst(func, mainX, one, pat.elemPtrType, false);
    auto * mainYRowNext = new GetElementPtrInst(func, mainYRow, rowStep, pat.elemPtrType, false);
    mainKBody->addInstruction(mainKNext);
    mainKBody->addInstruction(mainXNext);
    mainKBody->addInstruction(mainYRowNext);
    mainKBody->addInstruction(new BranchInst(func, mainKHeader));
    mainKBody->linkSuccessor(mainKHeader);
    mainK->addIncoming(mainKNext, mainKBody);
    mainX->addIncoming(mainXNext, mainKBody);
    mainYRow->addIncoming(mainYRowNext, mainKBody);
    for (int32_t lane = 0; lane < kConstantTailBlockCols; ++lane) {
        mainSums[lane]->addIncoming(mainNextSums[lane], mainKBody);
    }

    for (int32_t lane = 0; lane < kConstantTailBlockCols; ++lane) {
        Value * zPtr = mainZ;
        if (lane != 0) {
            auto * gep = new GetElementPtrInst(func, mainZ, mod->newConstInt32(lane), pat.elemPtrType, false);
            mainStore->addInstruction(gep);
            zPtr = gep;
        }
        auto * oldValue = new LoadInst(func, zPtr, pat.elemType);
        auto * delta = new BinaryInst(func, IRInstOperator::IRINST_OP_SUB_I, mainSums[lane], oldValue, pat.elemType);
        auto * updatedCol = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, mainColLoads[lane], delta, pat.elemType);
        mainStore->addInstruction(oldValue);
        mainStore->addInstruction(new StoreInst(func, mainSums[lane], zPtr));
        mainStore->addInstruction(delta);
        mainStore->addInstruction(updatedCol);
        mainStore->addInstruction(new StoreInst(func, updatedCol, mainColPtrs[lane]));
    }
    auto * mainJNext = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, mainJ, blockCols, idxType);
    auto * mainZNext = new GetElementPtrInst(func, mainZ, blockCols, pat.elemPtrType, false);
    auto * mainColNext = new GetElementPtrInst(func, mainCol, blockCols, pat.elemPtrType, false);
    auto * mainYNext = new GetElementPtrInst(func, mainY, blockCols, pat.elemPtrType, false);
    mainStore->addInstruction(mainJNext);
    mainStore->addInstruction(mainZNext);
    mainStore->addInstruction(mainColNext);
    mainStore->addInstruction(mainYNext);
    mainStore->addInstruction(new BranchInst(func, mainJHeader));
    mainStore->linkSuccessor(mainJHeader);
    mainJ->addIncoming(mainJNext, mainStore);
    mainZ->addIncoming(mainZNext, mainStore);
    mainCol->addIncoming(mainColNext, mainStore);
    mainY->addIncoming(mainYNext, mainStore);

    // ---- 剩余列：标量处理 ----
    auto * scalarJ = new PhiInst(func, idxType);
    auto * scalarZ = new PhiInst(func, pat.elemPtrType);
    auto * scalarCol = new PhiInst(func, pat.elemPtrType);
    auto * scalarY = new PhiInst(func, pat.elemPtrType);
    auto * scalarJCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, scalarJ, ubj, cmpType);
    auto * scalarJBr = new CondBranchInst(func, scalarJCmp, scalarInit, jExit);
    scalarJ->addIncoming(mainJ, mainJHeader);
    scalarZ->addIncoming(mainZ, mainJHeader);
    scalarCol->addIncoming(mainCol, mainJHeader);
    scalarY->addIncoming(mainY, mainJHeader);
    scalarJHeader->addInstruction(scalarJ);
    scalarJHeader->addInstruction(scalarZ);
    scalarJHeader->addInstruction(scalarCol);
    scalarJHeader->addInstruction(scalarY);
    scalarJHeader->addInstruction(scalarJCmp);
    scalarJHeader->addInstruction(scalarJBr);
    scalarJHeader->linkSuccessor(scalarInit);
    scalarJHeader->linkSuccessor(jExit);

    auto * scalarColLoad = new LoadInst(func, scalarCol, pat.elemType);
    auto * scalarInitial = new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I, tailValue, scalarColLoad, pat.elemType);
    scalarInit->addInstruction(scalarColLoad);
    scalarInit->addInstruction(scalarInitial);
    scalarInit->addInstruction(new BranchInst(func, scalarKHeader));
    scalarInit->linkSuccessor(scalarKHeader);

    auto * scalarK = new PhiInst(func, idxType);
    auto * scalarX = new PhiInst(func, pat.elemPtrType);
    auto * scalarYRow = new PhiInst(func, pat.elemPtrType);
    auto * scalarSum = new PhiInst(func, pat.elemType);
    auto * scalarKCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, scalarK, tail, cmpType);
    auto * scalarKBr = new CondBranchInst(func, scalarKCmp, scalarKBody, scalarStore);
    scalarK->addIncoming(zero, scalarInit);
    scalarX->addIncoming(xBase, scalarInit);
    scalarYRow->addIncoming(scalarY, scalarInit);
    scalarSum->addIncoming(scalarInitial, scalarInit);
    scalarKHeader->addInstruction(scalarK);
    scalarKHeader->addInstruction(scalarX);
    scalarKHeader->addInstruction(scalarYRow);
    scalarKHeader->addInstruction(scalarSum);
    scalarKHeader->addInstruction(scalarKCmp);
    scalarKHeader->addInstruction(scalarKBr);
    scalarKHeader->linkSuccessor(scalarKBody);
    scalarKHeader->linkSuccessor(scalarStore);

    auto * scalarXLoad = new LoadInst(func, scalarX, pat.elemType);
    auto * scalarDiff = new BinaryInst(func, IRInstOperator::IRINST_OP_SUB_I, scalarXLoad, tailValue, pat.elemType);
    auto * scalarYLoad = new LoadInst(func, scalarYRow, pat.elemType);
    auto * scalarProduct = new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I, scalarDiff, scalarYLoad, pat.elemType);
    auto * scalarSumNext = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, scalarSum, scalarProduct, pat.elemType);
    auto * scalarKNext = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, scalarK, one, idxType);
    auto * scalarXNext = new GetElementPtrInst(func, scalarX, one, pat.elemPtrType, false);
    auto * scalarYNextRow = new GetElementPtrInst(func, scalarYRow, rowStep, pat.elemPtrType, false);
    scalarKBody->addInstruction(scalarXLoad);
    scalarKBody->addInstruction(scalarDiff);
    scalarKBody->addInstruction(scalarYLoad);
    scalarKBody->addInstruction(scalarProduct);
    scalarKBody->addInstruction(scalarSumNext);
    scalarKBody->addInstruction(scalarKNext);
    scalarKBody->addInstruction(scalarXNext);
    scalarKBody->addInstruction(scalarYNextRow);
    scalarKBody->addInstruction(new BranchInst(func, scalarKHeader));
    scalarKBody->linkSuccessor(scalarKHeader);
    scalarK->addIncoming(scalarKNext, scalarKBody);
    scalarX->addIncoming(scalarXNext, scalarKBody);
    scalarYRow->addIncoming(scalarYNextRow, scalarKBody);
    scalarSum->addIncoming(scalarSumNext, scalarKBody);

    auto * scalarOld = new LoadInst(func, scalarZ, pat.elemType);
    auto * scalarDelta = new BinaryInst(func, IRInstOperator::IRINST_OP_SUB_I, scalarSum, scalarOld, pat.elemType);
    auto * scalarUpdatedCol = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, scalarColLoad, scalarDelta, pat.elemType);
    auto * scalarJNext = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, scalarJ, one, idxType);
    auto * scalarZNext = new GetElementPtrInst(func, scalarZ, one, pat.elemPtrType, false);
    auto * scalarColNext = new GetElementPtrInst(func, scalarCol, one, pat.elemPtrType, false);
    auto * scalarYNext = new GetElementPtrInst(func, scalarY, one, pat.elemPtrType, false);
    scalarStore->addInstruction(scalarOld);
    scalarStore->addInstruction(new StoreInst(func, scalarSum, scalarZ));
    scalarStore->addInstruction(scalarDelta);
    scalarStore->addInstruction(scalarUpdatedCol);
    scalarStore->addInstruction(new StoreInst(func, scalarUpdatedCol, scalarCol));
    scalarStore->addInstruction(scalarJNext);
    scalarStore->addInstruction(scalarZNext);
    scalarStore->addInstruction(scalarColNext);
    scalarStore->addInstruction(scalarYNext);
    scalarStore->addInstruction(new BranchInst(func, scalarJHeader));
    scalarStore->linkSuccessor(scalarJHeader);
    scalarJ->addIncoming(scalarJNext, scalarStore);
    scalarZ->addIncoming(scalarZNext, scalarStore);
    scalarCol->addIncoming(scalarColNext, scalarStore);
    scalarY->addIncoming(scalarYNext, scalarStore);

    replaceOldNest(func, pat, entryBlock, needsGuard ? fallback : nullptr);
    return true;
}

/// @brief 按匹配结果生成 ikj 形态的新嵌套并接管 CFG
void rewritePattern(Function * func, Module * mod, MatMulPattern & pat)
{
    const bool debug = std::getenv("MINIC_DEBUG_MATMUL") != nullptr;
    if (debug) std::cerr << "[MatMul] rewritePattern called, conditional=" << pat.cond.active << std::endl;
    BasicBlock * jPre = pat.jLoop.preheader;
    BasicBlock * oldJHeader = pat.jLoop.header;
    BasicBlock * jExit = pat.jLoop.exit;
    Type * idxType = pat.jLoop.induction->getType();
    Type * cmpType = pat.jLoop.cmp->getType();
    Value * ubj = pat.jLoop.boundValue;
    Value * ubk = pat.kLoop.boundValue;
    auto * zero = mod->newConstInt32(0);
    auto * one = mod->newConstInt32(1);
    const bool isFloat = pat.elemType->isFloatType();
    const IRInstOperator addOp = isFloat ? IRInstOperator::IRINST_OP_ADD_F : IRInstOperator::IRINST_OP_ADD_I;
    const IRInstOperator mulOp = isFloat ? IRInstOperator::IRINST_OP_MUL_F : IRInstOperator::IRINST_OP_MUL_I;

    // 迭代起点地址（必要时物化在 j preheader 中，支配 guard 与全部新块）
    Value * zBase = materializeBase(func, mod, pat.z, pat.elemPtrType, jPre);
    Value * xBase = materializeBase(func, mod, pat.x, pat.elemPtrType, jPre);
    Value * yBase = materializeYBase(func, mod, pat, jPre);
    Value * trueXBase = pat.cond.active ? pat.cond.secondXBaseAtZero : nullptr;
    Value * trueYBase = pat.cond.active
                            ? materializeConditionalYBase(func, mod, pat, jPre)
                            : nullptr;

    // 原地情形：入口块新建一行临时缓冲
    Value * accBase = zBase;
    if (pat.inPlace) {
        auto * bufAlloca = new AllocaInst(func, ArrayType::get(pat.elemType, pat.rowWidth));
        BasicBlock * entry = func->getEntryBlock();
        bufAlloca->setParentBlock(entry);
        entry->getInstructions().insert(entry->getInstructions().begin(), bufAlloca);

        auto * bufBase = new GetElementPtrInst(func, bufAlloca, zero, pat.elemPtrType, true);
        insertBeforeTerminator(jPre, bufBase);
        accBase = bufBase;
    }

    // ---- 创建新块 ----
    BasicBlock * guard = pat.needGuard ? func->newBasicBlock() : nullptr;
    BasicBlock * j1Header = func->newBasicBlock();
    BasicBlock * j1Body = func->newBasicBlock();
    BasicBlock * kHeader = func->newBasicBlock();
    BasicBlock * kBody = func->newBasicBlock();
    BasicBlock * j2Header = func->newBasicBlock();
    BasicBlock * j2Body = func->newBasicBlock();
    BasicBlock * j2True = pat.cond.active ? func->newBasicBlock() : nullptr;
    BasicBlock * j2Continue = pat.cond.active ? func->newBasicBlock() : j2Body;
    BasicBlock * kLatch = func->newBasicBlock();
    BasicBlock * j3Header = pat.inPlace ? func->newBasicBlock() : nullptr;
    BasicBlock * j3Body = pat.inPlace ? func->newBasicBlock() : nullptr;

    std::vector<BasicBlock *> newBlocks;
    if (guard) {
        newBlocks.push_back(guard);
    }
    newBlocks.insert(newBlocks.end(), {j1Header, j1Body, kHeader, kBody, j2Header, j2Body});
    if (pat.cond.active) {
        newBlocks.push_back(j2True);
        newBlocks.push_back(j2Continue);
    }
    newBlocks.push_back(kLatch);
    if (pat.inPlace) {
        newBlocks.push_back(j3Header);
        newBlocks.push_back(j3Body);
    }
    for (auto * bb : newBlocks) {
        insertBlockBefore(func, bb, oldJHeader);
    }

    BasicBlock * entryBlock = guard ? guard : j1Header;       // jPre 改跳的第一个新块
    BasicBlock * j1PredBlock = guard ? guard : jPre;          // j1Header 的实际前驱（phi 入边用）
    BasicBlock * kExitTarget = pat.inPlace ? j3Header : jExit;

    // ---- guard：Nj <= W 时走交换版本，否则回落旧循环 ----
    if (guard) {
        auto * wConst = mod->newConstInt32(pat.rowWidth);
        auto * guardCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LE_I, ubj, wConst, cmpType);
        auto * guardBr = new CondBranchInst(func, guardCmp, j1Header, oldJHeader);
        guard->addInstruction(guardCmp);
        guard->addInstruction(guardBr);
        guard->linkSuccessor(j1Header);
        guard->linkSuccessor(oldJHeader);
    }

    // ---- J1：acc[j] = init ----
    auto * j1IV = new PhiInst(func, idxType);
    auto * j1Acc = new PhiInst(func, pat.elemPtrType);
    auto * j1Cmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, j1IV, ubj, cmpType);
    auto * j1Br = new CondBranchInst(func, j1Cmp, j1Body, kHeader);
    j1IV->addIncoming(zero, j1PredBlock);
    j1Acc->addIncoming(accBase, j1PredBlock);
    j1Header->addInstruction(j1IV);
    j1Header->addInstruction(j1Acc);
    j1Header->addInstruction(j1Cmp);
    j1Header->addInstruction(j1Br);
    j1Header->linkSuccessor(j1Body);
    j1Header->linkSuccessor(kHeader);

    auto * j1Store = new StoreInst(func, pat.sumInit, j1Acc);
    auto * j1AccNext = new GetElementPtrInst(func, j1Acc, one, pat.elemPtrType, false);
    auto * j1Next = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, j1IV, one, idxType);
    auto * j1BodyBr = new BranchInst(func, j1Header);
    j1Body->addInstruction(j1Store);
    j1Body->addInstruction(j1AccNext);
    j1Body->addInstruction(j1Next);
    j1Body->addInstruction(j1BodyBr);
    j1Body->linkSuccessor(j1Header);
    j1IV->addIncoming(j1Next, j1Body);
    j1Acc->addIncoming(j1AccNext, j1Body);

    // ---- K：x = X[k]，内层 J2 扫一行 ----
    auto * kIV = new PhiInst(func, pat.kLoop.induction->getType());
    auto * kXCur = new PhiInst(func, pat.elemPtrType);
    Type * yScanPtrType = pat.yUsesColumnCursor ? pat.elemPtrType : pat.yRowPhi->getType();
    auto * kYRow = new PhiInst(func, yScanPtrType);
    PhiInst * kTrueXCur = nullptr;
    PhiInst * kTrueYRow = nullptr;
    Type * trueYScanPtrType = nullptr;
    if (pat.cond.active) {
        kTrueXCur = new PhiInst(func, pat.elemPtrType);
        trueYScanPtrType = pat.cond.secondYUsesColumn
                               ? pat.elemPtrType
                               : pat.cond.secondYRowPhi->getType();
        kTrueYRow = new PhiInst(func, trueYScanPtrType);
    }
    auto * kCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, kIV, ubk, cmpType);
    auto * kBr = new CondBranchInst(func, kCmp, kBody, kExitTarget);
    kIV->addIncoming(pat.kLoop.initialValue, j1Header);
    kXCur->addIncoming(xBase, j1Header);
    kYRow->addIncoming(yBase, j1Header);
    kHeader->addInstruction(kIV);
    kHeader->addInstruction(kXCur);
    kHeader->addInstruction(kYRow);
    if (pat.cond.active) {
        kTrueXCur->addIncoming(trueXBase, j1Header);
        kTrueYRow->addIncoming(trueYBase, j1Header);
        kHeader->addInstruction(kTrueXCur);
        kHeader->addInstruction(kTrueYRow);
    }
    kHeader->addInstruction(kCmp);
    kHeader->addInstruction(kBr);
    kHeader->linkSuccessor(kBody);
    kHeader->linkSuccessor(kExitTarget);

    // ---- K body：x = X[k] ----
    auto * xLoad = new LoadInst(func, kXCur, pat.elemType);
    Value * yElemBase = kYRow;
    if (!pat.yUsesColumnCursor) {
        yElemBase = new GetElementPtrInst(func, kYRow, zero, pat.elemPtrType, true);
    }
    Value * trueYElemBase = kTrueYRow;
    if (pat.cond.active && !pat.cond.secondYUsesColumn) {
        trueYElemBase = new GetElementPtrInst(func,
                                              kTrueYRow,
                                              zero,
                                              pat.elemPtrType,
                                              true);
    }
    auto * kBodyBr = new BranchInst(func, j2Header);
    kBody->addInstruction(xLoad);
    if (auto * yElemBaseInst = dynamic_cast<Instruction *>(yElemBase)) {
        if (yElemBaseInst != kYRow) {
            kBody->addInstruction(yElemBaseInst);
        }
    }
    if (pat.cond.active) {
        if (auto * trueYElemBaseInst = dynamic_cast<Instruction *>(trueYElemBase)) {
            if (trueYElemBaseInst != kTrueYRow) {
                kBody->addInstruction(trueYElemBaseInst);
            }
        }
    }
    kBody->addInstruction(kBodyBr);
    kBody->linkSuccessor(j2Header);

    // ---- J2：acc[j] += x * Y[k][j]（单位步长热循环） ----
    auto * j2IV = new PhiInst(func, idxType);
    auto * j2Y = new PhiInst(func, pat.elemPtrType);
    auto * j2Acc = new PhiInst(func, pat.elemPtrType);
    PhiInst * j2YTrue = nullptr; // 条件归约的第二把 Y 游标
    if (pat.cond.active) {
        j2YTrue = new PhiInst(func, pat.elemPtrType);
    }
    auto * j2Cmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, j2IV, ubj, cmpType);
    auto * j2Br = new CondBranchInst(func, j2Cmp, j2Body, kLatch);
    j2IV->addIncoming(zero, kBody);
    j2Y->addIncoming(yElemBase, kBody);
    j2Acc->addIncoming(accBase, kBody);
    j2Header->addInstruction(j2IV);
    j2Header->addInstruction(j2Y);
    if (j2YTrue) {
        j2YTrue->addIncoming(trueYElemBase, kBody);
        j2Header->addInstruction(j2YTrue);
    }
    j2Header->addInstruction(j2Acc);
    j2Header->addInstruction(j2Cmp);
    j2Header->addInstruction(j2Br);
    j2Header->linkSuccessor(j2Body);
    j2Header->linkSuccessor(kLatch);

    auto * yLoad = new LoadInst(func, j2Y, pat.elemType);
    if (pat.cond.active) {
        auto * condMulInst = pat.mulXFirst
            ? new BinaryInst(func, mulOp, xLoad, yLoad, pat.elemType)
            : new BinaryInst(func, mulOp, yLoad, xLoad, pat.elemType);
        auto * modTwo = mod->newConstInt32(2);
        auto * modZero = mod->newConstInt32(0);
        auto * condModInst = new BinaryInst(func, IRInstOperator::IRINST_OP_MOD_I, condMulInst, modTwo, pat.elemType);
        auto * condCmpInst = new ICmpInst(func, IRInstOperator::IRINST_OP_EQ_I, condModInst, modZero, cmpType);
        auto * condBr = new CondBranchInst(func, condCmpInst, j2True, j2Continue);
        j2Body->addInstruction(yLoad);
        j2Body->addInstruction(condMulInst);
        j2Body->addInstruction(condModInst);
        j2Body->addInstruction(condCmpInst);
        j2Body->addInstruction(condBr);
        j2Body->linkSuccessor(j2True);
        j2Body->linkSuccessor(j2Continue);

        // 第二组 load 保留在 true 块，避免把原条件访问变成推测执行
        auto * xTrueLoad = new LoadInst(func, kTrueXCur, pat.elemType);
        auto * yTrueLoad = new LoadInst(func, j2YTrue, pat.elemType);
        auto * trueMulInst = pat.cond.trueMulXFirst
                                ? new BinaryInst(func, mulOp, xTrueLoad, yTrueLoad, pat.elemType)
                                : new BinaryInst(func, mulOp, yTrueLoad, xTrueLoad, pat.elemType);
        auto * trueAccLoad = new LoadInst(func, j2Acc, pat.elemType);
        auto * trueAddInst = pat.accSumFirst
                                 ? new BinaryInst(func, addOp, trueAccLoad, trueMulInst, pat.elemType)
                                 : new BinaryInst(func, addOp, trueMulInst, trueAccLoad, pat.elemType);
        auto * trueAccStore = new StoreInst(func, trueAddInst, j2Acc);
        auto * trueBr = new BranchInst(func, j2Continue);
        j2True->addInstruction(xTrueLoad);
        j2True->addInstruction(yTrueLoad);
        j2True->addInstruction(trueMulInst);
        j2True->addInstruction(trueAccLoad);
        j2True->addInstruction(trueAddInst);
        j2True->addInstruction(trueAccStore);
        j2True->addInstruction(trueBr);
        j2True->linkSuccessor(j2Continue);
    } else {
        auto * mulInst = pat.mulXFirst ? new BinaryInst(func, mulOp, xLoad, yLoad, pat.elemType)
                                       : new BinaryInst(func, mulOp, yLoad, xLoad, pat.elemType);
        auto * accLoad = new LoadInst(func, j2Acc, pat.elemType);
        auto * addInst = pat.accSumFirst ? new BinaryInst(func, addOp, accLoad, mulInst, pat.elemType)
                                         : new BinaryInst(func, addOp, mulInst, accLoad, pat.elemType);
        auto * accStore = new StoreInst(func, addInst, j2Acc);
        j2Body->addInstruction(yLoad);
        j2Body->addInstruction(mulInst);
        j2Body->addInstruction(accLoad);
        j2Body->addInstruction(addInst);
        j2Body->addInstruction(accStore);
    }

    auto * j2YNext = new GetElementPtrInst(func, j2Y, one, pat.elemPtrType, false);
    auto * j2AccNext = new GetElementPtrInst(func, j2Acc, one, pat.elemPtrType, false);
    auto * j2Next = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, j2IV, one, idxType);
    auto * j2BodyBr = new BranchInst(func, j2Header);
    j2Continue->addInstruction(j2YNext);
    j2Continue->addInstruction(j2AccNext);
    if (j2YTrue) {
        auto * j2YTrueNext = new GetElementPtrInst(func, j2YTrue, one, pat.elemPtrType, false);
        j2Continue->addInstruction(j2YTrueNext);
        j2YTrue->addIncoming(j2YTrueNext, j2Continue);
    }
    j2Continue->addInstruction(j2Next);
    j2Continue->addInstruction(j2BodyBr);
    j2Continue->linkSuccessor(j2Header);
    j2IV->addIncoming(j2Next, j2Continue);
    j2Y->addIncoming(j2YNext, j2Continue);
    j2Acc->addIncoming(j2AccNext, j2Continue);

    auto * kNext = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, kIV, one, kIV->getType());
    auto * kXNext = new GetElementPtrInst(func, kXCur, one, pat.elemPtrType, false);
    auto * kYStep = pat.yUsesColumnCursor ? mod->newConstInteger(one->getType(), pat.rowWidth) : one;
    auto * kYNext = new GetElementPtrInst(func, kYRow, kYStep, yScanPtrType, false);
    auto * kLatchBr = new BranchInst(func, kHeader);
    kLatch->addInstruction(kNext);
    kLatch->addInstruction(kXNext);
    kLatch->addInstruction(kYNext);
    GetElementPtrInst * kTrueXNext = nullptr;
    GetElementPtrInst * kTrueYNext = nullptr;
    if (pat.cond.active) {
        kTrueXNext = new GetElementPtrInst(func, kTrueXCur, one, pat.elemPtrType, false);
        auto * trueYStep = pat.cond.secondYUsesColumn
                               ? mod->newConstInteger(one->getType(), pat.rowWidth)
                               : one;
        kTrueYNext = new GetElementPtrInst(func,
                                           kTrueYRow,
                                           trueYStep,
                                           trueYScanPtrType,
                                           false);
        kLatch->addInstruction(kTrueXNext);
        kLatch->addInstruction(kTrueYNext);
    }
    kLatch->addInstruction(kLatchBr);
    kLatch->linkSuccessor(kHeader);
    kIV->addIncoming(kNext, kLatch);
    kXCur->addIncoming(kXNext, kLatch);
    kYRow->addIncoming(kYNext, kLatch);
    if (pat.cond.active) {
        kTrueXCur->addIncoming(kTrueXNext, kLatch);
        kTrueYRow->addIncoming(kTrueYNext, kLatch);
    }

    // ---- J3（原地情形）：Z[j] = acc[j] ----
    if (pat.inPlace) {
        auto * j3IV = new PhiInst(func, idxType);
        auto * j3Z = new PhiInst(func, pat.elemPtrType);
        auto * j3Buf = new PhiInst(func, pat.elemPtrType);
        auto * j3Cmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, j3IV, ubj, cmpType);
        auto * j3Br = new CondBranchInst(func, j3Cmp, j3Body, jExit);
        j3IV->addIncoming(zero, kHeader);
        j3Z->addIncoming(zBase, kHeader);
        j3Buf->addIncoming(accBase, kHeader);
        j3Header->addInstruction(j3IV);
        j3Header->addInstruction(j3Z);
        j3Header->addInstruction(j3Buf);
        j3Header->addInstruction(j3Cmp);
        j3Header->addInstruction(j3Br);
        j3Header->linkSuccessor(j3Body);
        j3Header->linkSuccessor(jExit);

        auto * bufLoad = new LoadInst(func, j3Buf, pat.elemType);
        auto * zStore = new StoreInst(func, bufLoad, j3Z);
        auto * j3ZNext = new GetElementPtrInst(func, j3Z, one, pat.elemPtrType, false);
        auto * j3BufNext = new GetElementPtrInst(func, j3Buf, one, pat.elemPtrType, false);
        auto * j3Next = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, j3IV, one, idxType);
        auto * j3BodyBr = new BranchInst(func, j3Header);
        j3Body->addInstruction(bufLoad);
        j3Body->addInstruction(zStore);
        j3Body->addInstruction(j3ZNext);
        j3Body->addInstruction(j3BufNext);
        j3Body->addInstruction(j3Next);
        j3Body->addInstruction(j3BodyBr);
        j3Body->linkSuccessor(j3Header);
        j3IV->addIncoming(j3Next, j3Body);
        j3Z->addIncoming(j3ZNext, j3Body);
        j3Buf->addIncoming(j3BufNext, j3Body);
    }

    // ---- 接管 CFG ----
    replaceOldNest(func, pat, entryBlock, guard);
}

} // namespace

bool MatMulInterchange::run()
{
    if (!func || !mod || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    const bool debug = std::getenv("MINIC_DEBUG_MATMUL") != nullptr;
    if (debug) {
        std::cerr << "[MatMul] Running on function: " << func->getName() << std::endl;
    }
    bool changed = false;
    auto & cache = func->getAnalysisCache();
    while (true) {
        auto & domTree = cache.getOrCompute<DominatorTree>([this] { return DominatorTree(func); });
        auto & loopInfo = cache.getOrCompute<LoopInfo>([this, &domTree] { return LoopInfo(func, &domTree); });
        ScalarEvolution scev(func, &domTree, &loopInfo);

        bool localChanged = false;
        for (auto * header : func->getBlocks()) {
            if (!loopInfo.isLoopHeader(header)) {
                continue;
            }

            if (debug) {
                std::cerr << "[MatMul] Found loop header: " << header->getName() << std::endl;
            }

            MatMulPattern pat;
            if (!matchPattern(scev, loopInfo, header, pat)) {
                continue;
            }

            if (pat.inPlace) {
                if (rewriteInPlaceConstantTailBlocked(func, mod, pat)) {
                    CostModel::remark("matmul-interchange", true, "in-place constant-tail blocked");
                    return true;
                }
                rewriteInPlaceRegisterBlocked(func, mod, pat);
                CostModel::remark("matmul-interchange", true, "in-place register blocked");
            } else {
                rewritePattern(func, mod, pat);
                CostModel::remark("matmul-interchange", true, "direct accumulate");
            }
            localChanged = true;
            changed = true;
            // 改写新建/删除了基本块，CFG 派生分析整体失效
            cache.invalidateCFGAnalyses();
            break;
        }

        if (!localChanged) {
            break;
        }
    }

    return changed;
}
