///
/// @file CallInst.h
/// @brief 函数调用指令（块结构 IR 版本，参数直接作为操作数存储）
///

#pragma once

#include <vector>

#include "Instruction.h"

class Function;
class Value;

class CallInst final : public Instruction {

public:
    /// @param parentFunc   调用所在的函数（IR 上下文）
    /// @param callee       被调用函数
    /// @param args         实参列表
    /// @param resultType   返回值类型（void 则为 VoidType）
    CallInst(Function * parentFunc, Function * callee, const std::vector<Value *> & args, Type * resultType);

    Function * getCallee() const
    {
        return callee;
    }

    /// 实参数量
    int32_t getArgCount();

    /// 获取第 i 个实参（0-based）
    Value * getArg(int32_t i);

    void toString(std::string & str) override;

private:
    Function * callee = nullptr;
};
