///
/// @file ScopeStack.h
/// @brief 作用域栈管理
///

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Value.h"

class ScopeStack {

public:
    /// @brief 向当前作用域插入一个值对象
    void insertValue(Value * value);

    /// @brief 在当前作用域内按名称查找值对象
    Value * findCurrentScope(std::string name);

    /// @brief 获取当前作用域层级
    int getCurrentScopeLevel();

    /// @brief 在所有作用域中按名称查找值对象
    Value * findAllScope(std::string name);

    /// @brief 进入一层新的作用域
    void enterScope();

    /// @brief 离开当前作用域
    void leaveScope();

protected:
    std::vector<std::unordered_map<std::string, Value *>> valueStack;
};
