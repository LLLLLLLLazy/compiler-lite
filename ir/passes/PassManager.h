///
/// @file PassManager.h
/// @brief pass 注册与执行管理器
///

#pragma once

#include <cstdint>
#include <functional>
#include <vector>

class Function;
class Module;

class PassManager {

public:
    using ModulePassRunner = std::function<bool(Module *)>;
    using FunctionPassRunner = std::function<bool(Function *)>;

    /// @brief 构造 pass 管理器
    explicit PassManager(Module * module);

    /// @brief 注册默认优化流水线
    /// @param optLevel 优化级别
    /// @param enableRVVLoopVectorize 是否启用 RVV 循环向量化
    void registerDefaultOptimizationPipeline(int32_t optLevel, bool enableRVVLoopVectorize = false);

    /// @brief 注册后端前置的 phi 降级流水线
    void registerPhiLoweringPipeline();

    /// @brief 执行当前已注册的流水线
    void run();

private:
    /// @brief 清空当前已注册的所有 pass
    void clear();

    /// @brief 注册模块级 pass
    /// @param runner pass 执行器
    void registerModulePass(ModulePassRunner runner);

    /// @brief 注册定点函数级 pass 之前执行的模块级 pass
    /// @param runner pass 执行器
    void registerLateModulePass(ModulePassRunner runner);

    /// @brief 注册单次函数级 pass
    /// @param runner pass 执行器
    void registerFunctionPass(FunctionPassRunner runner);

    /// @brief 注册参与定点迭代的函数级 pass
    /// @param runner pass 执行器
    void registerFixedPointFunctionPass(FunctionPassRunner runner);

    /// @brief 注册在定点迭代收敛后执行的模块级 pass
    ///
    /// 用于晚期内联（LateInline）等场景：经过 SCEV 闭式替换与
    /// RemoveEmptyLoop 瘦身后，原本体积过大的函数可能已可内联。
    /// 若该组 pass 改变了 IR，定点迭代会再执行一轮以优化新内联的代码
    /// @param runner pass 执行器
    void registerPostFixedPointModulePass(ModulePassRunner runner);

    /// @brief 注册在定点迭代收敛后执行一次的后置函数级 pass
    /// @param runner pass 执行器
    void registerLateFunctionPass(FunctionPassRunner runner);

    /// @brief 执行一组函数级 pass
    /// @param runners pass 执行器列表
    /// @return true 表示至少有一个 pass 修改了 IR
    bool runFunctionPassGroup(const std::vector<FunctionPassRunner> & runners) const;

    /// @brief 判断函数是否应该参与 pass 执行
    /// @param func 待判断函数
    /// @return true 表示该函数可被优化或降级
    static bool isRunnableFunction(Function * func);

    Module * module = nullptr;
    std::vector<ModulePassRunner> modulePasses;
    std::vector<ModulePassRunner> lateModulePasses;
    std::vector<FunctionPassRunner> functionPasses;
    std::vector<FunctionPassRunner> fixedPointFunctionPasses;
    std::vector<ModulePassRunner> postFixedPointModulePasses;
    std::vector<FunctionPassRunner> lateFunctionPasses;
    int32_t maxFixedPointRounds = 0;
};
