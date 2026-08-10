///
/// @file PartialDeadStoreElim.cpp
/// @brief 部分死 store 消除（Partial Dead Store Elimination）实现
///

#include "PartialDeadStoreElim.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "BranchInst.h"
#include "BinaryInst.h"
#include "CallInst.h"
#include "CondBranchInst.h"
#include "CopyInst.h"
#include "FCmpInst.h"
#include "FPToSIInst.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "ICmpInst.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "MemoryLocation.h"
#include "PhiInst.h"
#include "ReturnInst.h"
#include "SIToFPInst.h"
#include "SelectInst.h"
#include "StoreInst.h"
#include "ZExtInst.h"
#include "Values/ConstFloat.h"
#include "Values/ConstInteger.h"
#include "Values/FormalParam.h"
#include "Values/GlobalVariable.h"

#include <cstdio>
#include <cstdlib>

namespace {

/// @brief 块是否恰好是一条 `ret void`
/// @param bb 待检查块
/// @return true 表示块内只有一条无返回值 return
bool isVoidReturnBlock(BasicBlock * bb)
{
    if (bb == nullptr) {
        return false;
    }
    const auto & insts = bb->getInstructions();
    if (insts.size() != 1) {
        return false;
    }
    auto * ret = dynamic_cast<ReturnInst *>(insts.front());
    return ret != nullptr && ret->getReturnValue() == nullptr;
}

/// @brief 沿 GEP 链回溯指针的底层 alloca
/// @param ptr 指针值
/// @return 底层 alloca；若根不是 alloca 则返回 nullptr
AllocaInst * getRootAlloca(Value * ptr)
{
    Value * cur = ptr;
    std::unordered_set<Value *> seen;
    while (cur != nullptr && seen.insert(cur).second) {
        if (auto * alloca = dynamic_cast<AllocaInst *>(cur)) {
            return alloca;
        }
        if (auto * gep = dynamic_cast<GetElementPtrInst *>(cur)) {
            cur = gep->getBasePointer();
            continue;
        }
        return nullptr;
    }
    return nullptr;
}

/// @brief 指针是否只会留在函数内部流动（不落入内存/调用/返回值）
///
/// 与 doesPointerEscape 不同：phi/copy/select 的指针使用仍留在函数内，
/// 不视为逃逸（地址流入 LSR 指针 phi 是常见合法形态）。只有指针被
/// 写入内存、作为调用实参或返回时才视为真逃逸。
///
/// @param ptr 指针值
/// @param visited 去重集合
/// @return true 表示指针可能被函数外观察（真逃逸）
bool pointerLeavesFunction(Value * ptr, std::unordered_set<Value *> & visited)
{
    if (ptr == nullptr || !visited.insert(ptr).second) {
        return false;
    }
    for (auto * u : ptr->getUseList()) {
        auto * inst = dynamic_cast<Instruction *>(u->getUser());
        if (inst == nullptr) {
            return true;
        }
        if (auto * store = dynamic_cast<StoreInst *>(inst)) {
            if (store->getPointerOperand() != ptr) {
                return true; // 指针作为值被写入内存
            }
            continue;
        }
        if (auto * load = dynamic_cast<LoadInst *>(inst)) {
            if (load->getPointerOperand() != ptr) {
                return true;
            }
            continue;
        }
        if (auto * gep = dynamic_cast<GetElementPtrInst *>(inst)) {
            if (gep->getBasePointer() != ptr) {
                return true;
            }
            if (pointerLeavesFunction(gep, visited)) {
                return true;
            }
            continue;
        }
        if (auto * phi = dynamic_cast<PhiInst *>(inst)) {
            if (pointerLeavesFunction(phi, visited)) {
                return true;
            }
            continue;
        }
        if (auto * copy = dynamic_cast<CopyInst *>(inst)) {
            if (pointerLeavesFunction(copy, visited)) {
                return true;
            }
            continue;
        }
        if (auto * select = dynamic_cast<SelectInst *>(inst)) {
            if (pointerLeavesFunction(select, visited)) {
                return true;
            }
            continue;
        }
        if (dynamic_cast<CallInst *>(inst) != nullptr ||
            dynamic_cast<ReturnInst *>(inst) != nullptr) {
            return true;
        }
        // 其余指令（比较/位运算等）不传递指针
        return true;
    }
    return false;
}

/// @brief 指令是否可在提前返回路径上整体跳过（不可观察）
/// @param inst 待检查指令
/// @param storeCount 输出：区域内可跳过的 store 数（收益统计）
/// @return true 表示可跳过
bool isSkippable(Instruction * inst, int & storeCount)
{
    if (inst->isTerminator()) {
        // 控制流本身可跳过；ret 由调用方单独排除
        return true;
    }
    if (dynamic_cast<CallInst *>(inst) != nullptr ||
        dynamic_cast<ReturnInst *>(inst) != nullptr) {
        return false;
    }

    switch (inst->getOp()) {
        case IRInstOperator::IRINST_OP_ALLOCA:
            // alloca 不产生任何代码，直接跳过
            return true;

        case IRInstOperator::IRINST_OP_LOAD:
        case IRInstOperator::IRINST_OP_STORE: {
            // 只允许访问不离开函数的栈对象：提前返回路径上其写入不可观察
            Value * addr = (inst->getOp() == IRInstOperator::IRINST_OP_LOAD)
                               ? static_cast<LoadInst *>(inst)->getPointerOperand()
                               : static_cast<StoreInst *>(inst)->getPointerOperand();
            AllocaInst * root = getRootAlloca(addr);
            std::unordered_set<Value *> visited;
            if (root == nullptr || pointerLeavesFunction(root, visited)) {
                return false;
            }
            if (inst->getOp() == IRInstOperator::IRINST_OP_STORE) {
                ++storeCount;
            }
            return true;
        }

        default:
            // 其余指令必须是无副作用的纯计算
            if (inst->mayReadMemory() || inst->mayWriteMemory() ||
                inst->mayHaveSideEffects()) {
                return false;
            }
            return true;
    }
}

/// @brief 判断值是否为闭包叶子（参数/常量/全局地址）
bool isClosureLeaf(Value * v)
{
    return dynamic_cast<ConstInteger *>(v) != nullptr ||
           dynamic_cast<ConstFloat *>(v) != nullptr ||
           dynamic_cast<FormalParam *>(v) != nullptr ||
           dynamic_cast<GlobalVariable *>(v) != nullptr;
}

/// @brief 判断指令是否可出现在克隆的纯条件 def-chain 中
/// @return true 表示允许（纯值指令或分支终结指令）
bool isClonablePureInst(Instruction * inst)
{
    if (dynamic_cast<CallInst *>(inst) != nullptr ||
        dynamic_cast<LoadInst *>(inst) != nullptr ||
        dynamic_cast<StoreInst *>(inst) != nullptr ||
        dynamic_cast<ReturnInst *>(inst) != nullptr) {
        return false;
    }
    switch (inst->getOp()) {
        // 禁止任何向量/其他指令进入条件闭包
        case IRInstOperator::IRINST_OP_VSETVL:
        case IRInstOperator::IRINST_OP_VLOAD:
        case IRInstOperator::IRINST_OP_VSTORE:
        case IRInstOperator::IRINST_OP_VSPLAT:
        case IRInstOperator::IRINST_OP_VBINARY:
        case IRInstOperator::IRINST_OP_VREDUCE:
        case IRInstOperator::IRINST_OP_VEXTRACT:
        case IRInstOperator::IRINST_OP_ALLOCA:
            return false;
        default:
            break;
    }
    // 允许纯值指令与分支终结指令
    if (inst->isTerminator()) {
        return dynamic_cast<BranchInst *>(inst) != nullptr ||
               dynamic_cast<CondBranchInst *>(inst) != nullptr;
    }
    return !inst->mayReadMemory() && !inst->mayWriteMemory() &&
           !inst->mayHaveSideEffects();
}

/// @brief 收集条件 def-chain 的闭包（值集与块集）
///
/// 闭包包含：
/// - 条件的全部纯计算 def 链指令；
/// - 含 phi 的块的所有前驱块（其终结指令决定 phi 取哪条入边）；
/// - 闭包内每个非决策块的分支终结指令及其目标块。
///
/// @param cond 条件值
/// @param decision 决策块（分支所在块，其终结指令不进入闭包）
/// @param entry 函数入口块
/// @param values 输出：闭包指令集
/// @param blocks 输出：闭包块集
/// @return false 表示闭包不满足纯化要求，拒绝变换
bool collectClosure(Value * cond,
                    BasicBlock * decision,
                    BasicBlock * entry,
                    std::unordered_set<Value *> & values,
                    std::unordered_set<BasicBlock *> & blocks)
{
    std::vector<Value *> valueWorklist;
    std::vector<BasicBlock *> blockWorklist;
    valueWorklist.push_back(cond);

    while (!valueWorklist.empty() || !blockWorklist.empty()) {
        if (!valueWorklist.empty()) {
            Value * v = valueWorklist.back();
            valueWorklist.pop_back();
            if (v == nullptr) {
                return false;
            }
            if (isClosureLeaf(v)) {
                continue;
            }
            auto * inst = dynamic_cast<Instruction *>(v);
            if (inst == nullptr) {
                return false;
            }
            if (!isClonablePureInst(inst)) {
                return false;
            }
            if (values.count(inst) > 0) {
                continue;
            }
            values.insert(inst);

            BasicBlock * parent = inst->getParentBlock();
            if (parent != nullptr && blocks.insert(parent).second) {
                blockWorklist.push_back(parent);
            }

            if (auto * phi = dynamic_cast<PhiInst *>(inst)) {
                // phi 的全部入边块都必须可克隆（含入口块：其终结指令
                // 决定 phi 的入口入边，须随闭包一起克隆）
                for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
                    valueWorklist.push_back(phi->getIncomingValue(i));
                    BasicBlock * p = phi->getIncomingBlock(i);
                    if (p == nullptr) {
                        return false;
                    }
                    if (blocks.insert(p).second) {
                        blockWorklist.push_back(p);
                    }
                }
            } else {
                for (auto * opnd : inst->getOperandsValue()) {
                    valueWorklist.push_back(opnd);
                }
            }
            continue;
        }

        // 处理闭包块：其终结指令必须可克隆、目标必须在闭包内
        BasicBlock * b = blockWorklist.back();
        blockWorklist.pop_back();
        if (b == decision) {
            continue;
        }
        Instruction * term = b->getTerminator();
        if (term == nullptr) {
            return false;
        }
        BasicBlock * t1 = nullptr;
        BasicBlock * t2 = nullptr;
        if (auto * br = dynamic_cast<BranchInst *>(term)) {
            t1 = br->getTarget();
            t2 = t1; // 无条件分支：双目标视为同一目标
        } else if (auto * cbr = dynamic_cast<CondBranchInst *>(term)) {
            t1 = cbr->getTrueDest();
            t2 = cbr->getFalseDest();
        } else {
            return false;
        }
        if (t1 == nullptr) {
            return false;
        }
        if (values.insert(term).second) {
            for (auto * opnd : term->getOperandsValue()) {
                valueWorklist.push_back(opnd);
            }
        }
        for (BasicBlock * t : {t1, t2}) {
            if (t == decision) {
                continue; // 决策块自身：其终结指令按新决策分支处理
            }
            if (blocks.insert(t).second) {
                blockWorklist.push_back(t);
            }
        }
    }

