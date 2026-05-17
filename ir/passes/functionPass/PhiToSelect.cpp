///
/// @file PhiToSelect.cpp
/// @brief 将简单分支合流处的 phi 转换为 select
///

#include "PhiToSelect.h"

#include <algorithm>
#include <string>
#include <vector>

#include "BasicBlock.h"
#include "BranchInst.h"
#include "CondBranchInst.h"
#include "ConstFloat.h"
#include "ConstInteger.h"
#include "DominatorTree.h"
#include "Function.h"
#include "Instruction.h"
#include "PhiInst.h"
#include "SelectInst.h"
#include "Value.h"

namespace {

/// @brief 一次 select 匹配的结果
struct SelectMatch {
    Value * condition = nullptr;
    Value * trueValue = nullptr;
    Value * falseValue = nullptr;
};

/// @brief 判断一条指令是否适合在 select 提升过程中被提前
/// @param inst 待检查的指令
/// @return true 表示该指令是允许被提前的纯计算
bool isHoistableForSelect(Instruction * inst)
{
    if (!inst || inst->isTerminator()) {
        return false;
    }

    switch (inst->getOp()) {
        case IRInstOperator::IRINST_OP_ADD_I:
        case IRInstOperator::IRINST_OP_SUB_I:
        case IRInstOperator::IRINST_OP_MUL_I:
        case IRInstOperator::IRINST_OP_LT_I:
        case IRInstOperator::IRINST_OP_GT_I:
        case IRInstOperator::IRINST_OP_LE_I:
        case IRInstOperator::IRINST_OP_GE_I:
        case IRInstOperator::IRINST_OP_EQ_I:
        case IRInstOperator::IRINST_OP_NE_I:
        case IRInstOperator::IRINST_OP_ADD_F:
        case IRInstOperator::IRINST_OP_SUB_F:
        case IRInstOperator::IRINST_OP_MUL_F:
        case IRInstOperator::IRINST_OP_LT_F:
        case IRInstOperator::IRINST_OP_GT_F:
        case IRInstOperator::IRINST_OP_LE_F:
        case IRInstOperator::IRINST_OP_GE_F:
        case IRInstOperator::IRINST_OP_EQ_F:
        case IRInstOperator::IRINST_OP_NE_F:
        case IRInstOperator::IRINST_OP_ZEXT:
        case IRInstOperator::IRINST_OP_SITOFP:
        case IRInstOperator::IRINST_OP_FPTOSI:
            return true;

        default:
            return false;
    }
}

/// @brief 判断两个 Value 是否可视为等价
/// @param lhs 左值
/// @param rhs 右值
/// @return true 表示两者在当前匹配中可当作同一个值
bool isValueEquivalent(Value * lhs, Value * rhs)
{
    if (lhs == rhs) {
        return true;
    }

    auto * lhsInt = dynamic_cast<ConstInteger *>(lhs);
    auto * rhsInt = dynamic_cast<ConstInteger *>(rhs);
    if (lhsInt && rhsInt) {
        return lhsInt->getVal() == rhsInt->getVal() &&
               lhsInt->getType()->toString() == rhsInt->getType()->toString();
    }

    auto * lhsFloat = dynamic_cast<ConstFloat *>(lhs);
    auto * rhsFloat = dynamic_cast<ConstFloat *>(rhs);
    if (lhsFloat && rhsFloat) {
        return lhsFloat->getBitPattern() == rhsFloat->getBitPattern();
    }

    return false;
}

/// @brief 判断两条指令是否在语义上等价
/// @param lhs 左侧指令
/// @param rhs 右侧指令
/// @return true 表示两条指令可被视为相同前缀
bool isInstructionEquivalent(Instruction * lhs, Instruction * rhs)
{
    if (!lhs || !rhs || lhs->getOp() != rhs->getOp() || lhs->getOperandsNum() != rhs->getOperandsNum()) {
        return false;
    }

    if (lhs->getType()->toString() != rhs->getType()->toString()) {
        return false;
    }

    for (int32_t index = 0; index < lhs->getOperandsNum(); ++index) {
        if (!isValueEquivalent(lhs->getOperand(index), rhs->getOperand(index))) {
            return false;
        }
    }

    return true;
}

/// @brief 统计基本块中非终结指令的数量
/// @param block 目标基本块
/// @return 非终结指令个数
int32_t getNonTerminatorInstructionCount(BasicBlock * block)
{
    if (!block) {
        return 0;
    }

    int32_t count = 0;
    for (auto * inst : block->getInstructions()) {
        if (!inst->isTerminator()) {
            ++count;
        }
    }
    return count;
}

/// @brief 获取基本块中唯一的非终结指令
/// @param block 目标基本块
/// @return 若且仅若存在唯一一条非终结指令则返回该指令
Instruction * getSingleNonTerminatorInstruction(BasicBlock * block)
{
    if (!block) {
        return nullptr;
    }

    Instruction * onlyInst = nullptr;
    for (auto * inst : block->getInstructions()) {
        if (inst->isTerminator()) {
            continue;
        }

        if (onlyInst != nullptr) {
            return nullptr;
        }
        onlyInst = inst;
    }

    return onlyInst;
}

/// @brief 获取基本块中第一条非终结指令
/// @param block 目标基本块
/// @return 第一条非终结指令，不存在时返回空指针
Instruction * getFirstNonTerminatorInstruction(BasicBlock * block)
{
    if (!block) {
        return nullptr;
    }

    for (auto * inst : block->getInstructions()) {
        if (!inst->isTerminator()) {
            return inst;
        }
    }

    return nullptr;
}

/// @brief 判断一个值在目标块终结指令前是否已经可用
/// @param value 待检查的值
/// @param block 目标基本块
/// @param domTree 当前函数的支配树
/// @return true 表示该值可直接用于目标块末尾
bool isValueAvailableBeforeTerminator(Value * value, BasicBlock * block, const DominatorTree & domTree)
{
    if (!value || !block) {
        return false;
    }

    auto * inst = dynamic_cast<Instruction *>(value);
    if (!inst) {
        return true;
    }

    BasicBlock * parent = inst->getParentBlock();
    if (!parent) {
        return false;
    }

    return parent == block || domTree.dominates(parent, block);
}

/// @brief 判断一条指令能否安全提前到目标块终结指令前
/// @param inst 待提前的指令
/// @param targetBlock 目标基本块
/// @param domTree 当前函数的支配树
/// @return true 表示提前后仍满足 SSA 可用性约束
bool canHoistInstructionBeforeTerminator(Instruction * inst, BasicBlock * targetBlock, const DominatorTree & domTree)
{
    if (!isHoistableForSelect(inst) || !targetBlock) {
        return false;
    }

    for (Value * operand : inst->getOperandsValue()) {
        if (!isValueAvailableBeforeTerminator(operand, targetBlock, domTree)) {
            return false;
        }
    }

    return true;
}

/// @brief 将一条指令移动到目标块终结指令之前
/// @param inst 待移动的指令
/// @param targetBlock 目标基本块
void moveInstructionBeforeTerminator(Instruction * inst, BasicBlock * targetBlock)
{
    if (!inst || !targetBlock) {
        return;
    }

    BasicBlock * sourceBlock = inst->getParentBlock();
    if (!sourceBlock || sourceBlock == targetBlock) {
        return;
    }

    auto & sourceInsts = sourceBlock->getInstructions();
    auto & targetInsts = targetBlock->getInstructions();
    auto instIt = std::find(sourceInsts.begin(), sourceInsts.end(), inst);
    auto termIt = targetInsts.end();
    if (!targetInsts.empty()) {
        auto lastIt = std::prev(targetInsts.end());
        if ((*lastIt)->isTerminator()) {
            termIt = lastIt;
        }
    }

    if (instIt == sourceInsts.end()) {
        return;
    }

    targetInsts.splice(termIt, sourceInsts, instIt);
    inst->setParentBlock(targetBlock);
}

/// @brief 判断一个基本块是否仅通过无条件跳转直接流向另一个块
/// @param from 起始块
/// @param to 目标块
/// @return true 表示 from 只有一条到 to 的直接边
bool branchesDirectlyTo(BasicBlock * from, BasicBlock * to)
{
    if (!from || !to) {
        return false;
    }

    auto * branch = dynamic_cast<BranchInst *>(from->getTerminator());
    return branch != nullptr && branch->getTarget() == to && from->getSuccessors().size() == 1 &&
           from->getSuccessors().front() == to;
}

/// @brief 判断一个值在 merge 块入口是否可直接使用
/// @param value 待检查的值
/// @param block merge 块
/// @param domTree 当前函数的支配树
/// @return true 表示该值在 merge 块入口已经可用
bool isValueAvailableAtBlock(Value * value, BasicBlock * block, const DominatorTree & domTree)
{
    if (!value || !block) {
        return false;
    }

    auto * inst = dynamic_cast<Instruction *>(value);
    if (!inst) {
        return true;
    }

    BasicBlock * parent = inst->getParentBlock();
    if (!parent || parent == block) {
        return false;
    }

    return domTree.dominates(parent, block);
}

/// @brief 提出两个空中间块开头相同的可安全计算前缀
/// @param merge merge 块
/// @param domTree 当前函数的支配树
/// @return true 表示至少成功提出一条公共前缀
bool hoistIdenticalPrefix(BasicBlock * merge, const DominatorTree & domTree)
{
    if (!merge || merge->getPredecessors().size() != 2) {
        return false;
    }

    BasicBlock * firstPred = merge->getPredecessors()[0];
    BasicBlock * secondPred = merge->getPredecessors()[1];
    if (!branchesDirectlyTo(firstPred, merge) || !branchesDirectlyTo(secondPred, merge)) {
        return false;
    }

    const auto & firstIncomingPreds = firstPred->getPredecessors();
    const auto & secondIncomingPreds = secondPred->getPredecessors();
    if (firstIncomingPreds.size() != 1 || secondIncomingPreds.size() != 1 ||
        firstIncomingPreds.front() != secondIncomingPreds.front()) {
        return false;
    }

    auto * commonPred = firstIncomingPreds.front();
    if (dynamic_cast<CondBranchInst *>(commonPred->getTerminator()) == nullptr) {
        return false;
    }

    bool changed = false;
    while (true) {
        auto * firstInst = getFirstNonTerminatorInstruction(firstPred);
        auto * secondInst = getFirstNonTerminatorInstruction(secondPred);
        if (!firstInst || !secondInst || !isHoistableForSelect(firstInst) ||
            !isInstructionEquivalent(firstInst, secondInst) ||
            !canHoistInstructionBeforeTerminator(firstInst, commonPred, domTree)) {
            break;
        }

        moveInstructionBeforeTerminator(firstInst, commonPred);
        secondInst->replaceAllUseWith(firstInst);
        secondInst->clearOperands();
        auto & secondInsts = secondPred->getInstructions();
        auto secondIt = std::find(secondInsts.begin(), secondInsts.end(), secondInst);
        if (secondIt != secondInsts.end()) {
            secondInsts.erase(secondIt);
        }
        delete secondInst;
        changed = true;
    }

    return changed;
}

/// @brief 在完成匹配前统一检查条件与候选值的可用性
/// @param condition 条件值
/// @param trueValue 真分支值
/// @param falseValue 假分支值
/// @param merge merge 块
/// @param domTree 当前函数的支配树
/// @param match 输出的匹配结果
/// @return true 表示该 select 匹配在 SSA 上合法
bool finalizeMatch(Value * condition,
                   Value * trueValue,
                   Value * falseValue,
                   BasicBlock * merge,
                   const DominatorTree & domTree,
                   SelectMatch & match)
{
    if (!isValueAvailableAtBlock(condition, merge, domTree) ||
        !isValueAvailableAtBlock(trueValue, merge, domTree) ||
        !isValueAvailableAtBlock(falseValue, merge, domTree)) {
        return false;
    }

    match = {condition, trueValue, falseValue};
    return true;
}

/// @brief 匹配一侧直达 merge、另一侧经单块落入 merge 的三角形结构
/// @param merge merge 块
/// @param branchIncoming 直接来自分支块的 incoming
/// @param nestedIncoming 来自嵌套块的 incoming
/// @param domTree 当前函数的支配树
/// @param match 输出的匹配结果
/// @return true 表示该 phi 可被转为 select
bool tryMatchTriangle(BasicBlock * merge,
                      const PhiInst::Incoming & branchIncoming,
                      const PhiInst::Incoming & nestedIncoming,
                      const DominatorTree & domTree,
                      SelectMatch & match)
{
    if (!merge || !branchIncoming.block || !nestedIncoming.block) {
        return false;
    }

    auto * condBr = dynamic_cast<CondBranchInst *>(branchIncoming.block->getTerminator());
    if (!condBr || !branchesDirectlyTo(nestedIncoming.block, merge)) {
        return false;
    }

    const auto & nestedPreds = nestedIncoming.block->getPredecessors();
    if (nestedPreds.size() != 1 || nestedPreds.front() != branchIncoming.block) {
        return false;
    }

    if (!isValueAvailableAtBlock(nestedIncoming.value, merge, domTree)) {
        auto * hoistInst = dynamic_cast<Instruction *>(nestedIncoming.value);
        if (hoistInst == nullptr || hoistInst->getParentBlock() != nestedIncoming.block ||
            getNonTerminatorInstructionCount(nestedIncoming.block) != 1 ||
            !canHoistInstructionBeforeTerminator(hoistInst, branchIncoming.block, domTree)) {
            return false;
        }

        moveInstructionBeforeTerminator(hoistInst, branchIncoming.block);
    }

    if (condBr->getTrueDest() == merge && condBr->getFalseDest() == nestedIncoming.block) {
        return finalizeMatch(condBr->getCondition(),
                             branchIncoming.value,
                             nestedIncoming.value,
                             merge,
                             domTree,
                             match);
    }

    if (condBr->getTrueDest() == nestedIncoming.block && condBr->getFalseDest() == merge) {
        return finalizeMatch(condBr->getCondition(),
                             nestedIncoming.value,
                             branchIncoming.value,
                             merge,
                             domTree,
                             match);
    }

    return false;
}

/// @brief 匹配标准 if-then-else 钻石结构上的二路 phi
/// @param merge merge 块
/// @param first 第一个 incoming
/// @param second 第二个 incoming
/// @param domTree 当前函数的支配树
/// @param match 输出的匹配结果
/// @return true 表示该 phi 可被转为 select
bool tryMatchDiamond(BasicBlock * merge,
                     const PhiInst::Incoming & first,
                     const PhiInst::Incoming & second,
                     const DominatorTree & domTree,
                     SelectMatch & match)
{
    if (!merge || !first.block || !second.block) {
        return false;
    }

    if (!branchesDirectlyTo(first.block, merge) || !branchesDirectlyTo(second.block, merge)) {
        return false;
    }

    const auto & firstPreds = first.block->getPredecessors();
    const auto & secondPreds = second.block->getPredecessors();
    if (firstPreds.size() != 1 || secondPreds.size() != 1 || firstPreds.front() != secondPreds.front()) {
        return false;
    }

    auto * condBr = dynamic_cast<CondBranchInst *>(firstPreds.front()->getTerminator());
    if (!condBr) {
        return false;
    }

    if (condBr->getTrueDest() == first.block && condBr->getFalseDest() == second.block) {
        return finalizeMatch(condBr->getCondition(), first.value, second.value, merge, domTree, match);
    }

    if (condBr->getTrueDest() == second.block && condBr->getFalseDest() == first.block) {
        return finalizeMatch(condBr->getCondition(), second.value, first.value, merge, domTree, match);
    }

    return false;
}

/// @brief 判断一个基本块是否仍以 phi 开头
/// @param block 目标基本块
/// @return true 表示块首仍存在 phi
bool hasLeadingPhi(BasicBlock * block)
{
    if (!block) {
        return false;
    }

    for (auto * inst : block->getInstructions()) {
        return dynamic_cast<PhiInst *>(inst) != nullptr;
    }

    return false;
}

/// @brief 从函数块列表中移除并销毁一个基本块
/// @param func 所属函数
/// @param block 待删除基本块
void eraseBlock(Function * func, BasicBlock * block)
{
    if (!func || !block) {
        return;
    }

    auto & blocks = func->getBlocks();
    auto blockIt = std::find(blocks.begin(), blocks.end(), block);
    if (blockIt == blocks.end()) {
        return;
    }

    blocks.erase(blockIt);
    delete block;
}

/// @brief 将条件跳转原地改写为无条件跳转
/// @param block 待改写的基本块
/// @param target 新的跳转目标
/// @return true 表示改写成功
bool replaceCondBranchWithGoto(BasicBlock * block, BasicBlock * target)
{
    if (!block || !target) {
        return false;
    }

    auto & insts = block->getInstructions();
    auto termIt = insts.end();
    if (!insts.empty()) {
        auto lastIt = std::prev(insts.end());
        if ((*lastIt)->isTerminator()) {
            termIt = lastIt;
        }
    }

    if (termIt == insts.end()) {
        return false;
    }

    auto * condBr = dynamic_cast<CondBranchInst *>(*termIt);
    if (!condBr) {
        return false;
    }

    auto * branch = new BranchInst(block->getParent(), target);
    branch->setParentBlock(block);
    condBr->clearOperands();
    delete condBr;
    *termIt = branch;
    return true;
}

/// @brief 在 merge 块的 phi 全部消失后尝试清理空的中间分支块
/// @param merge 目标 merge 块
/// @return true 表示成功收缩了一层冗余 CFG
bool tryCollapseMerge(BasicBlock * merge)
{
    if (!merge || hasLeadingPhi(merge)) {
        return false;
    }

    Function * func = merge->getParent();
    if (!func) {
        return false;
    }

    auto & preds = merge->getPredecessors();
    if (preds.size() != 2) {
        return false;
    }

    BasicBlock * first = preds[0];
    BasicBlock * second = preds[1];

    if (branchesDirectlyTo(first, merge) && branchesDirectlyTo(second, merge)) {
        const auto & firstPreds = first->getPredecessors();
        const auto & secondPreds = second->getPredecessors();
        if (firstPreds.size() == 1 && secondPreds.size() == 1 && firstPreds.front() == secondPreds.front()) {
            auto * commonPred = firstPreds.front();
            auto * condBr = dynamic_cast<CondBranchInst *>(commonPred->getTerminator());
            if (condBr != nullptr &&
                ((condBr->getTrueDest() == first && condBr->getFalseDest() == second) ||
                 (condBr->getTrueDest() == second && condBr->getFalseDest() == first)) &&
                replaceCondBranchWithGoto(commonPred, merge)) {
                commonPred->removeSuccessor(first);
                commonPred->removeSuccessor(second);
                commonPred->addSuccessor(merge);
                merge->removePredecessor(first);
                merge->removePredecessor(second);
                merge->addPredecessor(commonPred);
                first->removePredecessor(commonPred);
                second->removePredecessor(commonPred);
                eraseBlock(func, first);
                eraseBlock(func, second);
                return true;
            }
        }
    }

    for (BasicBlock * commonPred : preds) {
        auto * condBr = dynamic_cast<CondBranchInst *>(commonPred ? commonPred->getTerminator() : nullptr);
        if (!condBr) {
            continue;
        }

        BasicBlock * nested = nullptr;
        if (condBr->getTrueDest() == merge && condBr->getFalseDest() != merge) {
            nested = condBr->getFalseDest();
        } else if (condBr->getFalseDest() == merge && condBr->getTrueDest() != merge) {
            nested = condBr->getTrueDest();
        }

        if (!nested || nested == merge || !branchesDirectlyTo(nested, merge)) {
            continue;
        }

        const auto & nestedPreds = nested->getPredecessors();
        if (nestedPreds.size() != 1 || nestedPreds.front() != commonPred) {
            continue;
        }

        if (!replaceCondBranchWithGoto(commonPred, merge)) {
            continue;
        }

        commonPred->removeSuccessor(nested);
        commonPred->addSuccessor(merge);
        merge->removePredecessor(nested);
        merge->addPredecessor(commonPred);
        nested->removePredecessor(commonPred);
        eraseBlock(func, nested);
        return true;
    }

    return false;
}

/// @brief 尝试将一个二路 phi 识别为可安全替换的 select
/// @param phi 待匹配的 phi
/// @param domTree 当前函数的支配树
/// @param match 输出的匹配结果
/// @return true 表示匹配成功
bool tryMatchPhi(PhiInst * phi, const DominatorTree & domTree, SelectMatch & match)
{
    if (!phi || phi->getIncomingCount() != 2) {
        return false;
    }

    BasicBlock * merge = phi->getParentBlock();
    if (!merge || merge->getPredecessors().size() != 2) {
        return false;
    }

    PhiInst::Incoming first = phi->getIncoming(0);
    PhiInst::Incoming second = phi->getIncoming(1);
    if (first.block == second.block) {
        return false;
    }

    return tryMatchTriangle(merge, first, second, domTree, match) ||
           tryMatchTriangle(merge, second, first, domTree, match) ||
           tryMatchDiamond(merge, first, second, domTree, match);
}

} // namespace

