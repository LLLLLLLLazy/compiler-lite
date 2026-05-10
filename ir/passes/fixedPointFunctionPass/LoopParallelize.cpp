///
/// @file LoopParallelize.cpp
/// @brief 保守的循环并行 pass 实现
///
/// 识别并改写满足安全条件的循环为多路并行执行：
/// - 规范循环（步长>=16，依赖安全）→ 2路clone线程并行
/// - 归约循环（add+mod归约，无副作用）→ 4路线程并行+结果合并
///

#include "LoopParallelize.h"

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
#include "FormalParam.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "GlobalVariable.h"
#include "ICmpInst.h"
#include "Instruction.h"
#include "IntegerType.h"
#include "LoadInst.h"
#include "LoopInfo.h"
#include "Module.h"
#include "PhiInst.h"
#include "StoreInst.h"
#include "Use.h"
#include "Value.h"
#include "VoidType.h"

namespace {

/// 规范循环的并行线程数（2路：主线程+1个子线程）
constexpr int32_t kParallelThreads = 2;
/// 归约循环的并行线程数（4路：主线程+3个子线程）
constexpr int32_t kReductionParallelThreads = 4;
/// 规范循环并行化的最小步长要求
constexpr int32_t kMinParallelStep = 16;
/// 循环并行化的最小迭代次数要求
constexpr int32_t kMinParallelTripCount = 128;

/// @brief 规范循环结构描述
/// 记录循环的各个组成部分：头、前驱头、循环体、锁存、出口块，
/// 以及归纳变量phi、步进指令、比较指令、条件分支、初始值、上界和步长
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
    ConstInteger * init = nullptr;
    ConstInteger * bound = nullptr;
    int32_t step = 0;
};

/// @brief 归约循环结构描述
/// 在规范循环基础上增加归约phi、归约加法、取模运算、归约项和模数等信息
struct ReductionLoop {
    BasicBlock * header = nullptr;
    BasicBlock * preheader = nullptr;
    BasicBlock * body = nullptr;
    BasicBlock * latch = nullptr;
    BasicBlock * exit = nullptr;
    PhiInst * induction = nullptr;
    PhiInst * reduction = nullptr;
    BinaryInst * next = nullptr;
    BinaryInst * reductionAdd = nullptr;
    BinaryInst * reductionMod = nullptr;
    ICmpInst * cmp = nullptr;
    CondBranchInst * branch = nullptr;
    Value * init = nullptr;
    Value * bound = nullptr;
    Value * reductionTerm = nullptr;
    ConstInteger * modulus = nullptr;
    bool inclusiveBound = false;
};

/// @brief 指针根的类型分类
enum class RootKind {
    Unknown,	// 未知类型，不可并行化
    Formal,		// 函数形参（可并行写）
    Global,		// 全局变量（可并行写）
    Alloca,		// 局部分配（不可并行写）
};

/// @brief 指针根信息，用于判断store/load是否指向同一内存对象
struct PointerRoot {
    RootKind kind = RootKind::Unknown;
    Value * value = nullptr;
};

/// @brief 将Value动态转换为ConstInteger
ConstInteger * asConstInt(Value * value)
{
    return dynamic_cast<ConstInteger *>(value);
}

/// @brief 判断指针根是否已知（非Unknown且有值）
bool isKnownRoot(const PointerRoot & root)
{
    return root.kind != RootKind::Unknown && root.value != nullptr;
}

/// @brief 判断两个指针根是否相同（类型和值都相同）
bool sameRoot(const PointerRoot & lhs, const PointerRoot & rhs)
{
    return lhs.kind == rhs.kind && lhs.value == rhs.value;
}

/// @brief 判断指针根是否可并行写入（形参或全局变量）
bool isWritableParallelRoot(const PointerRoot & root)
{
    return root.kind == RootKind::Formal || root.kind == RootKind::Global;
}

/// @brief 递归判断value是否由root派生（即value的操作数链中是否包含root）
/// @param value 待判断的值
/// @param root 根值
/// @param visiting 已访问集合，防止循环
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

/// @brief isDerivedFrom的无状态包装，内部创建visiting集合
bool isDerivedFrom(Value * value, Value * root)
{
    std::unordered_set<Value *> visiting;
    return isDerivedFrom(value, root, visiting);
}

/// @brief 剥离指针的根对象：沿GEP链回溯到基指针，再判断基指针是形参/全局/Alloca
/// @param value 待分析的指针值
/// @param visiting 已访问集合，防止循环
/// @return 指针根信息
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

/// @brief stripPointerRoot的无状态包装
PointerRoot stripPointerRoot(Value * value)
{
    std::unordered_set<Value *> visiting;
    return stripPointerRoot(value, visiting);
}

