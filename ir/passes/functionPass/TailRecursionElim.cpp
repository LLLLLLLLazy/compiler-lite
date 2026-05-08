///
/// @file TailRecursionElim.cpp
/// @brief 尾递归消除优化 pass 实现。
///
/// 将函数末尾的自身调用（尾递归）改写为参数 phi 循环，
/// 消除递归调用开销，避免深递归导致的栈溢出。
/// 改写后函数入口分裂为 entry → header，header 中为每个形参插入 phi 节点，
/// 尾递归调用点替换为向 phi 添加 incoming 并跳回 header 的循环。
///

#include "TailRecursionElim.h"

#include <algorithm>
#include <iterator>
#include <vector>

#include "BasicBlock.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "Function.h"
#include "PhiInst.h"
#include "ReturnInst.h"
#include "Value.h"

namespace {

/// @brief 尾递归调用点信息：包含调用指令、返回指令和所在基本块
struct TailSite {
    BasicBlock * block = nullptr;
    CallInst * call = nullptr;
    ReturnInst * ret = nullptr;
};

/// @brief 将基本块中 phi 指令的某个 incoming 前驱块替换为新块
void replacePhiIncomingBlock(BasicBlock * bb, BasicBlock * oldBlock, BasicBlock * newBlock)
{
    if (!bb || !oldBlock || !newBlock) {
        return;
    }

    for (auto * inst : bb->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        phi->replaceIncomingBlock(oldBlock, newBlock);
    }
}

/// @brief 将基本块移动到另一个基本块之后，保持块列表的合理顺序
void insertBlockAfter(Function * func, BasicBlock * block, BasicBlock * after)
{
    if (!func || !block || !after) {
        return;
    }

    auto & blocks = func->getBlocks();
    auto blockPos = std::find(blocks.begin(), blocks.end(), block);
    auto afterPos = std::find(blocks.begin(), blocks.end(), after);
    if (blockPos == blocks.end() || afterPos == blocks.end() || block == after) {
        return;
    }

    blocks.erase(blockPos);
    afterPos = std::find(blocks.begin(), blocks.end(), after);
    blocks.insert(std::next(afterPos), block);
}

} // namespace

/// @brief 构造尾递归消除 pass
/// @param _func 待优化的函数
TailRecursionElim::TailRecursionElim(Function * _func) : func(_func)
{}

/// @brief 执行尾递归消除，将尾自调用改写为参数 phi 循环
/// @return 若函数被修改则返回 true
bool TailRecursionElim::run()
{
    if (!func || func->isBuiltin() || func->getBlocks().empty() || func->getParams().empty()) {
        return false;
    }

    std::vector<TailSite> tailSites;
    for (auto * bb : func->getBlocks()) {
        auto & insts = bb->getInstructions();
        if (insts.size() < 2) {
            continue;
        }

        auto retIt = std::prev(insts.end());
        auto * ret = dynamic_cast<ReturnInst *>(*retIt);
        if (!ret || !ret->hasReturnValue()) {
            continue;
        }

        auto callIt = std::prev(retIt);
        auto * call = dynamic_cast<CallInst *>(*callIt);
        if (!call || call->getCallee() != func || ret->getReturnValue() != call) {
            continue;
        }

        if (call->getArgCount() != static_cast<int32_t>(func->getParams().size())) {
            continue;
        }

        tailSites.push_back({bb, call, ret});
    }

    if (tailSites.empty()) {
        return false;
    }

    BasicBlock * entry = func->getEntryBlock();
    if (!entry || !entry->getPredecessors().empty()) {
        return false;
    }

    auto * header = func->newBasicBlock();
    insertBlockAfter(func, header, entry);

    auto & entryInsts = entry->getInstructions();
    auto & headerInsts = header->getInstructions();
    headerInsts.splice(headerInsts.end(), entryInsts);
    for (auto * inst : headerInsts) {
        inst->setParentBlock(header);
    }

    std::vector<BasicBlock *> oldSuccessors = entry->getSuccessors();
    entry->getSuccessors().clear();
    for (auto * succ : oldSuccessors) {
        succ->removePredecessor(entry);
        succ->addPredecessor(header);
        header->addSuccessor(succ);
        replacePhiIncomingBlock(succ, entry, header);
    }

    auto * entryBranch = new BranchInst(func, header);
    entry->addInstruction(entryBranch);
    entry->linkSuccessor(header);

    std::vector<PhiInst *> paramPhis;
    auto insertPos = headerInsts.begin();
    for (auto * param : func->getParams()) {
        auto * phi = new PhiInst(func, param->getType());
        phi->addIncoming(param, entry);
        phi->setParentBlock(header);
        insertPos = std::next(headerInsts.insert(insertPos, phi));
        paramPhis.push_back(phi);
    }

    for (std::size_t i = 0; i < func->getParams().size(); ++i) {
        auto * param = func->getParams()[i];
        auto * phi = paramPhis[i];
        param->replaceAllUseWith(phi);
        phi->setOperand(0, param);
    }

    for (auto & site : tailSites) {
        std::vector<Value *> args;
        args.reserve(static_cast<std::size_t>(site.call->getArgCount()));
        for (int32_t i = 0; i < site.call->getArgCount(); ++i) {
            args.push_back(site.call->getArg(i));
        }

        for (std::size_t i = 0; i < paramPhis.size(); ++i) {
            paramPhis[i]->addIncoming(args[i], site.block);
        }

        auto & insts = site.block->getInstructions();
        auto callIt = std::find(insts.begin(), insts.end(), static_cast<Instruction *>(site.call));
        auto retIt = std::find(insts.begin(), insts.end(), static_cast<Instruction *>(site.ret));
        if (callIt == insts.end() || retIt == insts.end()) {
            return false;
        }

        site.ret->clearOperands();
        site.call->clearOperands();
        insts.erase(retIt);
        insts.erase(callIt);
        delete site.ret;
        delete site.call;

        auto * backBranch = new BranchInst(func, header);
        site.block->addInstruction(backBranch);
        site.block->linkSuccessor(header);
    }

    return true;
}
