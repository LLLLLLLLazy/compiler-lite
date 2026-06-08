///
/// @file LoopConstantPromotion.h
/// @brief 循环常量提升 pass
///
/// 扫描循环体中被多次使用的大整数常量（|val| > 2047）和浮点常量，
/// 在 preheader 中将其固化为虚拟寄存器值，避免后端在每个使用点重复
/// 生成 lui+addiw 等常量物化指令序列
///

#pragma once

class Function;
class Module;

class LoopConstantPromotion {

public:
    /// @brief 构造循环常量提升 pass
    /// @param func 待优化的函数
    /// @param mod 所属模块
    explicit LoopConstantPromotion(Function * func, Module * mod = nullptr);

    /// @brief 对函数执行循环常量提升
    /// @return 若 IR 被修改则返回 true
    bool run();

private:
    Function * func = nullptr;
    Module * mod = nullptr;
};
