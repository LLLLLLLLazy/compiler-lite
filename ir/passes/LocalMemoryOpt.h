///
/// @file LocalMemoryOpt.h
/// @brief 局部内存访问优化 pass
///
/// 仅对可规范化为非逃逸局部对象槽位的 load/store 做保守优化：
///   1. 同址 store-to-load forwarding
///   2. 冗余 load 消除
///   3. 精确槽位的 dead store elimination
///

#pragma once

#include <unordered_set>

class AllocaInst;
class Function;

class LocalMemoryOpt {

public:
    /// @brief 构造局部内存优化器
    explicit LocalMemoryOpt(Function * func);

    /// @brief 对函数原地执行局部 load/store 优化
    /// @return 若本轮修改了 IR 则返回 true
    bool run();

private:
    /// @brief 收集可参与局部内存优化的非逃逸 alloca
    std::unordered_set<AllocaInst *> collectTrackableAllocas() const;

    /// @brief 清扫已标记为 dead 的 load/store
    bool sweepDeadInstructions() const;

    Function * func = nullptr;
};