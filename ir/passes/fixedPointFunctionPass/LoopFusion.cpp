///
/// @file LoopFusion.cpp
/// @brief 相邻计数循环融合 pass 实现
///

#include "LoopFusion.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>
#include <vector>

#include "AllocaInst.h"
#include "AnalysisCache.h"
#include "BasicBlock.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "GlobalVariable.h"
#include "CondBranchInst.h"
#include "DominatorTree.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "LoopInfo.h"
#include "Module.h"
#include "PhiInst.h"
#include "ScalarEvolution.h"
#include "StoreInst.h"
#include "Value.h"

namespace {

/// @brief 沿 GEP 链回溯得到指针的底层对象（全局/局部/形参）
/// @param ptr 起始指针值
/// @return 不再是 GEP 的底层对象；若不可解析则返回该指针本身
Value * getUnderlyingObject(Value * ptr)
{
    while (auto * gep = dynamic_cast<GetElementPtrInst *>(ptr)) {
        ptr = gep->getBasePointer();
    }
    return ptr;
}

/// @brief 判断底层对象是否为可精确区分别名的独立内存对象（全局或栈上局部数组）
///
/// 两个不同的 GlobalVariable、两个不同的 AllocaInst、或一个全局与一个 alloca 之间
/// 在 SysY 语义下互不别名；据此「不同底层对象即不冲突」的独立性结论才成立。
/// 形参指针可能指向全局，无法据基址区分别名，故不属此类。
bool isDistinguishableObject(Value * base)
{
    return dynamic_cast<GlobalVariable *>(base) != nullptr || dynamic_cast<AllocaInst *>(base) != nullptr;
}

/// @brief 取访问指针最外层（最贴近底层对象）GEP 的下标
/// @param ptr 访问指针
/// @return 最外层 GEP 的下标值；若指针不是 GEP 则返回 nullptr
Value * getOutermostGepIndex(Value * ptr)
{
    auto * gep = dynamic_cast<GetElementPtrInst *>(ptr);
    if (!gep) {
        return nullptr;
    }
    while (auto * base = dynamic_cast<GetElementPtrInst *>(gep->getBasePointer())) {
        gep = base;
    }
    return gep->getIndexOperand();
}

/// @brief 一次内存访问的抽象描述
struct MemAccess {
    Value * base = nullptr;    ///< 底层对象
    Value * pointer = nullptr; ///< 原始访问指针
    bool isStore = false;      ///< 是否为写
};

/// @brief 收集循环体（loopBody 中除 header 外的全部块）内的内存访问与调用情况
/// @param loopBody 循环自然体块集合
/// @param header 循环头（其内的 phi/比较/分支不计入体）
/// @param accesses 输出：内存访问列表
/// @return 若循环体内含有带副作用的调用（无法安全重排）则返回 false
bool collectBodyAccesses(const std::unordered_set<BasicBlock *> & loopBody,
                         BasicBlock * header,
                         std::vector<MemAccess> & accesses)
{
    for (auto * bb : loopBody) {
        if (bb == header) {
            continue;
        }
        for (auto * inst : bb->getInstructions()) {
            if (dynamic_cast<CallInst *>(inst) != nullptr) {
                // 循环体内的调用可能带 I/O 或未知内存副作用，融合会改变其相对次序，保守放弃
                return false;
            }
            Value * ptr = nullptr;
            bool isStore = false;
            if (auto * load = dynamic_cast<LoadInst *>(inst)) {
                ptr = load->getPointerOperand();
            } else if (auto * store = dynamic_cast<StoreInst *>(inst)) {
                ptr = store->getPointerOperand();
                isStore = true;
            } else {
                continue;
            }
            Value * base = getUnderlyingObject(ptr);
            // 底层对象若非全局/栈局部，则无法据基址精确区分别名（如形参指针可能指向全局），保守放弃
            if (!isDistinguishableObject(base)) {
                return false;
            }
            accesses.push_back({base, ptr, isStore});
        }
    }
    return true;
}

/// @brief 保守判定：两循环体在同一 i 上逐迭代交错是否合法
///
/// 合法当且仅当：对于「被一方写、且被另一方访问」的每一个共享底层对象，
/// 双方对它的每一次访问其最外层下标都恰为各自的归纳变量 i。此时循环 1 的第 i 次
/// 与循环 2 的第 i 次访问同一行，跨迭代依赖距离为 0，融合后同 i 内保持原序，安全。
/// 未被共享的对象天然独立；无法证明「最外层下标 == i」的共享对象一律拒绝。
///
/// @param acc1 循环 1 体内访问
/// @param acc2 循环 2 体内访问
/// @param ind1 循环 1 归纳变量
/// @param ind2 循环 2 归纳变量
/// @return 可安全融合返回 true
bool isFusionLegal(const std::vector<MemAccess> & acc1,
                   const std::vector<MemAccess> & acc2,
                   Value * ind1,
                   Value * ind2)
{
    // 统计各对象被读/写情况
    std::unordered_set<Value *> written1;
    std::unordered_set<Value *> accessed1;
    std::unordered_set<Value *> written2;
    std::unordered_set<Value *> accessed2;
    for (const auto & a : acc1) {
        accessed1.insert(a.base);
        if (a.isStore) {
            written1.insert(a.base);
        }
    }
    for (const auto & a : acc2) {
        accessed2.insert(a.base);
        if (a.isStore) {
            written2.insert(a.base);
        }
    }

    // 收集需要「同下标」证明的共享对象：被一方写且被另一方访问
    std::unordered_set<Value *> sharedConflict;
    for (Value * b : written1) {
        if (accessed2.count(b)) {
            sharedConflict.insert(b);
        }
    }
    for (Value * b : written2) {
        if (accessed1.count(b)) {
            sharedConflict.insert(b);
        }
    }
    if (sharedConflict.empty()) {
        return true; // 两循环体内存完全独立
    }

    // 对每一个共享冲突对象，要求双方所有访问都以自身归纳变量为最外层下标
    auto allIndexedByInduction = [](const std::vector<MemAccess> & acc,
                                    const std::unordered_set<Value *> & shared,
                                    Value * ind) {
        for (const auto & a : acc) {
            if (!shared.count(a.base)) {
                continue;
            }
            if (getOutermostGepIndex(a.pointer) != ind) {
                return false;
            }
        }
        return true;
    };
    if (!allIndexedByInduction(acc1, sharedConflict, ind1)) {
        return false;
    }
    if (!allIndexedByInduction(acc2, sharedConflict, ind2)) {
        return false;
    }
    return true;
}

/// @brief 验证循环仅能由头部计数条件跳向规范出口
/// @param loopBody 循环自然体块集合
/// @param header 循环头
/// @param exit 规范出口块
/// @return 不存在 break 等额外出环边时返回 true
bool hasOnlyCanonicalExit(const std::unordered_set<BasicBlock *> & loopBody,
	                      BasicBlock * header,
	                      BasicBlock * exit)
{
	for (auto * bb : loopBody) {
		for (auto * succ : bb->getSuccessors()) {
			if (loopBody.find(succ) == loopBody.end() && (bb != header || succ != exit)) {
				return false;
			}
		}
	}
	return true;
}

/// @brief 取归纳 phi 沿回边（来自 latch）的自增值
Value * getBackedgeValue(PhiInst * induction, BasicBlock * latch)
{
    for (int32_t i = 0; i < induction->getIncomingCount(); ++i) {
        if (induction->getIncomingBlock(i) == latch) {
            return induction->getIncomingValue(i);
        }
    }
    return nullptr;
}

/// @brief 将 pred 中所有 phi 的 incoming 块 oldBlock 改写为 newBlock
void remapPhiIncoming(BasicBlock * bb, BasicBlock * oldBlock, BasicBlock * newBlock)
{
    for (auto * inst : bb->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
            if (phi->getIncomingBlock(i) == oldBlock) {
                phi->replaceIncomingBlock(oldBlock, newBlock);
            }
        }
    }
}

