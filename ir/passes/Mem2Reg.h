///
/// @file Mem2Reg.h
/// @brief mem2reg pass
///
/// 将可提升的局部变量从 alloca/load/store 形式提升为 SSA φ 节点。
/// 只提升满足以下条件的 alloca：
///   - 所有用途均为 load（作为指针操作数）或 store（作为指针操作数）；
///   - 没有地址逃逸（不作为 store 的值操作数、不传入函数等）。
///
/// 算法步骤：
///   1. 找出可提升的 alloca（在入口块中）；
///   2. 用 Cytron et al. IDF 算法确定 φ 节点插入位置；
///   3. 沿支配树 DFS 进行重命名（替换 load/store 为 SSA 值，填充 φ incoming）；
///   4. 清除多余的 alloca/load/store 指令。
///

#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

class AllocaInst;
class BasicBlock;
class DominanceFrontier;
class DominatorTree;
class Function;
class Module;
class PhiInst;
class Value;

class Mem2Reg {

public:
    Mem2Reg(Function * func, Module * mod);

    /// 对函数原地执行 mem2reg。执行后，可提升的 alloca/load/store 被移除，
    /// 控制流汇合点插入 φ 节点。
    void run();

private:
    Function * func;
    Module * mod;

    // ---- Step 1 ----

    /// 收集入口块中所有可提升的 alloca 指令。
    std::vector<AllocaInst *> findPromotableAllocas();

    /// 若 alloca 的所有用途均为 load/store（且不作为 store 的值操作数），返回 true。
    bool isPromotable(AllocaInst * alloca) const;

    // ---- Step 2 ----

    /// 用 IDF 算法为每个可提升 alloca 在合适的块头插入 φ 节点。
    /// 结果写入 allocaPhis（alloca → block → phi）和 phiToAlloca（phi → alloca）。
    void insertPhiNodes(
        const std::vector<AllocaInst *> & allocas,
        const DominanceFrontier & df,
        std::unordered_map<AllocaInst *, std::unordered_map<BasicBlock *, PhiInst *>> & allocaPhis,
        std::unordered_map<PhiInst *, AllocaInst *> & phiToAlloca);

    // ---- Step 3 ----

    /// 重命名遍：沿支配树 DFS，用到达定值替换 load，记录 store 定值，填充 φ incoming。
    void rename(
        BasicBlock * bb,
        const DominatorTree & dt,
        std::unordered_map<AllocaInst *, std::vector<Value *>> & reachingDefs,
        const std::unordered_map<AllocaInst *, std::unordered_map<BasicBlock *, PhiInst *>> & allocaPhis,
        const std::unordered_map<PhiInst *, AllocaInst *> & phiToAlloca,
        std::unordered_set<BasicBlock *> & visited);

    // ---- Step 4 ----

    /// 删除所有被标记为 dead 的 load/store，以及可提升的 alloca 本身。
    void cleanup(const std::vector<AllocaInst *> & allocas);
};
