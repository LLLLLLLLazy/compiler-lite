///
/// @file DominanceFrontier.cpp
/// @brief 支配边界（Dominance Frontier）实现
///

#include "DominanceFrontier.h"

#include "BasicBlock.h"
#include "DominatorTree.h"
#include "Function.h"

const std::set<BasicBlock *> DominanceFrontier::emptySet;

DominanceFrontier::DominanceFrontier(Function * func, const DominatorTree & dt)
{
    if (func == nullptr || func->getBlocks().empty()) {
        return;
    }
    compute(func, dt);
}

void DominanceFrontier::compute(Function * func, const DominatorTree & dt)
{
    // Initialize empty frontier sets for all blocks
    for (auto * bb : func->getBlocks()) {
        frontierMap[bb];
    }

    // Cytron et al. algorithm:
    // For each join point (block with multiple predecessors), walk each
    // predecessor up the idom tree until we reach the idom of the join point,
    // adding the join point to the frontier of each runner.
    for (auto * bb : func->getBlocks()) {
        const auto & preds = bb->getPredecessors();
        if (preds.size() < 2) {
            continue; // Not a join point
        }

        BasicBlock * idomBB = dt.getIDom(bb);

        for (BasicBlock * pred : preds) {
            BasicBlock * runner = pred;
            // Walk up until runner reaches idom(bb)
            while (runner != idomBB && runner != nullptr) {
                frontierMap[runner].insert(bb);
                BasicBlock * runnerIdom = dt.getIDom(runner);
                // Safety: if idom equals self (entry block), stop
                if (runnerIdom == runner) {
                    break;
                }
                runner = runnerIdom;
            }
        }
    }
}

const std::set<BasicBlock *> & DominanceFrontier::getFrontier(BasicBlock * bb) const
{
    auto it = frontierMap.find(bb);
    if (it == frontierMap.end()) {
        return emptySet;
    }
    return it->second;
}

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