/// @brief 尝试将value匹配为"induction + 常数步长"的形式，提取步长值
/// @param value 待匹配的值
/// @param induction 归纳变量phi
/// @param step 输出步长值
/// @return 匹配成功返回true
bool matchAddConstStep(Value * value, PhiInst * induction, int32_t & step)
{
    auto * binary = dynamic_cast<BinaryInst *>(value);
    if (!binary || !induction || binary->getOp() != IRInstOperator::IRINST_OP_ADD_I) {
        return false;
    }

    if (binary->getLHS() == induction) {
        auto * rhs = asConstInt(binary->getRHS());
        if (rhs && rhs->getVal() > 0) {
            step = rhs->getVal();
            return true;
        }
    }

    if (binary->getRHS() == induction) {
        auto * lhs = asConstInt(binary->getLHS());
        if (lhs && lhs->getVal() > 0) {
            step = lhs->getVal();
            return true;
        }
    }

    return false;
}

/// @brief 判断value是否为"induction + expectedStep"的形式
bool isAddConstStep(Value * value, PhiInst * induction, int32_t expectedStep)
{
    int32_t step = 0;
    return matchAddConstStep(value, induction, step) && step == expectedStep;
}

/// @brief 判断value是否为GEP指令且基指针为phi、索引为常数expectedStep
bool isGepConstStep(Value * value, PhiInst * phi, int32_t expectedStep)
{
    auto * gep = dynamic_cast<GetElementPtrInst *>(value);
    if (!gep || gep->getBasePointer() != phi || gep->isArrayDecayGEP()) {
        return false;
    }

    auto * step = asConstInt(gep->getIndexOperand());
    return step && step->getVal() == expectedStep;
}

/// @brief 获取phi来自指定前驱块的入边值
Value * incomingValueFrom(PhiInst * phi, BasicBlock * block)
{
    if (!phi || !block) {
        return nullptr;
    }

    for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
        if (phi->getIncomingBlock(index) == block) {
            return phi->getIncomingValue(index);
        }
    }

    return nullptr;
}

/// @brief 判断value是否为int32类型的常量0
bool isZeroI32(Value * value)
{
    auto * constant = asConstInt(value);
    return constant && constant->getType()->isInt32Type() && constant->getVal() == 0;
}

/// @brief 判断bb是否仅有一条无条件分支跳转到target
bool hasSingleBranchTo(BasicBlock * bb, BasicBlock * target)
{
    auto * branch = bb ? dynamic_cast<BranchInst *>(bb->getTerminator()) : nullptr;
    return branch && branch->getTarget() == target && bb->getSuccessors().size() == 1;
}

/// @brief 判断pred是否为bb的前驱块
bool hasPred(BasicBlock * bb, BasicBlock * pred)
{
    if (!bb || !pred) {
        return false;
    }

    const auto & preds = bb->getPredecessors();
    return std::find(preds.begin(), preds.end(), pred) != preds.end();
}

/// @brief 匹配规范循环结构：归纳变量从0开始、常数步长递增、与常量上界比较
/// @param header 循环头基本块
/// @param loop 输出的规范循环描述
/// @return 匹配成功返回true
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
    ConstInteger * init = nullptr;
    int32_t step = 0;
    for (int32_t index = 0; index < induction->getIncomingCount(); ++index) {
        BasicBlock * incomingBlock = induction->getIncomingBlock(index);
        Value * incomingValue = induction->getIncomingValue(index);
        auto * initConst = asConstInt(incomingValue);
        if (initConst && initConst->getVal() == 0) {
            preheader = incomingBlock;
            init = initConst;
            continue;
        }

        int32_t incomingStep = 0;
        if (matchAddConstStep(incomingValue, induction, incomingStep)) {
            latch = incomingBlock;
            next = dynamic_cast<BinaryInst *>(incomingValue);
            step = incomingStep;
        }
    }

    if (!preheader || !latch || !next || step <= 0 || !next->getParentBlock() ||
        !hasSingleBranchTo(preheader, header) || !hasSingleBranchTo(latch, header)) {
        return false;
    }

    if (header->getPredecessors().size() != 2 || !hasPred(header, preheader) || !hasPred(header, latch)) {
        return false;
    }

    if (bound->getVal() - init->getVal() < kMinParallelTripCount) {
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
    loop.init = init;
    loop.bound = bound;
    loop.step = step;
    return loop.body != nullptr && loop.exit != nullptr;
}

