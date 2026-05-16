///
/// @file PassManager.cpp
/// @brief pass 注册与执行管理器实现
///

#include "PassManager.h"
#include "Function.h"
#include "Module.h"
#include "fixedPointFunctionPass/CFGSimplify.h"
#include "fixedPointFunctionPass/ConstProp.h"
#include "fixedPointFunctionPass/DeadInstElim.h"
#include "fixedPointFunctionPass/GVN.h"
#include "fixedPointFunctionPass/InstCombine.h"
#include "fixedPointFunctionPass/LICM.h"
#include "fixedPointFunctionPass/LoopStrengthReduce.h"
#include "fixedPointFunctionPass/LoopTiling.h"
#include "fixedPointFunctionPass/LocalMemoryOpt.h"
#include "fixedPointFunctionPass/SimpleLoopUnroll.h"
#include "fixedPointFunctionPass/UnreachableBlockElim.h"
#include "fixedPointFunctionPass/PureCallLoopCache.h"
#include "functionPass/ArrayScalarize.h"
#include "functionPass/Mem2Reg.h"
#include "functionPass/PhiToSelect.h"
#include "functionPass/PhiLowering.h"
#include "functionPass/PureCallCSE.h"
#include "functionPass/TailRecursionElim.h"
#include "modulePass/GlobalToLocal.h"
#include "modulePass/InterproceduralConstProp.h"
#include "modulePass/SmallFunctionInline.h"

namespace {

constexpr int32_t kDefaultMaxFixedPointRounds = 18;

bool isOptimizableFunction(Function * func)
{
    return func != nullptr && !func->isBuiltin() && !func->getBlocks().empty();
}

bool runPostInlineCleanupPipeline(Module * currentModule)
{
    if (currentModule == nullptr) {
        return false;
    }

    bool changed = false;
    for (auto * func : currentModule->getFunctionList()) {
        if (!isOptimizableFunction(func)) {
            continue;
        }

        Mem2Reg mem2reg(func, currentModule);
        mem2reg.run();

        GVN gvn(func, currentModule);
        changed = gvn.run() || changed;

        LICM licm(func, currentModule);
        changed = licm.run() || changed;

        InstCombine instCombine(func, currentModule);
        changed = instCombine.run() || changed;
    }

    return changed;
}

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

    registerFunctionPass([this](Function * func) {
        GVN pass(func, module);
        return pass.run();
    });

    registerFunctionPass([](Function * func) {
        TailRecursionElim pass(func);
        return pass.run();
    });

    registerLateModulePass([](Module * currentModule) {
        SmallFunctionInline pass(currentModule);
        bool changed = pass.run();
        changed = runPostInlineCleanupPipeline(currentModule) || changed;
        return changed;
    });

    maxFixedPointRounds = kDefaultMaxFixedPointRounds;

    registerFixedPointFunctionPass([](Function * func) {
        LocalMemoryOpt pass(func);
        return pass.run();
    });

    registerFixedPointFunctionPass([this](Function * func) {
        GVN pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass([this](Function * func) {
        LICM pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass([this](Function * func) {
        LoopTiling pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass([this](Function * func) {
        LoopStrengthReduce pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass([this](Function * func) {
        GVN pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass([this](Function * func) {
        SimpleLoopUnroll pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass([this](Function * func) {
        GVN pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass([this](Function * func) {
        PureCallCSE pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass([this](Function * func) {
        LICM pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass([this](Function * func) {
        PureCallLoopCache pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass([](Function * func) {
        PhiToSelect pass(func);
        return pass.run();
    });

    registerFixedPointFunctionPass([this](Function * func) {
        InstCombine pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass([this](Function * func) {
        PureCallCSE pass(func, module);
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

    registerFunctionPass([](Function * func) {
        PhiToSelect pass(func);
        return pass.run();
    });

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

    for (const auto & runner : lateModulePasses) {
        runner(module);
    }

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
    lateModulePasses.clear();
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

/// @brief 注册函数级 pass 之后、定点函数级 pass 之前执行的模块级 pass
/// @param runner pass 执行器
void PassManager::registerLateModulePass(ModulePassRunner runner)
{
    lateModulePasses.push_back(std::move(runner));
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
