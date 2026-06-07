///
/// @file LoopExitValueRewrite.h
/// @brief 基于 SCEV 的循环出口值闭式替换 pass
///
/// 对规范计数循环 for (i = 0; i < N; ++i)，把循环头部满足以下两类递推形式
/// 的 phi 在循环出口处的取值替换为闭式表达式：
///   1. 仿射递推    p_{k+1} = p_k + c              -> p_exit = start + c * N
///   2. 模加递推    p_{k+1} = (p_k + c) % M        -> p_exit = (start + c * N) % M
/// 其中 c、M、start 均为循环不变量。模加递推之所以成立，是因为模加运算可结合：
/// (((x + c) % M) + c) % M = (x + 2c) % M，故 N 次模加等于一次性加 c*N 再取模。
///
/// 替换后循环体对这些 phi 的递推计算不再被循环外使用，配合 RemoveEmptyLoop
/// 可整体删除无副作用的空循环
///

#pragma once

class BasicBlock;
class Function;
class LoopInfo;
class Module;
class PhiInst;
class ScalarEvolution;
class Value;

class LoopExitValueRewrite {

public:
    /// @brief 构造循环出口值闭式替换 pass
    /// @param func 待优化函数
    /// @param mod 所属模块
    LoopExitValueRewrite(Function * func, Module * mod);

    /// @brief 对所有可识别的规范计数循环替换头部 phi 的出口取值
    /// @return true 表示 IR 发生变化
    bool run();

private:
    /// @brief 尝试改写以 header 为头的循环
    /// @param header 循环头基本块
    /// @param scev 复用的标量演化分析
    /// @return true 表示成功改写至少一个出口值
    bool tryRewriteHeader(BasicBlock * header, ScalarEvolution & scev);

    Function * func = nullptr;
    Module * mod = nullptr;
    LoopInfo * currentLoopInfo = nullptr; ///< 当前轮复用的循环信息
};
