///
/// @file MatMulInterchange.cpp
/// @brief 矩阵乘法 j-k 循环交换 pass 实现
///

#include "MatMulInterchange.h"

#include <algorithm>
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

/// @brief 匹配到的 (j,k) 列访问归约嵌套
struct MatMulPattern {
    CanonicalLoop jLoop;
    CanonicalLoop kLoop;
    BasicBlock * forwardBlock = nullptr; // j.body 为空转发块时非空

    PhiInst * sumPhi = nullptr;
    Value * sumInit = nullptr;
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

    Type * elemType = nullptr;
    Type * elemPtrType = nullptr;
    int32_t rowWidth = 0;

    PointerRoot rootX;
    PointerRoot rootY;
    PointerRoot rootZ;
    bool inPlace = false;   // Z 与 Y 同根，需要临时行缓冲
    bool needGuard = false; // 运行期 j 上界需 guard Nj<=W 的双版本
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
    if (!matchUnitCountedLoop(scev, jHeader, pat.jLoop)) {
        return false;
    }

    // guard 回落版本不再二次版本化
    if (jHeader->isMatMulInterchangeFallback()) {
        return false;
    }

    // j 下界必须为 0：缓冲/新循环的下标都从 0 起
    if (!pat.jLoop.hasConstInitialValue || pat.jLoop.initialIntValue != 0) {
        return false;
    }

    const auto * jBodySetPtr = loopInfo.getLoopBody(jHeader);
    if (!jBodySetPtr) {
        return false;
    }
    const auto & jBodySet = *jBodySetPtr;

    // 定位 k 循环头：j.body 直接是 k.header，或经一个空转发块
    BasicBlock * kHeader = pat.jLoop.body;
    if (!loopInfo.isLoopHeader(kHeader) && kHeader->getInstructions().size() == 1) {
        if (auto * fwd = dynamic_cast<BranchInst *>(kHeader->getTerminator())) {
            pat.forwardBlock = kHeader;
            kHeader = fwd->getTarget();
        }
    }

    if (!matchUnitCountedLoop(scev, kHeader, pat.kLoop)) {
        return false;
    }

    BasicBlock * expectedKPreheader = pat.forwardBlock ? pat.forwardBlock : jHeader;
    if (pat.kLoop.preheader != expectedKPreheader || pat.kLoop.exit != pat.jLoop.latch ||
        pat.kLoop.body != pat.kLoop.latch) {
        return false;
    }

    BasicBlock * kBody = pat.kLoop.body;
    BasicBlock * jLatch = pat.jLoop.latch;