/// @brief 匹配归约更新模式：phi在preheader入边为0，latch入边为(phi + term) % modulus
/// @param phi 待匹配的归约phi
/// @param preheader 循环前驱头
/// @param latch 循环锁存块
/// @param reductionAdd 输出归约加法指令
/// @param reductionMod 输出归约取模指令
/// @param reductionTerm 输出归约项（每次迭代累加的值）
/// @param modulus 输出模数常量
/// @return 匹配成功返回true
bool matchReductionUpdate(PhiInst * phi,
                          BasicBlock * preheader,
                          BasicBlock * latch,
                          BinaryInst *& reductionAdd,
                          BinaryInst *& reductionMod,
                          Value *& reductionTerm,
                          ConstInteger *& modulus)
{
    Value * initValue = incomingValueFrom(phi, preheader);
    Value * latchValue = incomingValueFrom(phi, latch);
    if (!isZeroI32(initValue)) {
        return false;
    }

    auto * modInst = dynamic_cast<BinaryInst *>(latchValue);
    if (!modInst || modInst->getParentBlock() != latch || modInst->getOp() != IRInstOperator::IRINST_OP_MOD_I) {
        return false;
    }

    auto * modConst = asConstInt(modInst->getRHS());
    if (!modConst || !modConst->getType()->isInt32Type() || modConst->getVal() <= 0) {
        return false;
    }

    auto * addInst = dynamic_cast<BinaryInst *>(modInst->getLHS());
    if (!addInst || addInst->getParentBlock() != latch || addInst->getOp() != IRInstOperator::IRINST_OP_ADD_I) {
        return false;
    }

    Value * term = nullptr;
    if (addInst->getLHS() == phi) {
        term = addInst->getRHS();
    } else if (addInst->getRHS() == phi) {
        term = addInst->getLHS();
    } else {
        return false;
    }

    reductionAdd = addInst;
    reductionMod = modInst;
    reductionTerm = term;
    modulus = modConst;
    return true;
}

/// @brief 匹配归约循环结构：包含归纳变量phi和归约phi（add+mod模式）
/// @param header 循环头基本块
/// @param loop 输出的归约循环描述
/// @return 匹配成功返回true
bool matchReductionLoop(BasicBlock * header, ReductionLoop & loop)
{
    if (!header) {
        return false;
    }

    auto * condBr = dynamic_cast<CondBranchInst *>(header->getTerminator());
    if (!condBr) {
        return false;
    }

    auto * cmp = dynamic_cast<ICmpInst *>(condBr->getCondition());
    if (!cmp || cmp->getParentBlock() != header ||
        (cmp->getOp() != IRInstOperator::IRINST_OP_LT_I && cmp->getOp() != IRInstOperator::IRINST_OP_LE_I)) {
        return false;
    }

    auto * induction = dynamic_cast<PhiInst *>(cmp->getLHS());
    Value * bound = cmp->getRHS();
    if (!induction || induction->getParentBlock() != header || !bound ||
        !induction->getType()->isInt32Type() || induction->getIncomingCount() != 2) {
        return false;
    }

    BasicBlock * preheader = nullptr;
    BasicBlock * latch = nullptr;
    BinaryInst * next = nullptr;
    Value * init = nullptr;
    for (int32_t index = 0; index < induction->getIncomingCount(); ++index) {
        BasicBlock * incomingBlock = induction->getIncomingBlock(index);
        Value * incomingValue = induction->getIncomingValue(index);

        int32_t incomingStep = 0;
        if (matchAddConstStep(incomingValue, induction, incomingStep) && incomingStep == 1) {
            latch = incomingBlock;
            next = dynamic_cast<BinaryInst *>(incomingValue);
            continue;
        }

        preheader = incomingBlock;
        init = incomingValue;
    }

    if (!preheader || !latch || !next || !init || !hasSingleBranchTo(preheader, header) ||
        !hasSingleBranchTo(latch, header)) {
        return false;
    }

    if (header->getPredecessors().size() != 2 || !hasPred(header, preheader) || !hasPred(header, latch)) {
        return false;
    }

    PhiInst * reduction = nullptr;
    BinaryInst * reductionAdd = nullptr;
    BinaryInst * reductionMod = nullptr;
    Value * reductionTerm = nullptr;
    ConstInteger * modulus = nullptr;

    for (auto * inst : header->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        if (phi == induction) {
            continue;
        }

        BinaryInst * candidateAdd = nullptr;
        BinaryInst * candidateMod = nullptr;
        Value * candidateTerm = nullptr;
        ConstInteger * candidateModulus = nullptr;
        if (!matchReductionUpdate(phi,
                                  preheader,
                                  latch,
                                  candidateAdd,
                                  candidateMod,
                                  candidateTerm,
                                  candidateModulus)) {
            return false;
        }

        if (reduction != nullptr) {
            return false;
        }
        reduction = phi;
        reductionAdd = candidateAdd;
        reductionMod = candidateMod;
        reductionTerm = candidateTerm;
        modulus = candidateModulus;
    }

    if (!reduction) {
        return false;
    }

    loop.header = header;
    loop.preheader = preheader;
    loop.body = condBr->getTrueDest();
    loop.latch = latch;
    loop.exit = condBr->getFalseDest();
    loop.induction = induction;
    loop.reduction = reduction;
    loop.next = next;
    loop.reductionAdd = reductionAdd;
    loop.reductionMod = reductionMod;
    loop.cmp = cmp;
    loop.branch = condBr;
    loop.init = init;
    loop.bound = bound;
    loop.reductionTerm = reductionTerm;
    loop.modulus = modulus;
    loop.inclusiveBound = cmp->getOp() == IRInstOperator::IRINST_OP_LE_I;
    return loop.body != nullptr && loop.exit != nullptr;
}

