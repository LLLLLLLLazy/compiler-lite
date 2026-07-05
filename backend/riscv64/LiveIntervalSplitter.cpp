///
/// @file LiveIntervalSplitter.cpp
/// @brief 活跃区间分裂器的实现
///

#include "LiveIntervalSplitter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "InterferenceGraph.h"
#include "LiveInterval.h"
#include "Value.h"

/// @brief 构造函数
LiveIntervalSplitter::LiveIntervalSplitter(bool enabled, int maxTotalIntervals)
	: enabled_(enabled), maxTotalIntervals_(maxTotalIntervals)
{
}

/// @brief 判断区间是否已被分裂
bool LiveIntervalSplitter::isSplitDescendant(LiveInterval * interval) const
{
	return splitDescendants_.find(interval) != splitDescendants_.end();
}

/// @brief 选择最佳分裂点
int LiveIntervalSplitter::chooseSplitPos(LiveInterval * interval,
                                         const std::vector<int> & callInstNumbers,
                                         const std::vector<int> & extraSplitCandidates)
{
	if (interval == nullptr) {
		return -1;
	}

	auto hasUsesOnBothSides = [&](int pos) {
		bool left = false;
		bool right = false;
		for (int usePos : interval->getUsePositions()) {
			if (usePos < pos) {
				left = true;
			} else if (usePos >= pos) {
				right = true;
			}
			if (left && right) {
				return true;
			}
		}
		return left && right;
	};

	auto chooseBalanced = [&](const std::vector<int> & source) {
		int bestPos = -1;
		int bestDiff = 0;
		for (int pos : source) {
			if (pos <= interval->getStart() || pos >= interval->getEnd() || !hasUsesOnBothSides(pos)) {
				continue;
			}
			int diff = std::abs((pos - interval->getStart()) - (interval->getEnd() - pos));
			if (bestPos < 0 || diff < bestDiff) {
				bestPos = pos;
				bestDiff = diff;
			}
		}
		return bestPos;
	};

	// 调用点优先：跨call压力通常最需要拆分。
	int best = chooseBalanced(callInstNumbers);
	if (best >= 0) {
		return best;
	}

	// 其次考虑循环/基本块边界候选。
	best = chooseBalanced(extraSplitCandidates);
	if (best >= 0) {
		return best;
	}

	// 最后在最大use gap附近切一次，避免长无call热区间整体落栈。
	const auto & uses = interval->getUsePositions();
	if (uses.size() >= 4) {
		int bestGap = 0;
		int gapPos = -1;
		for (std::size_t i = 1; i < uses.size(); ++i) {
			int gap = uses[i] - uses[i - 1];
			int pos = uses[i];
			if (gap > bestGap && pos > interval->getStart() && pos < interval->getEnd()) {
				bestGap = gap;
				gapPos = pos;
			}
		}
		if (gapPos > 0 && hasUsesOnBothSides(gapPos)) {
			return gapPos;
		}
	}

	return -1;
}