/// @brief 判断两循环是否具有相同的迭代次数（同起点/步长/上界/比较）
bool sameTripCount(const ScalarEvolution::CanonicalLoop & a, const ScalarEvolution::CanonicalLoop & b)
{
    if (a.compareKind != b.compareKind) {
        return false;
    }
    if (a.boundValue != b.boundValue) {
        return false;
    }
    if (a.recurrence->getStep() != b.recurrence->getStep()) {
        return false;
    }
    if (a.hasConstInitialValue && b.hasConstInitialValue) {
        return a.initialIntValue == b.initialIntValue;
    }
    return a.initialValue == b.initialValue;
}

/// @brief 统计基本块中的 phi 指令条数
int countPhis(BasicBlock * bb)
{
    int n = 0;
    for (auto * inst : bb->getInstructions()) {
        if (dynamic_cast<PhiInst *>(inst)) {
            ++n;
        } else {
            break;
        }
    }
    return n;
}

/// @brief 尝试把相邻的 l2 融合进 l1（要求 l1.exit == l2.preheader）
/// @param func 所属函数
/// @param l1 前一个循环
/// @param l2 后一个循环
/// @return 成功融合返回 true
bool tryFuseLoops(Function * func,
                  const ScalarEvolution::CanonicalLoop & l1,
                  const ScalarEvolution::CanonicalLoop & l2)
{
    BasicBlock * H1 = l1.header;
    BasicBlock * L1 = l1.latch;
    BasicBlock * Ex1 = l1.exit;
    PhiInst * ind1 = l1.induction;

    BasicBlock * H2 = l2.header;
    BasicBlock * BE2 = l2.body;
    BasicBlock * L2 = l2.latch;
    BasicBlock * Ex2 = l2.exit;
    PhiInst * ind2 = l2.induction;
    BasicBlock * P2 = l2.preheader;

    // ---- 相邻性：l1 的专属出口即 l2 的前置头，且该块仅含一条到 l2 头的无条件跳转 ----
    if (!Ex1 || Ex1 != P2) {
        return false;
    }
    if (Ex1->getPredecessors().size() != 1 || Ex1->getPredecessors().front() != H1) {
        return false;
    }
    if (Ex1->getInstructions().size() != 1) {
        return false;
    }
    auto * p2Branch = dynamic_cast<BranchInst *>(Ex1->getTerminator());
    if (!p2Branch || p2Branch->getTarget() != H2) {
        return false;
    }

    // ---- 结构：两循环头各自只有一个归纳 phi ----
    if (countPhis(H1) != 1 || countPhis(H2) != 1) {
        return false;
    }

    // ---- 迭代次数一致 ----
    if (!sameTripCount(l1, l2)) {
        return false;
    }

    // ---- 出口互异且不与关键块重叠 ----
    if (Ex2 == H1 || Ex2 == Ex1 || Ex2 == BE2) {
        return false;
    }

    // ---- 合法性：逐迭代内存依赖判定 ----
    DominatorTree localDom(func);
    LoopInfo localLoopInfo(func, &localDom);
    const auto * body1 = localLoopInfo.getLoopBody(H1);
    const auto * body2 = localLoopInfo.getLoopBody(H2);
    if (!body1 || !body2) {
        return false;
    }
	if (!hasOnlyCanonicalExit(*body1, H1, Ex1) || !hasOnlyCanonicalExit(*body2, H2, Ex2)) {
		return false;
	}
    std::vector<MemAccess> acc1;
    std::vector<MemAccess> acc2;
    if (!collectBodyAccesses(*body1, H1, acc1) || !collectBodyAccesses(*body2, H2, acc2)) {
        return false;
    }
    if (!isFusionLegal(acc1, acc2, ind1, ind2)) {
        return false;
    }

    // ---- 至此确认可融合，执行 CFG 重连（不搬移任何指令） ----
    Value * inc1 = getBackedgeValue(ind1, L1);
    if (!inc1) {
        return false;
    }
    auto * l1Branch = l1.branch; // H1 的条件分支
    auto * l1LatchBr = dynamic_cast<BranchInst *>(L1->getTerminator());
    auto * l2LatchBr = dynamic_cast<BranchInst *>(L2->getTerminator());
    if (!l1LatchBr || !l2LatchBr) {
        return false;
    }

    // (a) 用 l1 的归纳变量替换 l2 的归纳变量
    ind2->replaceAllUseWith(ind1);

    // (b) H1 的出口边：Ex1 -> Ex2
    if (l1Branch->getTrueDest() == Ex1) {
        l1Branch->setTrueDest(Ex2);
    } else {
        l1Branch->setFalseDest(Ex2);
    }
    H1->removeSuccessor(Ex1);
    H1->addSuccessor(Ex2);

    // (c) L1 的回边改为进入 l2 体入口 BE2
    l1LatchBr->setTarget(BE2);
    L1->removeSuccessor(H1);
    L1->addSuccessor(BE2);
    remapPhiIncoming(BE2, H2, L1);
    BE2->removePredecessor(H2);
    BE2->addPredecessor(L1);

    // (d) L2 的回边改为回到融合后的头 H1，H1 的归纳 phi 回边来源由 L1 改为 L2
    l2LatchBr->setTarget(H1);
    L2->removeSuccessor(H2);
    L2->addSuccessor(H1);
    ind1->replaceIncomingBlock(L1, L2);
    H1->removePredecessor(L1);
    H1->addPredecessor(L2);

    // (e) Ex2 的出口 phi/前驱：H2 -> H1
    remapPhiIncoming(Ex2, H2, H1);
    Ex2->removePredecessor(H2);
    Ex2->addPredecessor(H1);

    // (f) 删除 l2 的头 H2 与前置头 Ex1(==P2)
    for (auto * inst : H2->getInstructions()) {
        inst->clearOperands();
    }
    for (auto * inst : Ex1->getInstructions()) {
        inst->clearOperands();
    }
    auto & blocks = func->getBlocks();
    for (BasicBlock * dead : {H2, Ex1}) {
        auto it = std::find(blocks.begin(), blocks.end(), dead);
        if (it != blocks.end()) {
            blocks.erase(it);
        }
    }
    delete H2;
    delete Ex1;

    if (std::getenv("MINIC_DEBUG_FUSION") != nullptr) {
        std::fprintf(stderr, "[LoopFusion] fused loops in %s\n", func->getName().c_str());
    }
    return true;
}

} // namespace

