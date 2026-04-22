///
/// @file InterferenceGraph.h
/// @brief 干涉图数据结构，用于Greedy寄存器分配
///
/// - 节点为LiveInterval*（每个虚拟寄存器一个节点）
/// - 边表示两个虚拟寄存器同时活跃（不能分配同一物理寄存器）
/// - 支持O(1)干涉查询（位向量实现）和O(degree)邻居遍历（邻接表实现）
///

#pragma once

#include <cstdint>
#include <set>
#include <vector>

class LiveInterval;

/// @brief 干涉图
///
/// 混合表示：邻接表（遍历邻居）+ 位向量（O(1)干涉查询）
/// 节点通过LiveInterval的索引编号标识
///

class InterferenceGraph {

public:
	/// @brief 构造函数
	/// @param numNodes 节点数量（活跃区间数量）
	explicit InterferenceGraph(int numNodes);

	/// @brief 添加干涉边 (nodeA, nodeB)
	/// 无向图，同时更新两个方向的邻接关系
	/// @param nodeA 节点A的索引
	/// @param nodeB 节点B的索引
	void addEdge(int nodeA, int nodeB);

	/// @brief 判断两个节点是否干涉
	/// O(1)查询，使用位向量
	/// @param nodeA 节点A的索引
	/// @param nodeB 节点B的索引
	/// @return 是否干涉
	bool hasInterference(int nodeA, int nodeB) const;

	/// @brief 获取某节点的所有干涉邻居
	/// @param node 节点索引
	/// @return 邻居节点索引集合
	const std::set<int> & getNeighbors(int node) const;

	/// @brief 获取与某节点干涉的所有已分配物理寄存器集合
	/// 遍历邻居，收集已分配physReg的邻居的物理寄存器编号
	/// @param node 节点索引
	/// @param intervals 活跃区间列表（用于查询physReg）
	/// @return 已分配的物理寄存器编号集合
	std::set<int> getInterferingRegs(int node, const std::vector<LiveInterval *> & intervals) const;

	/// @brief 获取节点数量
	/// @return 节点数量
	int getNumNodes() const { return numNodes; }

private:
	/// 节点数量
	int numNodes;

	/// 邻接表：adjList[i] = 节点i的邻居集合
	std::vector<std::set<int>> adjList;

	/// 每行位图的64位字宽数量
	std::size_t wordsPerRow;

	/// 位向量：interferenceBits[i][word] 的某一位表示节点i和j是否干涉
	std::vector<std::vector<std::uint64_t>> interferenceBits;
};