    // j 出口块不能有 phi（改写后出口新增前驱）
    for (auto * inst : pat.jLoop.exit->getInstructions()) {
        if (dynamic_cast<PhiInst *>(inst)) {
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

    for (auto * phi : kPhis) {
        if (phi == pat.kLoop.induction) {
            continue;
        }

        if (phi->getType()->isPointerType()) {
            Value * init = getPhiIncomingFrom(phi, pat.kLoop.preheader);
            auto * advance = dynamic_cast<GetElementPtrInst *>(getPhiIncomingFrom(phi, kBody));
            if (!init || !advance || advance->isArrayDecayGEP() || advance->getBasePointer() != phi) {
                return false;
            }
            auto * step = asConstInt(advance->getIndexOperand());
            if (!step || step->getVal() != 1) {
                return false;
            }

            const Type * pointee = pointeeOf(phi->getType());
            if (dynamic_cast<const ArrayType *>(pointee)) {
                if (pat.yRowPhi) {
                    return false;
                }
                pat.yRowPhi = phi;
                pat.yRowAdvance = advance;
                pat.yRowInit = init;
            } else {
                if (pat.x.cursorPhi) {
                    return false;
                }
                pat.x.cursorPhi = phi;
                pat.x.advanceGEP = advance;
                pat.x.baseAtZero = init;
            }
            continue;
        }

        // 标量 phi：唯一的 sum 归约
        if (pat.sumPhi) {
            return false;
        }
        pat.sumPhi = phi;
    }

    if (!pat.sumPhi || !pat.yRowPhi) {
        return false;
    }

    pat.elemType = pat.sumPhi->getType();
    if (!pat.elemType->isIntegerType() && !pat.elemType->isFloatType()) {
        return false;
    }

    const auto * yRowType = dynamic_cast<const ArrayType *>(pointeeOf(pat.yRowPhi->getType()));
    if (!yRowType || yRowType->getElementType() != pat.elemType) {
        return false;
    }
    pat.rowWidth = yRowType->getNumElements();

    pat.sumInit = getPhiIncomingFrom(pat.sumPhi, pat.kLoop.preheader);
    pat.acc = dynamic_cast<BinaryInst *>(getPhiIncomingFrom(pat.sumPhi, kBody));
    if (!pat.sumInit || !pat.acc) {
        return false;
    }

    const IRInstOperator accOp = pat.acc->getOp();
    const bool isFloat = pat.elemType->isFloatType();
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

    // ---- 识别两个 load 并区分 X / Y ----
    auto * lhsLoad = dynamic_cast<LoadInst *>(pat.mul->getLHS());
    auto * rhsLoad = dynamic_cast<LoadInst *>(pat.mul->getRHS());
    if (!lhsLoad || !rhsLoad || lhsLoad == rhsLoad) {
        return false;
    }

    auto classifyY = [&pat](LoadInst * load) -> GetElementPtrInst * {
        auto * gep = dynamic_cast<GetElementPtrInst *>(load->getPointerOperand());
        if (gep && gep->isArrayDecayGEP() && gep->getBasePointer() == pat.yRowPhi &&
            gep->getIndexOperand() == pat.jLoop.induction) {
            return gep;
        }
        return nullptr;
    };

    if (auto * gep = classifyY(rhsLoad)) {
        pat.loadY = rhsLoad;
        pat.yElemGEP = gep;
        pat.loadX = lhsLoad;
        pat.mulXFirst = true;
    } else if (auto * gepL = classifyY(lhsLoad)) {
        pat.loadY = lhsLoad;
        pat.yElemGEP = gepL;
        pat.loadX = rhsLoad;
        pat.mulXFirst = false;
    } else {
        return false;
    }

    if (pat.loadX->getType() != pat.elemType || pat.loadY->getType() != pat.elemType) {
        return false;
    }

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

    // ---- j.header 的 phi：归纳变量 + 可选 Z 游标 ----
    for (auto * inst : jHeader->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        if (phi == pat.jLoop.induction) {
            continue;
        }
        if (pat.z.cursorPhi || !matchUnitCursor(phi, pat.jLoop.preheader, jLatch, pat.z)) {
            return false;
        }
    }

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
        return false;
    }
    pat.elemPtrType = pat.z.cursorPhi ? pat.z.cursorPhi->getType() : pat.z.indexGEP->getType();
    if (pat.x.cursorPhi && pat.x.cursorPhi->getType() != pat.elemPtrType) {
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

    // k.body
    allow(pat.loadX);
    allow(pat.loadY);
    allow(pat.mul);
    allow(pat.acc);
    allow(pat.yElemGEP);
    allow(pat.yRowAdvance);
    allow(pat.x.advanceGEP);
    allow(pat.x.indexGEP);
    allow(dynamic_cast<Instruction *>(pat.kLoop.recurrence->getBackEdgeValue()));
    allow(kBody->getTerminator());

    // j.latch
    allow(pat.store);
    allow(pat.z.advanceGEP);
    allow(pat.z.indexGEP);
    allow(dynamic_cast<Instruction *>(pat.jLoop.recurrence->getBackEdgeValue()));
    allow(jLatch->getTerminator());

    std::vector<BasicBlock *> nestBlocks = {jHeader, kHeader, kBody, jLatch};
    if (pat.forwardBlock) {
        nestBlocks.push_back(pat.forwardBlock);
        allow(pat.forwardBlock->getTerminator());
    }

    for (auto * bb : nestBlocks) {
        for (auto * inst : bb->getInstructions()) {
            if (allowed.find(inst) == allowed.end()) {
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
                    return false;
                }
            }
        }
    }

    // ---- 外部输入必须在 j 循环外可用 ----
    const std::vector<Value *> externalInputs = {pat.sumInit,
                                                 pat.x.baseAtZero,
                                                 pat.yRowInit,
                                                 pat.z.baseAtZero,
                                                 pat.jLoop.boundValue,
                                                 pat.kLoop.boundValue,
                                                 pat.kLoop.initialValue};
    for (auto * value : externalInputs) {
        if (!value || !isInvariantOutside(value, jBodySet)) {
            return false;
        }
    }

    // ---- 别名/根对象合法性 ----
    pat.rootX = stripPointerRoot(pat.x.baseAtZero);
    pat.rootY = stripPointerRoot(pat.yRowInit);
    pat.rootZ = stripPointerRoot(pat.z.baseAtZero);
    if (!isKnownRoot(pat.rootX) || !isKnownRoot(pat.rootY) || !isKnownRoot(pat.rootZ)) {
        return false;
    }
    if (sameRoot(pat.rootX, pat.rootZ)) {
        // 原序读到的是被本轮 j 迭代逐步覆盖的 X，交换会改变语义
        return false;
    }

    pat.inPlace = sameRoot(pat.rootY, pat.rootZ);
    if (pat.inPlace) {
        // 原地情形要求 Y 行来源与 Z 基址都可证行对齐
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

/// @brief 按匹配结果生成 ikj 形态的新嵌套并接管 CFG
void rewritePattern(Function * func, Module * mod, MatMulPattern & pat)
{
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
    BasicBlock * kLatch = func->newBasicBlock();
    BasicBlock * j3Header = pat.inPlace ? func->newBasicBlock() : nullptr;
    BasicBlock * j3Body = pat.inPlace ? func->newBasicBlock() : nullptr;

    std::vector<BasicBlock *> newBlocks;
    if (guard) {
        newBlocks.push_back(guard);
    }
    newBlocks.insert(newBlocks.end(), {j1Header, j1Body, kHeader, kBody, j2Header, j2Body, kLatch});
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
    auto * kYRow = new PhiInst(func, pat.yRowPhi->getType());
    auto * kCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, kIV, ubk, cmpType);
    auto * kBr = new CondBranchInst(func, kCmp, kBody, kExitTarget);
    kIV->addIncoming(pat.kLoop.initialValue, j1Header);
    kXCur->addIncoming(xBase, j1Header);
    kYRow->addIncoming(pat.yRowInit, j1Header);
    kHeader->addInstruction(kIV);
    kHeader->addInstruction(kXCur);
    kHeader->addInstruction(kYRow);
    kHeader->addInstruction(kCmp);
    kHeader->addInstruction(kBr);
    kHeader->linkSuccessor(kBody);
    kHeader->linkSuccessor(kExitTarget);

    auto * xLoad = new LoadInst(func, kXCur, pat.elemType);
    auto * yElemBase = new GetElementPtrInst(func, kYRow, zero, pat.elemPtrType, true);
    auto * kBodyBr = new BranchInst(func, j2Header);
    kBody->addInstruction(xLoad);
    kBody->addInstruction(yElemBase);
    kBody->addInstruction(kBodyBr);
    kBody->linkSuccessor(j2Header);

    // ---- J2：acc[j] += x * Y[k][j]（单位步长热循环） ----
    auto * j2IV = new PhiInst(func, idxType);
    auto * j2Y = new PhiInst(func, pat.elemPtrType);
    auto * j2Acc = new PhiInst(func, pat.elemPtrType);
    auto * j2Cmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, j2IV, ubj, cmpType);
    auto * j2Br = new CondBranchInst(func, j2Cmp, j2Body, kLatch);
    j2IV->addIncoming(zero, kBody);
    j2Y->addIncoming(yElemBase, kBody);
    j2Acc->addIncoming(accBase, kBody);
    j2Header->addInstruction(j2IV);
    j2Header->addInstruction(j2Y);
    j2Header->addInstruction(j2Acc);
    j2Header->addInstruction(j2Cmp);
    j2Header->addInstruction(j2Br);
    j2Header->linkSuccessor(j2Body);
    j2Header->linkSuccessor(kLatch);

    auto * yLoad = new LoadInst(func, j2Y, pat.elemType);
    auto * accLoad = new LoadInst(func, j2Acc, pat.elemType);
    auto * mulInst = pat.mulXFirst ? new BinaryInst(func, mulOp, xLoad, yLoad, pat.elemType)
                                   : new BinaryInst(func, mulOp, yLoad, xLoad, pat.elemType);
    auto * addInst = pat.accSumFirst ? new BinaryInst(func, addOp, accLoad, mulInst, pat.elemType)
                                     : new BinaryInst(func, addOp, mulInst, accLoad, pat.elemType);
    auto * accStore = new StoreInst(func, addInst, j2Acc);
    auto * j2YNext = new GetElementPtrInst(func, j2Y, one, pat.elemPtrType, false);
    auto * j2AccNext = new GetElementPtrInst(func, j2Acc, one, pat.elemPtrType, false);
    auto * j2Next = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, j2IV, one, idxType);
    auto * j2BodyBr = new BranchInst(func, j2Header);
    j2Body->addInstruction(yLoad);
    j2Body->addInstruction(accLoad);
    j2Body->addInstruction(mulInst);
    j2Body->addInstruction(addInst);
    j2Body->addInstruction(accStore);
    j2Body->addInstruction(j2YNext);
    j2Body->addInstruction(j2AccNext);
    j2Body->addInstruction(j2Next);
    j2Body->addInstruction(j2BodyBr);
    j2Body->linkSuccessor(j2Header);
    j2IV->addIncoming(j2Next, j2Body);
    j2Y->addIncoming(j2YNext, j2Body);
    j2Acc->addIncoming(j2AccNext, j2Body);

