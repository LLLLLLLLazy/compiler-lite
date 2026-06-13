///
/// @file ModMulIdiom.h
/// @brief 递归倍加模乘惯用法识别 pass
///
/// 识别形如
///   int multiply(int a, int b){
///       if (b == 0) return 0;
///       if (b == 1) return a % m;
///       int cur = multiply(a, b/2);
///       cur = (cur + cur) % m;
///       if (b % 2 == 1) return (cur + a) % m;
///       else return cur;
///   }
/// 的"递归倍加模乘"函数（SysY 因无 64 位类型，用 O(log b) 次递归避免 32 位乘法
/// 溢出来计算 (a*b) % m）。在函数入口插入守卫：当 0<=a<m 且 b>=0 时，
/// 用一条 64 位宽乘加取模（MulModInst）O(1) 直接算出 (a*b)%m，否则仍走原递归。
/// 守卫保证任意输入下语义与原函数严格一致（守卫区内原递归无溢出且等于 (a*b)%m）
///

#pragma once

class Function;
class Module;

class ModMulIdiom {
public:
    /// @brief 构造递归倍加模乘惯用法识别 pass
    /// @param _func 目标函数
    /// @param _mod 所属模块（用于创建常量）
    ModMulIdiom(Function * _func, Module * _mod) : func(_func), mod(_mod)
    {}

    /// @brief 识别并改写递归倍加模乘函数
    /// @return true 表示发生了改写
    bool run();

private:
    Function * func = nullptr;
    Module * mod = nullptr;
};
