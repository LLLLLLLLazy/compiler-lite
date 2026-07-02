///
/// @file LoopFusion.h
/// @brief 相邻计数循环融合 pass
///
/// 将两个「相邻、同界（同起点/步长/上界/比较）」的规范计数循环合并为一个循环，
/// 在保持逐迭代程序序的前提下减少循环控制开销并改善数据的时间局部性。
///
/// 融合的合法性建立在保守的内存依赖判定之上：
///   - 若两循环体访问的数组对象互不相交（不存在被一方写、另一方又访问的共享对象），
///     则两循环体逐迭代独立，融合平凡合法；
///   - 若存在共享数组对象，则要求双方对该对象的每一次访问其「最外层可变下标」
///     恰为各自的循环归纳变量 i（其余下标在合法程序下不会越过第 i 行），
///     从而跨迭代依赖距离为 0，融合后同一 i 内保持原有先后次序，合法。
///
/// 变换本身不搬移任何指令，只做四条 CFG 边的重连与两个块（loop2 头/前置头）的删除，
/// 因此对单块循环体与含嵌套内层循环的多块循环体统一适用。
///

#pragma once

class Function;
class Module;

/// @brief 相邻计数循环融合 pass
class LoopFusion {

public:
    /// @brief 构造循环融合 pass
    /// @param func 待优化函数
    /// @param mod 所属模块
    explicit LoopFusion(Function * func, Module * mod = nullptr);

    /// @brief 对函数原地执行循环融合，反复融合直至不再有可融合对
    /// @return 若 IR 发生变化则返回 true
    bool run();

private:
    Function * func = nullptr;
};
