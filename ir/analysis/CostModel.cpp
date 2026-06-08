///
/// @file CostModel.cpp
/// @brief 轻量成本模型(mini-TTI)实现。
///

#include "CostModel.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "BasicBlock.h"
#include "Instruction.h"
#include "Type.h"

namespace CostModel {

bool profitabilityEnabled()
{
    // 置 MINIC_DISABLE_PROFITABILITY 时退化为 no-op，等同未加收益门的历史行为。
    static const bool enabled = (std::getenv("MINIC_DISABLE_PROFITABILITY") == nullptr);
    return enabled;
}

bool remarksEnabled()
{
    static const bool on = (std::getenv("MINIC_OPT_REMARKS") != nullptr);
    return on;
}

void remark(const char * pass, bool applied, const char * reason)
{
    if (!remarksEnabled()) {
        return;
    }
    std::fprintf(stderr, "[opt-remark] %-10s %-8s %s\n", pass ? pass : "?", applied ? "applied" : "skipped",
                 reason ? reason : "");
}

int instCost(Instruction * inst)
{
    if (inst == nullptr) {
        return 0;
    }

    switch (inst->getOp()) {
        // 廉价整型运算/比较/数据搬运
        case IRInstOperator::IRINST_OP_ADD_I:
        case IRInstOperator::IRINST_OP_SUB_I:
        case IRInstOperator::IRINST_OP_LT_I:
        case IRInstOperator::IRINST_OP_GT_I:
        case IRInstOperator::IRINST_OP_LE_I:
        case IRInstOperator::IRINST_OP_GE_I:
        case IRInstOperator::IRINST_OP_EQ_I:
        case IRInstOperator::IRINST_OP_NE_I:
        case IRInstOperator::IRINST_OP_ZEXT:
        case IRInstOperator::IRINST_OP_COPY:
        case IRInstOperator::IRINST_OP_GEP:
        case IRInstOperator::IRINST_OP_ALLOCA:
            return 1;

        case IRInstOperator::IRINST_OP_BR:
        case IRInstOperator::IRINST_OP_COND_BR:
        case IRInstOperator::IRINST_OP_RET:
            return 1;

        case IRInstOperator::IRINST_OP_PHI:
            return 0;

        case IRInstOperator::IRINST_OP_SELECT:
            return 2;

        // 访存
        case IRInstOperator::IRINST_OP_STORE:
            return 2;
        case IRInstOperator::IRINST_OP_LOAD:
            return 3;

        // 整型乘法/类型转换
        case IRInstOperator::IRINST_OP_MUL_I:
            return 3;
        case IRInstOperator::IRINST_OP_SITOFP:
        case IRInstOperator::IRINST_OP_FPTOSI:
            return 4;

        // 浮点运算/比较
        case IRInstOperator::IRINST_OP_ADD_F:
        case IRInstOperator::IRINST_OP_SUB_F:
        case IRInstOperator::IRINST_OP_LT_F:
        case IRInstOperator::IRINST_OP_GT_F:
        case IRInstOperator::IRINST_OP_LE_F:
        case IRInstOperator::IRINST_OP_GE_F:
        case IRInstOperator::IRINST_OP_EQ_F:
        case IRInstOperator::IRINST_OP_NE_F:
            return 4;
        case IRInstOperator::IRINST_OP_MUL_F:
            return 5;

        // 除法/取模(很贵)
        case IRInstOperator::IRINST_OP_DIV_I:
        case IRInstOperator::IRINST_OP_MOD_I:
            return 12;
        case IRInstOperator::IRINST_OP_DIV_F:
            return 18;

        // 调用
        case IRInstOperator::IRINST_OP_CALL:
            return 20;

        // RVV 向量指令
        case IRInstOperator::IRINST_OP_VSETVL:
        case IRInstOperator::IRINST_OP_VEXTRACT:
            return 1;
        case IRInstOperator::IRINST_OP_VSPLAT:
            return 2;
        case IRInstOperator::IRINST_OP_VLOAD:
        case IRInstOperator::IRINST_OP_VSTORE:
            return 3;
        case IRInstOperator::IRINST_OP_VBINARY:
            return 4;
        case IRInstOperator::IRINST_OP_VREDUCE:
            return 6;

        default:
            return 1;
    }
}

long blockCost(const BasicBlock * bb)
{
    if (bb == nullptr) {
        return 0;
    }
    long total = 0;
    for (auto * inst : bb->getInstructions()) {
        total += instCost(inst);
    }
    return total;
}

long loopBodyCost(const std::unordered_set<BasicBlock *> & body)
{
    long total = 0;
    for (auto * bb : body) {
        total += blockCost(bb);
    }
    return total;
}

static void accumulatePressure(Instruction * inst, RegPressure & rp)
{
    if (inst == nullptr || !inst->hasResultValue()) {
        return;
    }
    Type * ty = inst->getType();
    if (ty != nullptr && ty->isFloatType()) {
        ++rp.fpr;
    } else {
        ++rp.gpr;
    }
}

RegPressure estimateRegPressure(const std::vector<Instruction *> & insts)
{
    RegPressure rp;
    for (auto * inst : insts) {
        accumulatePressure(inst, rp);
    }
    return rp;
}

RegPressure estimateBodyRegPressure(const std::unordered_set<BasicBlock *> & body)
{
    RegPressure rp;
    for (auto * bb : body) {
        if (bb == nullptr) {
            continue;
        }
        for (auto * inst : bb->getInstructions()) {
            accumulatePressure(inst, rp);
        }
    }
    return rp;
}

int usableGPR()
{
    const int usable = kAllocatableGPR - kRegSafetyMargin;
    return usable > 1 ? usable : 1;
}

int usableFPR()
{
    const int usable = kAllocatableFPR - kRegSafetyMargin;
    return usable > 1 ? usable : 1;
}

double loopDepthWeight(int depth)
{
    if (depth < 0) {
        depth = 0;
    }
    if (depth > 6) {
        depth = 6;  // 封顶，避免权重溢出
    }
    return std::pow(10.0, static_cast<double>(depth));
}

int getInlineThreshold()
{
    static int threshold = -1;
    static bool initialized = false;

    if (!initialized) {
        const char * env = std::getenv("MINIC_INLINE_THRESHOLD");
        if (env != nullptr) {
            int value = std::atoi(env);
            if (value > 0) {
                threshold = value;
            }
        }
        initialized = true;
    }

    return threshold > 0 ? threshold : kInlineDefaultThreshold;
}

int getInlineHotMultiplier()
{
    static int multiplier = -1;
    static bool initialized = false;

    if (!initialized) {
        const char * env = std::getenv("MINIC_INLINE_HOT_MULTIPLIER");
        if (env != nullptr) {
            int value = std::atoi(env);
            if (value > 0) {
                multiplier = value;
            }
        }
        initialized = true;
    }

    return multiplier > 0 ? multiplier : kInlineHotMultiplier;
}

} // namespace CostModel
