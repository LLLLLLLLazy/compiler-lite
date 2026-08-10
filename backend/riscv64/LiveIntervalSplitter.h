///
/// @file LiveIntervalSplitter.h
/// @brief 活跃区间分裂器，在 Greedy 主循环中尝试分裂无法分配的区间
///
/// 当某活跃区间无法分配空闲寄存器且无法驱逐时，在标记溢出之前
/// 尝试将该区间分裂为两个子区间，使子区间可分别分配不同寄存器
/// 或仅在压力区间溢出，从而减少全局溢出。
///

#pragma once

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class InterferenceGraph;
class LiveInterval;
class Value;

/// @brief 活跃区间分裂信息
struct SplitInfo {
	LiveInterval * left = nullptr;  ///< 循环外左侧区间 [start, splitPos)
	LiveInterval * right = nullptr; ///< 循环区域区间 [splitPos, regionEnd)
	LiveInterval * tail = nullptr;  ///< 循环外右侧区间 [regionEnd, end)，可为空
	int splitPos = -1;             ///< 循环区域起点指令编号
	int regionEnd = -1;            ///< 循环区域结束指令编号（开区间）
	LiveInterval * original = nullptr; ///< 原始区间（已被替换）
	bool forceLeftStack = false; ///< 循环区域分裂：循环外侧固定走 canonical 栈槽
};

/// @brief 活跃区间分裂器
class LiveIntervalSplitter {

public:
	/// @brief 构造函数
	/// @param enabled 是否启用分裂
	/// @param maxTotalIntervals 最大允许的区间总数（防止区间爆炸）
	explicit LiveIntervalSplitter(bool enabled = false,
	                              int maxTotalIntervals = 0);

	/// @brief 尝试分裂活跃区间
	/// @param interval 待分裂的活跃区间
	/// @param intervals 所有活跃区间列表（输出：添加子区间）
	/// @param graph 干涉图（输出：更新节点和边）
	/// @param callInstNumbers 调用点指令编号列表
	/// @param intervalToIndex 区间到索引的映射（输出：更新）
	/// @return 分裂信息；若未分裂则返回 std::nullopt
	std::optional<SplitInfo> trySplit(
		LiveInterval * interval,
		std::vector<LiveInterval *> & intervals,
		InterferenceGraph *& graph,
		const std::vector<int> & callInstNumbers,
		const std::vector<int> & extraSplitCandidates,
		const std::unordered_map<int, int> & loopRegionEnds,
		const std::unordered_map<int, int> & loopEntryTransferPositions,
		std::unordered_map<LiveInterval *, int> & intervalToIndex);

	/// @brief 获取所有分裂记录
	const std::vector<SplitInfo> & getSplitRecords() const
	{
		return splitRecords_;
	}

	/// @brief 判断区间是否已被分裂（不可再分裂）
	bool isSplitDescendant(LiveInterval * interval) const;

private:
	/// @brief 选择最佳分裂点
	/// 策略：选择区间内的调用点；无调用点则不分裂
	int chooseSplitPos(LiveInterval * interval,
	                   const std::vector<int> & callInstNumbers,
	                   const std::vector<int> & extraSplitCandidates,
	                   const std::unordered_map<int, int> & loopRegionEnds);

	/// @brief 执行分裂：将 interval 拆分为循环外左段、循环区域段、可选循环外右段
	SplitInfo doSplit(LiveInterval * interval, int splitPos, int regionEnd,
	                  int entryTransferPos,
	                  std::vector<LiveInterval *> & intervals,
	                  std::unordered_map<LiveInterval *, int> & intervalToIndex);

	/// @brief 分裂后更新干涉图
	void updateInterferenceGraph(
		const SplitInfo & split,
		std::vector<LiveInterval *> & intervals,
		InterferenceGraph *& graph,
		const std::unordered_map<LiveInterval *, int> & intervalToIndex);

	bool enabled_;
	int maxTotalIntervals_;
	std::vector<SplitInfo> splitRecords_;
	std::unordered_set<LiveInterval *> splitDescendants_; ///< 已分裂产生的子区间，不可再分裂
};
