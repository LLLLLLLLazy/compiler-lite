///
/// @file PhiInst.cpp
/// @brief φ 节点实现
///

#include "PhiInst.h"

#include "Function.h"

PhiInst::PhiInst(Function * func, Type * _type) : Instruction(func, IRInstOperator::IRINST_OP_PHI, _type)
{}

void PhiInst::addIncoming(Value * value, BasicBlock * block)
{
    incoming.push_back({value, block});
    addOperand(value);
}

void PhiInst::removeIncomingBlock(BasicBlock * block)
{
    for (int32_t i = static_cast<int32_t>(incoming.size()) - 1; i >= 0; --i) {
        if (incoming[i].block != block) {
            continue;
        }

        removeOperand(i);
        incoming.erase(incoming.begin() + i);
    }
}

void PhiInst::toString(std::string & str)
{
    str = getIRName() + " = phi " + getType()->toString() + " ";
    for (int32_t i = 0; i < static_cast<int32_t>(incoming.size()); ++i) {
        if (i > 0) {
            str += ", ";
        }
        Value * val = getOperand(i);
        str += "[ " + val->getIRName() + ", " + incoming[i].block->getIRName() + " ]";
    }
}
