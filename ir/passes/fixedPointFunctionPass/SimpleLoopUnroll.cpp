///
/// @file SimpleLoopUnroll.cpp
/// @brief 小常数循环完全展开 pass 实现。
///

#include "SimpleLoopUnroll.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <vector>

#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "CondBranchInst.h"
#include "ConstInteger.h"
#include "DominatorTree.h"
#include "FCmpInst.h"
#include "FPToSIInst.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "ICmpInst.h"
#include "LoadInst.h"
#include "LoopInfo.h"
#include "Module.h"
#include "PhiInst.h"
#include "ScalarEvolution.h"
#include "SIToFPInst.h"
#include "StoreInst.h"
#include "Value.h"
#include "ZExtInst.h"

namespace {

constexpr int32_t kMaxUnrollTripCount = 16;
constexpr int32_t kMaxBodyInsts = 32;

bool hasSingleBranchTo(BasicBlock * bb, BasicBlock * target)
{
    if (!bb || !target) {
        return false;
    }

    auto * branch = dynamic_cast<BranchInst *>(bb->getTerminator());
    return branch && branch->getTarget() == target;
}

bool isCloneableBodyInstruction(Instruction * inst)
{
    return dynamic_cast<BinaryInst *>(inst) != nullptr ||
           dynamic_cast<ICmpInst *>(inst) != nullptr ||
           dynamic_cast<FCmpInst *>(inst) != nullptr ||
           dynamic_cast<GetElementPtrInst *>(inst) != nullptr ||
           dynamic_cast<LoadInst *>(inst) != nullptr ||
           dynamic_cast<StoreInst *>(inst) != nullptr ||
           dynamic_cast<ZExtInst *>(inst) != nullptr ||
           dynamic_cast<SIToFPInst *>(inst) != nullptr ||
           dynamic_cast<FPToSIInst *>(inst) != nullptr;
}

/// @brief 计算完全展开时某次迭代对应的归纳变量常量值
bool tryComputeInductionValue(int32_t start, int32_t step, int32_t iteration, int32_t & value)
{
    const int64_t result = static_cast<int64_t>(start) + static_cast<int64_t>(step) * iteration;
    if (result < std::numeric_limits<int32_t>::min() || result > std::numeric_limits<int32_t>::max()) {
        return false;
    }

    value = static_cast<int32_t>(result);
    return true;
}

} // namespace

SimpleLoopUnroll::SimpleLoopUnroll(Function * _func, Module * _mod) : func(_func), mod(_mod)
{}

