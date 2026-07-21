///
/// @file LICM.cpp
/// @brief 循环不变量外提 pass 实现
///

#include "LICM.h"

#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <utility>

#include "AnalysisCache.h"
#include "AllocaInst.h"
#include "BasicBlock.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "CondBranchInst.h"
#include "ConstInteger.h"
#include "CostModel.h"
#include "DominatorTree.h"
#include "Function.h"
#include "FormalParam.h"
#include "GetElementPtrInst.h"
#include "GlobalVariable.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "LoopInfo.h"
#include "MemoryAccess.h"
#include "MemoryLocation.h"
#include "Module.h"
#include "ParamAliasAnalysis.h"
#include "PhiInst.h"
#include "PureFunctionAnalysis.h"
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
        case IRInstOperator::IRINST_OP_SHL_I:
        case IRInstOperator::IRINST_OP_ASHR_I:
        case IRInstOperator::IRINST_OP_LSHR_I:
        case IRInstOperator::IRINST_OP_AND_I:
        case IRInstOperator::IRINST_OP_OR_I:
        case IRInstOperator::IRINST_OP_XOR_I:
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
///
/// 浮点除法在 RISC-V 与 x86（IRCompiler 解释执行）上都不会陷入，
/// 可自由推测执行，因此不在此列；整数除/模的常量除数豁免见
/// LICM::requiresExitDominance
bool needsExitDominance(IRInstOperator op)
{
    switch (op) {
        case IRInstOperator::IRINST_OP_DIV_I:
        case IRInstOperator::IRINST_OP_MOD_I:
            return true;

        default:
            return false;
    }
}

/// @brief 判断循环体是否可能写入调用者可见内存
///
/// 遍历循环体中的所有指令：纯函数调用不写内存，非逃逸局部对象的 store
/// 对被调函数不可见，两者都不构成外提纯调用时的读写顺序约束；
/// 其余 store、非纯调用以及向量写等按可能写内存处理。
/// @param loopBody 循环体基本块集合
/// @param purity 模块级纯函数分析
/// @return true 表示循环可能写入调用者可见内存
bool loopMayWriteMemory(const std::unordered_set<BasicBlock *> & loopBody, PureFunctionAnalysis & purity)
{
    for (auto * bb : loopBody) {
        for (auto * inst : bb->getInstructions()) {
            if (!inst || inst->isDead()) {
                continue;
            }
            if (auto * call = dynamic_cast<CallInst *>(inst)) {
                if (purity.isPure(call->getCallee())) {
                    continue;
                }
                return true;
            }
            if (auto * store = dynamic_cast<StoreInst *>(inst)) {
                if (isNonEscapingLocalStore(store)) {
                    continue;
                }
                return true;
            }
            if (inst->mayWriteMemory()) {
                return true;
            }
        }
    }
    return false;
}

