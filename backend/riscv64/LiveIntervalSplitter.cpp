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
#include "Rematerialization.h"
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
                                         const std::vector<int> & extraSplitCandidates,
                                         const std::unordered_map<int, int> & loopRegionEnds)
{
	if (interval == nullptr) {
		return -1;
	}

	auto hasUseOnLeft = [&](int pos) {
		for (int usePos : interval->getUsePositions()) {
			if (usePos < pos) {
				return true;
			}
		}
		return false;
	};

	auto hasUseOnRight = [&](int pos) {
		for (int usePos : interval->getUsePositions()) {
			if (usePos >= pos) {
				return true;
			}
		}
		return false;
	};

	auto hasUsesOnBothSides = [&](int pos) {
		return hasUseOnLeft(pos) && hasUseOnRight(pos);
	};

	auto canSplitForLoopBoundary = [&](int pos) {
		auto loopEndIt = loopRegionEnds.find(pos);
		if (loopEndIt == loopRegionEnds.end()) {
			return false;
		}

		// 最小可行的循环区域分裂：只处理单定义且定义在循环前的值。
		// 循环内段拿寄存器，循环外段统一走 canonical 栈槽；由于循环内无 def，
		// 退出循环时不需要写回，栈槽中的循环前值仍然正确。
		const auto & defs = interval->getDefPositions();
		if (defs.size() != 1 || defs.front() >= pos) {
			return false;
		}

		int usesInRegion = 0;
		for (int usePos : interval->getUsePositions()) {
			if (pos <= usePos && usePos < loopEndIt->second) {
				++usesInRegion;
			}
		}
		if (usesInRegion == 0) {
			return false;
		}

		// 对一条指令即可重算的值，整体溢出时 remat-only 往往更便宜；只有
		// 循环区域内存在多次使用时才值得为其建立入口 reload 的寄存器段。
		if (usesInRegion < 2 && RiscV64Rematerialization::isCheapRematerializable(interval->getVReg())) {
			return false;
		}
		return interval->getStart() < pos && loopEndIt->second > pos;
	};

	auto chooseBalanced = [&](const std::vector<int> & source, bool loopBoundaryMode = false) {
		int bestPos = -1;
		int bestUseDepth = -1;
		int bestDiff = 0;
		for (int pos : source) {
			if (pos <= interval->getStart() || pos >= interval->getEnd()) {
				continue;
			}
			if (loopBoundaryMode ? !canSplitForLoopBoundary(pos) : !hasUsesOnBothSides(pos)) {
				continue;
			}
			int useDepth = 0;
			const auto & uses = interval->getUsePositions();
			const auto & depths = interval->getUseLoopDepths();
			for (std::size_t i = 0; i < uses.size(); ++i) {
				if (uses[i] < pos) {
					continue;
				}
				useDepth = i < depths.size() ? depths[i] : interval->maxLoopDepth;
				break;
			}
			int diff = std::abs((pos - interval->getStart()) - (interval->getEnd() - pos));
			if (bestPos < 0 || useDepth > bestUseDepth || (useDepth == bestUseDepth && diff < bestDiff)) {
				bestPos = pos;
				bestUseDepth = useDepth;
				bestDiff = diff;
			}
		}
		return bestPos;
	};

	// P1-1 只落地循环区域分裂。旧的 call/gap 线性分裂缺少 CFG edge copy，
	// 仍容易在复杂控制流中读到错误位置，因此先不让它们产生活跃分段。
	int best = chooseBalanced(extraSplitCandidates, true);
	if (best >= 0) {
		return best;
	}

	return -1;
}

