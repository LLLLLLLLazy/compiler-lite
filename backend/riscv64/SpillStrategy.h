///
/// @file SpillStrategy.h
/// @brief 溢出决策策略抽象接口，用于Greedy寄存器分配
///
/// - SpillStrategy为纯虚接口类，定义溢出候选选择方法
/// - 默认实现为HeuristicSpillStrategy（启发式溢出权重策略）
/// - 接口设计支持后续通过继承实现MLGOSpillStrategy子类（MLGO升级预留）
///

#pragma once

#include <vector>

class LiveInterval;

/// @brief 溢出决策策略抽象接口
///
/// Greedy寄存器分配器在物理寄存器不足时，通过此接口选择应被溢出的活跃区间。
/// 默认实现为HeuristicSpillStrategy（选择溢出权重最低的区间）。
/// 后续可通过实现MLGOSpillStrategy子类，用强化学习模型替代启发式策略。
///
class SpillStrategy {

public:
	/// @brief 虚析构函数
	virtual ~SpillStrategy() = default;

	/// @brief 从候选活跃区间中选择溢出候选
	/// @param candidates 候选溢出区间列表（非空）
	/// @return 被选中的溢出候选区间指针，不应返回nullptr
	virtual LiveInterval * selectSpillCandidate(std::vector<LiveInterval *> & candidates) = 0;
};
