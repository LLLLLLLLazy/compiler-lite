///
/// @file ScopeStack.cpp
/// @brief 作用域栈管理
///

#include "ScopeStack.h"

/// @brief 进入一层新的作用域
void ScopeStack::enterScope()
{
    valueStack.emplace_back();
}

/// @brief 离开当前作用域
void ScopeStack::leaveScope()
{
    valueStack.pop_back();
}

/// @brief 向当前作用域插入一个值对象
/// @param value 待插入的值对象
void ScopeStack::insertValue(Value * value)
{
    if (!value || value->getName().empty()) {
        return;
    }

    valueStack.back().insert(std::make_pair(value->getName(), value));
}

/// @brief 在当前作用域内按名称查找值对象
/// @param name 标识符名称
/// @return 查找到的值对象，未找到时返回空指针
Value * ScopeStack::findCurrentScope(std::string name)
{
    auto it = valueStack.back().find(name);
    if (it != valueStack.back().end()) {
        return it->second;
    }
    return nullptr;
}

/// @brief 在所有作用域中按名称查找值对象
/// @param name 标识符名称
/// @return 从内向外查找到的第一个值对象，未找到时返回空指针
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

/// @brief 获取当前作用域层级
/// @return 当前作用域层级编号
int ScopeStack::getCurrentScopeLevel()
{
    return static_cast<int>(valueStack.size()) - 1;
}
