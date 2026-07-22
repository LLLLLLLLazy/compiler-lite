///
/// @file RangeModSimplify.cpp
/// @brief 基于值域的 2 的幂取模/除法削减 pass 实现
///

#include "RangeModSimplify.h"

#include <algorithm>
#include <limits>

#include "AnalysisCache.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "ConstInteger.h"
#include "DominatorTree.h"
#include "Function.h"
#include "Instruction.h"
#include "IntegerType.h"
#include "LoopInfo.h"
#include "Module.h"
#include "Value.h"

namespace {

/// @brief 判断常量是否为 [2, 2^30] 范围内的 2 的幂并返回移位量
bool asSmallPowerOfTwo(ConstInteger * constant, int32_t & shiftOut)
{
    if (!constant) {
        return false;
    }
    const std::int64_t value = constant->getVal();
    if (value < 2 || (value & (value - 1)) != 0 || value > (1 << 30)) {
        return false;
    }
    int32_t shift = 0;
    std::int64_t current = value;
    while (current > 1) {
        current >>= 1;
        ++shift;
    }
    shiftOut = shift;
    return true;
}

/// @brief 将新指令插入到锚点指令之前
void insertBefore(Instruction * anchor, Instruction * created)
{
    auto * bb = anchor->getParentBlock();
    if (!bb) {
        return;
    }
    auto & insts = bb->getInstructions();
    auto pos = std::find(insts.begin(), insts.end(), anchor);
    if (pos == insts.end()) {
        return;
    }
    created->setParentBlock(bb);
    insts.insert(pos, created);
}

constexpr std::int64_t kInt32Min = std::numeric_limits<std::int32_t>::min();
constexpr std::int64_t kInt32Max = std::numeric_limits<std::int32_t>::max();

} // namespace

RangeModSimplify::RangeModSimplify(Function * _func, Module * _mod) : func(_func), mod(_mod)
{}

std::int64_t RangeModSimplify::canonicalTripCount(ScalarEvolution & scev, BasicBlock * loopHeader)
{
    auto cached = tripCountCache.find(loopHeader);
    if (cached != tripCountCache.end()) {
        return cached->second;
    }

    std::int64_t trips = -1;
    ScalarEvolution::CanonicalLoop loop;
    if (scev.matchCanonicalLoop(loopHeader, loop) && loop.hasConstTripCount && loop.tripCount > 0) {
        trips = loop.tripCount;
    }
    tripCountCache[loopHeader] = trips;
    return trips;
}

RangeModSimplify::Range RangeModSimplify::rangeOfExpr(ScalarEvolution & scev,
                                                     const ScalarEvolution::Expr * expr,
                                                     int depth)
{
    Range result;
    if (!expr || depth > 8) {
        return result;
    }

    switch (expr->getKind()) {
        case ScalarEvolution::ExprKind::Constant: {
            auto * constant = static_cast<const ScalarEvolution::ConstantExpr *>(expr);
            result.lo = constant->getIntValue();
            result.hi = constant->getIntValue();
            result.known = true;
            return result;
        }

        case ScalarEvolution::ExprKind::Add: {
            auto * add = static_cast<const ScalarEvolution::AddExpr *>(expr);
            Range lhs = rangeOfExpr(scev, add->getLHS(), depth + 1);
            Range rhs = rangeOfExpr(scev, add->getRHS(), depth + 1);
            if (!lhs.known || !rhs.known) {
                return result;
            }
            result.lo = lhs.lo + rhs.lo;
            result.hi = lhs.hi + rhs.hi;
            result.known = result.lo >= kInt32Min && result.hi <= kInt32Max;
            return result;
        }

        case ScalarEvolution::ExprKind::Multiply: {
            auto * multiply = static_cast<const ScalarEvolution::MultiplyExpr *>(expr);
            Range lhs = rangeOfExpr(scev, multiply->getLHS(), depth + 1);
            Range rhs = rangeOfExpr(scev, multiply->getRHS(), depth + 1);
            if (!lhs.known || !rhs.known) {
                return result;
            }
            const std::int64_t candidates[4] = {lhs.lo * rhs.lo,
                                                lhs.lo * rhs.hi,
                                                lhs.hi * rhs.lo,
                                                lhs.hi * rhs.hi};
            result.lo = *std::min_element(candidates, candidates + 4);
            result.hi = *std::max_element(candidates, candidates + 4);
            result.known = result.lo >= kInt32Min && result.hi <= kInt32Max;
            return result;
        }

        case ScalarEvolution::ExprKind::AddRecurrence: {
            auto * recurrence = static_cast<const ScalarEvolution::AddRecurrenceExpr *>(expr);
            if (!recurrence->isIntegerRecurrence()) {
                return result;
            }
            Range start = rangeOfExpr(scev, recurrence->getStartExpr(), depth + 1);
            if (!start.known) {
                return result;
            }
            const std::int64_t trips = canonicalTripCount(scev, recurrence->getLoopHeader());
            if (trips <= 0) {
                return result;
            }
            // 循环内可观察到的取值为 start + step*j，j ∈ [0, trips]
            //（含循环头处最后一次失败测试所见的越界值，保守取闭区间并集）
            const std::int64_t step = recurrence->getStep();
            const std::int64_t deltaEnd = step * trips;
            result.lo = std::min(start.lo, start.lo + deltaEnd);
            result.hi = std::max(start.hi, start.hi + deltaEnd);
            result.known = result.lo >= kInt32Min && result.hi <= kInt32Max;
            return result;
        }

        default:
            return result;
    }
}

