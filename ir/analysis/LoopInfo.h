///
/// @file LoopInfo.h
/// @brief 自然循环信息分析
///
/// 基于已有支配树识别CFG中的自然循环（natural loop），
/// 并为每个基本块计算循环嵌套深度（loopDepth），
/// 供寄存器分配器使用以区分循环内外变量的溢出权重。
///
/// 算法：
/// 1. 遍历CFG边，若 A → B 且 B dominates A，则此边为回边（back edge）
/// 2. 对每条回边 n→h，构造自然循环：header=h, body={h}∪{可达n但不经过h的节点}
/// 3. 对每个基本块，循环深度 = 包含该块的自然循环个数
///

#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class BasicBlock;
class DominatorTree;
class Function;

class LoopInfo {

public:
    /// 构造时立即对给定函数完成循环分析
    /// @param func 待分析的函数
    /// @param domTree 已构建的支配树
    LoopInfo(Function * func, DominatorTree * domTree);

    /// 获取基本块的循环嵌套深度
    /// @param bb 基本块
    /// @return 循环深度（0 = 不在任何循环内）
    int getLoopDepth(BasicBlock * bb) const;

    /// 判断基本块是否为某个循环的header
    bool isLoopHeader(BasicBlock * bb) const
    {
        return loopHeaders.find(bb) != loopHeaders.end();
    }

    /// 获取循环header对应的body基本块集合
    const std::unordered_set<BasicBlock *> * getLoopBody(BasicBlock * header) const
    {
        for (const auto & loop : loops) {
            if (loop.header == header) {
                return &loop.body;
            }
        }
        return nullptr;
    }

    /// 将循环分析信息打印到 str（调试用）
    void print(std::string & str) const;

private:
    /// 识别所有自然循环
    void computeLoops(Function * func, DominatorTree * domTree);

    /// 对一条回边构造自然循环
    /// @param header 循环头（回边目标）
    /// @param backEdgeSrc 回边源
    void discoverLoop(BasicBlock * header, BasicBlock * backEdgeSrc);

    /// 计算所有基本块的循环深度
    void computeLoopDepths(Function * func);

    /// 每个自然循环的信息
    struct Loop {
        BasicBlock * header;
        std::unordered_set<BasicBlock *> body;
    };

    std::vector<Loop> loops;
    std::unordered_map<BasicBlock *, int> bbDepth;
    std::unordered_set<BasicBlock *> loopHeaders; ///< 已发现的循环头集合（去重用）
};