/// @brief 判断phi是否为可调整的header phi：归纳变量本身，或步长与循环一致的GEP/Add phi
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

    return initValue && latchValue &&
           (isGepConstStep(latchValue, phi, loop.step) || isAddConstStep(latchValue, phi, loop.step));
}

/// @brief 判断循环头中所有phi是否都可调整（步长与循环一致）
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

/// @brief 递归判断value是否依赖循环归纳变量
/// @param value 待判断的值
/// @param loop 规范循环描述
/// @param visiting 已访问集合
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

/// @brief valueDependsOnLoopIndex的无状态包装
bool valueDependsOnLoopIndex(Value * value, const CanonicalLoop & loop)
{
    std::unordered_set<Value *> visiting;
    return valueDependsOnLoopIndex(value, loop, visiting);
}

/// @brief 判断循环是否仅有唯一的出口边（从expectedExitPred到expectedExit）
bool hasSingleLoopExitEdge(const std::unordered_set<BasicBlock *> & loopBody,
                           BasicBlock * expectedExit,
                           BasicBlock * expectedExitPred)
{
    if (loopBody.empty() || !expectedExit || !expectedExitPred) {
        return false;
    }

    int32_t outsideEdges = 0;
    for (auto * bb : loopBody) {
        for (auto * succ : bb->getSuccessors()) {
            if (loopBody.find(succ) != loopBody.end()) {
                continue;
            }
            if (succ != expectedExit || bb != expectedExitPred) {
                return false;
            }
            ++outsideEdges;
        }
    }

    return outsideEdges == 1;
}

/// @brief 判断循环的依赖是否安全可并行化
/// 条件：无调用指令、store目标依赖循环变量、store根为可并行写根、
///       不同store指向同一根、load不与store指向同一根
bool isDependenceSafe(const CanonicalLoop & loop, const std::unordered_set<BasicBlock *> & loopBody)
{
    std::vector<PointerRoot> storeRoots;
    for (auto * bb : loopBody) {
        for (auto * inst : bb->getInstructions()) {
            if (dynamic_cast<CallInst *>(inst)) {
                return false;
            }

            auto * store = dynamic_cast<StoreInst *>(inst);
            if (!store) {
                continue;
            }

            if (!valueDependsOnLoopIndex(store->getPointerOperand(), loop)) {
                return false;
            }

            PointerRoot root = stripPointerRoot(store->getPointerOperand());
            if (!isWritableParallelRoot(root)) {
                return false;
            }

            bool alreadySeen = false;
            for (const auto & seen : storeRoots) {
                if (sameRoot(seen, root)) {
                    alreadySeen = true;
                    break;
                }
            }
            if (!alreadySeen) {
                storeRoots.push_back(root);
            }
        }
    }

    if (storeRoots.empty()) {
        return false;
    }

    for (auto * bb : loopBody) {
        for (auto * inst : bb->getInstructions()) {
            auto * load = dynamic_cast<LoadInst *>(inst);
            if (!load) {
                continue;
            }

            PointerRoot loadRoot = stripPointerRoot(load->getPointerOperand());
            if (!isKnownRoot(loadRoot)) {
                return false;
            }

            for (const auto & storeRoot : storeRoots) {
                if (sameRoot(loadRoot, storeRoot)) {
                    return false;
                }
            }
        }
    }

    return true;
}

/// @brief 判断归约循环体是否含有不支持的副作用（CallInst或StoreInst）
bool reductionLoopHasUnsupportedSideEffects(const std::unordered_set<BasicBlock *> & loopBody)
{
    for (auto * bb : loopBody) {
        for (auto * inst : bb->getInstructions()) {
            if (dynamic_cast<CallInst *>(inst) || dynamic_cast<StoreInst *>(inst)) {
                return true;
            }
        }
    }

    return false;
}

/// @brief 判断value是否定义在循环体内部
bool isDefinedInsideLoop(Value * value, const std::unordered_set<BasicBlock *> & loopBody)
{
    auto * inst = dynamic_cast<Instruction *>(value);
    return inst && loopBody.find(inst->getParentBlock()) != loopBody.end();
}

/// @brief 判断循环内的值是否可以在前驱头块中物化（即提前计算）
/// 仅支持LoadInst且其指针操作数不在循环内定义的情况
bool canMaterializePreheaderValue(Value * value, const std::unordered_set<BasicBlock *> & loopBody)
{
    auto * inst = dynamic_cast<Instruction *>(value);
    if (!inst || loopBody.find(inst->getParentBlock()) == loopBody.end()) {
        return true;
    }

    if (auto * load = dynamic_cast<LoadInst *>(inst)) {
        return !isDefinedInsideLoop(load->getPointerOperand(), loopBody);
    }

    return false;
}

