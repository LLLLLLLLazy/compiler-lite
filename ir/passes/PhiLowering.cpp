///
/// @file PhiLowering.cpp
/// @brief phi 降级优化实现
///
///        把 SSA 里的 phi 指令，变回普通的赋值/拷贝指令
///
/// 参考：
///   Briggs, Cooper, Harvey, Simpson, "Practical Improvements to the
///   Construction and Destruction of Static Single Assignment Form",
///   Software—Practice & Experience, 1998.
///
///   Boissinot et al., "Revisiting Out-of-SSA Translation for Correctness,
///   Code Quality, and Efficiency", CGO 2009.
///

#include "PhiLowering.h"

#include <algorithm>
#include <list>
#include <unordered_map>
#include <unordered_set>

#include "BasicBlock.h"
#include "CopyInst.h"
#include "Function.h"
#include "Instruction.h"
#include "Module.h"
#include "PhiInst.h"
#include "Value.h"

// ---------------------------------------------------------------------------
// 构造函数
// ---------------------------------------------------------------------------

/// @brief 构造 phi 降级优化器
/// @param _func 待处理的函数
/// @param _mod 所属模块
PhiLowering::PhiLowering(Function * _func, Module * _mod) : func(_func), mod(_mod)
{}

// ---------------------------------------------------------------------------
// 对外入口
// ---------------------------------------------------------------------------

/// @brief 对函数执行 phi 降级
void PhiLowering::run()
{
    if (!func || func->isBuiltin() || func->getBlocks().empty()) {
        return;
    }

    // 收集待删除的 phi 节点。
    // 遍历基本块列表的快照即可，因为删除 phi 只会改动指令链表，不会改动块列表。
    std::vector<PhiInst *> toDelete;

    for (auto * bb : func->getBlocks()) {
        // 收集当前块顶部连续出现的 phi 节点。
        std::vector<PhiInst *> phis;
        for (auto * inst : bb->getInstructions()) {
            if (auto * phi = dynamic_cast<PhiInst *>(inst)) {
                phis.push_back(phi);
            } else {
                break; // phi 节点总是位于基本块顶部
            }
        }
        if (phis.empty()) {
            continue;
        }

        // 按前驱块构造并行复制集合：
        // predCopies[pred] = { (phi_result*, incoming_value*) ... }
        std::unordered_map<BasicBlock *, std::vector<std::pair<Value *, Value *>>> predCopies;
        for (auto * phi : phis) {
            for (int i = 0; i < phi->getIncomingCount(); ++i) {
                const auto & inc = phi->getIncoming(i);
                // 目标值是 phi 自身，源值是该前驱传入的 incoming 值
                predCopies[inc.block].emplace_back(static_cast<Value *>(phi), inc.value);
            }
        }

        // 在每个前驱块中插入串行化后的 copy 指令。
        for (auto & [pred, copies] : predCopies) {
            insertSequentialCopies(pred, copies);
        }

        // 从 BB 中移除 phi 指令本体，真正 delete 延后到所有读取结束之后，
        // 避免前面仍在访问 phi->getIncoming(...) 时出现悬空引用。
        auto & insts = bb->getInstructions();
        for (auto * phi : phis) {
            phi->clearOperands(); // 从 use-def 链中摘除
            insts.remove(phi);
            toDelete.push_back(phi);
        }
    }

    // 不在此处删除phi对象。生成的CopyInst节点使用phi对象本身作为逻辑目标Value，
    // 在out-of-SSA降级后，后续指令仍会引用该Value。
    (void) toDelete;
}

// ---------------------------------------------------------------------------
// 并行复制串行化（Briggs-Cooper 工作队列算法）
// ---------------------------------------------------------------------------
//
// 给定一组并行复制 { (dst_i, src_i) }，生成语义等价的串行 copy 序列，
// 并正确处理交换等循环依赖场景。
//
// 关键不变量：
//   当且仅当 dst 没有作为其他待处理复制的 src 出现时，复制 (dst, src)
//   才是“可立即发射”的；否则过早写入 dst 会覆盖其他复制尚未读取的旧值。
//
// 当工作队列中只剩循环依赖时，可通过以下方式打破一个环：
//   1. 从环中任选一条复制 (dst0, src0)。
//   2. 先插入一条临时复制：%tmp = copy dst0。
//   3. 将所有待处理复制中 src == dst0 的位置替换为 %tmp。
//   4. 此时 (dst0, src0) 变为可发射，加入就绪队列。
//
// ---------------------------------------------------------------------------