/// @brief 判断指令是否属于「不会陷入但代价高」的可推测除法类指令
///
/// 到达调用点时除法类指令已通过 requiresExitDominance 的豁免检查
/// （常量除数整除/模，或浮点除法），因此这里只按操作码分类
/// @param inst 待检查的指令
/// @return true 表示该指令是可推测执行的除法类指令
bool isSpeculatableDivision(Instruction * inst)
{
    if (!inst) {
        return false;
    }
    switch (inst->getOp()) {
        case IRInstOperator::IRINST_OP_DIV_I:
        case IRInstOperator::IRINST_OP_MOD_I:
        case IRInstOperator::IRINST_OP_DIV_F:
            return true;

        default:
            return false;
    }
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

    // 模块级纯度分析：LICM 只移动指令、不增删 store 与调用，函数纯度在
    // 整个 run 期间保持有效，可跨循环与迭代轮次复用缓存
    PureFunctionAnalysis purity(mod);
    purityAnalysis = &purity;
    // 形参别名分析：LICM 不增删调用点、不改写实参，指向集在 run 期间有效
    ParamAliasAnalysis paramAlias(mod);
    paramAliasAnalysis = mod != nullptr ? &paramAlias : nullptr;

    auto & cache = func->getAnalysisCache();
    while (true) {
        auto & domTree = cache.getOrCompute<DominatorTree>([this] { return DominatorTree(func); });
        auto & loopInfo =
            cache.getOrCompute<LoopInfo>([this, &domTree] { return LoopInfo(func, &domTree); });

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

            if (tryHoistLoop(header, *loopBody, domTree, purity)) {
                localChanged = true;
                changed = true;
                break;
            }
        }

        if (!localChanged) {
            break;
        }
        // 本轮外提改动了 preheader/CFG，使所有 CFG 派生分析失效以便下轮重算
        cache.invalidateCFGAnalyses();
    }

    purityAnalysis = nullptr;
    paramAliasAnalysis = nullptr;
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
                        const DominatorTree & domTree,
                        PureFunctionAnalysis & purity)
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
    std::vector<std::pair<Instruction *, const char *>> invariantOrder;
    // 候选被拒原因（同一指令跨轮覆盖，仅保留最后一次判定），用于 opt-remark 统计
    std::unordered_map<Instruction *, const char *> rejectedCandidates;

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

                auto * callInst = dynamic_cast<CallInst *>(inst);
                // 内存无关调用：结果只由实参决定，不读写调用者可见内存，
                // 与循环内 store 无顺序约束
                bool memIndependentCall =
                    callInst != nullptr && mod != nullptr && purity.isMemoryIndependent(callInst->getCallee());

                // 若候选指令是可能读取内存的函数调用，且循环体可能写入调用者可见内存，
                // 则不可外提：外提后调用与内存写入的相对顺序可能改变，破坏语义
                if (callInst != nullptr && !memIndependentCall && loopMayWriteMemory(loopBody, purity)) {
                    rejectedCandidates[inst] = "call rejected: loop writes caller-visible memory";
                    continue;
                }

                if (!operandsAreLoopInvariant(inst, loopBody, invariants)) {
                    continue;
                }

                auto * loadInst = dynamic_cast<LoadInst *>(inst);
                LoadHoistKind loadKind = LoadHoistKind::Speculate;
                if (loadInst != nullptr) {
                    loadKind = classifyLoadHoist(inst, loopBody);
                    if (loadKind == LoadHoistKind::Reject) {
                        rejectedCandidates[inst] = "load rejected: clobbered or unsupported root";
                        continue;
                    }
                }

                if (callInst != nullptr) {
                    // 纯调用统一要求 latch 支配（每轮完整迭代必然执行）：
                    // 纯调用推测执行不会触发访存异常，仅零迭代循环会在
                    // preheader 多执行一次调用。这里假设纯函数对给定实参可
                    // 终止，与本 pass 对全局 load 的推测策略一致；可能读内存
                    // 的调用与循环写内存的顺序约束已由上方检查排除
                    if (!dominatesAllLoopLatches(bb, header, loopBody, domTree)) {
                        rejectedCandidates[inst] = "call rejected: not dominating all latches";
                        continue;
                    }
                } else if (loadInst != nullptr) {
                    // 形参根 load 的地址有效性由「首轮完整迭代必然执行该 load」
                    // 保证，因此要求 latch 支配；全局/本帧 alloca 根的 load
                    // 地址恒有效，可自由推测执行
                    if (loadKind == LoadHoistKind::LatchDominance &&
                        !dominatesAllLoopLatches(bb, header, loopBody, domTree)) {
                        rejectedCandidates[inst] = "param load rejected: not dominating all latches";
                        continue;
                    }
                } else if (requiresExitDominance(inst)) {
                    if (!dominatesAllLoopExits(bb, loopBody, domTree)) {
                        rejectedCandidates[inst] = "div/mod rejected: not dominating all exits";
                        continue;
                    }
                } else if (isSpeculatableDivision(inst) &&
                           !dominatesAllLoopLatches(bb, header, loopBody, domTree)) {
                    // 可推测的除法类指令虽不会陷入，但代价高：仅当每轮完整迭代
                    // 必然执行时才外提，避免把条件路径上的偶发除法变成每次进入
                    // 循环都要执行的固定开销
                    rejectedCandidates[inst] = "div rejected: not dominating all latches";
                    continue;
                }

                if (!dominatesAllUses(inst, domTree)) {
                    continue;
                }

                invariants.insert(inst);
                const char * hoistTag = "hoist arith";
                if (callInst != nullptr) {
                    hoistTag = "hoist call";
                } else if (loadInst != nullptr) {
                    hoistTag = loadKind == LoadHoistKind::LatchDominance ? "hoist load(param)"
                                                                        : "hoist load(global/local)";
                } else if (isSpeculatableDivision(inst)) {
                    hoistTag = "hoist div";
                }
                invariantOrder.emplace_back(inst, hoistTag);
                rejectedCandidates.erase(inst);
                discoveredNewInvariant = true;
            }
        }
    } while (discoveredNewInvariant);

    if (CostModel::remarksEnabled()) {
        for (const auto & entry : rejectedCandidates) {
            CostModel::remark("licm", false, entry.second);
        }
    }

    if (invariantOrder.empty()) {
        return false;
    }

    for (const auto & entry : invariantOrder) {
        moveToPreheader(entry.first, preheader);
        CostModel::remark("licm", true, entry.second);
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
    if (auto * call = dynamic_cast<CallInst *>(inst)) {
        return purityAnalysis != nullptr && purityAnalysis->isPure(call->getCallee());
    }

    if (dynamic_cast<LoadInst *>(inst)) {
        return true;
    }

    return isPureLoopInvariantOp(inst->getOp());
}

