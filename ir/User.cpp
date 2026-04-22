///
/// @file User.cpp
/// @brief 使用Value的User，该User也是Value。函数、指令都是User
///

#include "User.h"

#include <algorithm>

User::User(Type * _type) : Value(_type)
{}

void User::setOperand(int32_t pos, Value * val)
{
    if (pos < static_cast<int32_t>(operands.size())) {
        operands[pos]->setUsee(val);
    }
}

void User::addOperand(Value * val)
{
    auto use = new Use(val, this);
    operands.push_back(use);
    val->addUse(use);
}

void User::removeOperand(Value * val)
{
    for (auto & use: operands) {
        if (use->getUsee() == val) {
            use->remove();
            delete use;
            break;
        }
    }
}

void User::removeOperand(Use * use)
{
    removeUse(use);
    delete use;
}

void User::removeOperand(int pos)
{
    if (pos < static_cast<int32_t>(operands.size())) {
        Use * use = operands[pos];
        use->remove();
        delete use;
    }
}

void User::removeOperandRaw(Use * use)
{
    auto pIter = std::find(operands.begin(), operands.end(), use);
    if (pIter != operands.end()) {
        operands.erase(pIter);
    }
}

void User::removeUse(Use * use)
{
    auto pIter = std::find(operands.begin(), operands.end(), use);
    if (pIter != operands.end()) {
        use->remove();
    }
}

void User::clearOperands()
{
    while (!operands.empty()) {
        Use * use = operands.front();
        use->remove();
        delete use;
    }
}

std::vector<Use *> & User::getOperands()
{
    return operands;
}

std::vector<Value *> User::getOperandsValue()
{
    std::vector<Value *> operandsVec;
    operandsVec.reserve(operands.size());
    for (auto & use: operands) {
        operandsVec.emplace_back(use->getUsee());
    }
    return operandsVec;
}

int32_t User::getOperandsNum()
{
    return static_cast<int32_t>(operands.size());
}

Value * User::getOperand(int32_t pos)
{
    if (pos < static_cast<int32_t>(operands.size())) {
        return operands[pos]->getUsee();
    }

    return nullptr;
}

void User::swapTwoOperands()
{
    if (operands.size() == 2) {
        std::swap(operands[0], operands[1]);
    }
}

void User::replaceOperand(Value * val, Value * newVal)
{
    for (auto & use: operands) {
        if (use->getUsee() == val) {
            use->setUsee(newVal);
            break;
        }
    }
}
