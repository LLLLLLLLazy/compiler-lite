///
/// @file LICM.cpp
/// @brief 循环不变量外提 pass 实现
///

#include "LICM.h"

#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <utility>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "CondBranchInst.h"
#include "DominatorTree.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "GlobalVariable.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "LoopInfo.h"
#include "MemoryAccess.h"
#include "MemoryLocation.h"
#include "Module.h"
#include "PhiInst.h"
#include "StoreInst.h"
#include "Use.h"
#include "Value.h"

namespace {

/// @brief 判断操作码是否属于可保守外提的纯计算指令
/// @param op 指令操作码
/// @return true 表示该操作码可参与 LICM 候选
bool isPureLoopInvariantOp(IRInstOperator op)
{
    switch (op) {
        case IRInstOperator::IRINST_OP_ADD_I:
        case IRInstOperator::IRINST_OP_SUB_I:
        case IRInstOperator::IRINST_OP_MUL_I:
        case IRInstOperator::IRINST_OP_DIV_I:
        case IRInstOperator::IRINST_OP_MOD_I:
        case IRInstOperator::IRINST_OP_LT_I:
        case IRInstOperator::IRINST_OP_GT_I:
        case IRInstOperator::IRINST_OP_LE_I:
        case IRInstOperator::IRINST_OP_GE_I:
        case IRInstOperator::IRINST_OP_EQ_I:
        case IRInstOperator::IRINST_OP_NE_I:
        case IRInstOperator::IRINST_OP_ADD_F:
        case IRInstOperator::IRINST_OP_SUB_F:
        case IRInstOperator::IRINST_OP_MUL_F:
        case IRInstOperator::IRINST_OP_DIV_F:
        case IRInstOperator::IRINST_OP_LT_F:
        case IRInstOperator::IRINST_OP_GT_F:
        case IRInstOperator::IRINST_OP_LE_F:
        case IRInstOperator::IRINST_OP_GE_F:
        case IRInstOperator::IRINST_OP_EQ_F:
        case IRInstOperator::IRINST_OP_NE_F:
        case IRInstOperator::IRINST_OP_ZEXT:
        case IRInstOperator::IRINST_OP_SELECT:
        case IRInstOperator::IRINST_OP_SITOFP:
        case IRInstOperator::IRINST_OP_FPTOSI:
        case IRInstOperator::IRINST_OP_GEP:
            return true;

        default:
            return false;
    }
}

/// @brief 判断操作码是否需要用退出点支配来禁止不安全的推测执行
/// @param op 指令操作码
/// @return true 表示外提前必须保证原位置支配全部循环退出点
bool needsExitDominance(IRInstOperator op)
{
    switch (op) {
        case IRInstOperator::IRINST_OP_DIV_I:
        case IRInstOperator::IRINST_OP_MOD_I:
        case IRInstOperator::IRINST_OP_DIV_F:
            return true;

        default:
            return false;
    }
}

/// @brief 函数纯度分析状态枚举，用于 LICM 判断调用是否可安全外提
enum class FunctionPurityState {
    Unknown,   ///< 尚未分析
    Visiting,  ///< 正在访问（用于检测递归）
    Pure,      ///< 纯函数：无副作用，可安全外提
    Impure,    ///< 非纯函数：有副作用，不可外提
};

/// @brief 函数纯度分析器
///
/// 通过递归分析函数体中的指令来判断函数是否为纯函数。
/// 纯函数的判定条件：不包含 store 指令、所有调用也都是纯函数调用、
/// 其余指令均无副作用。用于 LICM 判断循环内的函数调用是否可安全外提。
class FunctionPurity {
public:
    bool isPure(Function * function)
    {
        if (!function || function->isBuiltin() || function->getBlocks().empty()) {
            return false;
        }

        auto it = states.find(function);
        if (it != states.end()) {
            return it->second == FunctionPurityState::Pure;
        }

        states[function] = FunctionPurityState::Visiting;
        bool pure = true;
        for (auto * bb : function->getBlocks()) {
            for (auto * inst : bb->getInstructions()) {
                if (!isInstructionAllowed(inst)) {
                    pure = false;
                    break;
                }
            }
            if (!pure) {
                break;
            }
        }

        states[function] = pure ? FunctionPurityState::Pure : FunctionPurityState::Impure;
        return pure;
    }

private:
    bool isInstructionAllowed(Instruction * inst)
    {
        if (!inst || inst->isDead() || inst->isTerminator()) {
            return true;
        }

        if (dynamic_cast<AllocaInst *>(inst) || dynamic_cast<LoadInst *>(inst)) {
            return true;
        }

        if (dynamic_cast<StoreInst *>(inst)) {
            return false;
        }

        if (auto * call = dynamic_cast<CallInst *>(inst)) {
            auto it = states.find(call->getCallee());
            if (it != states.end() && it->second == FunctionPurityState::Visiting) {
                return false;
            }
            return isPure(call->getCallee());
        }

        return !inst->mayHaveSideEffects();
    }

