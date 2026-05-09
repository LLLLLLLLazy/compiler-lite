///
/// @file LiveIntervalSplitter.cpp
/// @brief 活跃区间分裂器的实现
///

#include "LiveIntervalSplitter.h"

#include <algorithm>
#include <cmath>

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
                                         const std::vector<int> & callInstNumbers)
{
	// 收集区间内的调用点作为候选分裂点
	std::vector<int> candidates;
	for (int callNum : callInstNumbers) {
		if (callNum > interval->getStart() && callNum < interval->getEnd()) {
			candidates.push_back(callNum);
		}
	}

	if (!candidates.empty()) {
		// 选择使左右子区间长度差异最小的调用点
		int bestPos = candidates[0];
		int bestDiff = std::abs((candidates[0] - interval->getStart()) -
		                        (interval->getEnd() - candidates[0]));
		for (int pos : candidates) {
			int diff = std::abs((pos - interval->getStart()) -
			                    (interval->getEnd() - pos));
			if (diff < bestDiff) {
				bestDiff = diff;
				bestPos = pos;
			}
		}
		return bestPos;
	}

	// 当前后端的分配结果仍是 Value* -> RegAllocInfo，不能表达同一值在普通
	// 指令中点两侧使用不同寄存器，也没有插入 split copy。没有调用点时
	// 中点分裂既不可表示，也会在超多形参场景中造成大量无收益的图重建。
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

	// 创建右子区间 [splitPos, end)
	auto * right = new LiveInterval(interval->getVReg());
	right->maxLoopDepth = interval->maxLoopDepth;

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
	for (int pos : interval->getUsePositions()) {
		if (pos < splitPos) {
			left->addUsePosition(pos);
		} else {
			right->addUsePosition(pos);
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
	int splitPos = chooseSplitPos(interval, callInstNumbers);

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
