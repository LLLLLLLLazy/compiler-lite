///
/// @file InterferenceGraph.cpp
/// @brief 干涉图数据结构的实现
///

#include "InterferenceGraph.h"

#include <cstddef>

#include "LiveInterval.h"

namespace {

constexpr std::size_t kBitsPerWord = 64;

bool isValidNode(int node, int numNodes)
{
	return node >= 0 && node < numNodes;
}

std::size_t wordIndexFor(int node)
{
	return static_cast<std::size_t>(node) / kBitsPerWord;
}

std::uint64_t bitMaskFor(int node)
{
	return std::uint64_t{1} << (static_cast<std::size_t>(node) % kBitsPerWord);
}

} // namespace

/// @brief 构造函数
/// @param numNodes 节点数量
InterferenceGraph::InterferenceGraph(int numNodes)
	: numNodes(numNodes), adjList(numNodes), wordsPerRow((static_cast<std::size_t>(numNodes) + kBitsPerWord - 1) /
													  kBitsPerWord),
	  interferenceBits(numNodes, std::vector<std::uint64_t>(wordsPerRow, 0))
{}

/// @brief 添加干涉边 (nodeA, nodeB)
/// 无向图，同时更新两个方向的邻接关系
void InterferenceGraph::addEdge(int nodeA, int nodeB)
{
	if (nodeA == nodeB || !isValidNode(nodeA, numNodes) || !isValidNode(nodeB, numNodes)) {
		return; // 自环无意义
	}

	// 邻接表更新
	adjList[nodeA].insert(nodeB);
	adjList[nodeB].insert(nodeA);

	// 位向量更新
	interferenceBits[nodeA][wordIndexFor(nodeB)] |= bitMaskFor(nodeB);
	interferenceBits[nodeB][wordIndexFor(nodeA)] |= bitMaskFor(nodeA);
}

/// @brief 判断两个节点是否干涉
/// O(1)查询，使用位向量
bool InterferenceGraph::hasInterference(int nodeA, int nodeB) const
{
	if (!isValidNode(nodeA, numNodes) || !isValidNode(nodeB, numNodes)) {
		return false;
	}
	return (interferenceBits[nodeA][wordIndexFor(nodeB)] & bitMaskFor(nodeB)) != 0;
}

/// @brief 获取某节点的所有干涉邻居
const std::set<int> & InterferenceGraph::getNeighbors(int node) const
{
	static const std::set<int> emptySet;
	if (!isValidNode(node, numNodes)) {
		return emptySet;
	}
	return adjList[node];
}

/// @brief 获取与某节点干涉的所有已分配物理寄存器集合
std::set<int> InterferenceGraph::getInterferingRegs(int node, const std::vector<LiveInterval *> & intervals) const
{
	std::set<int> regs;
	if (!isValidNode(node, numNodes)) {
		return regs;
	}

	for (int neighbor : adjList[node]) {
		if (neighbor < 0 || static_cast<std::size_t>(neighbor) >= intervals.size() || intervals[neighbor] == nullptr) {
			continue;
		}

		int physReg = intervals[neighbor]->getPhysReg();
		if (physReg != -1) {
			regs.insert(physReg);
		}
	}
	return regs;
}
