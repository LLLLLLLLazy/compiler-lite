///
/// @file GuardedTailCollapse.cpp
/// @brief 单调守卫循环的空转尾部折叠 pass 实现
///

#include "GuardedTailCollapse.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <unordered_set>

#include "AnalysisCache.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CondBranchInst.h"
#include "ConstInteger.h"
#include "DominatorTree.h"
#include "Function.h"
#include "ICmpInst.h"
#include "Instruction.h"
#include "LoopInfo.h"
#include "Module.h"
#include "PhiInst.h"
#include "SelectInst.h"
#include "Type.h"
#include "Value.h"

namespace {

/// @brief 判断值是否在循环体内定义
bool isDefinedInLoop(Value * value, const std::unordered_set<BasicBlock *> & loopBody)
{
    auto * inst = dynamic_cast<Instruction *>(value);
    return inst && inst->getParentBlock() && loopBody.find(inst->getParentBlock()) != loopBody.end();
}

/// @brief 匹配 add(ivPhi, 正常量) 形态的归纳变量递增
/// @param incoming 待匹配的值
/// @param ivPhi 归纳变量 phi
/// @return 匹配成功返回正步长，否则返回 0
int32_t matchPositiveStep(Value * incoming, PhiInst * ivPhi)
{
    auto * add = dynamic_cast<BinaryInst *>(incoming);
    if (!add || add->getOp() != IRInstOperator::IRINST_OP_ADD_I) {
        return 0;
    }

    Value * other = nullptr;
    if (add->getLHS() == ivPhi) {
        other = add->getRHS();
    } else if (add->getRHS() == ivPhi) {
        other = add->getLHS();
    } else {
        return 0;
    }

    auto * step = dynamic_cast<ConstInteger *>(other);
    if (!step || step->getVal() <= 0) {
        return 0;
    }
    return step->getVal();
}

/// @brief 判断块内非终结指令是否全部为纯指令（无内存写、无副作用）
bool isPureBlock(BasicBlock * bb)
{
    for (auto * inst : bb->getInstructions()) {
        if (inst->isTerminator()) {
            continue;
        }
        if (inst->mayHaveSideEffects() || inst->mayWriteMemory()) {
            return false;
        }
    }
    return true;
}

/// @brief 判断块是否仅由 phi 与无条件跳转到 header 构成（CanonicalizeLoop 的合成 latch 形态）
bool isPhiOnlyLatch(BasicBlock * bb, BasicBlock * header)
{
    auto * br = dynamic_cast<BranchInst *>(bb->getTerminator());
    if (!br || br->getTarget() != header) {
        return false;
    }
    for (auto * inst : bb->getInstructions()) {
        if (inst == br) {
            continue;
        }
        if (dynamic_cast<PhiInst *>(inst) == nullptr) {
            return false;
        }
    }
    return true;
}

/// @brief 空转路径匹配结果
struct SkipPath {
    BasicBlock * skipBlock = nullptr;   ///< 守卫的空转侧后继
    BasicBlock * mergedLatch = nullptr; ///< 合成 latch（直连回头时为空）
    int32_t step = 0;                   ///< 空转路径上的 IV 正步长
};

/// @brief 匹配"从守卫出发经纯指令路径回到循环头，且路径上 IV 步进为正常量"的空转路径
///
/// 接受两种形态：
///   直连：S --br--> header，header 的 IV phi 来自 S 的入值为 add(IV, c>0)
///   合成 latch：S --br--> L（仅 phi + br header），header 的 IV phi 来自 L 的
///   入值为 L 内 phi P，且 P 来自 S 的入值为 add(IV, c>0)
/// S 自身只需全部为纯指令（IV 的 add 可因 GVN 合并而位于环内其他支配位置）。
/// @param cand 守卫的候选空转侧后继
/// @param header 循环头
/// @param ivPhi 归纳变量 phi
/// @param result [out] 匹配成功时填充路径信息
/// @return true 表示匹配成功
bool matchSkipPath(BasicBlock * cand, BasicBlock * header, PhiInst * ivPhi, SkipPath & result)
{
    if (!cand || cand == header || cand->getPredecessors().size() != 1 || !isPureBlock(cand)) {
        return false;
    }

    auto * br = dynamic_cast<BranchInst *>(cand->getTerminator());
    if (!br) {
        return false;
    }

    if (br->getTarget() == header) {
        // 直连回头
        for (int32_t i = 0; i < ivPhi->getIncomingCount(); ++i) {
            if (ivPhi->getIncomingBlock(i) != cand) {
                continue;
            }
            const int32_t step = matchPositiveStep(ivPhi->getIncomingValue(i), ivPhi);
            if (step <= 0) {
                return false;
            }
            result.skipBlock = cand;
            result.mergedLatch = nullptr;
            result.step = step;
            return true;
        }
        return false;
    }

    // 经合成 latch 回头
    BasicBlock * latch = br->getTarget();
    if (!latch || latch == cand || !isPhiOnlyLatch(latch, header)) {
        return false;
    }

    Value * fromLatch = nullptr;
    for (int32_t i = 0; i < ivPhi->getIncomingCount(); ++i) {
        if (ivPhi->getIncomingBlock(i) == latch) {
            fromLatch = ivPhi->getIncomingValue(i);
            break;
        }
    }
    auto * mergePhi = dynamic_cast<PhiInst *>(fromLatch);
    if (!mergePhi || mergePhi->getParentBlock() != latch) {
        return false;
    }

    for (int32_t i = 0; i < mergePhi->getIncomingCount(); ++i) {
        if (mergePhi->getIncomingBlock(i) != cand) {
            continue;
        }
        const int32_t step = matchPositiveStep(mergePhi->getIncomingValue(i), ivPhi);
        if (step <= 0) {
            return false;
        }
        result.skipBlock = cand;
        result.mergedLatch = latch;
        result.step = step;
        return true;
    }
    return false;
}

/// @brief 判断循环头 phi 在空转路径上是否恒等传递
///
/// 若 phi 沿空转路径的携带链（直连：phi 来自 skip 的入值即自身；合成 latch：
/// phi 来自 latch 的入值为 latch 内 phi Q，且 Q 来自 skip 的入值为原 phi），
/// 则空转迭代不改变其取值，删除空转尾部后该 phi 的出口值不变，可安全逃逸。
/// @param phi 循环头 phi
/// @param header 循环头
/// @param skipBlock 空转块
/// @param mergedLatch 合成 latch（直连形态为空）
/// @return true 表示空转路径恒等传递
bool isSpinInvariantHeaderPhi(PhiInst * phi, BasicBlock * header, BasicBlock * skipBlock, BasicBlock * mergedLatch)
{
    if (!phi || phi->getParentBlock() != header) {
        return false;
    }

    if (!mergedLatch) {
        for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
            if (phi->getIncomingBlock(i) == skipBlock) {
                return phi->getIncomingValue(i) == phi;
            }
        }
        return false;
    }

