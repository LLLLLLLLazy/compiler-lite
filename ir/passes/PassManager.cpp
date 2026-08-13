///
/// @file PassManager.cpp
/// @brief pass 注册与执行管理器实现
///

#include "PassManager.h"

#include <cstdlib>
#include <iostream>
#include <sstream>

#include "BasicBlock.h"
#include "Function.h"
#include "Module.h"
#include "fixedPointFunctionPass/CFGSimplify.h"
#include "fixedPointFunctionPass/ConstProp.h"
#include "fixedPointFunctionPass/BoundedBitLoopSolver.h"
#include "fixedPointFunctionPass/CanonicalizeLoop.h"
#include "fixedPointFunctionPass/DeadInstElim.h"
#include "fixedPointFunctionPass/GVN.h"
#include "fixedPointFunctionPass/InstCombine.h"
#include "fixedPointFunctionPass/PartialDeadStoreElim.h"
#include "fixedPointFunctionPass/LICM.h"
#include "fixedPointFunctionPass/LoopConstantPromotion.h"
#include "fixedPointFunctionPass/GuardedTailCollapse.h"
#include "fixedPointFunctionPass/LoopExitValueRewrite.h"
#include "fixedPointFunctionPass/LoopFusion.h"
#include "fixedPointFunctionPass/LoopStrengthReduce.h"
#include "fixedPointFunctionPass/LoopVersionInvariantSelect.h"
#include "fixedPointFunctionPass/IndVarSimplify.h"
#include "fixedPointFunctionPass/LoopTiling.h"
#include "fixedPointFunctionPass/LoopVectorize.h"
#include "fixedPointFunctionPass/MatMulInterchange.h"
#include "fixedPointFunctionPass/RangeModSimplify.h"
#include "fixedPointFunctionPass/LocalMemoryOpt.h"
#include "fixedPointFunctionPass/RemoveEmptyLoop.h"
#include "fixedPointFunctionPass/SimpleLoopUnroll.h"
#include "fixedPointFunctionPass/UnreachableBlockElim.h"
#include "fixedPointFunctionPass/PureCallLoopCache.h"
#include "functionPass/ArrayScalarize.h"
#include "functionPass/LateLoopCFGCleanup.h"
#include "functionPass/LoopParallelize.h"
#include "functionPass/LoopRotate.h"
#include "functionPass/Mem2Reg.h"
#include "functionPass/PhiToSelect.h"
#include "functionPass/PhiLowering.h"
#include "functionPass/PureCallCSE.h"
#include "functionPass/PureCallMemoize.h"
#include "functionPass/TailRecursionElim.h"
#include "modulePass/DeadFunctionElim.h"
#include "modulePass/DeadGlobalStoreElim.h"
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
void PassManager::registerDefaultOptimizationPipeline(int32_t optLevel,
                                                      bool enableRVVLoopVectorize,
                                                      bool enableParallel)
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

    // 内联后清除无调用者的死函数，释放其对全局标量的引用，
    // 使 GlobalToLocal 能将初始化型函数访问的全局内化到 main
    registerModulePass("DeadFunctionElim", [](Module * currentModule) {
        DeadFunctionElim pass(currentModule);
        return pass.run();
    });

    // 死函数清除后，只写不读且地址不逃逸的全局（典型如仅初始化的全局数组）
    // 上的 store 已无任何读者，消除之；此时尚未 Mem2Reg，删除 store 后其
    // 地址 GEP 即成无用户死指令，交由下游 DeadInstElim 清扫
    registerModulePass("DeadGlobalStoreElim", [](Module * currentModule) {
        DeadGlobalStoreElim pass(currentModule);
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

    // memo 的哈希表、epoch 和递归深度是函数级共享状态。当前 IR 尚无原子/
    // 线程局部存储支持，因此并行模式下不注册，避免多个线程同时调用同一递归
    // 函数时产生数据竞争。默认单线程流水线保持启用。
    if (!enableParallel) {
        registerFunctionPass("PureCallMemoize", [this](Function * func) {
            PureCallMemoize pass(func, module);
            return pass.run();
        });
    }

    registerFunctionPass("TailRecursionElim", [](Function * func) {
        TailRecursionElim pass(func);
        return pass.run();
    });

    // 循环并行（多线程）优化：默认关闭，仅在 --parallel=on 时插入
    // 须在 LSR/LoopTiling 等循环变换之前运行，否则规范循环形态被破坏后匹配器无法识别
    if (enableParallel) {
        registerFunctionPass("LoopParallelize", [this](Function * func) {
            LoopParallelize pass(func, module);
            return pass.run();
        });
    }

    registerLateModulePass("PostInlineCleanup", [](Module * currentModule) {
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

            LICM licm(func);
            changed = licm.run() || changed;

            InstCombine instCombine(func, currentModule);
            changed = instCombine.run() || changed;
        }
        return changed;
    });

    // 晚期内联可能让更多初始化型函数变为死函数，
    // 再次清除死函数并下沉其访问的全局标量，然后用 Mem2Reg 把这些
    // 槽位提升到 SSA，为后续循环优化创造条件
    registerLateModulePass("PostInlineGlobalCleanup", [](Module * currentModule) {
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

                LICM licm(func);
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
    registerFixedPointFunctionPass("LocalMemoryOpt", [](Function * func) {
        LocalMemoryOpt pass(func);
        return pass.run();
    });
    registerFixedPointFunctionPass("GVN", [this](Function * func) {
        GVN pass(func, module);
        return pass.run();
    });
    registerFixedPointFunctionPass("LICM", [](Function * func) {
        LICM pass(func);
        return pass.run();
    });
    registerFixedPointFunctionPass("CanonicalizeLoop", [this](Function * func) {
        CanonicalizeLoop pass(func, module);
        return pass.run();
    });
    // 仅用 DeadInstElim 这类 CFG 保形的值级清理；CFGSimplify 会合并块、
    // 破坏 CanonicalizeLoop 建立的 preheader / 专属 exit 规范形式，
    // 从而让下游基于 SCEV 的循环 pass（LoopExitValueRewrite / LoopTiling / LSR）
    // 误判循环形态并产生错误代码，故中期不插入 CFGSimplify
    registerFixedPointFunctionPass("DeadInstElim", [](Function * func) {
        DeadInstElim pass(func);
        return pass.run();
    });

    // ---- 子组2：循环变换 ----
    // 先做相邻同界计数循环融合：此时循环仍是规范非旋转形态（比较位于循环头），
    // 且尚未被 LSR 改写为指针游标，matchCanonicalLoop 可识别；融合后再交给
    // LoopExitValueRewrite / LoopTiling / LSR 等下游 pass
    registerFixedPointFunctionPass("LoopFusion", [this](Function * func) {
        LoopFusion pass(func, module);
        return pass.run();
    });
    // 基于值域的 2 的幂取模/除法削减：此时循环仍是规范计数形态，
    // SCEV 可用归纳变量的常量迭代数证明被除数非负，将带符号取模/除法
    // 改写为 and/移位；须在 LSR 把归纳变量改写为指针游标之前运行
    registerFixedPointFunctionPass("RangeModSimplify", [this](Function * func) {
        RangeModSimplify pass(func, module);
        return pass.run();
    });
    // 单调守卫循环的空转尾部折叠：把"唯一工作被 IV 单调谓词守卫"的计数循环
    // 上界钳制为工作区间上限并消除守卫分支，使循环收敛为单 latch 形态。
    // 须在 LSR 之前运行（折叠后 LSR 才能识别唯一 latch 并做指针游标化），
    // 也须在 RangeModSimplify 之后（钳制会把常量迭代数变为 select 动态值）
    registerFixedPointFunctionPass("GuardedTailCollapse", [this](Function * func) {
        GuardedTailCollapse pass(func, module);
        return pass.run();
    });
    // 基于 SCEV 把规范计数循环头部递推 phi 的出口取值替换为闭式表达式，
    // 随后消除因此变为无副作用且出口无依赖的空循环
    registerFixedPointFunctionPass("LoopExitValueRewrite", [this](Function * func) {
        LoopExitValueRewrite pass(func, module);
        return pass.run();
    });
    // 先清除出口值替换产生的死 phi / 死指令，使更多空循环能被 RemoveEmptyLoop 消除
    // 仅用 DeadInstElim（值级清理，不改变 CFG 形状），避免破坏循环规范形影响下游循环 pass
    registerFixedPointFunctionPass("DeadInstElim", [](Function * func) {
        DeadInstElim pass(func);
        return pass.run();
    });
    registerFixedPointFunctionPass("RemoveEmptyLoop", [this](Function * func) {
        RemoveEmptyLoop pass(func, module);
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
    // 仅用值级清理：LSR / Tiling 产生的死指令及时清除，不动 CFG 形状
    registerFixedPointFunctionPass("DeadInstElim", [](Function * func) {
        DeadInstElim pass(func);
        return pass.run();
    });

    // ---- 子组3：向量化 / 展开 ----
    if (enableRVVLoopVectorize) {
        // RVV 向量化放在循环规范化/强度削减之后，输入循环形态更稳定
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
    registerFixedPointFunctionPass("DeadInstElim", [](Function * func) {
        DeadInstElim pass(func);
        return pass.run();
    });
    // 注意， IndVarSimplify 一定要在 SimpleLoopUnroll 之后，否则 IndVarSimplify 会改写那些本来会被展开的循环(<16的迭代次数)，导致 SimpleLoopUnroll 无法 matchCanonicalLoop.
    registerFixedPointFunctionPass("IndVarSimplify", [this](Function * func) {
        IndVarSimplify pass(func, module);
        return pass.run();
    });
    registerFixedPointFunctionPass("ConstProp", [this](Function * func) {
        ConstProp pass(func, module);
        return pass.run();
    });

    // ---- 子组4：晚期值优化 ----
    registerFixedPointFunctionPass("PureCallCSE", [this](Function * func) {
        PureCallCSE pass(func, module);
        return pass.run();
    });
    registerFixedPointFunctionPass("LICM", [](Function * func) {
        LICM pass(func);
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
    // 有界位迭代循环求解：依赖 PhiToSelect 把累加分支规范成 select 形态
    registerFixedPointFunctionPass("BoundedBitLoopSolver", [this](Function * func) {
        BoundedBitLoopSolver pass(func, module);
        return pass.run();
    });
    // 循环不变 select 条件版本化：把「每次迭代都选同一侧」的 select 剥出循环
    registerFixedPointFunctionPass("LoopVersionInvariantSelect", [this](Function * func) {
        LoopVersionInvariantSelect pass(func, module);
        return pass.run();
    });
    registerFixedPointFunctionPass("InstCombine", [this](Function * func) {
        InstCombine pass(func, module);
        return pass.run();
    });
    // 部分死 store 消除：将「先做栈数组初始化、再判断提前返回」的检查上提
    // 到入口，提前返回路径不再执行死 store。依赖此前 LocalMemoryOpt /
    // ConstProp / InstCombine 已把 || 短路条件收敛为纯参数表达式（phi 形态）
    registerFixedPointFunctionPass("PartialDeadStoreElim", [](Function * func) {
        PartialDeadStoreElim pass(func);
        return pass.run();
    });
    // 子组4 清理：再做一次纯调用 CSE 与常量传播，然后消除不可达块与死指令并简化 CFG
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

    // LateInline：定点循环优化收敛后再做一轮内联。此时 SCEV 闭式替换、
    // RemoveEmptyLoop 等已消除大量循环体指令，原本超过指令阈值的函数
    // 可能已瘦身到可内联的规模。内联后清理死函数并下沉全局标量，
    // 若 IR 发生变化，PassManager 会再跑一轮定点迭代优化新内联的代码
    registerPostFixedPointModulePass("LateInline", [](Module * currentModule) {
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

                LICM licm(func);
                licm.run();

                InstCombine instCombine(func, currentModule);
                instCombine.run();
            }
        }
        return changed;
    });

    registerLateFunctionPass("PostFixedPointLoopCleanup", [](Function * func) {
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
    // LoopRotate 暂不执行；其余晚期循环优化仍放在最后，避免后续 pass 破坏结果
    registerLateFunctionPass("LateLoopOpt", [this](Function * func) {
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

        // 开发板 A/B 测试中关闭 LoopRotate 后总耗时由 144.7150s 降至 144.3572s，
        // 18 个受影响用例中有 16 个加速，因此暂时禁用并保留代码供后续复测
        // LoopRotate loopRotate(func);
        // changed = loopRotate.run() || changed;

        return changed;
    });

    // LateLoopOpt 中的 CanonicalizeLoop 会为多回边循环重建 synthetic 合并
    // latch（仅含 phi + 无条件跳转），该块在热循环每迭代引入额外的 phi 拷贝
    // 与跳转。此时所有循环分析已结束，不再需要唯一回边形态，补一轮
    // LateLoopCFGCleanup 把 latch phi 折回头部 phi、让各回边直接跳头部
    registerLateFunctionPass("PostLateLoopCFGCleanup", [](Function * func) {
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

}

/// @brief 注册后端前置的 phi 降级流水线
void PassManager::registerPhiLoweringPipeline()
{
    clear();
    if (module == nullptr) {
        return;
    }

    registerFunctionPass("PreLoweringPhiToSelect", [](Function * func) {
        PhiToSelect pass(func);
        return pass.run();
    });

    registerFunctionPass("PhiLowering", [this](Function * func) {
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

/// @brief 注册模块级 pass（带名字，可被 MINIC_DISABLE_PASSES 关闭）
void PassManager::registerModulePass(const std::string & name, ModulePassRunner runner)
{
    if (isPassEnabled(name)) {
        registerModulePass(std::move(runner));
    }
}

/// @brief 注册定点函数级 pass 之前执行的模块级 pass（带名字，可关闭）
void PassManager::registerLateModulePass(const std::string & name, ModulePassRunner runner)
{
    if (isPassEnabled(name)) {
        registerLateModulePass(std::move(runner));
    }
}

/// @brief 注册单次函数级 pass（带名字，可关闭）
void PassManager::registerFunctionPass(const std::string & name, FunctionPassRunner runner)
{
    if (isPassEnabled(name)) {
        registerFunctionPass(std::move(runner));
    }
}

/// @brief 注册参与定点迭代的函数级 pass（带名字，可关闭）
void PassManager::registerFixedPointFunctionPass(const std::string & name, FunctionPassRunner runner)
{
    if (isPassEnabled(name)) {
        registerFixedPointFunctionPass(std::move(runner));
    }
}

/// @brief 注册定点迭代收敛后执行的模块级 pass（带名字，可关闭）
void PassManager::registerPostFixedPointModulePass(const std::string & name, ModulePassRunner runner)
{
    if (isPassEnabled(name)) {
        registerPostFixedPointModulePass(std::move(runner));
    }
}

/// @brief 注册定点迭代收敛后执行一次的后置函数级 pass（带名字，可关闭）
void PassManager::registerLateFunctionPass(const std::string & name, FunctionPassRunner runner)
{
    if (isPassEnabled(name)) {
        registerLateFunctionPass(std::move(runner));
    }
}

/// @brief 从 MINIC_DISABLE_PASSES 环境变量加载被关闭的 pass 名集合（逗号/分号/空白分隔，仅加载一次）
void PassManager::loadPassToggles()
{
    if (togglesLoaded) {
        return;
    }
    togglesLoaded = true;
    disabledPasses.clear();

    const char * env = std::getenv("MINIC_DISABLE_PASSES");
    if (env == nullptr || env[0] == '\0') {
        return;
    }

    std::string token;
    std::istringstream iss(env);
    while (std::getline(iss, token, ',')) {
        // 去除前后空白
        std::size_t begin = 0;
        while (begin < token.size() && (token[begin] == ' ' || token[begin] == '\t')) {
            ++begin;
        }
        std::size_t end = token.size();
        while (end > begin && (token[end - 1] == ' ' || token[end - 1] == '\t')) {
            --end;
        }
        if (end > begin) {
            disabledPasses.insert(token.substr(begin, end - begin));
        }
    }
}

/// @brief 判断某个具名 pass 是否启用；MINIC_DUMP_PASSES 置位时把 pass 清单打到 stderr
/// @param name pass 名
/// @return true 表示该 pass 应被注册执行
bool PassManager::isPassEnabled(const std::string & name)
{
    loadPassToggles();
    const bool enabled = disabledPasses.find(name) == disabledPasses.end();
    if (std::getenv("MINIC_DUMP_PASSES") != nullptr) {
        std::cerr << "[PassManager] " << (enabled ? "ENABLE " : "DISABLE") << " pass: " << name << '\n';
    }
    return enabled;
}
