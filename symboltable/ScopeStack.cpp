///
/// @file ScopeStack.cpp
/// @brief 作用域栈管理
///

#include "ScopeStack.h"

void ScopeStack::enterScope()
{
    valueStack.emplace_back();
}

void ScopeStack::leaveScope()
{
    valueStack.pop_back();
}

void ScopeStack::insertValue(Value * value)
{
    if (!value || value->getName().empty()) {
        return;
    }

    valueStack.back().insert(std::make_pair(value->getName(), value));
}

Value * ScopeStack::findCurrentScope(std::string name)
{
    auto it = valueStack.back().find(name);
    if (it != valueStack.back().end()) {
        return it->second;
    }
    return nullptr;
}

Value * ScopeStack::findAllScope(std::string name)
{
    for (auto it = valueStack.rbegin(); it != valueStack.rend(); ++it) {
        auto p = it->find(name);
        if (p != it->end()) {
            return p->second;
        }
    }
    return nullptr;
}

int ScopeStack::getCurrentScopeLevel()
{
    return static_cast<int>(valueStack.size()) - 1;
}