/// @brief 执行分裂
SplitInfo LiveIntervalSplitter::doSplit(LiveInterval * interval, int splitPos,
                                        std::vector<LiveInterval *> & intervals,
                                        std::unordered_map<LiveInterval *, int> & intervalToIndex)
{
	// 创建左子区间 [start, splitPos)
	auto * left = new LiveInterval(interval->getVReg());
	left->maxLoopDepth = interval->maxLoopDepth;
	left->defLoopDepth = interval->defLoopDepth;

	// 创建右子区间 [splitPos, end)
	auto * right = new LiveInterval(interval->getVReg());
	right->maxLoopDepth = interval->maxLoopDepth;
	right->defLoopDepth = interval->defLoopDepth;

	// 分配 Segment
	for (const auto & seg : interval->getSegments()) {
		if (seg.end <= splitPos) {
			// 整段在左半
			left->addSegment(seg.start, seg.end);
		} else if (seg.start >= splitPos) {
			// 整段在右半
			right->addSegment(seg.start, seg.end);
		} else {
			// 跨越分裂点，拆分为两段
			left->addSegment(seg.start, splitPos);
			right->addSegment(splitPos, seg.end);
		}
	}

	// 分配 usePositions
	const auto & usePositions = interval->getUsePositions();
	const auto & useLoopDepths = interval->getUseLoopDepths();
	for (std::size_t i = 0; i < usePositions.size(); ++i) {
		int pos = usePositions[i];
		int depth = i < useLoopDepths.size() ? useLoopDepths[i] : interval->maxLoopDepth;
		if (pos < splitPos) {
			left->addUsePosition(pos, depth);
		} else {
			right->addUsePosition(pos, depth);
		}
	}

	// 重新计算溢出权重
	left->calcSpillWeight(left->maxLoopDepth);
	right->calcSpillWeight(right->maxLoopDepth);

	// 将原区间标记为无效
	interval->vreg = nullptr;

	// 添加子区间到列表
	intervals.push_back(left);
	intervals.push_back(right);

	// 更新 intervalToIndex
	int leftIdx = static_cast<int>(intervals.size()) - 2;
	int rightIdx = static_cast<int>(intervals.size()) - 1;
	intervalToIndex[left] = leftIdx;
	intervalToIndex[right] = rightIdx;

	return SplitInfo{left, right, splitPos, interval};
}

/// @brief 分裂后更新干涉图
void LiveIntervalSplitter::updateInterferenceGraph(
	const SplitInfo & split,
	std::vector<LiveInterval *> & intervals,
	InterferenceGraph *& graph,
	const std::unordered_map<LiveInterval *, int> & intervalToIndex)
{
	// 重建干涉图
	auto * newGraph = new InterferenceGraph(static_cast<int>(intervals.size()));

	for (int i = 0; i < static_cast<int>(intervals.size()); ++i) {
		if (intervals[i] == nullptr || intervals[i]->getVReg() == nullptr) {
			continue;
		}
		for (int j = i + 1; j < static_cast<int>(intervals.size()); ++j) {
			if (intervals[j] == nullptr || intervals[j]->getVReg() == nullptr) {
				continue;
			}
			if (intervals[i]->overlaps(*intervals[j])) {
				newGraph->addEdge(i, j);
			}
		}
	}
	newGraph->finalizeEdges();

	delete graph;
	graph = newGraph;
}

/// @brief 尝试分裂活跃区间
std::optional<SplitInfo> LiveIntervalSplitter::trySplit(
	LiveInterval * interval,
	std::vector<LiveInterval *> & intervals,
	InterferenceGraph *& graph,
	const std::vector<int> & callInstNumbers,
	const std::vector<int> & extraSplitCandidates,
	std::unordered_map<LiveInterval *, int> & intervalToIndex)
{
	// 前置检查
	if (!enabled_) {
		return std::nullopt;
	}
	if (interval == nullptr || interval->getVReg() == nullptr) {
		return std::nullopt;
	}
	// 区间长度必须 > 1 才能分裂
	if (interval->getEnd() - interval->getStart() <= 1) {
		return std::nullopt;
	}
	// 区间总数限制
	if (maxTotalIntervals_ > 0 &&
	    static_cast<int>(intervals.size()) >= maxTotalIntervals_) {
		return std::nullopt;
	}
	// 已分裂的子区间不可再分裂
	if (isSplitDescendant(interval)) {
		return std::nullopt;
	}

	// 选择分裂点
	int splitPos = chooseSplitPos(interval, callInstNumbers, extraSplitCandidates);

	// 确保分裂点在区间内部
	if (splitPos <= interval->getStart() || splitPos >= interval->getEnd()) {
		return std::nullopt;
	}

	// 执行分裂
	SplitInfo splitInfo = doSplit(interval, splitPos, intervals, intervalToIndex);

	// 更新干涉图
	updateInterferenceGraph(splitInfo, intervals, graph, intervalToIndex);

	// 记录分裂信息
	splitRecords_.push_back(splitInfo);
	splitDescendants_.insert(splitInfo.left);
	splitDescendants_.insert(splitInfo.right);

	return splitInfo;
}
