///
/// @file PassManager.cpp
/// @brief pass 注册与执行管理器实现
///

#include "PassManager.h"
#include "Function.h"
#include "Module.h"
#include "fixedPointFunctionPass/CFGSimplify.h"
#include "fixedPointFunctionPass/ConstProp.h"
#include "fixedPointFunctionPass/CanonicalizeLoop.h"
#include "fixedPointFunctionPass/DeadInstElim.h"
#include "fixedPointFunctionPass/GVN.h"
#include "fixedPointFunctionPass/InstCombine.h"
#include "fixedPointFunctionPass/LICM.h"
#include "fixedPointFunctionPass/LoopConstantPromotion.h"
#include "fixedPointFunctionPass/LoopExitValueRewrite.h"
#include "fixedPointFunctionPass/LoopStrengthReduce.h"
#include "fixedPointFunctionPass/IndVarSimplify.h"
#include "fixedPointFunctionPass/LoopTiling.h"
#include "fixedPointFunctionPass/LoopVectorize.h"
#include "fixedPointFunctionPass/LocalMemoryOpt.h"
#include "fixedPointFunctionPass/RemoveEmptyLoop.h"
#include "fixedPointFunctionPass/SimpleLoopUnroll.h"
#include "fixedPointFunctionPass/UnreachableBlockElim.h"
#include "fixedPointFunctionPass/PureCallLoopCache.h"
#include "functionPass/ArrayScalarize.h"
#include "functionPass/LateLoopCFGCleanup.h"
#include "functionPass/LoopRotate.h"
#include "functionPass/Mem2Reg.h"
#include "functionPass/PhiToSelect.h"
#include "functionPass/PhiLowering.h"
#include "functionPass/PureCallCSE.h"
#include "functionPass/TailRecursionElim.h"
#include "modulePass/DeadFunctionElim.h"
#include "modulePass/GlobalToLocal.h"
#include "modulePass/InterproceduralConstProp.h"
#include "modulePass/SmallFunctionInline.h"

namespace {

constexpr int32_t kDefaultMaxFixedPointRounds = 18;

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

    registerModulePass([](Module * currentModule) {
        InterproceduralConstProp pass(currentModule);
        return pass.run();
    });

    registerModulePass([](Module * currentModule) {
        SmallFunctionInline pass(currentModule);
        return pass.run();
    });

    // 内联后清除无调用者的死函数，释放其对全局标量的引用，
    // 使 GlobalToLocal 能将初始化型函数访问的全局内化到 main
    registerModulePass([](Module * currentModule) {
        DeadFunctionElim pass(currentModule);
        return pass.run();
    });

    registerModulePass([](Module * currentModule) {
        GlobalToLocal pass(currentModule);
        return pass.run();
    });

    registerFunctionPass([this](Function * func) {
        PureCallCSE pass(func, module);
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
        for (auto * func : currentModule->getFunctionList()) {
            if (!func || func->isBuiltin() || func->getBlocks().empty()) {
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
    });

    // 晚期内联可能让更多初始化型函数变为死函数，
    // 再次清除死函数并下沉其访问的全局标量，然后用 Mem2Reg 把这些
    // 槽位提升到 SSA，为后续循环优化创造条件
    registerLateModulePass([](Module * currentModule) {
        DeadFunctionElim deadFuncElim(currentModule);
        bool changed = deadFuncElim.run();

        GlobalToLocal globalToLocal(currentModule);
        changed = globalToLocal.run() || changed;

        if (changed) {
            for (auto * func : currentModule->getFunctionList()) {
                if (!func || func->isBuiltin() || func->getBlocks().empty()) {
                    continue;
                }

                Mem2Reg mem2reg(func, currentModule);
                mem2reg.run();

                GVN gvn(func, currentModule);
                gvn.run();

                LICM licm(func, currentModule);
                licm.run();

                InstCombine instCombine(func, currentModule);
                instCombine.run();
            }
        }
        return changed;
    });

    maxFixedPointRounds = kDefaultMaxFixedPointRounds;

    // ---- 子组1：值优化 + 循环规范化 + LICM ----
    // 编译时间不计分，因此采用「变换 → 清理 → 变换 → 清理」的节奏，
    // 让后续分析（SCEV / LoopInfo 等）始终工作在干净的 IR 上，
    // 减少定点迭代收敛所需的轮次
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
        CanonicalizeLoop pass(func, module);
        return pass.run();
    });
    // 仅用 DeadInstElim 这类 CFG 保形的值级清理；CFGSimplify 会合并块、
    // 破坏 CanonicalizeLoop 建立的 preheader / 专属 exit 规范形式，
    // 从而让下游基于 SCEV 的循环 pass（LoopExitValueRewrite / LoopTiling / LSR）
    // 误判循环形态并产生错误代码，故中期不插入 CFGSimplify
    registerFixedPointFunctionPass([](Function * func) {
        DeadInstElim pass(func);
        return pass.run();
    });

