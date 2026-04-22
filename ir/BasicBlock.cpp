///
/// @file BasicBlock.cpp
/// @brief 基本块实现
///

#include "BasicBlock.h"

#include <algorithm>

#include "Function.h"
#include "Instruction.h"
#include "Types/VoidType.h"

BasicBlock::BasicBlock(Function * _parent) : Value(VoidType::getType()), parent(_parent)
{}

BasicBlock::~BasicBlock()
{
    for (auto * inst : insts) {
        delete inst;
    }
    insts.clear();
}

void BasicBlock::addInstruction(Instruction * inst)
{
    inst->setParentBlock(this);
    insts.push_back(inst);
}

Instruction * BasicBlock::getTerminator()
{
    if (insts.empty()) {
        return nullptr;
    }
    Instruction * last = insts.back();
    return last->isTerminator() ? last : nullptr;
}

bool BasicBlock::isTerminated() const
{
    if (insts.empty()) {
        return false;
    }
    return insts.back()->isTerminator();
}

void BasicBlock::addPredecessor(BasicBlock * bb)
{
    if (std::find(preds.begin(), preds.end(), bb) == preds.end()) {
        preds.push_back(bb);
    }
}

void BasicBlock::addSuccessor(BasicBlock * bb)
{
    if (std::find(succs.begin(), succs.end(), bb) == succs.end()) {
        succs.push_back(bb);
    }
}

void BasicBlock::removePredecessor(BasicBlock * bb)
{
    preds.erase(std::remove(preds.begin(), preds.end(), bb), preds.end());
}

void BasicBlock::removeSuccessor(BasicBlock * bb)
{
    succs.erase(std::remove(succs.begin(), succs.end(), bb), succs.end());
}

void BasicBlock::linkSuccessor(BasicBlock * succ)
{
    addSuccessor(succ);
    succ->addPredecessor(this);
}

void BasicBlock::toString(std::string & str)
{
    // Strip leading '%' from IR name to get the plain label text (e.g. "%entry" -> "entry")
    std::string label = getIRName();
    if (!label.empty() && label.front() == '%') {
        label = label.substr(1);
    }
    if (!label.empty()) {
        str += label + ":\n";
    }
    for (auto * inst : insts) {
        std::string instStr;
        inst->toString(instStr);
        if (!instStr.empty()) {
            str += "  " + instStr + "\n";
        }
    }
}