    // 入口块必须参与克隆（其终结指令决定 phi 的入口入边）
    return blocks.count(entry) > 0;
}

/// @brief 闭包块拓扑排序（指令 def 先于 use）
///
/// 使用函数原始块顺序作为 Kahn 算法的稳定 tie-breaker，
/// 保证多次编译产出的克隆块布局完全一致（消除 unordered 容器迭代
/// 顺序带来的非确定性）。
///
/// @return false 表示闭包子图含环
bool topoSortClosure(const std::unordered_set<BasicBlock *> & blocks,
                     const std::vector<BasicBlock *> & funcBlockOrder,
                     std::vector<BasicBlock *> & topo)
{
    std::unordered_map<BasicBlock *, std::vector<BasicBlock *>> deps;
    std::unordered_map<BasicBlock *, int> indeg;
    for (auto * b : blocks) {
        indeg[b] = 0;
        deps[b] = {};
    }
    for (auto * b : blocks) {
        for (auto * inst : b->getInstructions()) {
            for (auto * opnd : inst->getOperandsValue()) {
                auto * opInst = dynamic_cast<Instruction *>(opnd);
                if (opInst == nullptr) {
                    continue;
                }
                BasicBlock * src = opInst->getParentBlock();
                if (src == nullptr || src == b || !blocks.count(src)) {
                    continue;
                }
                bool dup = false;
                for (auto * d : deps[b]) {
                    if (d == src) {
                        dup = true;
                        break;
                    }
                }
                if (!dup) {
                    deps[b].push_back(src);
                    indeg[b] += 1;
                }
            }
        }
    }

    auto blockRank = [&funcBlockOrder](BasicBlock * b) -> std::size_t {
        for (std::size_t i = 0; i < funcBlockOrder.size(); ++i) {
            if (funcBlockOrder[i] == b) {
                return i;
            }
        }
        return funcBlockOrder.size();
    };

    // 就绪队列按原始块序稳定弹出，保证确定性
    std::vector<BasicBlock *> ready;
    for (auto & [b, d] : indeg) {
        if (d == 0) {
            ready.push_back(b);
        }
    }
    auto popSmallest = [&]() -> BasicBlock * {
        std::size_t bestIdx = 0;
        for (std::size_t i = 1; i < ready.size(); ++i) {
            if (blockRank(ready[i]) < blockRank(ready[bestIdx])) {
                bestIdx = i;
            }
        }
        BasicBlock * best = ready[bestIdx];
        ready[bestIdx] = ready.back();
        ready.pop_back();
        return best;
    };

    while (!ready.empty()) {
        BasicBlock * b = popSmallest();
        topo.push_back(b);
        for (auto & [b2, srcs] : deps) {
            for (auto * s : srcs) {
                if (s == b) {
                    indeg[b2] -= 1;
                    if (indeg[b2] == 0) {
                        ready.push_back(b2);
                    }
                    break;
                }
            }
        }
    }
    return topo.size() == blocks.size();
}

