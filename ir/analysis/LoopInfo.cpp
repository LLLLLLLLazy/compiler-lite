///
/// @file LoopInfo.cpp
/// @brief 自然循环信息分析实现
///
/// 基于支配树识别CFG中的自然循环，并为每个基本块计算循环嵌套深度。
///
/// 回边判定：边 A → B 若 B dominates A，则 A → B 是回边
/// 自然循环体：header(=B) ∪ {从回边源A出发反向CFG可到达且不经过header的节点}
///

#include "LoopInfo.h"

#include <stack>

#include "BasicBlock.h"
#include "DominatorTree.h"
#include "Function.h"

LoopInfo::LoopInfo(Function * func, DominatorTree * domTree)
{
    if (func == nullptr || domTree == nullptr || func->getBlocks().empty()) {
        return;
    }
    computeLoops(func, domTree);
    computeLoopDepths(func);
}

void LoopInfo::computeLoops(Function * func, DominatorTree * domTree)
{
    for (auto * bb : func->getBlocks()) {
        for (auto * succ : bb->getSuccessors()) {
            // 回边判定：A → B 且 B dominates A
            if (domTree->dominates(succ, bb)) {
                discoverLoop(succ /*header*/, bb /*backEdgeSrc*/);
            }
        }
    }
}

void LoopInfo::discoverLoop(BasicBlock * header, BasicBlock * backEdgeSrc)
{
    // 同一个header可能已由另一条回边发现（如包含多重回边的循环），去重
    if (loopHeaders.find(header) != loopHeaders.end()) {
        return;
    }
    loopHeaders.insert(header);

    Loop loop;
    loop.header = header;
    loop.body.insert(header);

    // 从回边源出发，反向遍历CFG，收集所有可到达header的节点（不经过header）
    std::stack<BasicBlock *> worklist;
    worklist.push(backEdgeSrc);

    while (!worklist.empty()) {
        BasicBlock * node = worklist.top();
        worklist.pop();

        // 如果已在body中则跳过
        if (loop.body.find(node) != loop.body.end()) {
            continue;
        }
        loop.body.insert(node);

        for (auto * pred : node->getPredecessors()) {
            if (loop.body.find(pred) == loop.body.end()) {
                worklist.push(pred);
            }
        }
    }

    loops.push_back(std::move(loop));
}

void LoopInfo::computeLoopDepths(Function * func)
{
    // 对每个基本块，计算包含它的循环个数 = 循环深度
    for (auto * bb : func->getBlocks()) {
        int depth = 0;
        for (const auto & loop : loops) {
            if (loop.body.find(bb) != loop.body.end()) {
                ++depth;
            }
        }
        bbDepth[bb] = depth;
    }
}

int LoopInfo::getLoopDepth(BasicBlock * bb) const
{
    auto it = bbDepth.find(bb);
    if (it == bbDepth.end()) {
        return 0;
    }
    return it->second;
}

void LoopInfo::print(std::string & str) const
{
    str += "=== Loop Info ===\n";

    if (loops.empty()) {
        str += "  (no loops)\n";
    }

    for (std::size_t i = 0; i < loops.size(); ++i) {
        auto & loop = loops[i];
        str += "Loop " + std::to_string(i) + ": header=" + loop.header->getIRName() + "  body={";
        bool first = true;
        for (auto * bb : loop.body) {
            if (!first) str += ", ";
            str += bb->getIRName();
            first = false;
        }
        str += "}\n";
    }

    str += "Loop depths:\n";
    for (auto & [bb, depth] : bbDepth) {
        str += "  " + bb->getIRName() + ": depth=" + std::to_string(depth) + "\n";
    }

    str += "=== End Loop Info ===\n";
}
