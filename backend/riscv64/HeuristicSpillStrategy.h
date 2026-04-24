///
/// @file HeuristicSpillStrategy.h
/// @brief 启发式溢出决策策略，Greedy寄存器分配的默认溢出策略
///
/// - 继承SpillStrategy抽象接口
/// - selectSpillCandidate从候选列表中选择溢出权重最低的活跃区间
/// - 溢出权重越低表示溢出代价越小，越应优先溢出
///

#pragma once

#include "SpillStrategy.h"

/// @brief 启发式溢出决策策略
///
/// 从候选活跃区间中选择溢出权重（spillWeight）最低的区间作为溢出候选。
/// 溢出权重计算公式：spillWeight = (useCount / intervalLength) * pow(10, loopDepth)
/// 权重越低表示该区间使用密度越低或不在循环中，溢出代价越小。
///
class HeuristicSpillStrategy : public SpillStrategy {

public:
	/// @brief 从候选活跃区间中选择溢出权重最低的区间
	/// @param candidates 候选溢出区间列表（非空）
	/// @return 溢出权重最低的活跃区间指针
	LiveInterval * selectSpillCandidate(std::vector<LiveInterval *> & candidates) override;
};
