///
/// @file FuncCallInstruction.h
/// @brief 函数调用指令
///

#pragma once

#include <vector>

#include "Instruction.h"

class Function;

class FuncCallInstruction : public Instruction {

public:
    Function * calledFunction = nullptr;

public:
    FuncCallInstruction(Function * _func, Function * calledFunc, std::vector<Value *> & _srcVal, Type * _type);

    void toString(std::string & str) override;

    [[nodiscard]] std::string getCalledName() const;
};