/// @brief 克隆纯值指令（操作数经 valueMap 映射）
/// @return 克隆指令；失败（未知指令类型）返回 nullptr
Instruction * clonePureInst(Instruction * inst,
                            const std::unordered_map<Value *, Value *> & valueMap,
                            Function * func)
{
    auto mapVal = [&valueMap](Value * v) -> Value * {
        auto it = valueMap.find(v);
        return (it != valueMap.end()) ? it->second : v;
    };

    Function * f = func;
    switch (inst->getOp()) {
        case IRInstOperator::IRINST_OP_ADD_I:
        case IRInstOperator::IRINST_OP_SUB_I:
        case IRInstOperator::IRINST_OP_MUL_I:
        case IRInstOperator::IRINST_OP_DIV_I:
        case IRInstOperator::IRINST_OP_MOD_I:
        case IRInstOperator::IRINST_OP_SHL_I:
        case IRInstOperator::IRINST_OP_ASHR_I:
        case IRInstOperator::IRINST_OP_LSHR_I:
        case IRInstOperator::IRINST_OP_AND_I:
        case IRInstOperator::IRINST_OP_OR_I:
        case IRInstOperator::IRINST_OP_XOR_I:
        case IRInstOperator::IRINST_OP_ADD_F:
        case IRInstOperator::IRINST_OP_SUB_F:
        case IRInstOperator::IRINST_OP_MUL_F:
        case IRInstOperator::IRINST_OP_DIV_F:
            return new BinaryInst(f, inst->getOp(), mapVal(inst->getOperand(0)),
                                  mapVal(inst->getOperand(1)), inst->getType());

        case IRInstOperator::IRINST_OP_LT_I:
        case IRInstOperator::IRINST_OP_GT_I:
        case IRInstOperator::IRINST_OP_LE_I:
        case IRInstOperator::IRINST_OP_GE_I:
        case IRInstOperator::IRINST_OP_EQ_I:
        case IRInstOperator::IRINST_OP_NE_I:
            return new ICmpInst(f, inst->getOp(), mapVal(inst->getOperand(0)),
                                mapVal(inst->getOperand(1)), inst->getType());

        case IRInstOperator::IRINST_OP_LT_F:
        case IRInstOperator::IRINST_OP_GT_F:
        case IRInstOperator::IRINST_OP_LE_F:
        case IRInstOperator::IRINST_OP_GE_F:
        case IRInstOperator::IRINST_OP_EQ_F:
        case IRInstOperator::IRINST_OP_NE_F:
            return new FCmpInst(f, inst->getOp(), mapVal(inst->getOperand(0)),
                                mapVal(inst->getOperand(1)), inst->getType());

        case IRInstOperator::IRINST_OP_ZEXT:
            return new ZExtInst(f, mapVal(inst->getOperand(0)), inst->getType());

        case IRInstOperator::IRINST_OP_SITOFP:
            return new SIToFPInst(f, mapVal(inst->getOperand(0)), inst->getType());

        case IRInstOperator::IRINST_OP_FPTOSI:
            return new FPToSIInst(f, mapVal(inst->getOperand(0)), inst->getType());

        case IRInstOperator::IRINST_OP_SELECT:
            return new SelectInst(f, mapVal(inst->getOperand(0)),
                                  mapVal(inst->getOperand(1)),
                                  mapVal(inst->getOperand(2)), inst->getType());

        case IRInstOperator::IRINST_OP_COPY:
            return new CopyInst(f, mapVal(inst->getOperand(0)));

        default:
            return nullptr;
    }
}