/// @brief 构造循环融合 pass
/// @param _func 待优化函数
/// @param _mod 所属模块
LoopFusion::LoopFusion(Function * _func, Module * _mod) : func(_func)
{
    (void) _mod;
}

/// @brief 对函数原地执行循环融合
/// @return 若 IR 发生变化则返回 true
bool LoopFusion::run()
{
    if (!func || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    bool changed = false;
    while (true) {
        DominatorTree domTree(func);
        LoopInfo loopInfo(func, &domTree);
        ScalarEvolution scev(func, &domTree, &loopInfo);

        // 收集所有匹配成功的规范计数循环，按块在函数中的先后排序，便于寻找相邻对
        std::vector<ScalarEvolution::CanonicalLoop> loops;
        for (auto * bb : func->getBlocks()) {
            ScalarEvolution::CanonicalLoop cl;
            if (loopInfo.isLoopHeader(bb) && scev.matchCanonicalLoop(bb, cl)) {
                loops.push_back(cl);
            }
        }

        bool localChanged = false;
        for (auto & l1 : loops) {
            for (auto & l2 : loops) {
                if (l1.header == l2.header) {
                    continue;
                }
                if (tryFuseLoops(func, l1, l2)) {
                    localChanged = true;
                    changed = true;
                    func->getAnalysisCache().invalidateCFGAnalyses();
                    break;
                }
            }
            if (localChanged) {
                break;
            }
        }
        if (!localChanged) {
            break;
        }
    }

    return changed;
}
