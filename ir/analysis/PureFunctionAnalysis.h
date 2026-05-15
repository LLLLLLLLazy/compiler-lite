///
/// @file PureFunctionAnalysis.h
/// @brief 共享的纯函数与内存独立性分析
///

#pragma once

#include <unordered_map>

class Function;
class Instruction;
class Module;

enum class PureFunctionState {
    Unknown,
    Visiting,
    Pure,
    Impure,
};

class PureFunctionAnalysis {
public:
    /// @brief 构造纯函数分析器
    /// @param module 当前模块
    explicit PureFunctionAnalysis(Module * module);

    /// @brief 判断函数是否为纯函数
    /// @param function 待分析函数
    /// @return true 表示该函数无副作用且相同输入返回相同结果
    bool isPure(Function * function);

    /// @brief 判断纯函数是否不读取调用者可见内存
    /// @param function 待分析函数
    /// @return true 表示其结果不依赖调用者可见内存状态
    bool isMemoryIndependent(Function * function);

private:
    bool isInstructionAllowed(Instruction * inst);

    Module * mod = nullptr;
    std::unordered_map<Function *, PureFunctionState> states;
    std::unordered_map<Function *, bool> memoryIndependent;
};