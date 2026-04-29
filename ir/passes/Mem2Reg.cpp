///
/// @file Mem2Reg.cpp
/// @brief mem2reg 优化实现
///
///       把局部变量从`内存读写形式（alloca/load/store)`
///       提升成`SSA 寄存器形式（直接用值和 phi）`，
///       从而让 IR 更简洁、更容易优化。
///
/// 参考：
///   Cytron et al., "Efficiently Computing Static Single Assignment Form and
///   the Control Dependence Graph", TOPLAS 1991.
///   Cooper & Torczon, "Engineering a Compiler", 2nd ed., Chapter 9.
///

#include "Mem2Reg.h"

#include <list>
#include <set>
#include <unordered_set>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "ConstInt.h"
#include "DominanceFrontier.h"
#include "DominatorTree.h"
#include "Function.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "Module.h"
#include "PhiInst.h"
#include "StoreInst.h"
#include "Types/IntegerType.h"
#include "Value.h"

// ---------------------------------------------------------------------------
// 构造函数
// ---------------------------------------------------------------------------

/// @brief 构造 mem2reg 优化器
/// @param _func 待优化的函数
/// @param _mod 所属模块
Mem2Reg::Mem2Reg(Function * _func, Module * _mod) : func(_func), mod(_mod)
{}

// ---------------------------------------------------------------------------
// 对外入口
// ---------------------------------------------------------------------------

/// @brief 对函数原地执行 mem2reg
void Mem2Reg::run()
{
    if (!func || func->isBuiltin() || func->getBlocks().empty()) {
        return;
    }

    // 第 1 步：收集可提升的 alloca
    std::vector<AllocaInst *> allocas = findPromotableAllocas();
    if (allocas.empty()) {
        return;
    }

    // 第 2、3 步需要支配信息
    DominatorTree dt(func);
    DominanceFrontier df(func, dt);

    // 第 2 步：在定义块的迭代支配边界上插入 phi 节点
    // allocaPhis[alloca][block] 表示为该 alloca 在该块顶部插入的 phi
    std::unordered_map<AllocaInst *, std::unordered_map<BasicBlock *, PhiInst *>> allocaPhis;
    std::unordered_map<PhiInst *, AllocaInst *> phiToAlloca;
    insertPhiNodes(allocas, df, allocaPhis, phiToAlloca);

    // 第 3 步：重命名，并初始化到达定义栈
    std::unordered_map<AllocaInst *, std::vector<Value *>> reachingDefs;
    for (auto * alloca : allocas) {
        reachingDefs[alloca]; // 为每个 alloca 建立一条空栈记录
    }

    std::unordered_set<BasicBlock *> visited;
    rename(func->getEntryBlock(), dt, reachingDefs, allocaPhis, phiToAlloca, visited);

    // 第 4 步：移除死指令以及被提升的 alloca 本体
    cleanup(allocas);
}

// ---------------------------------------------------------------------------
// 第 1 步：寻找可提升的 alloca
// ---------------------------------------------------------------------------

/// @brief 收集入口块中可提升为 SSA 的 alloca 指令
/// @return 可提升的 alloca 列表
std::vector<AllocaInst *> Mem2Reg::findPromotableAllocas()
{
    std::vector<AllocaInst *> result;

    // IRGenerator 总是把 alloca 放在入口块中
    BasicBlock * entry = func->getEntryBlock();
    if (!entry) {
        return result;
    }

    for (auto * inst : entry->getInstructions()) {
        if (auto * alloca = dynamic_cast<AllocaInst *>(inst)) {
            if (isPromotable(alloca)) {
                result.push_back(alloca);
            }
        }
    }

    return result;
}

