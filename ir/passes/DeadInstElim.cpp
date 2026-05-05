///
/// @file DeadInstElim.cpp
/// @brief 死指令删除 pass 实现
///

#include "DeadInstElim.h"

#include <vector>

#include "BasicBlock.h"
#include "Function.h"
#include "Instruction.h"
#include "Value.h"

namespace {

/// @brief 判断一条指令是否为可安全删除的平凡死代码
/// @param inst 待判断的指令
/// @return true 表示该指令无 use、无副作用且可直接删除
bool isTriviallyDead(Instruction * inst)
{
    if (!inst || inst->isDead() || inst->isTerminator() || !inst->hasResultValue() ||
        !inst->getUseList().empty()) {
        return false;
    }

    return !inst->mayReadMemory() && !inst->mayHaveSideEffects();
}

/// @brief 从所有基本块中清扫已标记为 dead 的指令
/// @param func 待清扫的函数
/// @return 被真正从指令表中移除的死指令数量
int32_t sweepDeadInstructions(Function * func)
{
    if (!func) {
        return 0;
    }

    int32_t removedCount = 0;
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
            ++removedCount;
        }
    }

    return removedCount;
}

} // namespace

/// @brief 构造死指令删除器
/// @param _func 待优化的函数
DeadInstElim::DeadInstElim(Function * _func) : func(_func)
{}

/// @brief 执行死指令删除
/// @return 若 IR 被修改则返回 true
bool DeadInstElim::run()
{
    if (!func || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    std::vector<Instruction *> instWorklist;
    for (auto * bb : func->getBlocks()) {
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

    return sweepDeadInstructions(func) > 0;
}