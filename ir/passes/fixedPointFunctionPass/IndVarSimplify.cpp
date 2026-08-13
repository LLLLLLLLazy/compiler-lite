///
/// @file IndVarSimplify.cpp
/// @brief 归纳变量简化 pass 实现
///
/// 对规范计数循环，尝试将退出条件从整数计数器比较改写为指针比较，
/// 从而消除整数归纳变量
///

#include "IndVarSimplify.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <vector>

#include "BasicBlock.h"
#include "BinaryInst.h"
#include "CondBranchInst.h"
#include "ConstInteger.h"
#include "DominatorTree.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "ICmpInst.h"
#include "Instruction.h"
#include "LoopInfo.h"
#include "Module.h"
#include "PhiInst.h"
#include "ScalarEvolution.h"
#include "SelectInst.h"
#include "AnalysisCache.h"

namespace {

/// @brief 在基本块终结指令之前插入一条指令
void insertBeforeTerminator(BasicBlock * bb, Instruction * inst)
{
    if (!bb || !inst) {
        return;
    }

    auto & insts = bb->getInstructions();
    auto pos = insts.end();
    if (!insts.empty() && insts.back()->isTerminator()) {
        pos = std::prev(insts.end());
    }

    inst->setParentBlock(bb);
    insts.insert(pos, inst);
}

/// @brief 判断指令是否位于指定基本块集合内
bool isInLoop(Instruction * inst, const std::unordered_set<BasicBlock *> & loopBody)
{
    return inst && inst->getParentBlock() && loopBody.find(inst->getParentBlock()) != loopBody.end();
}

/// @brief 获取 phi 指令中来自指定前驱块的入边值
Value * getPhiIncomingForBlock(PhiInst * phi, BasicBlock * block)
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

} // namespace

IndVarSimplify::IndVarSimplify(Function * _func, Module * _mod) : func(_func), mod(_mod)
{}