    // ---- 子组2：循环变换 ----
    // 基于 SCEV 把规范计数循环头部递推 phi 的出口取值替换为闭式表达式，
    // 随后消除因此变为无副作用且出口无依赖的空循环
    registerFixedPointFunctionPass([this](Function * func) {
        LoopExitValueRewrite pass(func, module);
        return pass.run();
    });
    // 先清除出口值替换产生的死 phi / 死指令，使更多空循环能被 RemoveEmptyLoop 消除
    // 仅用 DeadInstElim（值级清理，不改变 CFG 形状），避免破坏循环规范形影响下游循环 pass
    registerFixedPointFunctionPass([](Function * func) {
        DeadInstElim pass(func);
        return pass.run();
    });
    registerFixedPointFunctionPass([this](Function * func) {
        RemoveEmptyLoop pass(func, module);
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
    // 仅用值级清理：LSR / Tiling 产生的死指令及时清除，不动 CFG 形状
    registerFixedPointFunctionPass([](Function * func) {
        DeadInstElim pass(func);
        return pass.run();
    });

    // ---- 子组3：向量化 / 展开 ----
    if (enableRVVLoopVectorize) {
        // RVV 向量化放在循环规范化/强度削减之后，输入循环形态更稳定
        registerFixedPointFunctionPass([this](Function * func) {
            LoopVectorize pass(func, module);
            return pass.run();
        });
    }
    registerFixedPointFunctionPass([this](Function * func) {
        SimpleLoopUnroll pass(func, module);
        return pass.run();
    });
    registerFixedPointFunctionPass([this](Function * func) {
        GVN pass(func, module);
        return pass.run();
    });
    registerFixedPointFunctionPass([](Function * func) {
        DeadInstElim pass(func);
        return pass.run();
    });
    // 注意， IndVarSimplify 一定要在 SimpleLoopUnroll 之后，否则 IndVarSimplify 会改写那些本来会被展开的循环(<16的迭代次数)，导致 SimpleLoopUnroll 无法 matchCanonicalLoop.
    registerFixedPointFunctionPass([this](Function * func) {
        IndVarSimplify pass(func, module);
        return pass.run();
    });
    registerFixedPointFunctionPass([this](Function * func) {
        ConstProp pass(func, module);
        return pass.run();
    });

    // ---- 子组4：晚期值优化 ----
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
    // 子组4 清理：再做一次纯调用 CSE 与常量传播，然后消除不可达块与死指令并简化 CFG
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

    // LateInline：定点循环优化收敛后再做一轮内联。此时 SCEV 闭式替换、
    // RemoveEmptyLoop 等已消除大量循环体指令，原本超过指令阈值的函数
    // 可能已瘦身到可内联的规模。内联后清理死函数并下沉全局标量，
    // 若 IR 发生变化，PassManager 会再跑一轮定点迭代优化新内联的代码
    registerPostFixedPointModulePass([](Module * currentModule) {
        SmallFunctionInline inlinePass(currentModule);
        bool changed = inlinePass.run();
        if (changed) {
            DeadFunctionElim deadFuncElim(currentModule);
            deadFuncElim.run();

            GlobalToLocal globalToLocal(currentModule);
            globalToLocal.run();

            for (auto * func : currentModule->getFunctionList()) {
                if (!func || func->isBuiltin() || func->getBlocks().empty()) {
                    continue;
                }

                Mem2Reg mem2reg(func, currentModule);
                mem2reg.run();

                GVN gvn(func, currentModule);
                gvn.run();

                LICM licm(func, currentModule);
                licm.run();

                InstCombine instCombine(func, currentModule);
                instCombine.run();
            }
        }
        return changed;
    });

    registerLateFunctionPass([](Function * func) {
        if (!func || func->isBuiltin() || func->getBlocks().empty()) {
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
    });

    // 晚期循环优化：在所有优化收敛且 CFG 化简完成后执行。
    // 先重跑 CanonicalizeLoop 重建可能被 CFGSimplify 破坏的 preheader/dedicated exit，
    // 再提升循环内重复使用的大常量到 preheader（此时无 ConstProp/InstCombine 会还原），
    // 最后对规范计数循环做 header-test → latch-test 旋转。
    // 放在最后确保无后续 pass 破坏优化结果
    registerLateFunctionPass([this](Function * func) {
        if (!func || func->isBuiltin() || func->getBlocks().empty()) {
            return false;
        }

        bool changed = false;

        // 重建循环规范形式（preheader、dedicated exit）
        CanonicalizeLoop canonicalizeLoop(func);
        changed = canonicalizeLoop.run() || changed;

        // 循环常量提升：将循环体内被多次引用的大立即数和浮点常量
        // 固化为 preheader 中的虚拟寄存器值，避免后端重复物化
        LoopConstantPromotion constPromo(func, module);
        changed = constPromo.run() || changed;

        // 对规范计数循环做旋转
        LoopRotate loopRotate(func);
        changed = loopRotate.run() || changed;

        return changed;
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

    auto runFixedPointLoop = [this]() {
        if (fixedPointFunctionPasses.empty()) {
            return;
        }
        bool changed = false;
        int32_t round = 0;
        do {
            changed = runFunctionPassGroup(fixedPointFunctionPasses);
            ++round;
        } while (changed && round < maxFixedPointRounds);
    };

    runFixedPointLoop();

    // 晚期内联（LateInline）等模块级 pass：在循环优化瘦身后再尝试内联，
    // 若改变了 IR，则再跑一轮定点迭代消化新内联进来的代码
    bool postChanged = false;
    for (const auto & runner : postFixedPointModulePasses) {
        postChanged = runner(module) || postChanged;
    }
    if (postChanged) {
        runFixedPointLoop();
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
    postFixedPointModulePasses.clear();
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

/// @brief 注册在定点迭代收敛后执行的模块级 pass
/// @param runner pass 执行器
void PassManager::registerPostFixedPointModulePass(ModulePassRunner runner)
{
    postFixedPointModulePasses.push_back(std::move(runner));
}

/// @brief 注册在定点迭代收敛后执行一次的后置函数级 pass
/// @param runner pass 执行器
void PassManager::registerLateFunctionPass(FunctionPassRunner runner)
{
    lateFunctionPasses.push_back(std::move(runner));
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