/// @brief 构造一个 phi-to-select 转换器
/// @param _func 待处理的函数
PhiToSelect::PhiToSelect(Function * _func) : func(_func)
{}

/// @brief 对当前函数执行 phi 到 select 的提升
/// @return true 表示函数 IR 被修改过
bool PhiToSelect::run()
{
    if (!func || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    bool changed = false;

    bool localChanged = false;
    do {
        localChanged = false;
        DominatorTree domTree(func);

        for (auto * bb : func->getBlocks()) {
            if (hoistIdenticalPrefix(bb, domTree)) {
                localChanged = true;
            }
        }

        for (auto * bb : func->getBlocks()) {
            auto & insts = bb->getInstructions();
            std::vector<PhiInst *> phis;
            for (auto * inst : insts) {
                auto * phi = dynamic_cast<PhiInst *>(inst);
                if (!phi) {
                    break;
                }
                phis.push_back(phi);
            }

            if (phis.empty()) {
                continue;
            }

            auto insertPos = std::find_if(insts.begin(), insts.end(), [](Instruction * inst) {
                return dynamic_cast<PhiInst *>(inst) == nullptr;
            });

            bool blockChanged = false;
            for (auto * phi : phis) {
                SelectMatch match;
                if (!tryMatchPhi(phi, domTree, match)) {
                    continue;
                }

                auto * select = new SelectInst(func, match.condition, match.trueValue, match.falseValue, phi->getType());
                select->setParentBlock(bb);
                insts.insert(insertPos, select);

                phi->replaceAllUseWith(select);
                phi->clearOperands();

                auto phiPos = std::find(insts.begin(), insts.end(), static_cast<Instruction *>(phi));
                if (phiPos != insts.end()) {
                    insts.erase(phiPos);
                }
                delete phi;
                blockChanged = true;
            }

            if (!blockChanged) {
                continue;
            }

            localChanged = true;
            if (tryCollapseMerge(bb)) {
                break;
            }
        }

        changed = changed || localChanged;
    } while (localChanged);

    return changed;
}