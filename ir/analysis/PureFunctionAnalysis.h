///
/// @file PureFunctionAnalysis.h
/// @brief 共享的纯函数与内存独立性分析
///

#pragma once

#include <unordered_map>
#include <unordered_set>

class Function;
class GlobalVariable;
class Instruction;
class Module;
class Value;

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
    /// @brief 在首次查询时对模块调用图做完整 SCC 分析
    void analyzeModule();

    /// @brief 通过形参写集合不动点证明模块中的只读全局对象
    void analyzeReadOnlyGlobals();

    /// @brief 判断地址是否为局部对象、形参或全局对象
    /// @param pointer 待读地址
    /// @return true 表示该地址允许出现在纯函数中
    bool isAllowedReadPointer(Value * pointer) const;

    /// @brief 判断地址是否不依赖调用者可变内存
    /// @param pointer 待读地址
    /// @return true 表示地址来自本帧对象或已证明只读的全局对象
    bool isMemoryIndependentPointer(Value * pointer) const;

    /// @brief 判断非调用指令是否满足无调用者可见副作用要求
    /// @param inst 待检查指令
    /// @return true 表示该指令的写入仅限本帧局部对象，读取来源可证明
    bool isLocallyPureInstruction(Instruction * inst) const;

    Module * mod = nullptr;
    bool analyzed = false;
    std::unordered_map<Function *, bool> pure;
    std::unordered_map<Function *, bool> memoryIndependent;
    std::unordered_set<GlobalVariable *> readOnlyGlobals;
};
