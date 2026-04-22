///
/// @file InterferenceGraph.h
/// @brief 干涉图数据结构，用于Greedy寄存器分配算法
///
#pragma once

#include <set>
#include <vector>

#include "LiveInterval.h"

/// @brief 干涉图，节点为活跃区间，边表示两个区间在同一程序点同时活跃
/// 采用邻接表+位向量混合表示：
/// - 邻接表：用于遍历某节点的所有邻居
/// - 位向量：用于O(1)查询两个节点是否干涉
class InterferenceGraph {

public:
	/// @brief 构造函数
	/// @param numIntervals 节点数量（活跃区间数量）
	explicit InterferenceGraph(int numIntervals);

	/// @brief 在区间i和j之间添加干涉边
	/// @param i 区间编号
	/// @param j 区间编号
	void addEdge(int i, int j);

	/// @brief 判断区间i和j是否干涉（O(1)查询，位向量实现）
	/// @param i 区间编号
	/// @param j 区间编号
	/// @return true表示存在干涉，false表示无干涉
	bool hasInterference(int i, int j);

	/// @brief 获取与区间i干涉的所有区间编号
	/// @param i 区间编号
	/// @return 干涉邻居区间编号列表
	std::vector<int> getNeighbors(int i);

	/// @brief 获取与某区间干涉的所有已分配物理寄存器集合
	/// @param interval 活跃区间
	/// @param intervals 所有活跃区间列表
	/// @return 已分配的物理寄存器编号集合
	std::set<int> getInterferingRegs(LiveInterval & interval, std::vector<LiveInterval *> & intervals);

private:
	/// @brief 节点数量
	int numNodes;

	/// @brief 邻接表，存储每个节点的邻居列表
	std::vector<std::vector<int>> adjList;

	/// @brief 位向量矩阵，用于O(1)干涉查询
	/// bitmap[i][j] = true 表示区间i和j存在干涉
	std::vector<std::vector<bool>> bitmap;
};