/// @brief 将并行复制集合串行化并插入到前驱块末尾
/// @param pred 前驱基本块
/// @param copies 并行复制集合，元素为目标值与源值的二元组
void PhiLowering::insertSequentialCopies(BasicBlock * pred,
                                          std::vector<std::pair<Value *, Value *>> & copies)
{
    // ---- 先移除恒等复制（dst == src） ----
    copies.erase(std::remove_if(copies.begin(),
                                copies.end(),
                                [](const std::pair<Value *, Value *> & p) {
                                    return p.first == p.second;
                                }),
                 copies.end());

    if (copies.empty()) {
        return;
    }

    // ---- 为每条复制建立状态跟踪 ----
    // pending[i] == true 表示第 i 条复制尚未发射
    std::vector<bool> pending(copies.size(), true);

    // 统计还有多少条待处理复制把某个目标值当作源值使用。
    // 当某个 dst 的计数为 0 时，说明以它为目标的复制可以立即发射。
    std::unordered_map<Value *, int> dstUsedAsSrc;
    for (auto & [d, s] : copies) {
        (void) d;
        dstUsedAsSrc[s]; // 确保映射项存在
    }
    for (auto & [d, s] : copies) {
        // 这里保留变量名仅为说明后续含义：我们关心的是某个 dst
        // 是否也被其他待处理复制当作 src 使用。
        (void) s;
        (void) d;
    }

    // 重新明确计算：dstAsSrcCount[v] 表示仍有多少条待处理复制以 v 为 src。
    std::unordered_map<Value *, int> dstAsSrcCount;
    for (auto & [d, s] : copies) {
        dstAsSrcCount[d] += 0; // 确保目标值已有计数槽位
        dstAsSrcCount[s] += 1; // 当前复制会消费一次源值 s
    }

    // ---- 找到插入位置：终结指令之前 ----
    auto & insts = pred->getInstructions();
    auto insertPos = insts.end();
    if (!insts.empty()) {
        auto lastIt = std::prev(insts.end());
        if ((*lastIt)->isTerminator()) {
            insertPos = lastIt; // 插在终结指令之前
        }
        // 若基本块没有终结指令，则退化为直接追加到末尾。
    }

    auto insertCopy = [&](CopyInst * ci) {
        insts.insert(insertPos, ci);
        ci->setParentBlock(pred);
    };

    // ---- 工作队列算法 ----
    // readyQueue 中保存当前可以直接发射的复制编号。
    std::vector<int> readyQueue;
    for (int i = 0; i < static_cast<int>(copies.size()); ++i) {
        if (dstAsSrcCount[copies[i].first] == 0) {
            readyQueue.push_back(i);
        }
    }

    int emitted = 0;

    while (emitted < static_cast<int>(copies.size())) {

        // ---- 持续发射所有当前已就绪的复制 ----
        while (!readyQueue.empty()) {
            int idx = readyQueue.back();
            readyQueue.pop_back();

            if (!pending[idx]) {
                continue; // 已处理过
            }

            auto [dst, src] = copies[idx];
            pending[idx] = false;
            ++emitted;

            // 发射正文复制：dst = copy src
            auto * ci = new CopyInst(func, src, dst);
            insertCopy(ci);

            // 发射后，凡是把 dst 当作旧值来源的复制都少了一个阻塞来源。
            auto it = dstAsSrcCount.find(dst);
            if (it != dstAsSrcCount.end()) {
                // 这里保留空分支仅为了强调：真正的解锁逻辑由下面对 src
                // 计数的递减与重新入队来完成。
            }

            // 更准确地说：发射当前复制后，源值 src 被“作为旧值读取”的需求少了一次。
            // 若 src 的需求计数因此降到 0，则以 src 为目标值的复制就可能转为就绪。
            dstAsSrcCount[src] -= 1;
            // 找到以 src 为目标值的复制，若其阻塞消失则加入就绪队列。
            for (int j = 0; j < static_cast<int>(copies.size()); ++j) {
                if (pending[j] && copies[j].first == src && dstAsSrcCount[src] == 0) {
                    readyQueue.push_back(j);
                }
            }
        }

        if (emitted == static_cast<int>(copies.size())) {
            break;
        }

        // ---- 若发生环，则借助临时值打破循环 ----
        // 找到第一条尚未发射的复制。
        int cycleIdx = -1;
        for (int i = 0; i < static_cast<int>(copies.size()); ++i) {
            if (pending[i]) {
                cycleIdx = i;
                break;
            }
        }
        if (cycleIdx == -1) {
            break; // 理论上不会发生，仅作保护
        }

        Value * cycleDst = copies[cycleIdx].first;

        // 先生成一条临时复制，保存 cycleDst 当前尚未被覆盖的旧值。
        auto * tmpCopy = new CopyInst(func, cycleDst); // 生成临时新值
        insertCopy(tmpCopy);

        // 将所有仍待发射且以 cycleDst 为源值的复制改为读取临时值 %tmp。
        for (int j = 0; j < static_cast<int>(copies.size()); ++j) {
            if (pending[j] && copies[j].second == cycleDst) {
                copies[j].second = static_cast<Value *>(tmpCopy);
                // 同步更新计数：cycleDst 少一个消费者，tmpCopy 多一个消费者。
                dstAsSrcCount[cycleDst] -= 1;
                dstAsSrcCount[static_cast<Value *>(tmpCopy)] += 1;
            }
        }

        // cycleDst 的阻塞计数可能已经降为 0，从而转为可立即发射。
        if (dstAsSrcCount[cycleDst] == 0) {
            readyQueue.push_back(cycleIdx);
        }
    }
}