    Value * fromLatch = nullptr;
    for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
        if (phi->getIncomingBlock(i) == mergedLatch) {
            fromLatch = phi->getIncomingValue(i);
            break;
        }
    }
    auto * mergePhi = dynamic_cast<PhiInst *>(fromLatch);
    if (!mergePhi || mergePhi->getParentBlock() != mergedLatch) {
        return false;
    }
    for (int32_t i = 0; i < mergePhi->getIncomingCount(); ++i) {
        if (mergePhi->getIncomingBlock(i) == skipBlock) {
            return mergePhi->getIncomingValue(i) == phi;
        }
    }
    return false;
}

/// @brief 交换比较关系的左右操作数（inv op IV ⇔ IV op' inv）
IRInstOperator swapCompare(IRInstOperator op)
{
    switch (op) {
        case IRInstOperator::IRINST_OP_LT_I:
            return IRInstOperator::IRINST_OP_GT_I;
        case IRInstOperator::IRINST_OP_GT_I:
            return IRInstOperator::IRINST_OP_LT_I;
        case IRInstOperator::IRINST_OP_LE_I:
            return IRInstOperator::IRINST_OP_GE_I;
        case IRInstOperator::IRINST_OP_GE_I:
            return IRInstOperator::IRINST_OP_LE_I;
        default:
            return op;
    }
}

/// @brief 取比较关系的逻辑非（¬(IV op inv)）
IRInstOperator negateCompare(IRInstOperator op)
{
    switch (op) {
        case IRInstOperator::IRINST_OP_LT_I:
            return IRInstOperator::IRINST_OP_GE_I;
        case IRInstOperator::IRINST_OP_GE_I:
            return IRInstOperator::IRINST_OP_LT_I;
        case IRInstOperator::IRINST_OP_GT_I:
            return IRInstOperator::IRINST_OP_LE_I;
        case IRInstOperator::IRINST_OP_LE_I:
            return IRInstOperator::IRINST_OP_GT_I;
        default:
            return op;
    }
}

} // namespace

