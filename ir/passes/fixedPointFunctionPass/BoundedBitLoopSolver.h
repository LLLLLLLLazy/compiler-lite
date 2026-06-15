///
/// @file BoundedBitLoopSolver.h
/// @brief 有界位迭代循环求解 pass
///
/// 识别迭代次数有界（≤ 字长）、逐位用 srem/sdiv 模拟按位运算的循环，对其做
/// 抽象解释求出闭式效果，用等价的原生指令替换整段循环。涵盖单操作数（恒等/
/// 取反/常量）与双操作数（与/或/异或），位宽 N=len 初值（取值 [1,32]）。
/// 形如 while(len){ bit_a=a%2; [bit_b=b%2;] a/=2; [b/=2;]
/// if(谓词) result+=power; power*=2; len--; }。
/// 在循环前插入取值范围守卫：各操作数均在 [0,2^N) 时直接用原生指令计算结果
/// 并跳过循环，否则仍走原循环，保证任意输入下语义不变
///

#pragma once

class Function;
class Module;

/// @brief 有界位迭代循环求解 pass
class BoundedBitLoopSolver {
public:
    /// @brief 构造有界位迭代循环求解 pass
    /// @param _func 目标函数
    /// @param _mod 所属模块（用于创建常量）
    BoundedBitLoopSolver(Function * _func, Module * _mod) : func(_func), mod(_mod)
    {}

    /// @brief 对函数中所有循环尝试识别并改写位迭代循环
    /// @return true 表示发生了改写
    bool run();

private:
    Function * func = nullptr;
    Module * mod = nullptr;
};
