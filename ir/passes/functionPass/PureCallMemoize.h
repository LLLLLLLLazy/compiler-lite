///
/// @file PureCallMemoize.h
/// @brief 递归纯函数记忆化 pass。
///
/// 对纯递归函数（无副作用、只读全局）插入 memo 表缓存：
/// 在函数入口检查缓存，命中则直接返回；在函数出口处将结果写入缓存。
/// 可将 O(2^N) 的朴素递归（如 knapsack）降至 O(N*W)。
///
/// 当前实现支持 1 个或 2 个 int 参数的函数：
/// - 单参数函数要求存在一条执行路径会进行至少两次自递归调用，避免给单链递归增加缓存开销；
/// - 双参数函数通过分析 GEP 访问的全局数组推断第一个参数的界，
///   第二个参数界使用启发式常量。
///

#pragma once

#include <cstdint>

class Function;
class Module;

class PureCallMemoize {

public:
    /// @brief 构造递归纯函数记忆化 pass
    /// @param func 待优化的函数
    /// @param mod  所属模块
    PureCallMemoize(Function * func, Module * mod);

    /// @brief 对纯递归函数插入 memo 缓存
    /// @return true 表示 IR 被修改
    bool run();

private:
    /// @brief 判断是否存在一条执行路径会执行至少两次自递归调用
    /// @return 存在潜在重叠递归子问题时返回 true
    bool hasOverlappingSelfCalls();

    /// @brief 尝试从函数体中的 GEP 访问推断参数界
    /// @param[out] maxArg0 第一个参数的上界
    /// @return 成功推断则返回 true
    bool inferParamBound(int32_t & maxArg0);

    /// @brief 推断单参数递归函数的缓存上界
    /// @param[out] maxArg 单参数上界
    /// @return 成功推断或采用默认上界时返回 true
    bool inferSingleParamBound(int32_t & maxArg);

    /// @brief 创建单参数 memo 缓存全局数组
    /// @param maxArg 参数最大值（包含）
    void createSingleParamMemoGlobals(int32_t maxArg);

    /// @brief 在模块中创建 memo 缓存所需的全局数组
    void createMemoGlobals(int32_t maxI, int32_t maxW);

    /// @brief 对单参数递归函数插入缓存查询和写回
    /// @param maxArg 参数最大值（包含）
    void transformSingleParamFunction(int32_t maxArg);

    /// @brief 变换函数：在入口添加缓存检查，在出口前添加缓存写入
    /// @param maxI 第一个参数最大值（包含）
    /// @param maxW 第二个参数最大值（包含）
    void transformFunction(int32_t maxI, int32_t maxW);

    Function * func = nullptr;
    Module * mod = nullptr;
    static constexpr int32_t kDefaultMaxArg = 1024;  ///< 单参数递归默认缓存上界
    static constexpr int32_t kMaxSingleMemoArg = 65535;  ///< 单参数稠密缓存硬上限
    static constexpr int32_t kDefaultMaxW = 1024;  ///< 默认 w 参数上界
};