    std::unordered_map<Function *, FunctionPurityState> states;  ///< 函数到纯度状态的映射
};

/// @brief 判断指令是否为纯函数调用（用于 LICM 候选判断）
/// @param inst 待检查的指令
/// @return true 表示该指令是纯函数调用且有返回值
bool isPureCall(Instruction * inst)
{
    auto * call = dynamic_cast<CallInst *>(inst);
    if (!call || !call->hasResultValue()) {
        return false;
    }

    FunctionPurity purity;
    return purity.isPure(call->getCallee());
}

/// @brief 判断循环体是否可能写入内存
///
/// 遍历循环体中的所有指令，若存在 store 指令或非纯函数调用，
/// 则认为循环可能修改内存，此时纯函数调用不可安全外提
/// （因为外提后可能改变调用与内存写入的相对顺序）。
/// @param loopBody 循环体基本块集合
/// @return true 表示循环可能写入内存
bool loopMayWriteMemory(const std::unordered_set<BasicBlock *> & loopBody)
{
    for (auto * bb : loopBody) {
        for (auto * inst : bb->getInstructions()) {
            if (!inst || inst->isDead()) {
                continue;
            }
            if (inst->mayWriteMemory()) {
                return true;
            }
            auto * call = dynamic_cast<CallInst *>(inst);
            if (call && !isPureCall(call)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

/// @brief 构造 LICM pass
/// @param _func 待优化的函数
LICM::LICM(Function * _func, Module * _mod) : func(_func), mod(_mod)
{}

/// @brief 对函数重复执行 LICM，直到本轮不再产生新的外提或 preheader 调整
/// @return 若函数 IR 被修改则返回 true
bool LICM::run()
{
    if (!func || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    bool changed = false;

    while (true) {
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
                             return loopInfo.getLoopDepth(lhs) > loopInfo.getLoopDepth(rhs);
                         });

        bool localChanged = false;
        for (auto * header : headers) {
            const auto * loopBody = loopInfo.getLoopBody(header);
            if (!loopBody || loopBody->empty()) {
                continue;
            }

            if (tryHoistLoop(header, *loopBody, domTree)) {
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

/// @brief 收集循环头所有来自循环外部的前驱块
/// @param header 循环头基本块
/// @param loopBody 当前自然循环的块集合
/// @return 所有位于循环外的前驱块列表
std::vector<BasicBlock *> LICM::collectOutsidePredecessors(
    BasicBlock * header,
    const std::unordered_set<BasicBlock *> & loopBody) const
{
    std::vector<BasicBlock *> outsidePreds;

    if (!header) {
        return outsidePreds;
    }

    for (auto * pred : header->getPredecessors()) {
        if (loopBody.find(pred) == loopBody.end()) {
            outsidePreds.push_back(pred);
        }
    }

    return outsidePreds;
}

/// @brief 判断现有循环外前驱是否已经形成可复用的 preheader
/// @param outsidePreds 循环头的循环外前驱列表
/// @return 若存在唯一且合法的 preheader 则返回该块，否则返回 nullptr
BasicBlock * LICM::getExistingPreheader(const std::vector<BasicBlock *> & outsidePreds) const
{
    if (outsidePreds.size() != 1) {
        return nullptr;
    }

    BasicBlock * pred = outsidePreds.front();
    if (!pred) {
        return nullptr;
    }

    if (pred->getSuccessors().size() != 1) {
        return nullptr;
    }

    return pred;
}

/// @brief 对单个自然循环执行外提
/// @param header 循环头基本块
/// @param loopBody 当前自然循环的块集合
/// @param domTree 当前函数的支配树
/// @return 若该循环被修改则返回 true
bool LICM::tryHoistLoop(BasicBlock * header,
                        const std::unordered_set<BasicBlock *> & loopBody,
                        const DominatorTree & domTree)
{
    if (!header || loopBody.empty()) {
        return false;
    }

    std::vector<BasicBlock *> outsidePreds = collectOutsidePredecessors(header, loopBody);
    if (outsidePreds.empty()) {
        return false;
    }

    BasicBlock * preheader = getExistingPreheader(outsidePreds);
    if (!preheader) {
        return createPreheader(header, outsidePreds);
    }

    std::unordered_set<Instruction *> invariants;
    std::vector<Instruction *> invariantOrder;

    bool discoveredNewInvariant = false;
    do {
        discoveredNewInvariant = false;

        for (auto * bb : func->getBlocks()) {
            if (loopBody.find(bb) == loopBody.end()) {
                continue;
            }

            for (auto * inst : bb->getInstructions()) {
                if (invariants.find(inst) != invariants.end()) {
                    continue;
                }

                if (!isHoistableInstruction(inst)) {
                    continue;
                }

                // 若候选指令是函数调用，且循环体可能写入内存，则不可外提
                // 因为外提后调用与内存写入的相对顺序可能改变，破坏语义
                if (dynamic_cast<CallInst *>(inst) && loopMayWriteMemory(loopBody)) {
                    continue;
                }

                if (!operandsAreLoopInvariant(inst, loopBody, invariants)) {
                    continue;
                }

                if (dynamic_cast<LoadInst *>(inst) && !isSafeLoadToHoist(inst, loopBody)) {
                    continue;
                }

                if (requiresExitDominance(inst) && !dominatesAllLoopExits(bb, loopBody, domTree)) {
                    continue;
                }

                if (!dominatesAllUses(inst, domTree)) {
                    continue;
                }

                invariants.insert(inst);
                invariantOrder.push_back(inst);
                discoveredNewInvariant = true;
            }
        }
    } while (discoveredNewInvariant);

    if (invariantOrder.empty()) {
        return false;
    }

    for (auto * inst : invariantOrder) {
        moveToPreheader(inst, preheader);
    }

    return true;
}

/// @brief 为循环头新建 preheader 并重写相关 phi 与 CFG 边
/// @param header 循环头基本块
/// @param outsidePreds 循环头的循环外前驱列表
/// @return 若成功创建并接入 preheader 则返回 true
bool LICM::createPreheader(BasicBlock * header, const std::vector<BasicBlock *> & outsidePreds)
{
    if (!header || outsidePreds.empty() || header == func->getEntryBlock()) {
        return false;
    }

    std::vector<HeaderPhiPlan> phiPlans;
    for (auto * inst : header->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }

        HeaderPhiPlan plan;
        plan.phi = phi;
        for (auto * pred : outsidePreds) {
            Value * incomingValue = nullptr;
            for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
                if (phi->getIncomingBlock(index) == pred) {
                    incomingValue = phi->getIncomingValue(index);
                    break;
                }
            }

            if (!incomingValue) {
                return false;
            }

            plan.outsideValues.push_back(incomingValue);
        }

        phiPlans.push_back(std::move(plan));
    }

    auto * preheader = func->newBasicBlock();
    insertBlockBefore(preheader, header);

    auto * branch = new BranchInst(func, header);
    preheader->addInstruction(branch);
    preheader->linkSuccessor(header);

    for (auto * pred : outsidePreds) {
        if (!rewriteTerminatorTarget(pred, header, preheader)) {
            return false;
        }

        pred->removeSuccessor(header);
        pred->addSuccessor(preheader);
        preheader->addPredecessor(pred);
        header->removePredecessor(pred);
    }

    if (outsidePreds.size() == 1) {
        for (auto & plan : phiPlans) {
            auto * phi = static_cast<PhiInst *>(plan.phi);
            phi->replaceIncomingBlock(outsidePreds.front(), preheader);
        }
        return true;
    }

    auto & preheaderInsts = preheader->getInstructions();
    auto insertPos = std::prev(preheaderInsts.end());

    for (auto & plan : phiPlans) {
        auto * headerPhi = static_cast<PhiInst *>(plan.phi);
        auto * preheaderPhi = new PhiInst(func, headerPhi->getType());
        for (std::size_t index = 0; index < outsidePreds.size(); ++index) {
            preheaderPhi->addIncoming(plan.outsideValues[index], outsidePreds[index]);
        }

        for (auto * pred : outsidePreds) {
            headerPhi->removeIncomingBlock(pred);
        }
        headerPhi->addIncoming(preheaderPhi, preheader);

        preheaderPhi->setParentBlock(preheader);
        preheaderInsts.insert(insertPos, preheaderPhi);
    }

    return true;
}

/// @brief 将前驱块终结指令中指向旧目标的边改写到新目标
/// @param pred 待改写的前驱块
/// @param oldTarget 旧跳转目标
/// @param newTarget 新跳转目标
/// @return 若成功改写至少一条 CFG 边则返回 true
bool LICM::rewriteTerminatorTarget(BasicBlock * pred, BasicBlock * oldTarget, BasicBlock * newTarget) const
{
    if (!pred || !oldTarget || !newTarget) {
        return false;
    }

    auto * terminator = pred->getTerminator();
    if (auto * branch = dynamic_cast<BranchInst *>(terminator)) {
        if (branch->getTarget() != oldTarget) {
            return false;
        }

        branch->setTarget(newTarget);
        return true;
    }

    if (auto * condBranch = dynamic_cast<CondBranchInst *>(terminator)) {
        bool rewritten = false;
        if (condBranch->getTrueDest() == oldTarget) {
            condBranch->setTrueDest(newTarget);
            rewritten = true;
        }
        if (condBranch->getFalseDest() == oldTarget) {
            condBranch->setFalseDest(newTarget);
            rewritten = true;
        }
        return rewritten;
    }

    return false;
}

/// @brief 将新建基本块插入到指定基本块之前
/// @param bb 待插入的基本块
/// @param before 作为插入锚点的基本块
void LICM::insertBlockBefore(BasicBlock * bb, BasicBlock * before) const
{
    if (!bb || !before) {
        return;
    }

    auto & blocks = func->getBlocks();
    auto bbPos = std::find(blocks.begin(), blocks.end(), bb);
    auto beforePos = std::find(blocks.begin(), blocks.end(), before);
    if (bbPos == blocks.end() || beforePos == blocks.end() || bbPos == beforePos) {
        return;
    }

    blocks.erase(bbPos);
    beforePos = std::find(blocks.begin(), blocks.end(), before);
    blocks.insert(beforePos, bb);
}

/// @brief 将一条循环不变量指令移动到 preheader 终结指令之前
/// @param inst 待移动的指令
/// @param preheader 目标 preheader 基本块
void LICM::moveToPreheader(Instruction * inst, BasicBlock * preheader) const
{
    if (!inst || !preheader) {
        return;
    }

    BasicBlock * fromBlock = inst->getParentBlock();
    if (!fromBlock || fromBlock == preheader) {
        return;
    }

    auto & fromInsts = fromBlock->getInstructions();
    auto instPos = std::find(fromInsts.begin(), fromInsts.end(), inst);
    if (instPos == fromInsts.end()) {
        return;
    }

    auto & preheaderInsts = preheader->getInstructions();
    auto insertPos = preheaderInsts.end();
    if (!preheaderInsts.empty()) {
        auto last = std::prev(preheaderInsts.end());
        if ((*last)->isTerminator()) {
            insertPos = last;
        }
    }

    preheaderInsts.splice(insertPos, fromInsts, instPos);
    inst->setParentBlock(preheader);
}

/// @brief 判断指令类型是否允许参与 LICM 候选
/// @param inst 待检查的指令
/// @return true 表示该指令属于可外提的纯计算指令
bool LICM::isHoistableInstruction(Instruction * inst) const
{
    if (!inst || inst->isTerminator() || !inst->hasResultValue() || inst->getUseList().empty()) {
        return false;
    }

    // 函数调用指令：仅当被调用函数是纯函数时才允许外提
    if (dynamic_cast<CallInst *>(inst)) {
        return isPureCall(inst);
    }

    if (dynamic_cast<LoadInst *>(inst)) {
        return true;
    }

    return isPureLoopInvariantOp(inst->getOp());
}

/// @brief 判断 load 是否满足安全外提条件
/// @param inst 待检查的 load 指令
/// @param loopBody 当前自然循环的块集合
/// @return true 表示该 load 不会被循环内写入改写
bool LICM::isSafeLoadToHoist(Instruction * inst, const std::unordered_set<BasicBlock *> & loopBody) const
{
    auto * load = dynamic_cast<LoadInst *>(inst);
    if (!load) {
        return false;
    }

    Value * pointer = load->getPointerOperand();
    if (auto * global = dynamic_cast<GlobalVariable *>(getPointerRoot(pointer))) {
        return isReadOnlyGlobal(mod, global) &&
               !blocksMayClobberLoad(pointer,
                                     loopBody,
                                     [](CallInst * call) { return call != nullptr && !isPureCall(call); });
    }

    MemoryLocation location = normalizeMemoryLocation(pointer);
    if (location.isPrecise() && !doesPointerEscape(location.object)) {
        return !blocksMayClobberLoad(pointer,
                                     loopBody,
                                     [](CallInst * call) { return call != nullptr && !isPureCall(call); });
    }

    return false;
}

/// @brief 判断候选指令是否需要额外满足退出点支配约束
/// @param inst 待检查的指令
/// @return true 表示该指令不可安全推测执行
bool LICM::requiresExitDominance(Instruction * inst) const
{
    if (!inst) {
        return false;
    }

    // 函数调用始终需要退出点支配，因为调用可能抛异常或产生副作用
    if (dynamic_cast<CallInst *>(inst)) {
        return true;
    }

    if (dynamic_cast<LoadInst *>(inst)) {
        return true;
    }

    return needsExitDominance(inst->getOp());
}

/// @brief 判断指令的全部操作数是否已经循环不变
/// @param inst 待检查的候选指令
/// @param loopBody 当前自然循环的块集合
/// @param invariants 已识别出的循环不变量集合
/// @return true 表示该指令的全部操作数均循环不变
bool LICM::operandsAreLoopInvariant(
    Instruction * inst,
    const std::unordered_set<BasicBlock *> & loopBody,
    const std::unordered_set<Instruction *> & invariants) const
{
    if (!inst) {
        return false;
    }

    for (auto * operand : inst->getOperandsValue()) {
        auto * operandInst = dynamic_cast<Instruction *>(operand);
        if (!operandInst) {
            continue;
        }

        BasicBlock * operandBlock = operandInst->getParentBlock();
        if (operandBlock && loopBody.find(operandBlock) != loopBody.end() &&
            invariants.find(operandInst) == invariants.end()) {
            return false;
        }
    }

    return true;
}

/// @brief 判断定义块是否支配当前循环的全部退出点
/// @param defBlock 候选指令所在基本块
/// @param loopBody 当前自然循环的块集合
/// @param domTree 当前函数的支配树
/// @return true 表示定义块支配所有循环退出点
bool LICM::dominatesAllLoopExits(BasicBlock * defBlock,
                                 const std::unordered_set<BasicBlock *> & loopBody,
                                 const DominatorTree & domTree) const
{
    if (!defBlock) {
        return false;
    }

    for (auto * bb : loopBody) {
        for (auto * succ : bb->getSuccessors()) {
            if (loopBody.find(succ) != loopBody.end()) {
                continue;
            }

            if (!domTree.dominates(defBlock, succ)) {
                return false;
            }
        }
    }

    return true;
}

/// @brief 判断候选指令是否支配其全部使用点
/// @param inst 待检查的候选指令
/// @param domTree 当前函数的支配树
/// @return true 表示该指令支配所有普通 use 与 phi incoming use
bool LICM::dominatesAllUses(Instruction * inst, const DominatorTree & domTree) const
{
    if (!inst) {
        return false;
    }

    BasicBlock * defBlock = inst->getParentBlock();
    if (!defBlock) {
        return false;
    }

    for (auto * use : inst->getUseList()) {
        auto * user = use->getUser();
        auto * userInst = dynamic_cast<Instruction *>(user);
        if (!userInst) {
            return false;
        }

        if (auto * phi = dynamic_cast<PhiInst *>(userInst)) {
            for (int32_t index = 0; index < phi->getIncomingCount(); ++index) {
                if (phi->getIncomingValue(index) != inst) {
                    continue;
                }

                if (!domTree.dominates(defBlock, phi->getIncomingBlock(index))) {
                    return false;
                }
            }
            continue;
        }

        BasicBlock * useBlock = userInst->getParentBlock();
        if (!useBlock || !domTree.dominates(defBlock, useBlock)) {
            return false;
        }
    }

    return true;
}
