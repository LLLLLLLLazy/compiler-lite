///
/// @file FunctionSideEffectAnalysis.h
/// @brief 共享的函数外可见副作用分析
///

#pragma once

#include <unordered_map>

class Function;
class Instruction;

enum class FunctionSideEffectState {
    Unknown,
    Visiting,
    SideEffectFree,
    HasSideEffect,
};

class FunctionSideEffectAnalysis {
public:
    /// @brief 判断 callee 是否无函数外可见副作用
    /// @param function 待分析函数
    /// @return true 表示该函数只会写入非逃逸局部对象
    bool isSideEffectFree(Function * function);

private:
    bool isInstructionAllowed(Instruction * inst);

    std::unordered_map<Function *, FunctionSideEffectState> states;
};