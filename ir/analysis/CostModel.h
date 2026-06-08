///
/// @file CostModel.h
/// @brief 轻量成本模型(mini-TTI)：为各优化 pass 提供统一的“收益性判断”依据。
///
/// 设计目标对标 LLVM 的 TargetTransformInfo：把原本散落在各 pass 里的阈值魔数
/// (PhiToSelect 的 FunctionScale、SimpleLoopUnroll 的 kMax*、SmallFunctionInline 的 k* 等)
/// 收敛到一处，作为“合法性 → 收益性”两段式判断里 isProfitable 那一段的共享查询接口。
///
/// 用法约定：所有 pass 的收益门都先判 profitabilityEnabled()，置环境变量
/// MINIC_DISABLE_PROFITABILITY 时全部门退化为 no-op(等同未加门的历史行为)，
/// 便于不重新编译就做“加门 vs 不加门”的 A/B 对比。
///

#pragma once

#include <unordered_set>
#include <vector>

class BasicBlock;
class Instruction;

namespace CostModel {

// ===================== 全局开关 =====================

/// @brief 收益门是否启用(读 MINIC_DISABLE_PASSES 同风格的 MINIC_DISABLE_PROFITABILITY)
/// @return false 表示所有收益门应退化为 no-op(恒认为“值得做”)
bool profitabilityEnabled();

/// @brief 是否打印优化决策 remark(读 MINIC_OPT_REMARKS，对标 LLVM -Rpass)
bool remarksEnabled();

/// @brief 打印一条优化决策 remark 到 stderr(仅在 remarksEnabled 时生效)
/// @param pass    pass 名，如 "vectorize"
/// @param applied true=已应用，false=已跳过
/// @param reason  原因短语
void remark(const char * pass, bool applied, const char * reason);

// ===================== 指令/循环代价 =====================

/// @brief 单条指令的相对静态代价(cycle 量级的相对权重，越大越贵)
int instCost(Instruction * inst);

/// @brief 基本块所有指令的代价之和
long blockCost(const BasicBlock * bb);

/// @brief 循环体(块集合)的代价之和
long loopBodyCost(const std::unordered_set<BasicBlock *> & body);

// ===================== 寄存器压力(粗估) =====================

/// @brief 寄存器压力上界代理：分别统计产生整型/浮点结果的指令数
struct RegPressure {
    int gpr = 0;  ///< 产生整型/指针结果的指令数
    int fpr = 0;  ///< 产生浮点结果的指令数
};

/// @brief 估计一组指令产生的寄存器压力(以“产生结果的指令数”作上界代理)
RegPressure estimateRegPressure(const std::vector<Instruction *> & insts);

/// @brief 估计循环体(块集合)产生的寄存器压力
RegPressure estimateBodyRegPressure(const std::unordered_set<BasicBlock *> & body);

// ===================== 目标事实(来自后端 GreedyRegAllocator) =====================

constexpr int kAllocatableGPR = 24;  ///< 整型可分配寄存器数(buildRegisterPool)
constexpr int kAllocatableFPR = 18;  ///< 浮点可分配寄存器数(buildFloatRegisterPool)
constexpr int kAllocatableVec = 29;  ///< 向量可分配寄存器数(buildVectorRegisterPool)
constexpr int kRegSafetyMargin = 2;  ///< 给指令选择/reload 预留的余量

/// @brief 可供分配器使用的整型寄存器数(留余量)
int usableGPR();
/// @brief 可供分配器使用的浮点寄存器数(留余量)
int usableFPR();

/// @brief 循环深度对应的热度权重(镜像溢出权重 pow(10, depth)，封顶防溢出)
double loopDepthWeight(int depth);

// ===================== 内联阈值查询(支持环境变量覆盖) =====================

/// @brief 获取内联默认阈值(可通过 MINIC_INLINE_THRESHOLD 环境变量覆盖)
int getInlineThreshold();

/// @brief 获取热点调用点内联倍数(可通过 MINIC_INLINE_HOT_MULTIPLIER 环境变量覆盖)
int getInlineHotMultiplier();

// ===================== 阈值(调参中心) =====================

// —— 向量化(LoopVectorize) ——
constexpr int kVecMinTripCount = 8;  ///< 已知 trip count 小于此值不向量化(vsetvli/横向 reduce 摊不开)
constexpr long kVecMinBodyCost = 3;  ///< 循环体代价过低不值得向量化

// —— 循环分块(LoopTiling) ——
constexpr long kTileMinIterations = 1024;  ///< 已知 outer*inner 迭代数小于此值不分块(工作集太小，cache 命中)

// —— 完全展开(SimpleLoopUnroll) ——
constexpr int kUnrollMaxLiveProduct = 256;  ///< (体内结果指令数 × tripCount) 超此值不展开(防 spill/代码膨胀)

// —— if-conversion(PhiToSelect) ——
constexpr int kSelectMaxSpeculatedInstCost = 6;   ///< 三角形结构中被提前投机执行的单条指令代价上限
constexpr int kSelectMaxDiamondTotalCost = 12;    ///< diamond 两臂被无条件执行的非终结指令总代价上限

// —— 函数内联(SmallFunctionInline) ——
// 参考 GCC: max-inline-insns-single=70, max-inline-insns-auto=15, inline-min-speedup=30
// 参考 LLVM: default threshold=225, hot multiplier=3x, OptSize=75
constexpr int kInlineDefaultThreshold = 150;         ///< 默认内联成本阈值(加权成本，非指令数)
constexpr int kInlineSmallThreshold = 25;            ///< 小函数自动内联阈值(对应 GCC auto=15)
constexpr int kInlineHotMultiplier = 3;              ///< 热点调用点倍数(对应 LLVM 3x)
constexpr int kInlineRecursiveThreshold = 400;       ///< 递归函数阈值(对应 GCC 450)
constexpr int kInlineMaxBlocks = 15;                 ///< 最大基本块数(结构复杂度限制)
constexpr int kInlineMaxParams = 8;                  ///< 最大参数数量
constexpr int kInlineMaxAllocaBytes = 128;           ///< 最大 alloca 字节数
constexpr int kInlineHotMaxAllocaBytes = 256;        ///< 热点最大 alloca 字节数
constexpr int kInlineUnitGrowthPercent = 50;         ///< 函数体积增长上限百分比(对应 GCC 40)

// —— cache 假设(供 footprint 估算；当前后端无真实 cache 模型) ——
constexpr long kL1Bytes = 32 * 1024;
constexpr long kL2Bytes = 256 * 1024;

} // namespace CostModel
