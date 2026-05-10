///
/// @file SimpleLoopUnroll.cpp
/// @brief 小常数循环完全展开 pass 实现。
///

#include "SimpleLoopUnroll.h"

#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <vector>

#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "CondBranchInst.h"
#include "ConstInteger.h"
#include "FCmpInst.h"
#include "FPToSIInst.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "ICmpInst.h"
#include "LoadInst.h"
#include "Module.h"
#include "PhiInst.h"
#include "SIToFPInst.h"
#include "StoreInst.h"
#include "Value.h"
#include "ZExtInst.h"

namespace {

constexpr int32_t kMaxUnrollTripCount = 16;
constexpr int32_t kMaxBodyInsts = 32;

ConstInteger * asConstInt(Value * value)
{
    return dynamic_cast<ConstInteger *>(value);
}

bool isAddOne(Instruction * inst, Value * induction)
{
    auto * binary = dynamic_cast<BinaryInst *>(inst);
    if (!binary || binary->getOp() != IRInstOperator::IRINST_OP_ADD_I) {
        return false;
    }

    auto * rhsConst = asConstInt(binary->getRHS());
    if (binary->getLHS() == induction && rhsConst && rhsConst->getVal() == 1) {
        return true;
    }

    auto * lhsConst = asConstInt(binary->getLHS());
    return binary->getRHS() == induction && lhsConst && lhsConst->getVal() == 1;
}

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
        std::vector<BasicBlock *> blocks = func->getBlocks();
        for (auto * bb : blocks) {
            if (tryUnrollHeader(bb)) {
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

bool SimpleLoopUnroll::tryUnrollHeader(BasicBlock * header)
{
    if (!header) {
        return false;
    }

    auto * condBr = dynamic_cast<CondBranchInst *>(header->getTerminator());
    if (!condBr) {
        return false;
    }

    auto * cmp = dynamic_cast<ICmpInst *>(condBr->getCondition());
    if (!cmp || cmp->getParentBlock() != header || cmp->getOp() != IRInstOperator::IRINST_OP_LT_I) {
        return false;
    }

    auto * induction = dynamic_cast<PhiInst *>(cmp->getLHS());
    auto * bound = asConstInt(cmp->getRHS());
    if (!induction || induction->getParentBlock() != header || !bound) {
        return false;
    }

    BasicBlock * body = condBr->getTrueDest();
    BasicBlock * exit = condBr->getFalseDest();
    if (!body || !exit || body == header || exit == header || !hasSingleBranchTo(body, header)) {
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

    BasicBlock * preheader = nullptr;
    BasicBlock * latch = body;
    Value * inductionInit = nullptr;
    Instruction * inductionNext = nullptr;
    for (int32_t i = 0; i < induction->getIncomingCount(); ++i) {
        BasicBlock * incomingBlock = induction->getIncomingBlock(i);
        Value * incomingValue = induction->getIncomingValue(i);
        if (incomingBlock == latch) {
            inductionNext = dynamic_cast<Instruction *>(incomingValue);
        } else {
            preheader = incomingBlock;
            inductionInit = incomingValue;
        }
    }

    auto * initConst = asConstInt(inductionInit);
    if (!preheader || !initConst || !inductionNext || inductionNext->getParentBlock() != body ||
        !isAddOne(inductionNext, induction)) {
        return false;
    }

    const int32_t start = initConst->getVal();
    const int32_t end = bound->getVal();
    const int32_t tripCount = end - start;
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
        for (auto * phi : headerPhis) {
            if (phi == induction) {
                valueMap[phi] = mod->newConstInt32(start + iter);
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
                currentValues[phi] = mod->newConstInt32(start + iter + 1);
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
