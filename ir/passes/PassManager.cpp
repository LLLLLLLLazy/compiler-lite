///
/// @file PassManager.cpp
/// @brief pass 注册与执行管理器实现
///
/// 注意 fixedPointFunctionPass 和 functionPass 的区别：
/// 前者是从 '模块设计上' 需要多次迭代运行；
/// 后者是从 '模块设计上' 仅运行一次，但是其内部可能包含多轮迭代。

#include "PassManager.h"

#include <cstdlib>
#include <iostream>

#include "BasicBlock.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "Module.h"
#include "PhiInst.h"
#include "fixedPointFunctionPass/CFGSimplify.h"
#include "fixedPointFunctionPass/ConstProp.h"
#include "fixedPointFunctionPass/CanonicalizeLoop.h"
#include "fixedPointFunctionPass/DeadInstElim.h"
#include "fixedPointFunctionPass/GVN.h"
#include "fixedPointFunctionPass/InstCombine.h"
#include "fixedPointFunctionPass/LICM.h"
#include "fixedPointFunctionPass/LoopStrengthReduce.h"
#include "fixedPointFunctionPass/LoopTiling.h"
#include "fixedPointFunctionPass/LoopVectorize.h"
#include "fixedPointFunctionPass/MatMulInterchange.h"
#include "fixedPointFunctionPass/LocalMemoryOpt.h"
#include "fixedPointFunctionPass/SimpleLoopUnroll.h"
#include "fixedPointFunctionPass/UnreachableBlockElim.h"
#include "fixedPointFunctionPass/PureCallLoopCache.h"
#include "functionPass/ArrayScalarize.h"
#include "functionPass/LateLoopCFGCleanup.h"
#include "functionPass/LoopParallelize.h"
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

/// @brief 调试用：检测 GEP 基址链上的环与 phi 入边/前驱不一致（MINIC_CHECK_GEP_CYCLE 置位时启用）
void checkGEPCycles(Function * func, const std::string & passName)
{
    static const bool enabled = std::getenv("MINIC_CHECK_GEP_CYCLE") != nullptr;
    if (!enabled || func == nullptr) {
        return;
    }

    for (auto * bb : func->getBlocks()) {
        for (auto * inst : bb->getInstructions()) {
            if (auto * phi = dynamic_cast<PhiInst *>(inst)) {
                for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
                    BasicBlock * incoming = phi->getIncomingBlock(i);
                    const auto & preds = bb->getPredecessors();
                    if (std::find(preds.begin(), preds.end(), incoming) == preds.end()) {
                        std::string blockStr;
                        bb->toString(blockStr);
                        std::cerr << "[phi-stale-incoming] after pass " << passName << " in func "
                                  << func->getName() << "\n  block:\n" << blockStr << "\n";
                        std::abort();
                    }
                }
            }
            auto * gep = dynamic_cast<GetElementPtrInst *>(inst);
            if (!gep) {
                continue;
            }
            Value * cursor = gep->getBasePointer();
            for (int step = 0; step < 64 && cursor != nullptr; ++step) {
                if (cursor == gep) {
                    std::string gepStr;
                    gep->toString(gepStr);
                    std::string blockStr;
                    bb->toString(blockStr);
                    std::cerr << "[gep-cycle] after pass " << passName << " in func " << func->getName()
                              << "\n  inst: " << gepStr << "\n  block:\n" << blockStr << "\n";
                    std::abort();
                }
                auto * baseGEP = dynamic_cast<GetElementPtrInst *>(cursor);
                if (!baseGEP) {
                    break;
                }
                cursor = baseGEP->getBasePointer();
            }
        }
    }
}

/// @brief 第二轮 SmallFunctionInline 后的清理流水线，主要针对内联展开后产生的冗余代码进行清理
/// @param currentModule 
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

/// @brief 固定点循环优化结束后的晚期 CFG 收尾流水线
/// @param func 待处理函数
/// @return true 表示至少有一个 pass 修改了 IR
bool runPostFixedPointLoopCleanupPipeline(Function * func)
{
    if (!isOptimizableFunction(func)) {
        return false;
    }

    bool changed = false;
    bool localChanged = false;
    do {
        localChanged = false;

        LateLoopCFGCleanup lateLoopCFGCleanup(func);
        localChanged = lateLoopCFGCleanup.run() || localChanged;

        CFGSimplify cfgSimplify(func);
        localChanged = cfgSimplify.run() || localChanged;

        changed = localChanged || changed;
    } while (localChanged);

    return changed;
}

} // namespace