    auto * kNext = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, kIV, one, kIV->getType());
    auto * kXNext = new GetElementPtrInst(func, kXCur, one, pat.elemPtrType, false);
    auto * kYNext = new GetElementPtrInst(func, kYRow, one, pat.yRowPhi->getType(), false);
    auto * kLatchBr = new BranchInst(func, kHeader);
    kLatch->addInstruction(kNext);
    kLatch->addInstruction(kXNext);
    kLatch->addInstruction(kYNext);
    kLatch->addInstruction(kLatchBr);
    kLatch->linkSuccessor(kHeader);
    kIV->addIncoming(kNext, kLatch);
    kXCur->addIncoming(kXNext, kLatch);
    kYRow->addIncoming(kYNext, kLatch);

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
    rewriteTerminatorTarget(jPre, oldJHeader, entryBlock);
    jPre->removeSuccessor(oldJHeader);
    jPre->addSuccessor(entryBlock);
    entryBlock->addPredecessor(jPre);
    oldJHeader->removePredecessor(jPre);

    if (pat.needGuard) {
        // 版本化：旧循环保留为回落路径，phi 入边改自 guard，并标记禁止再次版本化
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

    // 非版本化：旧 (j,k) 嵌套整体不可达，按 UnreachableBlockElim 的方式立即删除
    std::vector<BasicBlock *> deadBlocks = {oldJHeader, pat.kLoop.header, pat.kLoop.body, pat.jLoop.latch};
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

} // namespace

bool MatMulInterchange::run()
{
    if (!func || !mod || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
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

            MatMulPattern pat;
            if (!matchPattern(scev, loopInfo, header, pat)) {
                continue;
            }

            rewritePattern(func, mod, pat);
            CostModel::remark("matmul-interchange", true, pat.inPlace ? "in-place with row buffer" : "direct accumulate");
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
