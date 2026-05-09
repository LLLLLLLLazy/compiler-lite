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
	LiveInterval * left;      ///< 分裂后的左子区间 [start, splitPos)
	LiveInterval * right;     ///< 分裂后的右子区间 [splitPos, end)
	int splitPos;             ///< 分裂点指令编号
	LiveInterval * original;  ///< 原始区间（已被替换）
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
	                   const std::vector<int> & callInstNumbers);

	/// @brief 执行分裂：将 interval 拆分为 [start, splitPos) 和 [splitPos, end)
	SplitInfo doSplit(LiveInterval * interval, int splitPos,
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