/// @brief 对 load 按指针根对象分类外提安全性
/// @param inst 待检查的 load 指令
/// @param loopBody 当前自然循环的块集合
/// @return 分类结果，见 LoadHoistKind
LICM::LoadHoistKind LICM::classifyLoadHoist(Instruction * inst,
                                            const std::unordered_set<BasicBlock *> & loopBody) const
{
    auto * load = dynamic_cast<LoadInst *>(inst);
    if (!load) {
        return LoadHoistKind::Reject;
    }

    Value * pointer = load->getPointerOperand();
    // 视为可能改写内存的调用：非纯函数（分析器缺失时保守处理）
    auto mayClobberCall = [this](CallInst * call) {
        return call != nullptr && (purityAnalysis == nullptr || !purityAnalysis->isPure(call->getCallee()));
    };

    Value * root = getPointerRoot(pointer);

    if (dynamic_cast<GlobalVariable *>(root) != nullptr) {
        // 全局变量的地址在程序启动时已静态分配，恒为可访问地址，推测执行其 load 不会
        // 触发访存异常。因此无需要求全局只读，只要循环内没有别名 store/调用改写该地址，
        // 外提即安全。blocksMayClobberLoad 已通过指针根对象比较判定别名
        return blocksMayClobberLoad(pointer, loopBody, mayClobberCall) ? LoadHoistKind::Reject
                                                                       : LoadHoistKind::Speculate;
    }

    if (auto * alloca = dynamic_cast<AllocaInst *>(root)) {
        MemoryLocation location = normalizeMemoryLocation(pointer);
        if (location.isPrecise() && !doesPointerEscape(location.object)) {
            // 精确非逃逸位点：沿用精确别名路径
            return blocksMayClobberLoad(pointer, loopBody, mayClobberCall) ? LoadHoistKind::Reject
                                                                           : LoadHoistKind::Speculate;
        }
        // 逃逸或非精确位点的本帧 alloca：栈地址恒有效，可推测执行；
        // 全局/其他 alloca/形参都不会与本帧对象别名，仅同根 store 与
        // 可能写内存的调用视为改写
        return loopMayClobberAllocaLoad(alloca, loopBody) ? LoadHoistKind::Reject : LoadHoistKind::Speculate;
    }

    if (auto * param = dynamic_cast<FormalParam *>(root)) {
        if (paramAliasAnalysis == nullptr) {
            return LoadHoistKind::Reject;
        }
        // 形参根地址由调用方保证进入循环时有效，但零迭代循环下推测执行仍可能
        // 访问越界地址，因此调用方要求 latch 支配（首轮完整迭代必然执行该 load）
        return loopMayClobberParamLoad(param, loopBody) ? LoadHoistKind::Reject
                                                        : LoadHoistKind::LatchDominance;
    }

    // 指针 phi/select 等未知根（如 LSR 改写后的指针游标）：保守拒绝
    return LoadHoistKind::Reject;
}

