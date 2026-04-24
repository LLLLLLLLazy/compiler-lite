///
/// @file HeuristicSpillStrategy.cpp
/// @brief 启发式溢出决策策略实现
///

#include "HeuristicSpillStrategy.h"
#include "LiveInterval.h"

#include <algorithm>

LiveInterval *
HeuristicSpillStrategy::selectSpillCandidate(std::vector<LiveInterval *> & candidates)
{
	// 使用std::min_element找到溢出权重最低的活跃区间
	auto it = std::min_element(candidates.begin(), candidates.end(),
		[](LiveInterval * a, LiveInterval * b) {
			return a->getSpillWeight() < b->getSpillWeight();
		});
	return *it;
}
