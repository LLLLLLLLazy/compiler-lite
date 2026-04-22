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
    void insertValue(Value * value);

    Value * findCurrentScope(std::string name);

    int getCurrentScopeLevel();

    Value * findAllScope(std::string name);

    void enterScope();

    void leaveScope();

protected:
    std::vector<std::unordered_map<std::string, Value *>> valueStack;
};
