///
/// @file LoopVersionInvariantSelect.cpp
/// @brief 循环不变 select 条件版本化 pass 实现
///
/// 思路：循环体内的 select 若其条件为循环不变量，则该 select 在循环的每次
/// 迭代中必然选择同一侧操作数。把循环克隆为「条件为真 / 条件为假」两个版本，
/// 各自把 select 直接替换为对应分支操作数，并在 preheader 用该条件做一次
/// 选路。循环体按版本分别化简后，select 的分支/拷贝序列与未选中侧的死代码
/// 一并消失。
///
/// 与 LSR 版本化共用同款克隆-重映射-出口 phi 修复机制，但不要求规范旋转
/// 计数循环：任意带唯一 preheader、指令均可克隆的循环都能版本化。多个不同
/// 不变条件在 pass 内部多轮处理（每轮版本化一个条件，轮数有上限，避免
/// 2^N 代码膨胀）。
///

#include "LoopVersionInvariantSelect.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CondBranchInst.h"
#include "DominatorTree.h"
#include "FCmpInst.h"
#include "FPToSIInst.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "ICmpInst.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "LoopInfo.h"
#include "Module.h"
#include "PhiInst.h"
#include "SelectInst.h"
#include "SIToFPInst.h"
#include "StoreInst.h"
#include "Value.h"
#include "ZExtInst.h"
#include "AnalysisCache.h"
#include "fixedPointFunctionPass/ConstProp.h"
#include "fixedPointFunctionPass/DeadInstElim.h"
#include "fixedPointFunctionPass/InstCombine.h"

namespace {

constexpr int32_t kMaxBodyBlocks = 16;
constexpr int32_t kMaxBodyInsts = 64;
constexpr int32_t kMaxVersionRounds = 4;
constexpr int32_t kMaxClonedLoops = 8;

bool isDefinedInLoop(Value * value, const std::unordered_set<BasicBlock *> & loopBody)
{
    auto * inst = dynamic_cast<Instruction *>(value);
    return inst && inst->getParentBlock() && loopBody.find(inst->getParentBlock()) != loopBody.end();
}

bool isLoopInvariantValue(Value * value, const std::unordered_set<BasicBlock *> & loopBody)
{
    return !isDefinedInLoop(value, loopBody);
}

/// @brief 把 phi 插入块首 phi 前缀之后
void insertPhiAtTop(BasicBlock * bb, PhiInst * phi)
{
    if (!bb || !phi) {
        return;
    }

    auto & insts = bb->getInstructions();
    auto insertPos = insts.begin();
    while (insertPos != insts.end() && dynamic_cast<PhiInst *>(*insertPos) != nullptr) {
        ++insertPos;
    }

    phi->setParentBlock(bb);
    insts.insert(insertPos, phi);
}

/// @brief 找唯一环外前驱，且其唯一后继是 header（版本化选路要改写它）
BasicBlock * findExistingPreheader(BasicBlock * header, const std::unordered_set<BasicBlock *> & loopBody)
{
    if (!header) {
        return nullptr;
    }

    BasicBlock * preheader = nullptr;
    for (auto * pred : header->getPredecessors()) {
        if (loopBody.find(pred) != loopBody.end()) {
            continue;
        }

        if (preheader != nullptr) {
            return nullptr;
        }
        preheader = pred;
    }

    if (!preheader || preheader->getSuccessors().size() != 1) {
        return nullptr;
    }

    return preheader;
}

bool isCloneableInstruction(Instruction * inst)
{
    return dynamic_cast<BinaryInst *>(inst) != nullptr || dynamic_cast<ICmpInst *>(inst) != nullptr ||
           dynamic_cast<FCmpInst *>(inst) != nullptr || dynamic_cast<LoadInst *>(inst) != nullptr ||
           dynamic_cast<StoreInst *>(inst) != nullptr || dynamic_cast<GetElementPtrInst *>(inst) != nullptr ||
           dynamic_cast<ZExtInst *>(inst) != nullptr || dynamic_cast<SelectInst *>(inst) != nullptr ||
           dynamic_cast<SIToFPInst *>(inst) != nullptr || dynamic_cast<FPToSIInst *>(inst) != nullptr ||
           dynamic_cast<PhiInst *>(inst) != nullptr;
}

Instruction * cloneInstructionShell(Instruction * inst, Function * func)
{
    if (auto * binary = dynamic_cast<BinaryInst *>(inst)) {
        return new BinaryInst(func, binary->getOp(), binary->getLHS(), binary->getRHS(), binary->getType());
    }
    if (auto * icmp = dynamic_cast<ICmpInst *>(inst)) {
        return new ICmpInst(func, icmp->getOp(), icmp->getLHS(), icmp->getRHS(), icmp->getType());
    }
    if (auto * fcmp = dynamic_cast<FCmpInst *>(inst)) {
        return new FCmpInst(func, fcmp->getOp(), fcmp->getLHS(), fcmp->getRHS(), fcmp->getType());
    }
    if (auto * load = dynamic_cast<LoadInst *>(inst)) {
        return new LoadInst(func, load->getPointerOperand(), load->getType());
    }
    if (auto * store = dynamic_cast<StoreInst *>(inst)) {
        return new StoreInst(func, store->getValueOperand(), store->getPointerOperand());
    }
    if (auto * gep = dynamic_cast<GetElementPtrInst *>(inst)) {
        return new GetElementPtrInst(func, gep->getBasePointer(), gep->getIndexOperand(), gep->getType(),
                                     gep->isArrayDecayGEP(), gep->isIndexPreScaled());
    }
    if (auto * zext = dynamic_cast<ZExtInst *>(inst)) {
        return new ZExtInst(func, zext->getSource(), zext->getType());
    }
    if (auto * select = dynamic_cast<SelectInst *>(inst)) {
        return new SelectInst(func, select->getCondition(), select->getTrueValue(), select->getFalseValue(),
                              select->getType());
    }
    if (auto * sitofp = dynamic_cast<SIToFPInst *>(inst)) {
        return new SIToFPInst(func, sitofp->getSource(), sitofp->getType());
    }
    if (auto * fptosi = dynamic_cast<FPToSIInst *>(inst)) {
        return new FPToSIInst(func, fptosi->getSource(), fptosi->getType());
    }
    if (auto * phi = dynamic_cast<PhiInst *>(inst)) {
        return new PhiInst(func, phi->getType());
    }
    return nullptr;
}

} // namespace