bool SimpleLoopUnroll::run()
{
    if (!func || !mod || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    bool changed = false;
    while (true) {
        bool localChanged = false;
        DominatorTree domTree(func);
        LoopInfo loopInfo(func, &domTree);
        ScalarEvolution scev(func, &domTree, &loopInfo);
        std::vector<BasicBlock *> blocks = func->getBlocks();
        for (auto * bb : blocks) {
            if (tryUnrollHeader(bb, scev)) {
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

bool SimpleLoopUnroll::tryUnrollHeader(BasicBlock * header, ScalarEvolution & scev)
{
    if (!header) {
        return false;
    }

    ScalarEvolution::CanonicalLoop loop;
    if (!scev.matchCanonicalLoop(header, loop) || !loop.hasConstInitialValue || !loop.hasConstTripCount ||
        !loop.recurrence || loop.recurrence->getStep() == 0) {
        return false;
    }

    BasicBlock * body = loop.body;
    BasicBlock * exit = loop.exit;
    if (!body || !exit || body == header || exit == header || loop.latch != body || !hasSingleBranchTo(body, header)) {
        return false;
    }

    std::vector<Instruction *> bodyInsts;
    for (auto * inst : body->getInstructions()) {
        if (inst->isTerminator()) {
            continue;
        }
        if (dynamic_cast<PhiInst *>(inst) || dynamic_cast<CallInst *>(inst)) {
            return false;
        }
        if (!isCloneableBodyInstruction(inst)) {
            return false;
        }
        bodyInsts.push_back(inst);
    }
    if (bodyInsts.empty() || static_cast<int32_t>(bodyInsts.size()) > kMaxBodyInsts) {
        return false;
    }

    BasicBlock * latch = body;
    BasicBlock * preheader = loop.preheader;
    PhiInst * induction = loop.induction;
    if (!preheader || !induction) {
        return false;
    }

    const int32_t start = loop.initialIntValue;
    const int32_t step = loop.recurrence->getStep();
    const int32_t tripCount = loop.tripCount;
    if (tripCount <= 0 || tripCount > kMaxUnrollTripCount) {
        return false;
    }

    if (!hasSingleBranchTo(preheader, header)) {
        return false;
    }

    std::vector<PhiInst *> headerPhis;
    for (auto * inst : header->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        headerPhis.push_back(phi);
    }
    if (headerPhis.empty()) {
        return false;
    }

    std::unordered_map<PhiInst *, Value *> currentValues;
    for (auto * phi : headerPhis) {
        Value * initValue = nullptr;
        for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
            if (phi->getIncomingBlock(i) == preheader) {
                initValue = phi->getIncomingValue(i);
                break;
            }
        }
        if (!initValue) {
            return false;
        }
        currentValues[phi] = initValue;
    }

    auto & preheaderInsts = preheader->getInstructions();
    auto insertPos = std::prev(preheaderInsts.end());

    for (int32_t iter = 0; iter < tripCount; ++iter) {
        std::unordered_map<Value *, Value *> valueMap;
        int32_t iterValue = 0;
        if (!tryComputeInductionValue(start, step, iter, iterValue)) {
            return false;
        }
        for (auto * phi : headerPhis) {
            if (phi == induction) {
                valueMap[phi] = mod->newConstInteger(induction->getType(), iterValue);
            } else {
                valueMap[phi] = currentValues[phi];
            }
        }

        auto mapValue = [&valueMap](Value * value) -> Value * {
            auto it = valueMap.find(value);
            return it == valueMap.end() ? value : it->second;
        };

        for (auto * inst : bodyInsts) {
            Instruction * cloned = cloneInstruction(inst);
            if (!cloned) {
                return false;
            }

            for (int32_t operand = 0; operand < cloned->getOperandsNum(); ++operand) {
                cloned->setOperand(operand, mapValue(cloned->getOperand(operand)));
            }

            cloned->setParentBlock(preheader);
            preheaderInsts.insert(insertPos, cloned);
            if (inst->hasResultValue()) {
                valueMap[inst] = cloned;
            }
        }

        for (auto * phi : headerPhis) {
            if (phi == induction) {
                int32_t nextValue = 0;
                if (!tryComputeInductionValue(start, step, iter + 1, nextValue)) {
                    return false;
                }
                currentValues[phi] = mod->newConstInteger(induction->getType(), nextValue);
                continue;
            }

            Value * carried = nullptr;
            for (int32_t incoming = 0; incoming < phi->getIncomingCount(); ++incoming) {
                if (phi->getIncomingBlock(incoming) == latch) {
                    carried = phi->getIncomingValue(incoming);
                    break;
                }
            }
            if (!carried) {
                return false;
            }
            currentValues[phi] = mapValue(carried);
        }
    }

    for (auto * phi : headerPhis) {
        phi->replaceAllUseWith(currentValues[phi]);
    }

    auto * preheaderBranch = dynamic_cast<BranchInst *>(preheader->getTerminator());
    if (!preheaderBranch || preheaderBranch->getTarget() != header) {
        return false;
    }
    preheaderBranch->setTarget(exit);
    preheader->removeSuccessor(header);
    preheader->addSuccessor(exit);
    header->removePredecessor(preheader);
    exit->addPredecessor(preheader);

    return true;
}

Instruction * SimpleLoopUnroll::cloneInstruction(Instruction * inst)
{
    if (!inst || !func) {
        return nullptr;
    }

    if (auto * binary = dynamic_cast<BinaryInst *>(inst)) {
        return new BinaryInst(func, binary->getOp(), binary->getLHS(), binary->getRHS(), binary->getType());
    }
    if (auto * icmp = dynamic_cast<ICmpInst *>(inst)) {
        return new ICmpInst(func, icmp->getOp(), icmp->getLHS(), icmp->getRHS(), icmp->getType());
    }
    if (auto * fcmp = dynamic_cast<FCmpInst *>(inst)) {
        return new FCmpInst(func, fcmp->getOp(), fcmp->getLHS(), fcmp->getRHS(), fcmp->getType());
    }
    if (auto * gep = dynamic_cast<GetElementPtrInst *>(inst)) {
        return new GetElementPtrInst(func,
                                     gep->getBasePointer(),
                                     gep->getIndexOperand(),
                                     gep->getType(),
                                     gep->isArrayDecayGEP());
    }
    if (auto * load = dynamic_cast<LoadInst *>(inst)) {
        return new LoadInst(func, load->getPointerOperand(), load->getType());
    }
    if (auto * store = dynamic_cast<StoreInst *>(inst)) {
        return new StoreInst(func, store->getValueOperand(), store->getPointerOperand());
    }
    if (auto * zext = dynamic_cast<ZExtInst *>(inst)) {
        return new ZExtInst(func, zext->getSource(), zext->getType());
    }
    if (auto * sitofp = dynamic_cast<SIToFPInst *>(inst)) {
        return new SIToFPInst(func, sitofp->getSource(), sitofp->getType());
    }
    if (auto * fptosi = dynamic_cast<FPToSIInst *>(inst)) {
        return new FPToSIInst(func, fptosi->getSource(), fptosi->getType());
    }

    return nullptr;
}
