///
/// @file BitwiseLoopIdiom.h
/// @brief 位模拟循环惯用法识别 pass
///
/// 识别用 srem/sdiv 逐位模拟按位与/或/异或的 32 轮循环
/// （形如 while(len){ bit_a=a%2; bit_b=b%2; a/=2; b/=2;
/// if(谓词) result+=power; power*=2; len--; }），
/// 在循环前插入非负守卫：两个操作数均非负时直接用原生按位指令
/// 计算结果并跳过循环，否则仍走原循环，保证任意输入下语义不变
///

#pragma once

class Function;
class Module;

class BitwiseLoopIdiom {
public:
    /// @brief 构造位模拟循环惯用法识别 pass
    /// @param _func 目标函数
    /// @param _mod 所属模块（用于创建常量）
    BitwiseLoopIdiom(Function * _func, Module * _mod) : func(_func), mod(_mod)
    {}

    /// @brief 对函数中所有循环尝试识别并改写位模拟循环
    /// @return true 表示发生了改写
    bool run();

private:
    Function * func = nullptr;
    Module * mod = nullptr;
};