LoopVersionInvariantSelect::LoopVersionInvariantSelect(Function * _func, Module * _mod)
    : func(_func), mod(_mod)
{}

bool LoopVersionInvariantSelect::run()
{
    if (!func || !mod || func->isBuiltin() || func->getBlocks().empty() ||
        std::getenv("MINIC_DISABLE_SELECT_VERSIONING") != nullptr) {
        return false;
    }

    bool changed = false;
    int32_t clonedLoops = 0;
    auto & cache = func->getAnalysisCache();

    // 每轮版本化一个不变条件；多个不同条件由后续轮次继续处理。
    // 轮数与克隆数双重上限，防止 2^N 代码膨胀。
    for (int32_t round = 0; round < kMaxVersionRounds && clonedLoops < kMaxClonedLoops; ++round) {
        auto & domTree = cache.getOrCompute<DominatorTree>([this] { return DominatorTree(func); });
        auto & loopInfo =
            cache.getOrCompute<LoopInfo>([this, &domTree] { return LoopInfo(func, &domTree); });

        std::vector<BasicBlock *> headers;
        for (auto * bb : func->getBlocks()) {
            if (loopInfo.isLoopHeader(bb)) {
                headers.push_back(bb);
            }
        }

        // 最内层优先：先版本化内层循环，外层循环克隆时顺带把内层 select 一并固化
        std::stable_sort(headers.begin(), headers.end(), [&loopInfo](BasicBlock * lhs, BasicBlock * rhs) {
            return loopInfo.getLoopDepth(lhs) > loopInfo.getLoopDepth(rhs);
        });

        bool versioned = false;
        for (auto * header : headers) {
            if (versionedHeaders.find(header) != versionedHeaders.end()) {
                continue;
            }
            // 仅版本化深度 1 的最外层循环：嵌套循环的出口值会经外层循环 phi
            // 流转，克隆出口边无法正确接入外层 phi，强行版本化会破坏外层状态
            if (loopInfo.getLoopDepth(header) != 1) {
                continue;
            }
            const auto * bodyPtr = loopInfo.getLoopBody(header);
            if (!bodyPtr || bodyPtr->empty()) {
                continue;
            }
            const auto & loopBody = *bodyPtr;
            if (loopBody.size() > kMaxBodyBlocks) {
                continue;
            }

            BasicBlock * preheader = findExistingPreheader(header, loopBody);
            if (preheader == nullptr) {
                continue;
            }

            // 收益门控：循环体足够小才值得克隆一份
            int32_t liveInsts = 0;
            bool cloneable = true;
            for (auto * bb : loopBody) {
                for (auto * inst : bb->getInstructions()) {
                    if (inst->isDead()) {
                        continue;
                    }
                    if (inst->isTerminator()) {
                        if (dynamic_cast<BranchInst *>(inst) == nullptr &&
                            dynamic_cast<CondBranchInst *>(inst) == nullptr) {
                            cloneable = false;
                            break;
                        }
                        continue;
                    }
                    if (dynamic_cast<PhiInst *>(inst) == nullptr) {
                        ++liveInsts;
                    }
                    if (!isCloneableInstruction(inst)) {
                        cloneable = false;
                        break;
                    }
                }
                if (!cloneable) {
                    break;
                }
            }
            if (!cloneable || liveInsts > kMaxBodyInsts) {
                continue;
            }

            // 唯一出口：环内块的所有环外后继必须是同一个块，否则出口值合并
            // 的克隆补边无法确定唯一接收者（tryVersionLoop 内还会做逐值逃逸检查）
            BasicBlock * uniqueExit = nullptr;
            bool uniqueExitOk = true;
            for (auto * bb : loopBody) {
                for (auto * succ : bb->getSuccessors()) {
                    if (loopBody.find(succ) != loopBody.end()) {
                        continue;
                    }
                    if (uniqueExit == nullptr) {
                        uniqueExit = succ;
                    } else if (uniqueExit != succ) {
                        uniqueExitOk = false;
                        break;
                    }
                }
                if (!uniqueExitOk) {
                    break;
                }
            }
            if (!uniqueExitOk) {
                continue;
            }

            // 收集条件为循环不变的 select，按条件分组；本轮处理第一个条件
            std::unordered_map<Value *, std::vector<SelectInst *>> grouped;
            for (auto * bb : loopBody) {
                for (auto * inst : bb->getInstructions()) {
                    auto * select = dynamic_cast<SelectInst *>(inst);
                    if (!select || select->isDead()) {
                        continue;
                    }
                    Value * cond = select->getCondition();
                    if (isLoopInvariantValue(cond, loopBody)) {
                        grouped[cond].push_back(select);
                    }
                }
            }
            if (grouped.empty()) {
                continue;
            }
            auto best = std::max_element(grouped.begin(), grouped.end(),
                                         [](const auto & a, const auto & b) { return a.second.size() < b.second.size(); });
            if (best == grouped.end()) {
                continue;
            }

            if (!tryVersionLoop(header, loopBody, best->first)) {
                continue;
            }
            versioned = true;
            changed = true;
            ++clonedLoops;

            // 版本化改写了 CFG，下一轮必须重新识别循环；版本化后 select 已被
            // 固化，用一轮 ConstProp/InstCombine/死指令清扫把未选中侧的死链折叠掉
            cache.invalidateCFGAnalyses();
            ConstProp constProp(func, mod);
            constProp.run();
            InstCombine instCombine(func, mod);
            instCombine.run();
            DeadInstElim deadInstElim(func);
            deadInstElim.run();
            cache.invalidateCFGAnalyses();
            break;
        }

        if (!versioned) {
            break;
        }
    }

    return changed;
}