bool RangeModSimplify::run()
{
    if (!func || !mod || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    auto & cache = func->getAnalysisCache();
    auto & domTree = cache.getOrCompute<DominatorTree>([this] { return DominatorTree(func); });
    auto & loopInfo = cache.getOrCompute<LoopInfo>([this, &domTree] { return LoopInfo(func, &domTree); });
    ScalarEvolution scev(func, &domTree, &loopInfo);
    tripCountCache.clear();

    bool changed = false;
    for (auto * bb : func->getBlocks()) {
        // 本 pass 的新增证明能力来自循环 add-recurrence。循环外的常量/直线
        // 表达式由 ConstProp/InstCombine 处理；不要为它们构造 SCEV。超长直线
        // 用例可能含上千条 %/÷（如 2026_func_29_long_line），在固定点每轮
        // 对其递归求值会造成数量级编译时回归。
        if (loopInfo.getLoopDepth(bb) <= 0) {
            continue;
        }
        for (auto * inst : bb->getInstructions()) {
            if (!inst || inst->isDead()) {
                continue;
            }
            auto * binary = dynamic_cast<BinaryInst *>(inst);
            if (!binary) {
                continue;
            }
            const IRInstOperator op = binary->getOp();
            if (op != IRInstOperator::IRINST_OP_MOD_I && op != IRInstOperator::IRINST_OP_DIV_I) {
                continue;
            }
            int32_t shift = 0;
            auto * divisor = dynamic_cast<ConstInteger *>(binary->getRHS());
            if (!asSmallPowerOfTwo(divisor, shift)) {
                continue;
            }

            Range range = rangeOfExpr(scev, scev.getSCEV(binary->getLHS()), 0);
            if (!range.known || range.lo < 0) {
                continue;
            }

            BinaryInst * replacement = nullptr;
            if (op == IRInstOperator::IRINST_OP_MOD_I) {
                // 非负被除数：x % 2^k == x & (2^k - 1)
                replacement = new BinaryInst(func,
                                             IRInstOperator::IRINST_OP_AND_I,
                                             binary->getLHS(),
                                             mod->newConstInteger(binary->getType(), divisor->getVal() - 1),
                                             binary->getType());
            } else {
                // 非负被除数：向零截断除法与算术右移一致
                replacement = new BinaryInst(func,
                                             IRInstOperator::IRINST_OP_ASHR_I,
                                             binary->getLHS(),
                                             mod->newConstInteger(binary->getType(), shift),
                                             binary->getType());
            }
            insertBefore(binary, replacement);
            binary->replaceAllUseWith(replacement);
            binary->clearOperands();
            binary->setDead(true);
            changed = true;
        }
    }

    if (changed) {
        func->getAnalysisCache().invalidateValueAnalyses();
    }
    return changed;
}