/// @brief 将基本块bb移动到before之前的位置
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

/// @brief 在基本块的终止指令之前插入指令
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

/// @brief 在前驱头块中物化循环内的值（目前仅支持LoadInst的克隆）
/// @param func 所属函数
/// @param value 待物化的值
/// @param preheader 前驱头块，物化指令插入到此块的终止指令前
/// @param loopBody 循环体基本块集合
/// @return 物化后的新值，若无法物化则返回nullptr
Value * materializePreheaderValue(Function * func,
                                  Value * value,
                                  BasicBlock * preheader,
                                  const std::unordered_set<BasicBlock *> & loopBody)
{
    auto * inst = dynamic_cast<Instruction *>(value);
    if (!inst || loopBody.find(inst->getParentBlock()) == loopBody.end()) {
        return value;
    }

    if (auto * load = dynamic_cast<LoadInst *>(inst)) {
        Value * pointer = load->getPointerOperand();
        if (isDefinedInsideLoop(pointer, loopBody)) {
            return nullptr;
        }

        auto * clonedLoad = new LoadInst(func, pointer, load->getType());
        insertBeforeTerminator(preheader, clonedLoad);
        return clonedLoad;
    }

    return nullptr;
}

/// @brief 更新phi的指定前驱块的入边值为newValue
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

/// @brief 将基本块bb中所有phi的入边块oldPred替换为newPred
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

/// @brief 为并行化创建phi的初始值：根据latch值的模式（GEP或Add）生成init+chunkStart
Instruction * createParallelInitialValue(Function * func,
                                         const CanonicalLoop & loop,
                                         PhiInst * phi,
                                         Value * chunkStart)
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

    if (isGepConstStep(latchValue, phi, loop.step)) {
        return new GetElementPtrInst(func, initValue, chunkStart, phi->getType(), false);
    }

    if (isAddConstStep(latchValue, phi, loop.step)) {
        return new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, initValue, chunkStart, phi->getType());
    }

    return nullptr;
}

/// @brief 将循环头中所有phi的初始值（来自preheader的入边）重定向到chunkStart对应的并行起始值
bool retargetHeaderPhiInitialValues(Function * func,
                                    const CanonicalLoop & loop,
                                    Value * chunkStart,
                                    BasicBlock * insertBlock)
{
    for (auto * inst : loop.header->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }

        if (phi == loop.induction) {
            if (!updatePhiIncomingValue(phi, loop.preheader, chunkStart)) {
                return false;
            }
            continue;
        }

        Instruction * initInst = createParallelInitialValue(func, loop, phi, chunkStart);
        if (!initInst) {
            return false;
        }

        insertBeforeTerminator(insertBlock, initInst);
        if (!updatePhiIncomingValue(phi, loop.preheader, initInst)) {
            return false;
        }
    }

    return true;
}

/// @brief 获取或创建2路并行启动函数__mtstart
/// @param mod 所属模块
/// @return __mtstart函数指针
Function * getOrCreateMtStart(Module * mod)
{
    if (auto * existing = mod->findFunction("__mtstart")) {
        return existing;
    }

    return mod->newFunction("__mtstart", IntegerType::getTypeInt32(), {}, true);
}

/// @brief 获取或创建4路并行启动函数__mtstart4
/// @param mod 所属模块
/// @return __mtstart4函数指针
Function * getOrCreateMtStart4(Module * mod)
{
    if (auto * existing = mod->findFunction("__mtstart4")) {
        return existing;
    }

    return mod->newFunction("__mtstart4", IntegerType::getTypeInt32(), {}, true);
}

/// @brief 获取或创建4路并行线程数查询函数__mtthreadcount4
/// @param mod 所属模块
/// @return __mtthreadcount4函数指针
Function * getOrCreateMtThreadCount4(Module * mod)
{
    if (auto * existing = mod->findFunction("__mtthreadcount4")) {
        return existing;
    }

    return mod->newFunction("__mtthreadcount4", IntegerType::getTypeInt32(), {}, true);
}

/// @brief 获取或创建线程安全存储函数__mtstorei32
/// @param mod 所属模块
/// @return __mtstorei32函数指针
Function * getOrCreateMtStoreI32(Module * mod)
{
    if (auto * existing = mod->findFunction("__mtstorei32")) {
        return existing;
    }

    return mod->newFunction("__mtstorei32",
                            VoidType::getType(),
                            {new FormalParam{IntegerType::getTypeInt32(), ""},
                             new FormalParam{IntegerType::getTypeInt32(), ""}},
                            true);
}

