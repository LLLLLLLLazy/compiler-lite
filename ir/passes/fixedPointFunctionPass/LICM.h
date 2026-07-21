///
/// @file LICM.h
/// @brief 循环不变量外提 pass
///
/// 基于 DominatorTree 与 LoopInfo 对自然循环做保守 LICM：
///   1. 为循环头创建或复用 preheader
///   2. 识别仅依赖循环外值或已知循环不变量的纯计算指令
///   3. 对不可安全推测执行的候选指令，要求其支配所有循环退出点
///   4. 要求候选指令支配全部使用点
///   5. 将满足条件的指令移动到 preheader 终结指令之前
///

#pragma once

#include <unordered_set>
#include <vector>

class AllocaInst;
class BasicBlock;
class DominatorTree;
class FormalParam;
class Function;
class Instruction;
class Module;
class ParamAliasAnalysis;
class PureFunctionAnalysis;

class LICM {

public:
    /// @brief 构造 LICM pass
    /// @param func 待优化的函数
    explicit LICM(Function * func, Module * mod = nullptr);

    /// @brief 对函数原地执行 LICM
    /// @return 若 IR 被修改则返回 true
    bool run();

private:
    /// @brief load 外提安全性分类
    enum class LoadHoistKind {
        Reject,          ///< 不可外提
        Speculate,       ///< 地址恒有效（全局/本帧 alloca 根），可推测执行
        LatchDominance,  ///< 形参根：需支配全部 latch，仅零迭代循环会多执行一次
    };

    struct HeaderPhiPlan {
        Instruction * phi = nullptr;
        std::vector<class Value *> outsideValues;
    };

    /// @brief 收集循环头所有来自循环外部的前驱块
    /// @param header 循环头基本块
    /// @param loopBody 当前自然循环的块集合
    /// @return 所有位于循环外的前驱块列表
    std::vector<BasicBlock *> collectOutsidePredecessors(
        BasicBlock * header,
        const std::unordered_set<BasicBlock *> & loopBody) const;

    /// @brief 判断现有循环外前驱是否已经形成可复用的 preheader
    /// @param outsidePreds 循环头的循环外前驱列表
    /// @return 若存在唯一且合法的 preheader 则返回该块，否则返回 nullptr
    BasicBlock * getExistingPreheader(const std::vector<BasicBlock *> & outsidePreds) const;

    /// @brief 对单个自然循环执行外提
    /// @param header 循环头基本块
    /// @param loopBody 当前自然循环的块集合
    /// @param domTree 当前函数的支配树
    /// @param purity 模块级纯函数分析（用于识别内存无关调用）
    /// @return 若该循环被修改则返回 true
    bool tryHoistLoop(BasicBlock * header,
                      const std::unordered_set<BasicBlock *> & loopBody,
                      const DominatorTree & domTree,
                      PureFunctionAnalysis & purity);

    /// @brief 为循环头新建 preheader 并重写相关 phi 与 CFG 边
    /// @param header 循环头基本块
    /// @param outsidePreds 循环头的循环外前驱列表
    /// @return 若成功创建并接入 preheader 则返回 true
    bool createPreheader(BasicBlock * header, const std::vector<BasicBlock *> & outsidePreds);

    /// @brief 将前驱块终结指令中指向旧目标的边改写到新目标
    /// @param pred 待改写的前驱块
    /// @param oldTarget 旧跳转目标
    /// @param newTarget 新跳转目标
    /// @return 若成功改写至少一条 CFG 边则返回 true
    bool rewriteTerminatorTarget(BasicBlock * pred, BasicBlock * oldTarget, BasicBlock * newTarget) const;

    /// @brief 将新建基本块插入到指定基本块之前
    /// @param bb 待插入的基本块
    /// @param before 作为插入锚点的基本块
    void insertBlockBefore(BasicBlock * bb, BasicBlock * before) const;