/// @brief 构造 pass 管理器
/// @param _module 待管理模块
PassManager::PassManager(Module * _module) : module(_module)
{}

/// @brief 注册默认优化流水线
/// @param optLevel 优化级别
/// @param enableRVVLoopVectorize 是否启用 RVV 循环向量化
void PassManager::registerDefaultOptimizationPipeline(int32_t optLevel, bool enableRVVLoopVectorize)
{
    clear();
    if (module == nullptr || optLevel <= 0) {
        return;
    }

    registerModulePass("InterproceduralConstProp", [](Module * currentModule) {
        InterproceduralConstProp pass(currentModule);
        return pass.run();
    });

    registerModulePass("SmallFunctionInline", [](Module * currentModule) {
        SmallFunctionInline pass(currentModule);
        return pass.run();
    });

    registerModulePass("GlobalToLocal", [](Module * currentModule) {
        GlobalToLocal pass(currentModule);
        return pass.run();
    });

    registerFunctionPass("PureCallCSE", [this](Function * func) {
        PureCallCSE pass(func, module);
        return pass.run();
    });

    registerFunctionPass("ArrayScalarize", [](Function * func) {
        ArrayScalarize pass(func);
        return pass.run();
    });

    registerFunctionPass("Mem2Reg", [this](Function * func) {
        Mem2Reg pass(func, module);
        pass.run();
        return false;
    });

    registerFunctionPass("GVN", [this](Function * func) {
        GVN pass(func, module);
        return pass.run();
    });

    registerFunctionPass("TailRecursionElim", [](Function * func) {
        TailRecursionElim pass(func);
        return pass.run();
    });

    registerLateModulePass("LateSmallFunctionInline", [](Module * currentModule) {
        SmallFunctionInline pass(currentModule);
        bool changed = pass.run();
        changed = runPostInlineCleanupPipeline(currentModule) || changed;
        return changed;
    });

    maxFixedPointRounds = kDefaultMaxFixedPointRounds;

    registerFixedPointFunctionPass("LocalMemoryOpt", [](Function * func) {
        LocalMemoryOpt pass(func);
        return pass.run();
    });

    registerFixedPointFunctionPass("GVN", [this](Function * func) {
        GVN pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass("LICM", [this](Function * func) {
        LICM pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass("CanonicalizeLoop", [this](Function * func) {
        CanonicalizeLoop pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass("LoopTiling", [this](Function * func) {
        LoopTiling pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass("LoopStrengthReduce", [this](Function * func) {
        LoopStrengthReduce pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass("GVN", [this](Function * func) {
        GVN pass(func, module);
        return pass.run();
    });

    // matmul 列访问归约的 j-k 交换：放在强度削减收敛之后（匹配指针游标形态）、
    // 向量化之前（交换出的单位步长内层循环可继续被向量化）。
    registerFixedPointFunctionPass("MatMulInterchange", [this](Function * func) {
        MatMulInterchange pass(func, module);
        return pass.run();
    });

    if (enableRVVLoopVectorize) {
        // RVV 向量化放在循环规范化/强度削减之后，输入循环形态更稳定。
        registerFixedPointFunctionPass("LoopVectorize", [this](Function * func) {
            LoopVectorize pass(func, module);
            return pass.run();
        });
    }

    registerFixedPointFunctionPass("SimpleLoopUnroll", [this](Function * func) {
        SimpleLoopUnroll pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass("GVN", [this](Function * func) {
        GVN pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass("PureCallCSE", [this](Function * func) {
        PureCallCSE pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass("LICM", [this](Function * func) {
        LICM pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass("PureCallLoopCache", [this](Function * func) {
        PureCallLoopCache pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass("PhiToSelect", [](Function * func) {
        PhiToSelect pass(func);
        return pass.run();
    });

    registerFixedPointFunctionPass("InstCombine", [this](Function * func) {
        InstCombine pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass("PureCallCSE", [this](Function * func) {
        PureCallCSE pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass("ConstProp", [this](Function * func) {
        ConstProp pass(func, module);
        return pass.run();
    });

    registerFixedPointFunctionPass("UnreachableBlockElim", [](Function * func) {
        UnreachableBlockElim pass(func);
        return pass.run();
    });

    registerFixedPointFunctionPass("DeadInstElim", [](Function * func) {
        DeadInstElim pass(func);
        return pass.run();
    });

    registerFixedPointFunctionPass("CFGSimplify", [](Function * func) {
        CFGSimplify pass(func);
        return pass.run();
    });

    registerLateFunctionPass("PostFixedPointLoopCleanup", [](Function * func) {
        return runPostFixedPointLoopCleanupPipeline(func);
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

    if (!fixedPointFunctionPasses.empty()) {
        bool changed = false;
        int32_t round = 0;
        do {
            changed = runFunctionPassGroup(fixedPointFunctionPasses);
            ++round;
        } while (changed && round < maxFixedPointRounds);
    }

    runFunctionPassGroup(lateFunctionPasses);
}

/// @brief 清空当前已注册的所有 pass
void PassManager::clear()
{
    modulePasses.clear();
    lateModulePasses.clear();
    functionPasses.clear();
    fixedPointFunctionPasses.clear();
    lateFunctionPasses.clear();
    maxFixedPointRounds = 0;
}

/// @brief 注册模块级 pass
/// @param runner pass 执行器
void PassManager::registerModulePass(ModulePassRunner runner)
{
    modulePasses.push_back(std::move(runner));
}

/// @brief 注册定点函数级 pass 之前执行的模块级 pass
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

/// @brief 注册在定点迭代收敛后执行一次的后置函数级 pass
/// @param runner pass 执行器
void PassManager::registerLateFunctionPass(FunctionPassRunner runner)
{
    lateFunctionPasses.push_back(std::move(runner));
}

/// @brief 从 MINIC_DISABLE_PASSES 加载被关闭的 pass 名集合（逗号/分号/空白分隔，仅加载一次）
void PassManager::loadPassToggles()
{
    if (togglesLoaded) {
        return;
    }
    togglesLoaded = true;
    disabledPasses.clear();

    const char * env = std::getenv("MINIC_DISABLE_PASSES");
    if (env == nullptr) {
        return;
    }

    std::string token;
    auto flush = [&]() {
        const size_t begin = token.find_first_not_of(" \t\r\n");
        if (begin != std::string::npos) {
            const size_t end = token.find_last_not_of(" \t\r\n");
            disabledPasses.insert(token.substr(begin, end - begin + 1));
        }
        token.clear();
    };

    for (const char ch : std::string(env)) {
        if (ch == ',' || ch == ';' || ch == ':' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            flush();
        } else {
            token.push_back(ch);
        }
    }
    flush();
}

/// @brief 判断某个具名 pass 是否启用；MINIC_DUMP_PASSES 置位时把 pass 清单打到 stderr
/// @param name pass 名
/// @return true 表示该 pass 应被注册执行
bool PassManager::isPassEnabled(const std::string & name)
{
    loadPassToggles();
    const bool enabled = disabledPasses.find(name) == disabledPasses.end();
    if (std::getenv("MINIC_DUMP_PASSES") != nullptr) {
        std::cerr << "PASS:" << name << ":" << (enabled ? "on" : "off") << "\n";
    }
    return enabled;
}

/// @brief 注册模块级 pass（带名字，可被 MINIC_DISABLE_PASSES 关闭）
void PassManager::registerModulePass(const std::string & name, ModulePassRunner runner)
{
    if (isPassEnabled(name)) {
        modulePasses.push_back(std::move(runner));
    }
}

/// @brief 注册定点函数级 pass 之前执行的模块级 pass（带名字，可关闭）
void PassManager::registerLateModulePass(const std::string & name, ModulePassRunner runner)
{
    if (isPassEnabled(name)) {
        lateModulePasses.push_back(std::move(runner));
    }
}

/// @brief 注册单次函数级 pass（带名字，可关闭）
void PassManager::registerFunctionPass(const std::string & name, FunctionPassRunner runner)
{
    if (isPassEnabled(name)) {
        functionPasses.push_back(std::move(runner));
    }
}

/// @brief 注册参与定点迭代的函数级 pass（带名字，可关闭）
void PassManager::registerFixedPointFunctionPass(const std::string & name, FunctionPassRunner runner)
{
    if (isPassEnabled(name)) {
        fixedPointFunctionPasses.push_back([name, runner = std::move(runner)](Function * func) {
            const bool changed = runner(func);
            checkGEPCycles(func, name);
            return changed;
        });
    }
}

/// @brief 注册定点迭代收敛后执行一次的后置函数级 pass（带名字，可关闭）
void PassManager::registerLateFunctionPass(const std::string & name, FunctionPassRunner runner)
{
    if (isPassEnabled(name)) {
        lateFunctionPasses.push_back(std::move(runner));
    }
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