bool LoopVersionInvariantSelect::tryVersionLoop(BasicBlock * header,
                                                const std::unordered_set<BasicBlock *> & loopBody,
                                                Value * cond)
{
    BasicBlock * preheader = findExistingPreheader(header, loopBody);
    if (preheader == nullptr) {
        return false;
    }
    auto * preBr = dynamic_cast<BranchInst *>(preheader->getTerminator());
    if (preBr == nullptr || preBr->getTarget() != header) {
        return false;
    }

    // 收集本轮要固化的 select（条件均为 cond）
    std::vector<SelectInst *> selects;
    for (auto * bb : loopBody) {
        for (auto * inst : bb->getInstructions()) {
            auto * select = dynamic_cast<SelectInst *>(inst);
            if (select && !select->isDead() && select->getCondition() == cond) {
                selects.push_back(select);
            }
        }
    }
    if (selects.empty()) {
        return false;
    }

    // 唯一出口块（run() 已确认存在）与出口块内逃逸值的 LCSSA 补全。
    // 环内定义值在环外非 phi 使用时，仅在唯一出口块内允许：插入出口 phi，
    // 环内前驱入边取该值、环外前驱取 header phi 的对应初值（无法取初值时放弃）。
    BasicBlock * exitBlock = nullptr;
    for (auto * bb : loopBody) {
        for (auto * succ : bb->getSuccessors()) {
            if (loopBody.find(succ) == loopBody.end()) {
                exitBlock = succ;
                break;
            }
        }
        if (exitBlock != nullptr) {
            break;
        }
    }
    if (exitBlock == nullptr) {
        return false;
    }

    // 出口块内逃逸值的 LCSSA 补全。先收集所有「环内值 → 出口块内非 phi 使用」，
    // 再逐值插入出口 phi 并统一改写（不可在遍历 use 列表的同时 setOperand）。
    struct EscapeUse {
        Value * value = nullptr;
        Instruction * user = nullptr;
    };
    std::vector<EscapeUse> escapes;
    for (auto * bb : loopBody) {
        for (auto * inst : bb->getInstructions()) {
            if (inst->isDead()) {
                continue;
            }
            for (auto * use : inst->getUseList()) {
                auto * user = dynamic_cast<Instruction *>(use->getUser());
                if (!user || !user->getParentBlock()) {
                    return false;
                }
                BasicBlock * userBB = user->getParentBlock();
                if (loopBody.find(userBB) != loopBody.end()) {
                    continue;
                }
                // 环外使用允许两种形态：
                //  1) 出口块内的 phi 入边（克隆后由出口补边逻辑补克隆入边）；
                //  2) 出口块内的非 phi 使用（此处插入出口 phi）。
                if (userBB != exitBlock) {
                    return false;
                }
                if (dynamic_cast<PhiInst *>(user) != nullptr) {
                    continue;
                }
                escapes.push_back({inst, user});
            }
        }
    }
    for (const auto & esc : escapes) {
        auto * inst = esc.value;
        auto * exitPhi = new PhiInst(func, inst->getType());
        auto * headerPhi = dynamic_cast<PhiInst *>(inst);
        for (auto * pred : exitBlock->getPredecessors()) {
            if (loopBody.find(pred) != loopBody.end()) {
                exitPhi->addIncoming(inst, pred);
            } else if (headerPhi != nullptr) {
                Value * initVal = nullptr;
                for (int32_t i = 0; i < headerPhi->getIncomingCount(); ++i) {
                    if (headerPhi->getIncomingBlock(i) == pred) {
                        initVal = headerPhi->getIncomingValue(i);
                        break;
                    }
                }
                if (initVal == nullptr) {
                    delete exitPhi;
                    return false;
                }
                exitPhi->addIncoming(initVal, pred);
            } else {
                // 非 phi 值在环外前驱上无定义，放弃版本化
                delete exitPhi;
                return false;
            }
        }
        insertPhiAtTop(exitBlock, exitPhi);
        for (auto * u : exitBlock->getInstructions()) {
            if (u == exitPhi) {
                continue;
            }
            for (int32_t i = 0; i < u->getOperandsNum(); ++i) {
                if (u->getOperand(i) == inst) {
                    u->setOperand(i, exitPhi);
                }
            }
        }
    }

    std::vector<BasicBlock *> orderedLoopBlocks;
    for (auto * bb : func->getBlocks()) {
        if (loopBody.find(bb) != loopBody.end()) {
            orderedLoopBlocks.push_back(bb);
        }
    }

    // 快（条件为真）/慢（条件为假）两个前置空块；慢侧维持 header 的前驱形态
    auto * fastPre = func->newBasicBlock();
    auto * slowPre = func->newBasicBlock();

    std::unordered_map<BasicBlock *, BasicBlock *> blockMap;
    for (auto * bb : orderedLoopBlocks) {
        blockMap[bb] = func->newBasicBlock();
    }

    std::unordered_map<Value *, Value *> valueMap;
    auto mapValue = [&valueMap](Value * value) -> Value * {
        auto it = valueMap.find(value);
        return it == valueMap.end() ? value : it->second;
    };
    auto mapBlock = [&blockMap](BasicBlock * bb) -> BasicBlock * {
        auto it = blockMap.find(bb);
        return it == blockMap.end() ? bb : it->second;
    };

    std::vector<std::pair<Instruction *, Instruction *>> clonedInsts;
    for (auto * bb : orderedLoopBlocks) {
        BasicBlock * cloneBB = blockMap[bb];
        for (auto * inst : bb->getInstructions()) {
            if (inst->isDead() || inst->isTerminator()) {
                continue;
            }
            Instruction * cloned = cloneInstructionShell(inst, func);
            cloneBB->addInstruction(cloned);
            clonedInsts.push_back({inst, cloned});
            if (inst->hasResultValue()) {
                valueMap[inst] = cloned;
            }
        }
    }

    for (auto & [orig, cloned] : clonedInsts) {
        if (auto * origPhi = dynamic_cast<PhiInst *>(orig)) {
            auto * clonedPhi = static_cast<PhiInst *>(cloned);
            for (int32_t i = 0; i < origPhi->getIncomingCount(); ++i) {
                BasicBlock * inBlock = origPhi->getIncomingBlock(i);
                clonedPhi->addIncoming(mapValue(origPhi->getIncomingValue(i)),
                                       inBlock == preheader ? fastPre : mapBlock(inBlock));
            }
            continue;
        }
        for (int32_t i = 0; i < cloned->getOperandsNum(); ++i) {
            cloned->setOperand(i, mapValue(cloned->getOperand(i)));
        }
    }

    // 克隆版（条件为真）：select 固化为 true 侧操作数；原循环（条件为假）固化为 false 侧
    for (auto * select : selects) {
        auto * clonedSelect = static_cast<Instruction *>(valueMap.at(select));
        clonedSelect->replaceAllUseWith(mapValue(select->getTrueValue()));
        clonedSelect->clearOperands();
        clonedSelect->setDead(true);

        select->replaceAllUseWith(select->getFalseValue());
        select->clearOperands();
        select->setDead(true);
    }

    for (auto * bb : orderedLoopBlocks) {
        BasicBlock * cloneBB = blockMap[bb];
        Instruction * term = bb->getTerminator();
        if (auto * br = dynamic_cast<BranchInst *>(term)) {
            BasicBlock * target = mapBlock(br->getTarget());
            auto * clonedBr = new BranchInst(func, target);
            cloneBB->addInstruction(clonedBr);
            cloneBB->linkSuccessor(target);
            continue;
        }
        auto * condBr = static_cast<CondBranchInst *>(term);
        BasicBlock * trueTarget = mapBlock(condBr->getTrueDest());
        BasicBlock * falseTarget = mapBlock(condBr->getFalseDest());
        auto * clonedCond = new CondBranchInst(func, mapValue(condBr->getCondition()), trueTarget, falseTarget);
        cloneBB->addInstruction(clonedCond);
        cloneBB->linkSuccessor(trueTarget);
        if (falseTarget != trueTarget) {
            cloneBB->linkSuccessor(falseTarget);
        }
    }

    // 出口块 phi 补入克隆出口边（每个环内前驱边对应一条克隆边）
    for (auto * bb : orderedLoopBlocks) {
        for (auto * succ : bb->getSuccessors()) {
            if (loopBody.find(succ) != loopBody.end()) {
                continue;
            }
            for (auto * inst : succ->getInstructions()) {
                auto * exitPhi = dynamic_cast<PhiInst *>(inst);
                if (!exitPhi) {
                    break;
                }
                for (int32_t i = 0; i < exitPhi->getIncomingCount(); ++i) {
                    BasicBlock * inBlock = exitPhi->getIncomingBlock(i);
                    if (loopBody.find(inBlock) != loopBody.end()) {
                        exitPhi->addIncoming(mapValue(exitPhi->getIncomingValue(i)), blockMap[inBlock]);
                    }
                }
            }
        }
    }

    // 选路接线：preheader --cond--> fastPre --> 克隆循环
    //                    \--else--> slowPre --> 原循环
    auto & preInsts = preheader->getInstructions();
    preInsts.pop_back();
    preBr->clearOperands();
    delete preBr;
    auto * gateBr = new CondBranchInst(func, cond, fastPre, slowPre);
    gateBr->setParentBlock(preheader);
    preInsts.push_back(gateBr);
    preheader->removeSuccessor(header);
    header->removePredecessor(preheader);
    preheader->linkSuccessor(fastPre);
    preheader->linkSuccessor(slowPre);

    auto * fastBr = new BranchInst(func, blockMap[header]);
    fastPre->addInstruction(fastBr);
    fastPre->linkSuccessor(blockMap[header]);

    auto * slowBr = new BranchInst(func, header);
    slowPre->addInstruction(slowBr);
    slowPre->linkSuccessor(header);

    // 原 header 的 phi 前驱边 preheader -> slowPre
    for (auto * inst : header->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        phi->replaceIncomingBlock(preheader, slowPre);
    }

    versionedHeaders.insert(header);
    versionedHeaders.insert(blockMap[header]);
    return true;
}