    /// @brief 将一条循环不变量指令移动到 preheader 终结指令之前
    /// @param inst 待移动的指令
    /// @param preheader 目标 preheader 基本块
    void moveToPreheader(Instruction * inst, BasicBlock * preheader) const;

    /// @brief 判断指令类型是否允许参与 LICM 候选
    /// @param inst 待检查的指令
    /// @return true 表示该指令属于可外提的纯计算指令
    bool isHoistableInstruction(Instruction * inst) const;

    /// @brief 对 load 按指针根对象分类外提安全性
    /// @param inst 待检查的 load 指令
    /// @param loopBody 当前自然循环的块集合
    /// @return 分类结果，见 LoadHoistKind
    LoadHoistKind classifyLoadHoist(Instruction * inst,
                                    const std::unordered_set<BasicBlock *> & loopBody) const;

    /// @brief 判断循环体是否可能改写以本帧 alloca 为根的 load 地址
    /// @param root load 地址的根 alloca
    /// @param loopBody 当前自然循环的块集合
    /// @return true 表示存在可能的改写
    bool loopMayClobberAllocaLoad(AllocaInst * root,
                                  const std::unordered_set<BasicBlock *> & loopBody) const;

    /// @brief 判断循环体是否可能改写以形参为根的 load 地址
    /// @param root load 地址的根形参
    /// @param loopBody 当前自然循环的块集合
    /// @return true 表示存在可能的改写
    bool loopMayClobberParamLoad(FormalParam * root,
                                 const std::unordered_set<BasicBlock *> & loopBody) const;

    /// @brief 判断候选指令是否需要额外满足退出点支配约束
    /// @param inst 待检查的指令
    /// @return true 表示该指令不可安全推测执行
    bool requiresExitDominance(Instruction * inst) const;

    /// @brief 判断指令的全部操作数是否已经循环不变
    /// @param inst 待检查的候选指令
    /// @param loopBody 当前自然循环的块集合
    /// @param invariants 已识别出的循环不变量集合
    /// @return true 表示该指令的全部操作数均循环不变
    bool operandsAreLoopInvariant(
        Instruction * inst,
        const std::unordered_set<BasicBlock *> & loopBody,
        const std::unordered_set<Instruction *> & invariants) const;

    /// @brief 判断定义块是否支配当前循环的全部退出点
    /// @param defBlock 候选指令所在基本块
    /// @param loopBody 当前自然循环的块集合
    /// @param domTree 当前函数的支配树
    /// @return true 表示定义块支配所有循环退出点
    bool dominatesAllLoopExits(BasicBlock * defBlock,
                               const std::unordered_set<BasicBlock *> & loopBody,
                               const DominatorTree & domTree) const;

    /// @brief 判断定义块是否支配当前循环的全部 latch 块
    /// @param defBlock 候选指令所在基本块
    /// @param header 循环头基本块
    /// @param loopBody 当前自然循环的块集合
    /// @param domTree 当前函数的支配树
    /// @return true 表示每轮完整迭代都必然执行该定义块
    bool dominatesAllLoopLatches(BasicBlock * defBlock,
                                 BasicBlock * header,
                                 const std::unordered_set<BasicBlock *> & loopBody,
                                 const DominatorTree & domTree) const;

    /// @brief 判断候选指令是否支配其全部使用点
    /// @param inst 待检查的候选指令
    /// @param domTree 当前函数的支配树
    /// @return true 表示该指令支配所有普通 use 与 phi incoming use
    bool dominatesAllUses(Instruction * inst, const DominatorTree & domTree) const;

    Function * func = nullptr;
    Module * mod = nullptr;
    /// 共享纯函数分析，仅在 run() 执行期间有效（指向栈上对象）
    PureFunctionAnalysis * purityAnalysis = nullptr;
    /// 共享形参别名分析，仅在 run() 执行期间有效（指向栈上对象）
    ParamAliasAnalysis * paramAliasAnalysis = nullptr;
};
