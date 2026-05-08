///
/// @file PureCallLoopCache.h
/// @brief 循环内纯调用缓存 pass。
///
/// 对循环中实参不变的纯函数调用进行缓存优化：
/// 在循环头插入 cache/valid phi 节点，在循环 latch 中判断缓存是否有效，
/// 若有效则直接复用上一轮迭代的计算结果，避免跨迭代重复执行相同的纯调用。
/// 这是一种通用的循环优化，不依赖特定函数名或输入模式。

#pragma once

class Function;
class Module;

class PureCallLoopCache {

public:
    /// @brief 构造循环内纯调用缓存 pass
    /// @param func 待优化的函数
    /// @param mod 所属模块
    PureCallLoopCache(Function * func, Module * mod);

    /// @brief 对循环内实参不变的纯调用构造 valid/cache phi，避免跨迭代重复执行
    ///
    /// 遍历函数中所有自然循环，对 latch 块中实参循环不变的纯函数调用，
    /// 插入 cache phi 和 valid phi 节点实现缓存：若上一轮迭代后缓存仍有效
    /// （即循环体未写入内存），则直接复用缓存值，跳过实际调用。
    /// @return true 表示 IR 被修改
    bool run();

private:
    Function * func = nullptr;  ///< 待优化的函数
    Module * mod = nullptr;     ///< 所属模块，用于创建常量
};