/// @brief 判断某条 alloca 是否满足 mem2reg 提升条件
/// @param alloca 待判断的 alloca 指令
/// @return true 表示可提升，false 表示不可提升
bool Mem2Reg::isPromotable(AllocaInst * alloca) const
{
    // 仅提升标量元素类型，不提升指针元素类型
    Type * elemType = alloca->getAllocaType();
    if (!elemType || elemType->isPointerType()) {
        return false;
    }

    for (auto * use : alloca->getUseList()) {
        auto * u = use->getUser();

        if (auto * load = dynamic_cast<LoadInst *>(u)) {
            // alloca 必须作为 load 的指针操作数出现
            if (load->getPointerOperand() != static_cast<Value *>(alloca)) {
                return false;
            }
        } else if (auto * store = dynamic_cast<StoreInst *>(u)) {
            // alloca 必须作为 store 的地址操作数，而不能作为被写入的值
            if (store->getValueOperand() == static_cast<Value *>(alloca)) {
                // 地址发生逃逸：alloca 被当作普通值存储了
                return false;
            }
            if (store->getPointerOperand() != static_cast<Value *>(alloca)) {
                return false;
            }
        } else {
            // 其他任何用法（例如作为实参传给函数）都不可提升
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// 第 2 步：放置 phi 节点（Cytron 等人的 IDF 算法）
// ---------------------------------------------------------------------------

/// @brief 为可提升的 alloca 在合适的基本块顶部插入 phi 节点
/// @param allocas 可提升的 alloca 列表
/// @param df 支配边界信息
/// @param allocaPhis 输出 alloca 到块中 phi 的映射
/// @param phiToAlloca 输出 phi 到来源 alloca 的映射
void Mem2Reg::insertPhiNodes(
    const std::vector<AllocaInst *> & allocas,
    const DominanceFrontier & df,
    std::unordered_map<AllocaInst *, std::unordered_map<BasicBlock *, PhiInst *>> & allocaPhis,
    std::unordered_map<PhiInst *, AllocaInst *> & phiToAlloca)
{
    for (auto * alloca : allocas) {
        // 收集对该 alloca 执行过 store 的基本块，作为定义点
        std::vector<BasicBlock *> defBlocks;
        for (auto * bb : func->getBlocks()) {
            for (auto * inst : bb->getInstructions()) {
                if (auto * store = dynamic_cast<StoreInst *>(inst)) {
                    if (store->getPointerOperand() == static_cast<Value *>(alloca)) {
                        defBlocks.push_back(bb);
                        break; // 该块出现过一次 store 就足够说明是定义点
                    }
                }
            }
        }

        if (defBlocks.empty()) {
            // 若从未被写入，则后续 load 会在重命名阶段被替换为 0
            continue;
        }

        // 计算该 alloca 的迭代支配边界并插入 phi
        std::unordered_set<BasicBlock *> placed;         // 已插入 phi 的基本块
        std::unordered_set<BasicBlock *> everOnWorklist; // 曾经进入工作队列的基本块
        std::vector<BasicBlock *> worklist;

        for (auto * bb : defBlocks) {
            everOnWorklist.insert(bb);
            worklist.push_back(bb);
        }

        while (!worklist.empty()) {
            BasicBlock * bb = worklist.back();
            worklist.pop_back();

            for (BasicBlock * y : df.getFrontier(bb)) {
                if (placed.count(y)) {
                    continue;
                }

                // 在 y 的块头为该 alloca 插入 phi
                auto * phi = new PhiInst(func, alloca->getAllocaType());
                // phi 节点必须位于基本块顶部，因此插到指令链表开头
                auto & insts = y->getInstructions();
                insts.push_front(phi);
                phi->setParentBlock(y);

                placed.insert(y);
                allocaPhis[alloca][y] = phi;
                phiToAlloca[phi] = alloca;

                if (!everOnWorklist.count(y)) {
                    everOnWorklist.insert(y);
                    worklist.push_back(y);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 第 3 步：重命名遍历（沿支配树 DFS）
// ---------------------------------------------------------------------------

/// @brief 沿支配树递归重命名 SSA 值并回填 phi incoming
/// @param bb 当前处理的基本块
/// @param dt 支配树信息
/// @param reachingDefs 每个 alloca 的到达定义栈
/// @param allocaPhis alloca 到所在块 phi 的映射
/// @param phiToAlloca phi 到来源 alloca 的映射
/// @param visited 已访问基本块集合
void Mem2Reg::rename(
    BasicBlock * bb,
    const DominatorTree & dt,
    std::unordered_map<AllocaInst *, std::vector<Value *>> & reachingDefs,
    const std::unordered_map<AllocaInst *, std::unordered_map<BasicBlock *, PhiInst *>> & allocaPhis,
    const std::unordered_map<PhiInst *, AllocaInst *> & phiToAlloca,
    std::unordered_set<BasicBlock *> & visited)
{
    if (visited.count(bb)) {
        return;
    }
    visited.insert(bb);

    // 记录每个 alloca 在当前块中压栈了多少个值，便于离开时回退
    std::unordered_map<AllocaInst *, int> pushCount;

    // ---- 处理当前块中的指令 ----
    for (auto * inst : bb->getInstructions()) {
        if (auto * phi = dynamic_cast<PhiInst *>(inst)) {
            // 块头的 phi 会为对应 alloca 产生新的到达定义
            auto it = phiToAlloca.find(phi);
            if (it != phiToAlloca.end()) {
                AllocaInst * alloca = it->second;
                reachingDefs[alloca].push_back(phi);
                pushCount[alloca]++;
            }

        } else if (auto * load = dynamic_cast<LoadInst *>(inst)) {
            // 用当前到达定义替换从可提升 alloca 读取的 load
            Value * ptr = load->getPointerOperand();
            if (auto * alloca = dynamic_cast<AllocaInst *>(ptr)) {
                auto it = reachingDefs.find(alloca);
                if (it != reachingDefs.end()) {
                    // 若尚无定义，则默认以常量 0 作为初值
                    Value * reaching =
                        it->second.empty() ? static_cast<Value *>(mod->newConstInt(0)) : it->second.back();
                    load->replaceAllUseWith(reaching);
                    load->setDead(true);
                }
            }

        } else if (auto * store = dynamic_cast<StoreInst *>(inst)) {
            // 将 store 的值记录为该 alloca 的最新到达定义
            Value * ptr = store->getPointerOperand();
            if (auto * alloca = dynamic_cast<AllocaInst *>(ptr)) {
                auto it = reachingDefs.find(alloca);
                if (it != reachingDefs.end()) {
                    Value * val = store->getValueOperand();
                    it->second.push_back(val);
                    pushCount[alloca]++;
                    store->setDead(true);
                }
            }
        }
    }

    // ---- 为直接后继块中的 phi 补充 incoming 值 ----
    for (auto * succ : bb->getSuccessors()) {
        for (auto * inst : succ->getInstructions()) {
            auto * phi = dynamic_cast<PhiInst *>(inst);
            if (!phi) {
                break; // phi 必然位于块头，遇到非 phi 即可停止
            }

            auto it = phiToAlloca.find(phi);
            if (it == phiToAlloca.end()) {
                continue; // 不是本轮 mem2reg 插入的 phi，跳过
            }

            AllocaInst * alloca = it->second;
            auto defIt = reachingDefs.find(alloca);
            Value * reaching = (defIt == reachingDefs.end() || defIt->second.empty())
                                    ? static_cast<Value *>(mod->newConstInt(0))
                                    : defIt->second.back();
            phi->addIncoming(reaching, bb);
        }
    }

    // ---- 递归处理支配树子节点 ----
    for (auto * child : dt.getDomChildren(bb)) {
        rename(child, dt, reachingDefs, allocaPhis, phiToAlloca, visited);
    }

    // ---- 回退当前块压入的到达定义 ----
    for (auto & [alloca, count] : pushCount) {
        auto & stack = reachingDefs[alloca];
        for (int i = 0; i < count; ++i) {
            if (!stack.empty()) {
                stack.pop_back();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 第 4 步：清理死指令与已提升的 alloca
// ---------------------------------------------------------------------------

/// @brief 删除被 mem2reg 消除的 load/store 以及已提升的 alloca
/// @param allocas 已提升的 alloca 列表
void Mem2Reg::cleanup(const std::vector<AllocaInst *> & allocas)
{
    std::unordered_set<AllocaInst *> allocaSet(allocas.begin(), allocas.end());

    // 第 1 轮：删除所有基本块中的死 load/store。
    // 必须先做这一步，确保 alloca 的 use 链在删除 alloca 之前已经清空。
    for (auto * bb : func->getBlocks()) {
        auto & insts = bb->getInstructions();
        auto it = insts.begin();
        while (it != insts.end()) {
            Instruction * inst = *it;
            if (inst->isDead()) {
                inst->clearOperands(); // 将该指令从所有操作数的 use 链中摘除
                auto next = std::next(it);
                insts.erase(it);
                delete inst;
                it = next;
            } else {
                ++it;
            }
        }
    }

    // 第 2 轮：从入口块中删除已提升的 alloca。
    // 其 use 链此时应已为空，因为所有使用者已经在上一步被删除。
    BasicBlock * entry = func->getEntryBlock();
    if (!entry) {
        return;
    }

    auto & entryInsts = entry->getInstructions();
    auto it = entryInsts.begin();
    while (it != entryInsts.end()) {
        if (auto * alloca = dynamic_cast<AllocaInst *>(*it)) {
            if (allocaSet.count(alloca)) {
                if (!alloca->getUseList().empty()) {
                    ++it;
                    continue;
                }

                alloca->clearOperands();
                auto next = std::next(it);
                entryInsts.erase(it);
                delete alloca;
                it = next;
                continue;
            }
        }
        ++it;
    }
}
