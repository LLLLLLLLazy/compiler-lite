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

class BasicBlock;
class DominatorTree;
class Function;
class Instruction;
class Module;

class LICM {

public:
    /// @brief 构造 LICM pass
    /// @param func 待优化的函数
    explicit LICM(Function * func, Module * mod = nullptr);

    /// @brief 对函数原地执行 LICM
    /// @return 若 IR 被修改则返回 true
    bool run();

private:
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
    /// @return 若该循环被修改则返回 true
    bool tryHoistLoop(BasicBlock * header,
                      const std::unordered_set<BasicBlock *> & loopBody,
                      const DominatorTree & domTree);

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

    /// @brief 判断 load 是否满足安全外提条件
    /// @param inst 待检查的 load 指令
    /// @param loopBody 当前自然循环的块集合
    /// @return true 表示 load 指向只读全局或未逃逸局部精确槽且循环内无冲突写
    bool isSafeLoadToHoist(Instruction * inst, const std::unordered_set<BasicBlock *> & loopBody) const;

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

    /// @brief 判断候选指令是否支配其全部使用点
    /// @param inst 待检查的候选指令
    /// @param domTree 当前函数的支配树
    /// @return true 表示该指令支配所有普通 use 与 phi incoming use
    bool dominatesAllUses(Instruction * inst, const DominatorTree & domTree) const;

    Function * func = nullptr;
    Module * mod = nullptr;
};
