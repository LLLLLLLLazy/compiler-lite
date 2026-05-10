///
/// @file SmallFunctionInline.h
/// @brief 保守的小函数内联优化 pass。
///
/// 对满足体积和结构约束的 callee 进行内联展开，
/// 使后续优化 pass 能跨函数体进行常量折叠和代码移动。
///

#pragma once

#include <cstdint>

class CallInst;
class Function;
class Instruction;
class Module;
class Value;

class SmallFunctionInline {

public:
    explicit SmallFunctionInline(Module * mod);

    /// @brief 对模块中满足条件的小函数进行内联展开
    /// @return true 表示 IR 被修改
    bool run();

private:
    /// @brief 查找并内联第一个满足条件的调用点
    bool inlineFirstCall();
    /// @brief 判断 callee 是否满足内联条件
    /// @param caller 调用方函数
    /// @param call 调用点
    /// @param callLoopDepth 调用点所在循环深度
    /// @return true 表示可以内联该 callee
    bool shouldInlineCallee(Function * caller, CallInst * call, int32_t callLoopDepth) const;
    /// @brief 对一个调用点执行内联展开
    bool inlineCall(CallInst * call);
    /// @brief 克隆指令的外壳（不填充操作数）
    Instruction * cloneInstructionShell(Instruction * inst, Function * caller);

    Module * mod = nullptr;
};