/// @brief 构造守卫尾部折叠 pass
/// @param _func 待优化函数
/// @param _mod 所属模块
GuardedTailCollapse::GuardedTailCollapse(Function * _func, Module * _mod) : func(_func), mod(_mod)
{}

/// @brief 尝试折叠以 header 为头的循环的空转尾部
/// @param header 循环头基本块
/// @param loopInfo 当前函数的循环信息
/// @return true 表示该循环被改写
bool GuardedTailCollapse::tryCollapseLoop(BasicBlock * header, const LoopInfo & loopInfo)
{
    if (!loopInfo.isLoopHeader(header)) {
        return false;
    }
    const auto * bodyPtr = loopInfo.getLoopBody(header);
    if (!bodyPtr || bodyPtr->empty()) {
        return false;
    }
    const auto & loopBody = *bodyPtr;

    // 1. 循环头终结：condbr(icmp IV < Bound)，真分支入环、假分支出环（规范非旋转计数循环）
    auto * headerBr = dynamic_cast<CondBranchInst *>(header->getTerminator());
    if (!headerBr) {
        return false;
    }
    auto * exitCmp = dynamic_cast<ICmpInst *>(headerBr->getCondition());
    if (!exitCmp) {
        return false;
    }
    BasicBlock * inDest = headerBr->getTrueDest();
    BasicBlock * outDest = headerBr->getFalseDest();
    if (loopBody.find(inDest) == loopBody.end() || loopBody.find(outDest) != loopBody.end()) {
        return false;
    }

    // 归一化继续条件为 IV < Bound（真分支继续）
    PhiInst * ivPhi = nullptr;
    Value * bound = nullptr;
    int32_t boundOperandIndex = -1;
    const IRInstOperator exitOp = exitCmp->getOp();
    if (exitOp == IRInstOperator::IRINST_OP_LT_I) {
        ivPhi = dynamic_cast<PhiInst *>(exitCmp->getLHS());
        bound = exitCmp->getRHS();
        boundOperandIndex = 1;
    } else if (exitOp == IRInstOperator::IRINST_OP_GT_I) {
        ivPhi = dynamic_cast<PhiInst *>(exitCmp->getRHS());
        bound = exitCmp->getLHS();
        boundOperandIndex = 0;
    } else {
        return false;
    }
    if (!ivPhi || ivPhi->getParentBlock() != header || !ivPhi->getType() || !ivPhi->getType()->isInt32Type()) {
        return false;
    }
    if (!bound || isDefinedInLoop(bound, loopBody)) {
        return false;
    }

    // 2. 唯一 preheader
    BasicBlock * preheader = nullptr;
    for (auto * pred : header->getPredecessors()) {
        if (loopBody.find(pred) != loopBody.end()) {
            continue;
        }
        if (preheader != nullptr) {
            return false;
        }
        preheader = pred;
    }
    if (!preheader) {
        return false;
    }

    // 3. 守卫块：循环头入环后继，仅含纯指令，以 condbr(icmp) 结束且两个分支都在环内
    BasicBlock * guardBlock = inDest;
    if (guardBlock == header || !isPureBlock(guardBlock)) {
        return false;
    }
    auto * guardBr = dynamic_cast<CondBranchInst *>(guardBlock->getTerminator());
    if (!guardBr) {
        return false;
    }
    auto * guardCmp = dynamic_cast<ICmpInst *>(guardBr->getCondition());
    if (!guardCmp) {
        return false;
    }
    BasicBlock * trueDest = guardBr->getTrueDest();
    BasicBlock * falseDest = guardBr->getFalseDest();
    if (trueDest == falseDest || loopBody.find(trueDest) == loopBody.end() ||
        loopBody.find(falseDest) == loopBody.end()) {
        return false;
    }

    // 4. 守卫比较：一侧为归纳变量，另一侧循环不变
    Value * inv = nullptr;
    IRInstOperator guardRel; // 归一化为 IV rel inv
    if (guardCmp->getLHS() == ivPhi) {
        inv = guardCmp->getRHS();
        guardRel = guardCmp->getOp();
    } else if (guardCmp->getRHS() == ivPhi) {
        inv = guardCmp->getLHS();
        guardRel = swapCompare(guardCmp->getOp());
    } else {
        return false;
    }
    if (guardRel != IRInstOperator::IRINST_OP_LT_I && guardRel != IRInstOperator::IRINST_OP_GT_I &&
        guardRel != IRInstOperator::IRINST_OP_LE_I && guardRel != IRInstOperator::IRINST_OP_GE_I) {
        return false;
    }
    if (!inv || isDefinedInLoop(inv, loopBody) || !inv->getType() || !inv->getType()->isInt32Type()) {
        return false;
    }

    // 5. 识别空转侧：空转条件须归一化为 IV ≥ L（空转在大 IV 一侧，即尾部）。
    //    两侧关系互补，恰有一侧落在 {GT, GE}；先按方向定侧，再校验该侧确为纯空转路径
    const IRInstOperator relIfTrueSkips = guardRel;
    const IRInstOperator relIfFalseSkips = negateCompare(guardRel);
    bool trueIsSkip;
    if (relIfTrueSkips == IRInstOperator::IRINST_OP_GT_I || relIfTrueSkips == IRInstOperator::IRINST_OP_GE_I) {
        trueIsSkip = true;
    } else if (relIfFalseSkips == IRInstOperator::IRINST_OP_GT_I ||
               relIfFalseSkips == IRInstOperator::IRINST_OP_GE_I) {
        trueIsSkip = false;
    } else {
        return false;
    }
    const IRInstOperator skipRel = trueIsSkip ? relIfTrueSkips : relIfFalseSkips;
    // 空转当 IV > inv ⇒ 工作区间上限 L = inv + 1；空转当 IV ≥ inv ⇒ L = inv
    const bool workLimitPlusOne = (skipRel == IRInstOperator::IRINST_OP_GT_I);

    SkipPath skipPath;
    if (!matchSkipPath(trueIsSkip ? trueDest : falseDest, header, ivPhi, skipPath)) {
        return false;
    }
    BasicBlock * skipBlock = skipPath.skipBlock;
    BasicBlock * workDest = trueIsSkip ? falseDest : trueDest;
    if (workDest == header || workDest == skipBlock) {
        return false;
    }

    // 5.5 空转步进不得回绕（按二进制补码语义论证，不假设有符号溢出不发生）。
    //     折叠的前提是"IV 一旦进入空转区间 [L, Bound) 就不再回到工作区间"，
    //     而这只有在 IV+s 不越过 INT32_MAX 时成立：空转迭代满足 IV < Bound，
    //     故 IV ≤ Bound-1，其后继 IV+s ≤ Bound-1+s，要求 Bound-1+s ≤ INT32_MAX。
    //     s == 1 时由 Bound ≤ INT32_MAX 恒成立；s ≥ 2 时只在 Bound 为满足该式的
    //     常量时折叠——回绕分支会重新执行工作块，删掉守卫后无法再现，故不能靠
    //     运行期 select 退回，只能整体放弃。
    if (skipPath.step != 1) {
        auto * boundConst = dynamic_cast<ConstInteger *>(bound);
        const int64_t maxBound =
            static_cast<int64_t>(std::numeric_limits<int32_t>::max()) - skipPath.step + 1;
        if (!boundConst || static_cast<int64_t>(boundConst->getVal()) > maxBound) {
            return false;
        }
    }

    // 6. 环内定义值的逃逸检查：删除空转迭代后环外观察值必须不变。
    //    允许两类安全逃逸：
    //    (a) 空转路径恒等传递的循环头 phi（如被守卫跳过的累加器）——空转不改其值，
    //        钳制后出口值不变；IV 自身出口值会从原上界变为钳制值，不允许逃逸；
    //    (b) 仅经工作路径出口边逃逸（环外 phi 的对应入边块是环内工作块）——
    //        钳制不改变任何工作迭代，经 break 类出口离开时程序状态与原来一致。
    for (auto * bb : loopBody) {
        for (auto * inst : bb->getInstructions()) {
            for (auto * use : inst->getUseList()) {
                auto * userInst = dynamic_cast<Instruction *>(use->getUser());
                if (!userInst || !userInst->getParentBlock()) {
                    return false;
                }
                if (loopBody.find(userInst->getParentBlock()) != loopBody.end()) {
                    continue;
                }

                auto * headerPhi = dynamic_cast<PhiInst *>(inst);
                if (headerPhi && headerPhi != ivPhi &&
                    isSpinInvariantHeaderPhi(headerPhi, header, skipBlock, skipPath.mergedLatch)) {
                    continue;
                }

                auto * userPhi = dynamic_cast<PhiInst *>(userInst);
                if (!userPhi) {
                    return false;
                }
                bool onlyWorkExitEdges = true;
                for (int32_t k = 0; k < userPhi->getIncomingCount(); ++k) {
                    if (userPhi->getIncomingValue(k) != inst) {
                        continue;
                    }
                    BasicBlock * inBlock = userPhi->getIncomingBlock(k);
                    if (inBlock == header || inBlock == guardBlock || inBlock == skipBlock ||
                        loopBody.find(inBlock) == loopBody.end()) {
                        onlyWorkExitEdges = false;
                        break;
                    }
                }
                if (!onlyWorkExitEdges) {
                    return false;
                }
            }
        }
    }

    // ---- 变换 ----
    // 7. preheader 末尾构造 newBound = min(Bound, L)。
    //    inv+1 的回绕由 select 结构本身排除，而非假设有符号加法不溢出：
    //    选中 inv+1 的臂以 inv < Bound 为条件，此时 inv ≤ Bound-1 ≤ INT32_MAX-1，
    //    inv+1 ≤ INT32_MAX 必然不回绕；inv ≥ Bound 时选中 Bound，
    //    而 min(Bound, inv+1) 在该情形下本就等于 Bound，取值同样精确。
    Type * i1Type = guardCmp->getType();
    Type * i32Type = ivPhi->getType();
    auto & preInsts = preheader->getInstructions();
    auto insertPos = preInsts.end();
    if (!preInsts.empty() && preInsts.back()->isTerminator()) {
        insertPos = std::prev(preInsts.end());
    }

    auto * limitCmp = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, inv, bound, i1Type);
    limitCmp->setParentBlock(preheader);
    preInsts.insert(insertPos, limitCmp);

    Value * limitValue = inv;
    if (workLimitPlusOne) {
        auto * onePlus =
            new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, inv, mod->newConstInt32(1), i32Type);
        onePlus->setParentBlock(preheader);
        preInsts.insert(insertPos, onePlus);
        limitValue = onePlus;
    }

    auto * newBound = new SelectInst(func, limitCmp, limitValue, bound, i32Type);
    newBound->setParentBlock(preheader);
    preInsts.insert(insertPos, newBound);

    // 8. 循环头比较的上界操作数替换为钳制后的 newBound
    exitCmp->setOperand(boundOperandIndex, newBound);

    // 9. 钳制后环内恒有 IV < L，守卫必走工作方向：condbr 改写为无条件跳转
    auto & guardInsts = guardBlock->getInstructions();
    guardInsts.pop_back();
    guardBr->clearOperands();
    delete guardBr;
    auto * directBr = new BranchInst(func, workDest);
    directBr->setParentBlock(guardBlock);
    guardInsts.push_back(directBr);
    guardBlock->removeSuccessor(skipBlock);

    // 10. 空转路径块成为不可达块，整体摘除；其 phi 入边按直连/合成 latch 两种形态分别拆除
    BasicBlock * phiHolder = skipPath.mergedLatch ? skipPath.mergedLatch : header;
    for (auto * inst : phiHolder->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        phi->removeIncomingBlock(skipBlock);
    }
    phiHolder->removePredecessor(skipBlock);
    for (auto * inst : skipBlock->getInstructions()) {
        inst->clearOperands();
    }
    auto & blocks = func->getBlocks();
    auto blockIt = std::find(blocks.begin(), blocks.end(), skipBlock);
    if (blockIt != blocks.end()) {
        blocks.erase(blockIt);
    }
    delete skipBlock;

    return true;
}

/// @brief 对函数内所有匹配循环执行守卫尾部折叠
/// @return true 表示 IR 发生变化
bool GuardedTailCollapse::run()
{
    if (!func || !mod || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    bool changed = false;
    while (true) {
        DominatorTree domTree(func);
        LoopInfo loopInfo(func, &domTree);

        bool localChanged = false;
        std::vector<BasicBlock *> blocks = func->getBlocks();
        for (auto * bb : blocks) {
            if (tryCollapseLoop(bb, loopInfo)) {
                localChanged = true;
                changed = true;
                func->getAnalysisCache().invalidateCFGAnalyses();
                break;
            }
        }
        if (!localChanged) {
            break;
        }
    }

    return changed;
}
