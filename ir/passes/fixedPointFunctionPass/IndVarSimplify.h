///
/// @file IndVarSimplify.h
/// @brief 归纳变量简化 pass —— 将循环退出条件从整数计数器比较改写为指针比较
///
/// 对于规范计数循环 for (i = 0; i < N; ++i)，若存在另一个指针型 phi
/// 以相同节奏递推（如 LSR 创建的 ptrPhi），则将退出条件从
///   icmp slt i, N
/// 改写为
///   icmp ne ptrPhi, endPtr
/// 从而消除整数计数器及其递推指令
///

#pragma once

class BasicBlock;
class Function;
class Module;
class ScalarEvolution;

class IndVarSimplify {

public:
    /// @brief 构造归纳变量简化 pass
    /// @param func 待优化函数
    /// @param mod 所属模块
    IndVarSimplify(Function * func, Module * mod);

    /// @brief 对所有规范计数循环尝试做归纳变量替换
    /// @return true 表示 IR 被修改
    bool run();

private:
    /// @brief 尝试简化以 header 为头的循环的归纳变量
    /// @param header 循环头基本块
    /// @param scev 复用的标量演化分析
    /// @return true 表示成功替换退出条件
    bool trySimplifyHeader(BasicBlock * header, ScalarEvolution & scev);

    Function * func = nullptr;
    Module * mod = nullptr;
};