/// @brief 执行分裂
SplitInfo LiveIntervalSplitter::doSplit(LiveInterval * interval, int splitPos, int regionEnd,
                                        int entryTransferPos,
                                        std::vector<LiveInterval *> & intervals,
                                        std::unordered_map<LiveInterval *, int> & intervalToIndex)
{
	// 创建循环外左侧子区间 [start, splitPos)
	auto * left = new LiveInterval(interval->getVReg());
	left->maxLoopDepth = interval->maxLoopDepth;
	left->defLoopDepth = interval->defLoopDepth;

	// 创建循环区域子区间 [splitPos, regionEnd)
	auto * right = new LiveInterval(interval->getVReg());
	right->maxLoopDepth = interval->maxLoopDepth;
	right->defLoopDepth = interval->defLoopDepth;

	// 创建循环外右侧子区间 [regionEnd, end)，按需保留
	auto * tail = new LiveInterval(interval->getVReg());
	tail->maxLoopDepth = interval->maxLoopDepth;
	tail->defLoopDepth = interval->defLoopDepth;

	for (int defPos : interval->getDefPositions()) {
		if (defPos < splitPos) {
			left->addDefPosition(defPos);
		} else if (defPos < regionEnd) {
			right->addDefPosition(defPos);
		} else {
			tail->addDefPosition(defPos);
		}
	}

	// 分配 Segment
	// 注意：right 段覆盖到区间末尾而非 regionEnd。regionEnd 截断会把循环
	// right 段从 entryTransferPos（preheader 终结指令号）起：循环入口的
	// stack->reg reload 是 IR 外指令，插在 header 首指令之前，RA 必须为该
	// 位置预留寄存器，否则 reload 会覆盖其他活跃值。right 到 regionEnd 截断，
	// 循环后（>= regionEnd）的 use 走 tail 栈槽（长距离/跨 call 安全）。
	for (const auto & seg : interval->getSegments()) {
		left->addSegment(seg.start, std::min(seg.end, splitPos));
		right->addSegment(std::max(seg.start, entryTransferPos), std::min(seg.end, regionEnd));
		tail->addSegment(std::max(seg.start, regionEnd), seg.end);
	}

	// 分配 usePositions
	const auto & usePositions = interval->getUsePositions();
	const auto & useLoopDepths = interval->getUseLoopDepths();
	for (std::size_t i = 0; i < usePositions.size(); ++i) {
		int pos = usePositions[i];
		int depth = i < useLoopDepths.size() ? useLoopDepths[i] : interval->maxLoopDepth;
		if (pos < splitPos) {
			left->addUsePosition(pos, depth);
		} else if (pos < regionEnd) {
			right->addUsePosition(pos, depth);
		} else {
			tail->addUsePosition(pos, depth);
		}
	}

	// 重新计算溢出权重
	left->calcSpillWeight(left->maxLoopDepth);
	right->calcSpillWeight(right->maxLoopDepth);
	tail->calcSpillWeight(tail->maxLoopDepth);

	// 将原区间标记为无效
	interval->vreg = nullptr;

	// 添加子区间到列表。右侧尾区间只有实际覆盖时才发布，避免空段参与统计和干涉。
	intervals.push_back(left);
	intervalToIndex[left] = static_cast<int>(intervals.size()) - 1;
	intervals.push_back(right);
	intervalToIndex[right] = static_cast<int>(intervals.size()) - 1;
	if (!tail->getSegments().empty()) {
		intervals.push_back(tail);
		intervalToIndex[tail] = static_cast<int>(intervals.size()) - 1;
	} else {
		delete tail;
		tail = nullptr;
	}

	SplitInfo info;
	info.left = left;
	info.right = right;
	info.tail = tail;
	info.splitPos = splitPos;
	info.regionEnd = regionEnd;
	info.original = interval;
	return info;
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
	const std::unordered_map<int, int> & loopRegionEnds,
	const std::unordered_map<int, int> & loopEntryTransferPositions,
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
	int splitPos = chooseSplitPos(interval, callInstNumbers, extraSplitCandidates, loopRegionEnds);

	// 确保分裂点在区间内部
	if (splitPos <= interval->getStart() || splitPos >= interval->getEnd()) {
		return std::nullopt;
	}

		// 循环区域分裂应截出 [header, loopEnd) 这一段；若循环范围异常则放弃。
	auto regionEndIt = loopRegionEnds.find(splitPos);
	if (regionEndIt == loopRegionEnds.end() || regionEndIt->second <= splitPos ||
	    regionEndIt->second > interval->getEnd()) {
		return std::nullopt;
	}
	int regionEnd = regionEndIt->second;

	// 执行分裂
	int entryTransferPos = -1;
	{
		auto etIt = loopEntryTransferPositions.find(splitPos);
		if (etIt != loopEntryTransferPositions.end()) {
			entryTransferPos = etIt->second;
		}
	}
	SplitInfo splitInfo = doSplit(interval, splitPos, regionEnd, entryTransferPos,
	                              intervals, intervalToIndex);
	splitInfo.forceLeftStack = std::find(extraSplitCandidates.begin(), extraSplitCandidates.end(), splitPos) !=
	                           extraSplitCandidates.end();
	// 更新干涉图
	updateInterferenceGraph(splitInfo, intervals, graph, intervalToIndex);

	// 记录分裂信息
	splitRecords_.push_back(splitInfo);
	splitDescendants_.insert(splitInfo.left);
	splitDescendants_.insert(splitInfo.right);

	return splitInfo;
}
