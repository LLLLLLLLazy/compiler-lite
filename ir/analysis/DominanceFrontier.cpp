///
/// @file DominanceFrontier.cpp
/// @brief 支配边界（Dominance Frontier）实现
///

#include "DominanceFrontier.h"

#include "BasicBlock.h"
#include "DominatorTree.h"
#include "Function.h"

const std::set<BasicBlock *> DominanceFrontier::emptySet;

/// @brief 构造并计算指定函数的支配边界
/// @param func 待分析的函数
/// @param dt 已构建好的支配树
DominanceFrontier::DominanceFrontier(Function * func, const DominatorTree & dt)
{
    if (func == nullptr || func->getBlocks().empty()) {
        return;
    }
    compute(func, dt);
}

/// @brief 计算函数中每个基本块的支配边界集合
/// @param func 待分析的函数
/// @param dt 支配树信息
void DominanceFrontier::compute(Function * func, const DominatorTree & dt)
{
    // 为所有基本块初始化空的支配边界集合
    for (auto * bb : func->getBlocks()) {
        frontierMap[bb];
    }

    // Cytron 等人的算法：
    // 对于每个汇合点（拥有多个前驱的基本块），从各前驱沿 idom 链向上走，
    // 直到到达该汇合点的直接支配者，并把汇合点加入沿途节点的支配边界中。
    for (auto * bb : func->getBlocks()) {
        const auto & preds = bb->getPredecessors();
        if (preds.size() < 2) {
            continue; // 不是控制流汇合点
        }

        BasicBlock * idomBB = dt.getIDom(bb);

        for (BasicBlock * pred : preds) {
            BasicBlock * runner = pred;
            // 沿 idom 链向上走，直到 runner 到达 idom(bb)
            while (runner != idomBB && runner != nullptr) {
                frontierMap[runner].insert(bb);
                BasicBlock * runnerIdom = dt.getIDom(runner);
                // 保护性处理：若 idom 指回自身（通常是入口块），则停止
                if (runnerIdom == runner) {
                    break;
                }
                runner = runnerIdom;
            }
        }
    }
}

/// @brief 获取指定基本块的支配边界集合
/// @param bb 目标基本块
/// @return 支配边界集合引用
const std::set<BasicBlock *> & DominanceFrontier::getFrontier(BasicBlock * bb) const
{
    auto it = frontierMap.find(bb);
    if (it == frontierMap.end()) {
        return emptySet;
    }
    return it->second;
}

/// @brief 输出支配边界调试信息
/// @param str 输出字符串
void DominanceFrontier::print(std::string & str) const
{
    str += "=== Dominance Frontier ===\n";

    if (frontierMap.empty()) {
        str += "  (empty)\n";
        str += "=== End Dominance Frontier ===\n";
        return;
    }

    for (const auto & [bb, frontier] : frontierMap) {
        str += "  DF(" + bb->getIRName() + ") = {";
        bool first = true;
        for (auto * f : frontier) {
            if (!first) {
                str += ", ";
            }
            str += f->getIRName();
            first = false;
        }
        str += "}\n";
    }

    str += "=== End Dominance Frontier ===\n";
}
