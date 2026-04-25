///
/// @file DCE.cpp
/// @brief 死代码消除 pass 实现
///
/// 当前实现包含两部分：
///   1. 删除从入口基本块不可达的基本块，并同步清理 CFG 与 phi incoming
///   2. 对无副作用且结果未被使用的纯指令做保守 dead instruction elimination
///

#include "DCE.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "BasicBlock.h"
#include "Function.h"
#include "Instruction.h"
#include "PhiInst.h"

namespace {

void removePhiIncomingFromDeadPred(BasicBlock * bb, BasicBlock * deadPred)
{
    for (auto * inst : bb->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }

        phi->removeIncomingBlock(deadPred);
    }
}

bool isTriviallyDead(Instruction * inst)
{
    if (!inst || inst->isDead() || inst->isTerminator() || !inst->hasResultValue() ||
        !inst->getUseList().empty()) {
        return false;
    }

    // 这里仅删除确定无副作用的纯值指令。
    // load/call/store/branch/ret 这类涉及内存或控制流的指令，先保守保留。
    switch (inst->getOp()) {
        case IRInstOperator::IRINST_OP_ALLOCA:
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
        case IRInstOperator::IRINST_OP_PHI:
        case IRInstOperator::IRINST_OP_ZEXT:
        case IRInstOperator::IRINST_OP_COPY:
            return true;

        default:
            return false;
    }
}

void sweepDeadInstructions(Function * func)
{
    for (auto * bb : func->getBlocks()) {
        auto & insts = bb->getInstructions();
        auto it = insts.begin();
        while (it != insts.end()) {
            Instruction * inst = *it;
            if (!inst->isDead()) {
                ++it;
                continue;
            }

            auto next = std::next(it);
            insts.erase(it);
            delete inst;
            it = next;
        }
    }
}

} // namespace

DCE::DCE(Function * _func) : func(_func)
{}

void DCE::run()
{
    if (!func || func->isBuiltin() || func->getBlocks().empty()) {
        return;
    }

    BasicBlock * entry = func->getEntryBlock();
    if (!entry) {
        return;
    }

    std::unordered_set<BasicBlock *> reachable;
    std::vector<BasicBlock *> worklist;
    reachable.insert(entry);
    worklist.push_back(entry);

    while (!worklist.empty()) {
        BasicBlock * bb = worklist.back();
        worklist.pop_back();

        for (auto * succ : bb->getSuccessors()) {
            if (succ && reachable.insert(succ).second) {
                worklist.push_back(succ);
            }
        }
    }

    auto & blocks = func->getBlocks();
    std::vector<BasicBlock *> deadBlocks;
    deadBlocks.reserve(blocks.size());
    for (auto * bb : blocks) {
        if (!reachable.count(bb)) {
            deadBlocks.push_back(bb);
        }
    }

    if (!deadBlocks.empty()) {
        for (auto * deadBB : deadBlocks) {
            for (auto * pred : deadBB->getPredecessors()) {
                if (reachable.count(pred)) {
                    pred->removeSuccessor(deadBB);
                }
            }

            for (auto * succ : deadBB->getSuccessors()) {
                if (!reachable.count(succ)) {
                    continue;
                }

                succ->removePredecessor(deadBB);
                removePhiIncomingFromDeadPred(succ, deadBB);
            }
        }

        // 先拆掉死块内所有指令的 use-def 边，再统一释放块对象
        for (auto * deadBB : deadBlocks) {
            for (auto * inst : deadBB->getInstructions()) {
                inst->clearOperands();
            }
        }

        blocks.erase(std::remove_if(blocks.begin(),
                                    blocks.end(),
                                    [&](BasicBlock * bb) {
                                        return !reachable.count(bb);
                                    }),
                     blocks.end());

        for (auto * deadBB : deadBlocks) {
            delete deadBB;
        }
    }

    std::vector<Instruction *> instWorklist;
    for (auto * bb : blocks) {
        for (auto * inst : bb->getInstructions()) {
            if (isTriviallyDead(inst)) {
                instWorklist.push_back(inst);
            }
        }
    }

    while (!instWorklist.empty()) {
        Instruction * inst = instWorklist.back();
        instWorklist.pop_back();

        if (!isTriviallyDead(inst)) {
            continue;
        }

        std::vector<Value *> operands = inst->getOperandsValue();
        inst->clearOperands();
        inst->setDead(true);

        for (auto * operand : operands) {
            auto * operandInst = dynamic_cast<Instruction *>(operand);
            if (isTriviallyDead(operandInst)) {
                instWorklist.push_back(operandInst);
            }
        }
    }

    sweepDeadInstructions(func);
}