/// @brief 克隆 phi（入边块经 blockMap 映射，入边值经 valueMap 映射）
PhiInst * clonePhi(PhiInst * phi,
                   const std::unordered_map<Value *, Value *> & valueMap,
                   const std::unordered_map<BasicBlock *, BasicBlock *> & blockMap,
                   Function * func)
{
    auto mapVal = [&valueMap](Value * v) -> Value * {
        auto it = valueMap.find(v);
        return (it != valueMap.end()) ? it->second : v;
    };
    auto mapBlock = [&blockMap](BasicBlock * b) -> BasicBlock * {
        auto it = blockMap.find(b);
        return (it != blockMap.end()) ? it->second : b;
    };

    auto * clone = new PhiInst(func, phi->getType());
    for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
        clone->addIncoming(mapVal(phi->getIncomingValue(i)),
                           mapBlock(phi->getIncomingBlock(i)));
    }
    return clone;
}

} // namespace

PartialDeadStoreElim::PartialDeadStoreElim(Function * f)
    : func(f)
{
}

bool PartialDeadStoreElim::run()
{
    if (func == nullptr || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    // 快照块列表：变换会插入新块
    const auto snapshot = func->getBlocks();
    BasicBlock * entry = func->getBlocks().front();

    for (auto * bb : snapshot) {
        auto * term = dynamic_cast<CondBranchInst *>(bb->getTerminator());
        if (term == nullptr) {
            continue;
        }
        // 决策块不能是入口块：克隆决策分支会与入口终结在入口块内
        // 产生两个终结指令，且入口终结目标的 blockMap 映射缺失
        if (bb == entry) {
            continue;
        }
        BasicBlock * trueDest = term->getTrueDest();
        BasicBlock * falseDest = term->getFalseDest();
        if (trueDest == nullptr || falseDest == nullptr || trueDest == falseDest) {
            continue;
        }

        // 找返回目标：其中一个后继必须是纯 ret void 块
        BasicBlock * retTarget = nullptr;
        BasicBlock * contTarget = nullptr;
        if (isVoidReturnBlock(trueDest)) {
            retTarget = trueDest;
            contTarget = falseDest;
        } else if (isVoidReturnBlock(falseDest)) {
            retTarget = falseDest;
            contTarget = trueDest;
        } else {
            continue;
        }
        // initBB 终结被线程化为无条件跳转到 contTarget，其 phi 入边
        // 无法补全（原检查流不可达），保守要求 contTarget 无 phi
        {
            bool contHasPhi = false;
            for (auto * inst : contTarget->getInstructions()) {
                if (dynamic_cast<PhiInst *>(inst) != nullptr) {
                    contHasPhi = true;
                    break;
                }
                break; // phi 只在块首
            }
            if (contHasPhi) {
                continue;
            }
        }

        // 1) 条件 def-chain 闭包
        std::unordered_set<Value *> closureValues;
        std::unordered_set<BasicBlock *> closureBlocks;
        if (!collectClosure(term->getCondition(), bb, entry, closureValues,
                            closureBlocks)) {
            continue;
        }

        // 2) 区域：从入口可达 且 可到达决策块的块集
        std::unordered_set<BasicBlock *> fwd;
        std::unordered_set<BasicBlock *> bwd;
        std::unordered_set<BasicBlock *> seen;
        std::vector<BasicBlock *> worklist;
        worklist.push_back(entry);
        seen.insert(entry);
        while (!worklist.empty()) {
            BasicBlock * b = worklist.back();
            worklist.pop_back();
            fwd.insert(b);
            for (auto * s : b->getSuccessors()) {
                if (s != nullptr && seen.insert(s).second) {
                    worklist.push_back(s);
                }
            }
        }
        seen.clear();
        worklist.push_back(bb);
        seen.insert(bb);
        while (!worklist.empty()) {
            BasicBlock * b = worklist.back();
            worklist.pop_back();
            bwd.insert(b);
            for (auto * p : b->getPredecessors()) {
                if (p != nullptr && seen.insert(p).second) {
                    worklist.push_back(p);
                }
            }
        }
        std::unordered_set<BasicBlock *> region;
        for (auto * b : fwd) {
            if (bwd.count(b) > 0) {
                region.insert(b);
            }
        }
        if (region.count(bb) == 0) {
            continue; // 决策块从入口不可达
        }

        // 3) 区域整体可跳过 + 收益门槛（至少一条可跳过的 store）
        int storeCount = 0;
        bool regionOk = true;
        for (auto * b : region) {
            for (auto * inst : b->getInstructions()) {
                if (dynamic_cast<ReturnInst *>(inst) != nullptr) {
                    regionOk = false;
                    break;
                }
                if (!isSkippable(inst, storeCount)) {
                    regionOk = false;
                    break;
                }
            }
            if (!regionOk) {
                break;
            }
        }
        if (!regionOk || storeCount == 0) {
            continue;
        }

        // 4) 闭包块必须全部落在区域内（防御性检查）
        bool closureInRegion = true;
        for (auto * cb : closureBlocks) {
            if (region.count(cb) == 0) {
                closureInRegion = false;
                break;
            }
        }
        if (!closureInRegion) {
            continue;
        }

        // 5) 闭包拓扑排序（含环则拒绝）
        std::vector<BasicBlock *> topo;
        if (!topoSortClosure(closureBlocks, func->getBlocks(), topo)) {
            continue;
        }

        // ---- 变换 ----
        auto & blocksVec = func->getBlocks();
        Value * cond = term->getCondition();

        // 5a) 创建新块
        BasicBlock * earlyRet = func->newBasicBlock();
        auto * earlyRetInst = new ReturnInst(func, nullptr);
        earlyRet->getInstructions().push_back(earlyRetInst);
        earlyRetInst->setParentBlock(earlyRet);
        BasicBlock * initBB = func->newBasicBlock();
        std::unordered_map<BasicBlock *, BasicBlock *> blockMap;
        blockMap[entry] = entry;
        for (auto * cb : topo) {
            if (cb != entry) {
                blockMap[cb] = func->newBasicBlock();
            }
        }

        // 5b) 克隆闭包指令（按拓扑序）
        std::unordered_map<Value *, Value *> valueMap;
        for (auto * cb : topo) {
            BasicBlock * nb = (cb == entry) ? entry : blockMap[cb];
            auto & insts = nb->getInstructions();
            for (auto * inst : cb->getInstructions()) {
                if (inst->isTerminator()) {
                    continue; // 终结指令由下方单独的克隆步骤处理
                }
                if (closureValues.count(inst) == 0) {
                    continue;
                }
                if (cb == entry) {
                    // 入口块的 def-chain 指令就地保留（移动而非克隆），
                    // initBB 终结已被线程化为无条件跳转，入口克隆值
                    // 只被入口分支消费（折叠、不物化）
                    valueMap[inst] = inst;
                    continue;
                }
                Value * clone = nullptr;
                if (auto * phi = dynamic_cast<PhiInst *>(inst)) {
                    clone = clonePhi(phi, valueMap, blockMap, func);
                } else {
                    clone = clonePureInst(inst, valueMap, func);
                }
                if (clone == nullptr) {
                    return false; // 不应发生（闭包已检查）
                }
                valueMap[inst] = clone;
                auto * cloneInst = static_cast<Instruction *>(clone);
                insts.push_back(cloneInst);
                cloneInst->setParentBlock(nb);
            }

            // 闭包内非决策块的终结指令克隆（目标经 blockMap 映射）
            if (cb == entry || cb == bb) {
                continue;
            }
            Instruction * termClone = nullptr;
            auto * oterm = cb->getTerminator();
            if (auto * br = dynamic_cast<BranchInst *>(oterm)) {
                termClone = new BranchInst(func, blockMap[br->getTarget()]);
            } else if (auto * cbr = dynamic_cast<CondBranchInst *>(oterm)) {
                termClone = new CondBranchInst(func, cbr->getCondition(),
                                               blockMap[cbr->getTrueDest()],
                                               blockMap[cbr->getFalseDest()]);
            }
            if (termClone == nullptr) {
                return false;
            }
            insts.push_back(termClone);
            termClone->setParentBlock(nb);
            // 克隆块的控制流边必须登记，保持 CFG 与终结指令一致
            if (auto * br = dynamic_cast<BranchInst *>(termClone)) {
                nb->linkSuccessor(br->getTarget());
            } else if (auto * cbr = dynamic_cast<CondBranchInst *>(termClone)) {
                nb->linkSuccessor(cbr->getTrueDest());
                nb->linkSuccessor(cbr->getFalseDest());
            }
        }

        // 5c) 决策块克隆的终结：新决策分支 -> earlyRet / initBB
        BasicBlock * decClone = blockMap[bb];
        Value * clonedCond = cond;
        if (dynamic_cast<Instruction *>(cond) != nullptr) {
            auto it = valueMap.find(cond);
            if (it == valueMap.end()) {
                return false; // 条件的 def 必须在闭包内
            }
            clonedCond = it->second;
        }
        CondBranchInst * decBr = nullptr;
        if (retTarget == trueDest) {
            decBr = new CondBranchInst(func, clonedCond, earlyRet, initBB);
        } else {
            decBr = new CondBranchInst(func, clonedCond, initBB, earlyRet);
        }
        decClone->getInstructions().push_back(decBr);
        decBr->setParentBlock(decClone);
        decClone->linkSuccessor(earlyRet);
        decClone->linkSuccessor(initBB);

        // 5d) 入口块：重定向终结指令目标到克隆块。
        //     注意：这里操作的是入口自身终结指令的目标（原 bb63/bb59），
        //     与决策分支的目标（retTarget/contTarget）无关
        auto * entryTerm = dynamic_cast<CondBranchInst *>(entry->getTerminator());
        if (entryTerm == nullptr) {
            return false;
        }
        BasicBlock * origE1 = entryTerm->getTrueDest();
        BasicBlock * origE2 = entryTerm->getFalseDest();
        BasicBlock * eT1 = blockMap[origE1];
        BasicBlock * eT2 = blockMap[origE2];
        entryTerm->setTrueDest(eT1);
        entryTerm->setFalseDest(eT2);
        for (auto * s : {origE1, origE2}) {
            entry->removeSuccessor(s);
            s->removePredecessor(entry);
        }
        entry->linkSuccessor(eT1);
        entry->linkSuccessor(eT2);

        // 5e) 入口块除终结外的指令：非 def-chain 指令移入 initBB；
        //     def-chain 原版指令整体丢弃（5f 已让 initBB 终结使用入口
        //     克隆值，原版链成为死代码且与克隆同构，若不删除会被 GVN
        //     合并出跨路径 def 的非法 IR）
        auto & entryInsts = entry->getInstructions();
        std::vector<Instruction *> toMove;
        for (auto * inst : entryInsts) {
            if (inst == entryTerm) {
                continue;
            }
            if (closureValues.count(inst) == 0) {
                toMove.push_back(inst);
            }
        }
        auto & initInsts = initBB->getInstructions();
        for (auto * inst : toMove) {
            entryInsts.remove(inst);
            initInsts.push_back(inst);
            inst->setParentBlock(initBB);
        }

        // 5f) initBB 终结：线程化为无条件跳转到续行目标。
        //     入口的克隆检查保证到达 initBB 时原条件恒为假（条件纯、逐点
        //     一致），原检查的返回边永不执行。同时使入口克隆值只被提前
        //     路径的分支消费（折叠、不物化），后端入口 shrink-wrapping
        //     无需在提前路径物化任何寄存器
        auto * initTerm = new BranchInst(func, contTarget);
        initInsts.push_back(initTerm);
        initTerm->setParentBlock(initBB);
        initBB->linkSuccessor(contTarget);

        // 5h) 线程化原检查：决策块的终结替换为无条件跳转到续行目标。
        //     所有到达决策块的路径都经过入口的克隆检查且条件恒为假，
        //     因此返回边永不执行（条件纯、逐点值一致）。
        auto * oldTerm = bb->getTerminator();
        oldTerm->clearOperands();
        bb->getInstructions().remove(oldTerm);
        func->adoptDetachedValue(oldTerm);
        auto * nbr = new BranchInst(func, contTarget);
        bb->getInstructions().push_back(nbr);
        nbr->setParentBlock(bb);
        bb->removeSuccessor(retTarget);
        retTarget->removePredecessor(bb);

        // 5i) 块布局：newBasicBlock 已把新块追加到向量末尾，
        //     先摘除，再按「入口之后依次放克隆块与 initBB」重排，
        //     earlyRet 放末尾（冷路径），避免向量中出现重复块条目
        std::vector<BasicBlock *> newBlocks = {earlyRet, initBB};
        for (auto * cb : topo) {
            if (cb != entry) {
                newBlocks.push_back(blockMap[cb]);
            }
        }
        for (auto * nb : newBlocks) {
            blocksVec.erase(std::remove(blocksVec.begin(), blocksVec.end(), nb),
                            blocksVec.end());
        }
        std::vector<BasicBlock *> afterEntry;
        for (auto * cb : topo) {
            if (cb != entry) {
                afterEntry.push_back(blockMap[cb]);
            }
        }
        afterEntry.push_back(initBB);
        blocksVec.insert(blocksVec.begin() + 1, afterEntry.begin(), afterEntry.end());
        blocksVec.push_back(earlyRet);

        return true;
    }

    return false;
}
