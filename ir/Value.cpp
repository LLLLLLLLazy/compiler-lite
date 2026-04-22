///
/// @file Value.cpp
/// @brief 值操作类型，所有的变量、函数、常量都是Value
///

#include "Value.h"

#include <algorithm>

#include "Use.h"
#include "User.h"

Value::Value(Type * _type) : type(_type)
{}

Value::~Value() = default;

std::string Value::getName() const
{
    return name;
}

void Value::setName(std::string _name)
{
    this->name = std::move(_name);
}

std::string Value::getIRName() const
{
    return IRName;
}

void Value::setIRName(std::string _name)
{
    this->IRName = std::move(_name);
}

Type * Value::getType()
{
    return type;
}

void Value::addUse(Use * use)
{
    uses.push_back(use);
}

void Value::removeUse(Use * use)
{
    auto pIter = std::find(uses.begin(), uses.end(), use);
    if (pIter != uses.end()) {
        uses.erase(pIter);
    }
}

void Value::removeUses()
{
    while (!uses.empty()) {
        Use * use = uses.front();
        use->remove();
        delete use;
    }
}

int32_t Value::getScopeLevel()
{
    return -1;
}

int32_t Value::getRegId()
{
    return -1;
}

bool Value::getMemoryAddr(int32_t * regId, int64_t * offset)
{
    (void) regId;
    (void) offset;
    return false;
}

int32_t Value::getLoadRegId()
{
    return -1;
}

void Value::setLoadRegId(int32_t regId)
{
    (void) regId;
}

void Value::replaceAllUseWith(Value * new_val)
{
    std::vector<Use *> usesCopy = uses;
    for (auto use: usesCopy) {
        auto user = dynamic_cast<User *>(use->getUser());
        if (!user) {
            continue;
        }

        for (int32_t index = 0; index < user->getOperandsNum(); ++index) {
            if (user->getOperand(index) == this) {
                user->setOperand(index, new_val);
                break;
            }
        }
    }
}