/// @brief 获取或创建线程安全加载函数__mtloadi32
/// @param mod 所属模块
/// @return __mtloadi32函数指针
Function * getOrCreateMtLoadI32(Module * mod)
{
    if (auto * existing = mod->findFunction("__mtloadi32")) {
        return existing;
    }

    return mod->newFunction("__mtloadi32",
                            IntegerType::getTypeInt32(),
                            {new FormalParam{IntegerType::getTypeInt32(), ""}},
                            true);
}

/// @brief 获取或创建4路并行结束函数__mtend4
/// @param mod 所属模块
/// @return __mtend4函数指针
Function * getOrCreateMtEnd4(Module * mod)
{
    if (auto * existing = mod->findFunction("__mtend4")) {
        return existing;
    }

    return mod->newFunction("__mtend4",
                            VoidType::getType(),
                            {new FormalParam{IntegerType::getTypeInt32(), ""}},
                            true);
}

/// @brief 获取或创建2路并行结束函数__mtend
/// @param mod 所属模块
/// @return __mtend函数指针
Function * getOrCreateMtEnd(Module * mod)
{
    if (auto * existing = mod->findFunction("__mtend")) {
        return existing;
    }

    return mod->newFunction("__mtend",
                            VoidType::getType(),
                            {new FormalParam{IntegerType::getTypeInt32(), ""}},
                            true);
}

/// @brief 将循环内定义的值在循环外的使用替换为新值
/// @param oldValue 原始值
/// @param newValue 替换后的新值
/// @param loopBody 循环体基本块集合，循环内的使用不会被替换
void replaceUsesOutsideLoop(Value * oldValue,
                            Value * newValue,
                            const std::unordered_set<BasicBlock *> & loopBody)
{
    if (!oldValue || !newValue) {
        return;
    }

    std::vector<Use *> uses(oldValue->getUseList().begin(), oldValue->getUseList().end());
    for (auto * use : uses) {
        auto * userInst = dynamic_cast<Instruction *>(use->getUser());
        if (!userInst || loopBody.find(userInst->getParentBlock()) != loopBody.end()) {
            continue;
        }

        for (int32_t operand = 0; operand < userInst->getOperandsNum(); ++operand) {
            if (userInst->getOperand(operand) == oldValue) {
                userInst->setOperand(operand, newValue);
            }
        }
    }
}

/// @brief 将原始边界值对齐到步长的整数倍，确保并行分块的边界与循环步长对齐
/// @param func 所属函数
/// @param mod 所属模块
/// @param insertBlock 插入指令的基本块
/// @param rawBoundary 原始未对齐的边界值
/// @param loop 规范循环描述
/// @return 对齐后的边界值
Value * createAlignedChunkBoundary(Function * func,
                                   Module * mod,
                                   BasicBlock * insertBlock,
                                   Value * rawBoundary,
                                   const CanonicalLoop & loop)
{
    if (loop.step == 1) {
        return rawBoundary;
    }

    auto * step = mod->newConstInt32(loop.step);
    auto * stepMinusOne = mod->newConstInt32(loop.step - 1);
    auto * biased = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, rawBoundary, stepMinusOne, rawBoundary->getType());
    auto * divided = new BinaryInst(func, IRInstOperator::IRINST_OP_DIV_I, biased, step, rawBoundary->getType());
    auto * aligned = new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I, divided, step, rawBoundary->getType());
    insertBeforeTerminator(insertBlock, biased);
    insertBeforeTerminator(insertBlock, divided);
    insertBeforeTerminator(insertBlock, aligned);
    return aligned;
}

} // namespace

LoopParallelize::LoopParallelize(Function * _func, Module * _mod) : func(_func), mod(_mod)
{}