bool IndVarSimplify::run()
{
    if (!func || !mod || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    auto & cache = func->getAnalysisCache();
    auto & domTree = cache.getOrCompute<DominatorTree>([this] { return DominatorTree(func); });
    auto & loopInfo =
        cache.getOrCompute<LoopInfo>([this, &domTree] { return LoopInfo(func, &domTree); });
    auto & scev = cache.getOrCompute<ScalarEvolution>(
        [this, &domTree, &loopInfo] { return ScalarEvolution(func, &domTree, &loopInfo); });

    // 按深度降序（最内层优先）
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

    bool changed = false;
    for (auto * header : headers) {
        if (trySimplifyHeader(header, scev)) {
            changed = true;
            break; // 一次只改一个循环，让 fixed-point 重跑分析
        }
    }

    if (changed) {
        func->getAnalysisCache().invalidateValueAnalyses();
    }

    return changed;
}

bool IndVarSimplify::trySimplifyHeader(BasicBlock * header, ScalarEvolution & scev)
{
    if (!header) {
        return false;
    }

    // 匹配规范计数循环
    ScalarEvolution::CanonicalLoop loop;
    if (!scev.matchCanonicalLoop(header, loop)) {
        return false;
    }
    if (!loop.induction || !loop.cmp || !loop.branch) {
        return false;
    }
    if (!loop.preheader || !loop.latch || !loop.exit) {
        return false;
    }

    auto * ivPhi = loop.induction;
    auto * exitICmp = loop.cmp;

    // 获取循环体集合，用于判断指令是否在循环内
    auto & cache = func->getAnalysisCache();
    auto & domTree = cache.getOrCompute<DominatorTree>([this] { return DominatorTree(func); });
    auto & loopInfo =
        cache.getOrCompute<LoopInfo>([this, &domTree] { return LoopInfo(func, &domTree); });
    const auto * bodyPtr = loopInfo.getLoopBody(header);
    if (!bodyPtr) {
        return false;
    }
    const auto & loopBody = *bodyPtr;

    // 检查 IV 是否可以被消除：剩余使用仅限于退出条件 icmp 和自身递推 add
    bool canEliminateIV = true;
    int useCount = 0;
    for (auto * use : ivPhi->getUseList()) {
        auto * user = dynamic_cast<Instruction *>(use->getUser());
        if (!user || !isInLoop(user, loopBody)) {
            // 循环外使用不能消除 IV
            canEliminateIV = false;
            break;
        }
        if (user == exitICmp) {
            ++useCount;
            continue;
        }
        if (auto * bin = dynamic_cast<BinaryInst *>(user)) {
            if (bin->getOp() == IRInstOperator::IRINST_OP_ADD_I) {
                ++useCount;
                continue;
            }
        }
        canEliminateIV = false;
        break;
    }

    if (!canEliminateIV || useCount != 2) {
        return false;
    }

    // 在 header 的 phi 中寻找指针型候选归纳变量
    // 候选必须：是指针类型、有 SCEV AddRecurrence、和 IV 同循环
    PhiInst * candidatePhi = nullptr;
    Value * candidateStart = nullptr;
    int32_t candidateIndexStep = 0;

    for (auto * inst : header->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi || phi == ivPhi) {
            if (!phi) {
                break; // phi 列表结束
            }
            continue;
        }

        // 必须是指针类型
        if (!phi->getType()->isPointerType()) {
            continue;
        }

        // 检查 SCEV 是否识别为 AddRecurrence（同循环）
        auto * rec = scev.getAddRecurrence(phi);
        if (!rec || rec->getLoopHeader() != header || rec->getPreheader() != loop.preheader
            || rec->getLatch() != loop.latch) {
            continue;
        }

        // 检查 latch 入边值是否是 GEP(phi, constantIndex)
        // 这是指针递推的典型形态
        Value * latchVal = getPhiIncomingForBlock(phi, loop.latch);
        auto * latchGEP = dynamic_cast<GetElementPtrInst *>(latchVal);
        if (!latchGEP || latchGEP->getBasePointer() != phi) {
            continue;
        }

        // 提取 GEP 的索引步长
        Value * indexOp = latchGEP->getIndexOperand();
        auto * indexConst = dynamic_cast<ConstInteger *>(indexOp);
        if (!indexConst || indexConst->getVal() <= 0) {
            continue;
        }

        // 获取 preheader 入边值作为起始指针
        Value * startVal = getPhiIncomingForBlock(phi, loop.preheader);
        if (!startVal) {
            continue;
        }

        candidatePhi = phi;
        candidateStart = startVal;
        candidateIndexStep = indexConst->getVal();
        break;
    }

    if (!candidatePhi) {
        return false;
    }

    // 计算 endPtr
    // tripCount 是 IV 从 start 到 bound 的迭代次数
    // 候选指针的索引步长 = candidateIndexStep
    // endPtr 的索引偏移 = tripCount * candidateIndexStep
    int32_t endIndex = 0;
    if (loop.hasConstTripCount) {
        const std::int64_t wideEndIndex =
            static_cast<std::int64_t>(loop.tripCount) * candidateIndexStep;
        if (wideEndIndex > std::numeric_limits<int32_t>::max()) {
            return false;
        }
        endIndex = static_cast<int32_t>(wideEndIndex);
    } else {
        // 非常量上界：仅处理 start=0、step=1、比较谓词为 < 的递增循环
        // 真实 tripCount 为 max(bound, 0)，见下方 endPtr 构造
        if (!loop.recurrence || loop.recurrence->getStep() != 1 || !loop.hasConstInitialValue
            || loop.initialIntValue != 0 || !loop.boundValue) {
            return false;
        }
        if (loop.compareKind != ScalarEvolution::CompareKind::LessThan) {
            return false; // 其他比较谓词暂不处理
        }
    }

    Instruction * endPtr = nullptr;
    auto * latchGEPType = dynamic_cast<GetElementPtrInst *>(getPhiIncomingForBlock(candidatePhi, loop.latch));
    if (!latchGEPType) {
        return false;
    }

    if (loop.hasConstTripCount) {
        // 常量 tripCount：endPtr = GEP(start, tripCount * candidateIndexStep)
        auto * endIdx = mod->newConstInteger(
            latchGEPType->getIndexOperand()->getType(), endIndex);
        endPtr = new GetElementPtrInst(func, candidateStart, endIdx,
                                        latchGEPType->getType(), false);
    } else {
        // boundValue 是循环上界，start=0, step=1
        // tripCount = max(boundValue, 0)，保留负上界时的零次迭代语义
        // endPtr = GEP(start, tripCount * candidateIndexStep)
        auto * zero = mod->newConstInteger(loop.boundValue->getType(), 0);
        auto * isPositive = new ICmpInst(func,
                                         IRInstOperator::IRINST_OP_GT_I,
                                         loop.boundValue,
                                         zero,
                                         exitICmp->getType());
        insertBeforeTerminator(loop.preheader, isPositive);
        auto * tripCountVal = new SelectInst(func,
                                             isPositive,
                                             loop.boundValue,
                                             zero,
                                             loop.boundValue->getType());
        insertBeforeTerminator(loop.preheader, tripCountVal);
        Value * endIdx = tripCountVal;
        if (candidateIndexStep != 1) {
            auto * stepConst = mod->newConstInteger(
                tripCountVal->getType(), candidateIndexStep);
            auto * mul = new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I,
                                        tripCountVal, stepConst,
                                        tripCountVal->getType());
            insertBeforeTerminator(loop.preheader, mul);
            endIdx = mul;
        }
        endPtr = new GetElementPtrInst(func, candidateStart, endIdx,
                                        latchGEPType->getType(), false);
    }

    insertBeforeTerminator(loop.preheader, endPtr);

    // 创建新的退出条件：icmp ne candidatePhi, endPtr
    auto * ptrCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_NE_I,
                                  candidatePhi, endPtr, exitICmp->getType());
    insertBeforeTerminator(header, ptrCmp);

    // 替换旧退出条件
    exitICmp->replaceAllUseWith(ptrCmp);
    exitICmp->clearOperands();
    exitICmp->setDead(true);

    return true;
}
