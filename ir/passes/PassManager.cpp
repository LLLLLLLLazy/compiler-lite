///
/// @file PassManager.cpp
/// @brief pass 注册与执行管理器实现
///

#include "PassManager.h"

#include "ArrayScalarize.h"
#include "CFGSimplify.h"
#include "ConstProp.h"
#include "DeadInstElim.h"
#include "Function.h"
#include "GlobalToLocal.h"
#include "InstCombine.h"
#include "LICM.h"
#include "LocalMemoryOpt.h"
#include "Mem2Reg.h"
#include "Module.h"
#include "PhiLowering.h"
#include "UnreachableBlockElim.h"
#include "InterproceduralConstProp.h"
#include "SmallFunctionInline.h"
#include "TailRecursionElim.h"
#include "PureCallCSE.h"

namespace {

constexpr int32_t kDefaultMaxFixedPointRounds = 8;

} // namespace

/// @brief 构造 pass 管理器
/// @param _module 待管理模块
PassManager::PassManager(Module * _module) : module(_module)
{}

/// @brief 注册默认优化流水线
/// @param optLevel 优化级别
void PassManager::registerDefaultOptimizationPipeline(int32_t optLevel)
{
    clear();
    if (module == nullptr || optLevel <= 0) {
        return;
    }

    registerModulePass([](Module * currentModule) {
        InterproceduralConstProp pass(currentModule);
        return pass.run();
    });

    registerFunctionPass([this](Function * func) {
        PureCallCSE pass(func, module);
        return pass.run();
    });

    registerModulePass([](Module * currentModule) {
        SmallFunctionInline pass(currentModule);
        return pass.run();
    });

    registerModulePass([](Module * currentModule) {
        GlobalToLocal pass(currentModule);
        return pass.run();
    });

    registerFunctionPass([](Function * func) {
        ArrayScalarize pass(func);
        return pass.run();
    });

    registerFunctionPass([this](Function * func) {
        Mem2Reg pass(func, module);
        pass.run();
        return false;
    });

    registerFunctionPass([](Function * func) {
        TailRecursionElim pass(func);
        return pass.run();
    });

    maxFixedPointRounds = kDefaultMaxFixedPointRounds;

    registerFixedPointFunctionPass([](Function * func) {
        LocalMemoryOpt pass(func);
        return pass.run();
    });

    registerFixedPointFunctionPass([](Function * func) {
        LICM pass(func);
        return pass.run();
    });

    registerFixedPointFunctionPass([this](Function * func) {
        InstCombine pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass([this](Function * func) {
        ConstProp pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass([](Function * func) {
        UnreachableBlockElim pass(func);
        return pass.run();
    });

    registerFixedPointFunctionPass([](Function * func) {
        DeadInstElim pass(func);
        return pass.run();
    });

    registerFixedPointFunctionPass([](Function * func) {
        CFGSimplify pass(func);
        return pass.run();
    });
}

/// @brief 注册后端前置的 phi 降级流水线
void PassManager::registerPhiLoweringPipeline()
{
    clear();
    if (module == nullptr) {
        return;
    }

    registerFunctionPass([this](Function * func) {
        PhiLowering pass(func, module);
        pass.run();
        return false;
    });
}

/// @brief 执行当前已注册的流水线
void PassManager::run()
{
    if (module == nullptr) {
        return;
    }

    for (const auto & runner : modulePasses) {
        runner(module);
    }

    runFunctionPassGroup(functionPasses);

    if (fixedPointFunctionPasses.empty()) {
        return;
    }

    bool changed = false;
    int32_t round = 0;
    do {
        changed = runFunctionPassGroup(fixedPointFunctionPasses);
        ++round;
    } while (changed && round < maxFixedPointRounds);
}

/// @brief 清空当前已注册的所有 pass
void PassManager::clear()
{
    modulePasses.clear();
    functionPasses.clear();
    fixedPointFunctionPasses.clear();
    maxFixedPointRounds = 0;
}

/// @brief 注册模块级 pass
/// @param runner pass 执行器
void PassManager::registerModulePass(ModulePassRunner runner)
{
    modulePasses.push_back(std::move(runner));
}

/// @brief 注册单次函数级 pass
/// @param runner pass 执行器
void PassManager::registerFunctionPass(FunctionPassRunner runner)
{
    functionPasses.push_back(std::move(runner));
}

/// @brief 注册参与定点迭代的函数级 pass
/// @param runner pass 执行器
void PassManager::registerFixedPointFunctionPass(FunctionPassRunner runner)
{
    fixedPointFunctionPasses.push_back(std::move(runner));
}

/// @brief 执行一组函数级 pass
/// @param runners pass 执行器列表
/// @return true 表示至少有一个 pass 修改了 IR
bool PassManager::runFunctionPassGroup(const std::vector<FunctionPassRunner> & runners) const
{
    bool changed = false;
    for (const auto & runner : runners) {
        for (auto * func : module->getFunctionList()) {
            if (!isRunnableFunction(func)) {
                continue;
            }

            changed = runner(func) || changed;
        }
    }

    return changed;
}

/// @brief 判断函数是否应该参与 pass 执行
/// @param func 待判断函数
/// @return true 表示该函数可被优化或降级
bool PassManager::isRunnableFunction(Function * func)
{
    return func != nullptr && !func->isBuiltin() && !func->getBlocks().empty();
}