/// @brief 判断循环体是否可能改写以本帧 alloca 为根的 load 地址
/// @param root load 地址的根 alloca
/// @param loopBody 当前自然循环的块集合
/// @return true 表示存在可能的改写
bool LICM::loopMayClobberAllocaLoad(AllocaInst * root,
                                    const std::unordered_set<BasicBlock *> & loopBody) const
{
    for (auto * bb : loopBody) {
        for (auto * inst : bb->getInstructions()) {
            if (auto * store = dynamic_cast<StoreInst *>(inst)) {
                Value * storeRoot = getPointerRoot(store->getPointerOperand());
                if (storeRoot == root) {
                    return true;
                }
                // 其他 alloca/全局与本帧对象不别名；形参指向调用方的对象，
                // 不可能指向本帧 alloca（递归时指向的是调用方帧的同名对象）
                if (dynamic_cast<AllocaInst *>(storeRoot) != nullptr ||
                    dynamic_cast<GlobalVariable *>(storeRoot) != nullptr ||
                    dynamic_cast<FormalParam *>(storeRoot) != nullptr) {
                    continue;
                }
                return true;
            }
            if (auto * call = dynamic_cast<CallInst *>(inst)) {
                if (purityAnalysis == nullptr || !purityAnalysis->isPure(call->getCallee())) {
                    return true;
                }
                continue;
            }
            if (inst->mayWriteMemory()) {
                return true;
            }
        }
    }
    return false;
}

/// @brief 判断循环体是否可能改写以形参为根的 load 地址
/// @param root load 地址的根形参
/// @param loopBody 当前自然循环的块集合
/// @return true 表示存在可能的改写
bool LICM::loopMayClobberParamLoad(FormalParam * root,
                                   const std::unordered_set<BasicBlock *> & loopBody) const
{
    for (auto * bb : loopBody) {
        for (auto * inst : bb->getInstructions()) {
            if (auto * store = dynamic_cast<StoreInst *>(inst)) {
                Value * storeRoot = getPointerRoot(store->getPointerOperand());
                // 本帧 alloca 不可能被形参别名
                if (dynamic_cast<AllocaInst *>(storeRoot) != nullptr) {
                    continue;
                }
                if (auto * globalRoot = dynamic_cast<GlobalVariable *>(storeRoot)) {
                    if (paramAliasAnalysis->mayAliasGlobal(root, globalRoot)) {
                        return true;
                    }
                    continue;
                }
                if (auto * paramRoot = dynamic_cast<FormalParam *>(storeRoot)) {
                    if (paramAliasAnalysis->mayAliasParam(root, paramRoot)) {
                        return true;
                    }
                    continue;
                }
                return true;
            }
            if (auto * call = dynamic_cast<CallInst *>(inst)) {
                if (purityAnalysis == nullptr || !purityAnalysis->isPure(call->getCallee())) {
                    return true;
                }
                continue;
            }
            if (inst->mayWriteMemory()) {
                return true;
            }
        }
    }
    return false;
}

/// @brief 判断候选指令是否需要额外满足退出点支配约束
/// @param inst 待检查的指令
/// @return true 表示该指令不可安全推测执行
///
/// 仅整数除/模需要该约束（除零/INT_MIN 除 -1 在 x86 解释执行下会陷入）。
/// 非零且非 -1 的常量除数在任一执行环境都不会陷入，可推测执行；
/// 调用与 load 的安全性由 tryHoistLoop 中的专门路径处理，不经过本函数
bool LICM::requiresExitDominance(Instruction * inst) const
{
    if (!inst) {
        return false;
    }

    IRInstOperator op = inst->getOp();
    if (op == IRInstOperator::IRINST_OP_DIV_I || op == IRInstOperator::IRINST_OP_MOD_I) {
        auto operands = inst->getOperandsValue();
        if (operands.size() >= 2) {
            auto * divisor = dynamic_cast<ConstInteger *>(operands[1]);
            if (divisor != nullptr && divisor->getVal() != 0 && divisor->getVal() != -1) {
                return false;
            }
        }
        return true;
    }

    return needsExitDominance(op);
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

/// @brief 判断定义块是否支配当前循环的全部 latch 块
/// @param defBlock 候选指令所在基本块
/// @param header 循环头基本块
/// @param loopBody 当前自然循环的块集合
/// @param domTree 当前函数的支配树
/// @return true 表示每轮完整迭代都必然执行该定义块
bool LICM::dominatesAllLoopLatches(BasicBlock * defBlock,
                                   BasicBlock * header,
                                   const std::unordered_set<BasicBlock *> & loopBody,
                                   const DominatorTree & domTree) const
{
    if (!defBlock || !header) {
        return false;
    }

    for (auto * pred : header->getPredecessors()) {
        if (loopBody.find(pred) == loopBody.end()) {
            continue;
        }

        if (!domTree.dominates(defBlock, pred)) {
            return false;
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