/// @brief 执行循环并行化优化
/// 遍历函数中所有循环头，按循环深度从浅到深依次尝试并行化，
/// 每次成功并行化后重新构建支配树和循环信息，直到无法继续并行化为止
/// @return 若修改了IR则返回true
bool LoopParallelize::run()
{
    if (!func || !mod || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    bool changed = false;
    std::unordered_set<BasicBlock *> transformedHeaders;
    while (true) {
        bool localChanged = false;
        DominatorTree domTree(func);
        LoopInfo loopInfo(func, &domTree);

        std::vector<BasicBlock *> headers;
        for (auto * bb : func->getBlocks()) {
            if (loopInfo.isLoopHeader(bb) && transformedHeaders.find(bb) == transformedHeaders.end()) {
                headers.push_back(bb);
            }
        }

        std::stable_sort(headers.begin(),
                         headers.end(),
                         [&loopInfo](BasicBlock * lhs, BasicBlock * rhs) {
                             return loopInfo.getLoopDepth(lhs) < loopInfo.getLoopDepth(rhs);
                         });

        for (auto * header : headers) {
            if (tryParallelizeHeader(header, loopInfo)) {
                transformedHeaders.insert(header);
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

/// @brief 尝试对规范循环头进行2路并行化
/// 先尝试归约循环并行化，若不满足则尝试规范循环并行化：
/// 将循环迭代范围均分为2份，通过__mtstart获取线程id，
/// 各线程执行各自的迭代分块，最后调用__mtend同步
/// @param header 循环头基本块
/// @param loopInfo 循环信息
/// @return 若成功并行化则返回true
bool LoopParallelize::tryParallelizeHeader(BasicBlock * header, LoopInfo & loopInfo)
{
    if (tryParallelizeReductionHeader(header, loopInfo)) {
        return true;
    }

    if (loopInfo.getLoopDepth(header) != 1) {
        return false;
    }

    CanonicalLoop loop;
    if (!matchCanonicalLoop(header, loop) || loop.step < kMinParallelStep || !headerPhisAreAdjustable(loop)) {
        return false;
    }

    const auto * body = loopInfo.getLoopBody(loop.header);
    if (!body || !hasSingleLoopExitEdge(*body, loop.exit, loop.header) || !isDependenceSafe(loop, *body)) {
        return false;
    }

    Function * mtStart = getOrCreateMtStart(mod);
    Function * mtEnd = getOrCreateMtEnd(mod);
    if (!mtStart || !mtEnd) {
        return false;
    }

    auto * two = mod->newConstInt32(kParallelThreads);
    auto * one = mod->newConstInt32(1);
    auto * total = loop.bound;

    auto * tid = new CallInst(func, mtStart, {}, IntegerType::getTypeInt32());
    insertBeforeTerminator(loop.preheader, tid);

    auto * tidPlusOne = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, tid, one, tid->getType());
    auto * startProduct = new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I, total, tid, tid->getType());
    auto * rawStart = new BinaryInst(func, IRInstOperator::IRINST_OP_DIV_I, startProduct, two, tid->getType());
    auto * endProduct = new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I, total, tidPlusOne, tid->getType());
    auto * rawEnd = new BinaryInst(func, IRInstOperator::IRINST_OP_DIV_I, endProduct, two, tid->getType());
    insertBeforeTerminator(loop.preheader, tidPlusOne);
    insertBeforeTerminator(loop.preheader, startProduct);
    insertBeforeTerminator(loop.preheader, rawStart);
    insertBeforeTerminator(loop.preheader, endProduct);
    insertBeforeTerminator(loop.preheader, rawEnd);

    Value * chunkStart = createAlignedChunkBoundary(func, mod, loop.preheader, rawStart, loop);
    Value * chunkEnd = createAlignedChunkBoundary(func, mod, loop.preheader, rawEnd, loop);

    if (!retargetHeaderPhiInitialValues(func, loop, chunkStart, loop.preheader)) {
        return false;
    }
    loop.cmp->setOperand(1, chunkEnd);

    auto * mtEndBlock = func->newBasicBlock();
    insertBlockBefore(func, mtEndBlock, loop.exit);
    auto * endCall = new CallInst(func, mtEnd, {tid}, VoidType::getType());
    mtEndBlock->addInstruction(endCall);
    mtEndBlock->addInstruction(new BranchInst(func, loop.exit));
    mtEndBlock->linkSuccessor(loop.exit);

    loop.branch->setFalseDest(mtEndBlock);
    loop.header->removeSuccessor(loop.exit);
    loop.header->addSuccessor(mtEndBlock);
    loop.exit->removePredecessor(loop.header);
    mtEndBlock->addPredecessor(loop.header);
    rewritePhiIncomingBlock(loop.exit, loop.header, mtEndBlock);

    return true;
}

/// @brief 尝试对归约循环头进行4路并行化
/// 将循环迭代范围均分为4份，通过__mtstart4获取线程id，
/// 各线程独立计算部分归约结果，循环结束后通过__mtstorei32存储部分结果，
/// 调用__mtend4同步，最后在合并块中通过__mtloadi32加载并合并所有线程的归约值
/// @param header 循环头基本块
/// @param loopInfo 循环信息
/// @return 若成功并行化则返回true
bool LoopParallelize::tryParallelizeReductionHeader(BasicBlock * header, LoopInfo & loopInfo)
{
    if (loopInfo.getLoopDepth(header) != 1) {
        return false;
    }

    ReductionLoop loop;
    if (!matchReductionLoop(header, loop)) {
        return false;
    }

    const auto * body = loopInfo.getLoopBody(loop.header);
    if (!body || !hasSingleLoopExitEdge(*body, loop.exit, loop.header) ||
        loop.exit->getPredecessors().size() != 1 || !hasPred(loop.exit, loop.header) ||
        reductionLoopHasUnsupportedSideEffects(*body) ||
        !canMaterializePreheaderValue(loop.bound, *body)) {
        return false;
    }

    Function * mtStart4 = getOrCreateMtStart4(mod);
    Function * mtThreadCount4 = getOrCreateMtThreadCount4(mod);
    Function * mtStoreI32 = getOrCreateMtStoreI32(mod);
    Function * mtLoadI32 = getOrCreateMtLoadI32(mod);
    Function * mtEnd4 = getOrCreateMtEnd4(mod);
    if (!mtStart4 || !mtThreadCount4 || !mtStoreI32 || !mtLoadI32 || !mtEnd4) {
        return false;
    }

    auto * int32Type = IntegerType::getTypeInt32();
    auto * int1Type = IntegerType::getTypeInt1();
    auto * zero = mod->newConstInt32(0);
    auto * one = mod->newConstInt32(1);

    auto * tid = new CallInst(func, mtStart4, {}, int32Type);
    auto * threadCount = new CallInst(func, mtThreadCount4, {}, int32Type);
    insertBeforeTerminator(loop.preheader, tid);
    insertBeforeTerminator(loop.preheader, threadCount);

    Value * preheaderBound = materializePreheaderValue(func, loop.bound, loop.preheader, *body);
    if (!preheaderBound) {
        return false;
    }

    auto * tripBase = new BinaryInst(func, IRInstOperator::IRINST_OP_SUB_I, preheaderBound, loop.init, int32Type);
    insertBeforeTerminator(loop.preheader, tripBase);
    Value * totalTripCount = tripBase;
    if (loop.inclusiveBound) {
        auto * inclusiveTripCount = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, tripBase, one, int32Type);
        insertBeforeTerminator(loop.preheader, inclusiveTripCount);
        totalTripCount = inclusiveTripCount;
    }

    auto * tidPlusOne = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, tid, one, int32Type);
    auto * startProduct = new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I, totalTripCount, tid, int32Type);
    auto * startOffset = new BinaryInst(func, IRInstOperator::IRINST_OP_DIV_I, startProduct, threadCount, int32Type);
    auto * endProduct = new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I, totalTripCount, tidPlusOne, int32Type);
    auto * endOffset = new BinaryInst(func, IRInstOperator::IRINST_OP_DIV_I, endProduct, threadCount, int32Type);
    auto * chunkStart = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, loop.init, startOffset, int32Type);
    auto * chunkEnd = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, loop.init, endOffset, int32Type);
    insertBeforeTerminator(loop.preheader, tidPlusOne);
    insertBeforeTerminator(loop.preheader, startProduct);
    insertBeforeTerminator(loop.preheader, startOffset);
    insertBeforeTerminator(loop.preheader, endProduct);
    insertBeforeTerminator(loop.preheader, endOffset);
    insertBeforeTerminator(loop.preheader, chunkStart);
    insertBeforeTerminator(loop.preheader, chunkEnd);

    if (!updatePhiIncomingValue(loop.induction, loop.preheader, chunkStart) ||
        !updatePhiIncomingValue(loop.reduction, loop.preheader, zero)) {
        return false;
    }

    auto * chunkCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, loop.induction, chunkEnd, int1Type);
    insertBeforeTerminator(loop.header, chunkCmp);
    loop.branch->setOperand(0, chunkCmp);

    auto * combineBlock = func->newBasicBlock();
    insertBlockBefore(func, combineBlock, loop.exit);

    Value * combined = zero;
    for (int32_t tidValue = 0; tidValue < kReductionParallelThreads; ++tidValue) {
        auto * part = new CallInst(func, mtLoadI32, {mod->newConstInt32(tidValue)}, int32Type);
        auto * sum = new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, combined, part, int32Type);
        auto * reduced = new BinaryInst(func, IRInstOperator::IRINST_OP_MOD_I, sum, loop.modulus, int32Type);
        combineBlock->addInstruction(part);
        combineBlock->addInstruction(sum);
        combineBlock->addInstruction(reduced);
        combined = reduced;
    }

    replaceUsesOutsideLoop(loop.reduction, combined, *body);

    combineBlock->addInstruction(new BranchInst(func, loop.exit));
    combineBlock->linkSuccessor(loop.exit);

    auto * mtEndBlock = func->newBasicBlock();
    insertBlockBefore(func, mtEndBlock, combineBlock);
    mtEndBlock->addInstruction(new CallInst(func, mtStoreI32, {tid, loop.reduction}, VoidType::getType()));
    mtEndBlock->addInstruction(new CallInst(func, mtEnd4, {tid}, VoidType::getType()));
    mtEndBlock->addInstruction(new BranchInst(func, combineBlock));
    mtEndBlock->linkSuccessor(combineBlock);

    loop.branch->setFalseDest(mtEndBlock);
    loop.header->removeSuccessor(loop.exit);
    loop.header->addSuccessor(mtEndBlock);
    loop.exit->removePredecessor(loop.header);
    mtEndBlock->addPredecessor(loop.header);
    rewritePhiIncomingBlock(loop.exit, loop.header, combineBlock);

    return true;
}
