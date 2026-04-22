///
/// @file InterferenceGraph.cpp
/// @brief 干涉图数据结构实现
///

#include "InterferenceGraph.h"

/// @brief 构造函数
/// @param numIntervals 节点数量（活跃区间数量）
InterferenceGraph::InterferenceGraph(int numIntervals) : numNodes(numIntervals)
{
	// 初始化邻接表
	adjList.resize(numNodes);

	// 初始化位向量矩阵（N x N）
	bitmap.resize(numNodes);
	for (int i = 0; i < numNodes; ++i) {
		bitmap[i].resize(numNodes, false);
	}
}

/// @brief 在区间i和j之间添加干涉边
/// 同时更新邻接表和位向量
/// @param i 区间编号
/// @param j 区间编号
void InterferenceGraph::addEdge(int i, int j)
{
	if (i == j) {
		// 自干涉无意义，跳过
		return;
	}

	if (i < 0 || i >= numNodes || j < 0 || j >= numNodes) {
		// 越界检查
		return;
	}

	// 检查是否已存在干涉边
	if (bitmap[i][j]) {
		return;
	}

	// 更新位向量（无向图，对称设置）
	bitmap[i][j] = true;
	bitmap[j][i] = true;

	// 更新邻接表
	adjList[i].push_back(j);
	adjList[j].push_back(i);
}

/// @brief 判断区间i和j是否干涉（O(1)查询，位向量实现）
/// @param i 区间编号
/// @param j 区间编号
/// @return true表示存在干涉，false表示无干涉
bool InterferenceGraph::hasInterference(int i, int j)
{
	if (i < 0 || i >= numNodes || j < 0 || j >= numNodes) {
		return false;
	}
	return bitmap[i][j];
}

/// @brief 获取与区间i干涉的所有区间编号
/// @param i 区间编号
/// @return 干涉邻居区间编号列表
std::vector<int> InterferenceGraph::getNeighbors(int i)
{
	if (i < 0 || i >= numNodes) {
		return {};
	}
	return adjList[i];
}

/// @brief 获取与某区间干涉的所有已分配物理寄存器集合
/// 遍历与该区间干涉的所有邻居区间，收集已分配物理寄存器的编号
/// @param interval 活跃区间
/// @param intervals 所有活跃区间列表
/// @return 已分配的物理寄存器编号集合
std::set<int> InterferenceGraph::getInterferingRegs(LiveInterval & interval, std::vector<LiveInterval *> & intervals)
{
	std::set<int> interferingRegs;

	// 找到该区间在intervals列表中的编号
	int intervalIdx = -1;
	for (int i = 0; i < static_cast<int>(intervals.size()); ++i) {
		if (intervals[i] == &interval) {
			intervalIdx = i;
			break;
		}
	}

	if (intervalIdx < 0 || intervalIdx >= numNodes) {
		return interferingRegs;
	}

	// 遍历所有干涉邻居
	for (int neighborIdx : adjList[intervalIdx]) {
		if (neighborIdx >= 0 && neighborIdx < static_cast<int>(intervals.size())) {
			LiveInterval * neighbor = intervals[neighborIdx];
			// 如果邻居已分配物理寄存器，加入结果集合
			if (neighbor->physReg >= 0) {
				interferingRegs.insert(neighbor->physReg);
			}
		}
	}

	return interferingRegs;
}
