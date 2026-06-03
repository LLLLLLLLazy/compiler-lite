///
/// @file AnalysisCache.h
/// @brief 函数级 analysis 结果缓存
///
/// 在定点迭代式的 pass 流水线中，同一 analysis（如支配树、循环信息、标量演化）
/// 会在相邻 pass 之间被反复重建。本缓存以 analysis 类型为键缓存其计算结果，
/// 并提供按类型显式失效的接口，使未受影响的 analysis 可以跨 pass 复用。
///
/// 设计要点：
///   - 每个 Function 持有一个 AnalysisCache 实例，生命周期与函数绑定
///   - 结果通过类型擦除（AnalysisResultBase）统一存储，以 std::type_index 为键
///   - 修改 IR 的 pass 需在其影响范围内显式调用 invalidate<AnalysisT>()
///     精确声明哪些 analysis 失效，而不是一刀切地失效全部
///

#pragma once

#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>

/// @brief analysis 结果的类型擦除基类
class AnalysisResultBase {

public:
    virtual ~AnalysisResultBase() = default;
};

/// @brief 模板化的 analysis 结果容器
/// @tparam AnalysisT 被缓存的 analysis 类型
template <typename AnalysisT>
class AnalysisResult : public AnalysisResultBase {

public:
    /// @brief 就地构造被缓存的 analysis 对象
    /// @param args 转发给 analysis 构造函数的参数
    template <typename... Args>
    explicit AnalysisResult(Args &&... args) : analysis(std::forward<Args>(args)...)
    {}

    /// @brief 获取被缓存的 analysis 对象引用
    AnalysisT & get()
    {
        return analysis;
    }

private:
    AnalysisT analysis;
};

/// @brief 函数级 analysis 结果缓存
///
/// 每种 analysis 以其类型为键缓存一份结果。缓存中存在的结果即视为有效，
/// 失效操作直接将对应结果从缓存中移除，下次访问时按需重算。
class AnalysisCache {

public:
    AnalysisCache() = default;
    ~AnalysisCache() = default;

    AnalysisCache(const AnalysisCache &) = delete;
    AnalysisCache & operator=(const AnalysisCache &) = delete;

    /// @brief 获取缓存的 analysis 结果，若不存在则调用 computeFunc 计算并缓存
    /// @tparam AnalysisT analysis 类型
    /// @tparam ComputeFn 计算函数类型，需返回可构造 AnalysisT 的值
    /// @param computeFunc 缓存缺失时用于计算 analysis 的回调
    /// @return 被缓存的 analysis 对象引用，引用在该 analysis 被失效前保持有效
    template <typename AnalysisT, typename ComputeFn>
    AnalysisT & getOrCompute(ComputeFn && computeFunc)
    {
        auto key = std::type_index(typeid(AnalysisT));
        auto it = results.find(key);
        if (it != results.end()) {
            return static_cast<AnalysisResult<AnalysisT> *>(it->second.get())->get();
        }

        auto result = std::make_unique<AnalysisResult<AnalysisT>>(
            std::forward<ComputeFn>(computeFunc)());
        AnalysisT & ref = result->get();
        results.emplace(key, std::move(result));
        return ref;
    }

    /// @brief 失效指定类型的 analysis 缓存
    /// @tparam AnalysisT 待失效的 analysis 类型
    template <typename AnalysisT>
    void invalidate()
    {
        results.erase(std::type_index(typeid(AnalysisT)));
    }

    /// @brief 失效所有 analysis 缓存
    void invalidateAll()
    {
        results.clear();
    }

    /// @brief CFG 结构改变（增删基本块、改终结指令目标）时统一失效所有 CFG 派生分析
    ///
    /// 包含支配树、支配边界、后支配树、循环信息与标量演化。由于循环信息持有支配树指针、
    /// 标量演化又持有支配树与循环信息指针，这些分析必须作为整体一起失效，避免悬空引用
    void invalidateCFGAnalyses();

    /// @brief 仅指令/值发生改变（CFG 结构不变）时失效依赖具体指令的分析
    ///
    /// 当前仅标量演化依赖具体指令内容，支配树与循环信息因仅依赖 CFG 结构而得以保留
    void invalidateValueAnalyses();

    /// @brief 判断指定类型的 analysis 是否已缓存且有效
    /// @tparam AnalysisT 待查询的 analysis 类型
    /// @return true 表示缓存中存在有效结果
    template <typename AnalysisT>
    bool isValid() const
    {
        return results.find(std::type_index(typeid(AnalysisT))) != results.end();
    }

private:
    /// @brief 以 analysis 类型为键的结果存储
    std::unordered_map<std::type_index, std::unique_ptr<AnalysisResultBase>> results;
};
