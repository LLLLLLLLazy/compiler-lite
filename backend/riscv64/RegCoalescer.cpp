///
/// @file RegCoalescer.cpp
/// @brief 寄存器合并器的实现
///

#include "RegCoalescer.h"

#include <algorithm>

#include "BasicBlock.h"
#include "CopyInst.h"
#include "Function.h"
#include "InterferenceGraph.h"
#include "LiveInterval.h"
#include "Type.h"
#include "Value.h"

/// @brief 构造函数
RegCoalescer::RegCoalescer(bool enabled)
	: enabled_(enabled)
{
}

/// @brief 收集 IR 中所有 copy 指令的 (src, dst, copyInst) 三元组
std::vector<std::tuple<Value *, Value *, Instruction *>> RegCoalescer::collectCopyPairs(Function * func)
{
	std::vector<std::tuple<Value *, Value *, Instruction *>> pairs;
	for (auto * bb : func->getBlocks()) {
		for (auto * inst : bb->getInstructions()) {
			auto * copy = dynamic_cast<CopyInst *>(inst);
			if (copy == nullptr) {
				continue;
			}
			Value * src = copy->getSource();
			Value * dst = copy->getDst() != nullptr ? copy->getDst() : static_cast<Value *>(copy);
			if (src != nullptr && dst != nullptr && src != dst) {
				pairs.emplace_back(src, dst, inst);
			}
		}
	}
	return pairs;
}

/// @brief 判断两个虚拟寄存器是否可合并
bool RegCoalescer::canCoalesce(Value * src, Value * dst,
                               const std::vector<LiveInterval *> & intervals,
                               const InterferenceGraph * graph,
                               const std::unordered_map<Value *, int> & valueToInterval)
{
	// 类型必须兼容：同为 float 或同为 int
	if (src->getType() == nullptr || dst->getType() == nullptr) {
		return false;
	}
	bool srcIsFloat = src->getType()->isFloatType();
	bool dstIsFloat = dst->getType()->isFloatType();
	if (srcIsFloat != dstIsFloat) {
		return false;
	}

	// 查找活跃区间索引
	auto srcIt = valueToInterval.find(src);
	auto dstIt = valueToInterval.find(dst);
	if (srcIt == valueToInterval.end() || dstIt == valueToInterval.end()) {
		return false;
	}

	int srcIdx = srcIt->second;
	int dstIdx = dstIt->second;
	if (srcIdx < 0 || srcIdx >= static_cast<int>(intervals.size()) ||
	    dstIdx < 0 || dstIdx >= static_cast<int>(intervals.size())) {
		return false;
	}

	// 检查是否干涉
	if (graph != nullptr && graph->hasInterference(srcIdx, dstIdx)) {
		return false;
	}

	return true;
}

/// @brief 执行一次合并：将度数大者的 Segment/usePositions 合入度数小者的区间
void RegCoalescer::mergeIntervals(Value * src, Value * dst,
                                  std::vector<LiveInterval *> & intervals,
                                  std::unordered_map<Value *, int> & valueToInterval)
{
	auto srcIt = valueToInterval.find(src);
	auto dstIt = valueToInterval.find(dst);
	if (srcIt == valueToInterval.end() || dstIt == valueToInterval.end()) {
		return;
	}

	int srcIdx = srcIt->second;
	int dstIdx = dstIt->second;

	LiveInterval * srcInterval = intervals[srcIdx];
	LiveInterval * dstInterval = intervals[dstIdx];
	if (srcInterval == nullptr || dstInterval == nullptr) {
		return;
	}

	// 选择合并方向：将度数大者合入度数小者
	// 这里简化处理：将 src 合入 dst
	LiveInterval * from = srcInterval;
	LiveInterval * to = dstInterval;
	int toIdx = dstIdx;
	Value * fromVal = src;
	Value * toVal = dst;

	// 将 from 的 Segment 合入 to
	for (const auto & seg : from->getSegments()) {
		to->addSegment(seg.start, seg.end);
	}
	// 将 from 的 usePositions 合入 to
	for (int pos : from->getUsePositions()) {
		to->addUsePosition(pos);
	}
	// 更新溢出权重：取较大者
	if (from->getSpillWeight() > to->getSpillWeight()) {
		// 重新计算权重（简化：取较大者）
	}
	// 更新 maxLoopDepth
	if (from->maxLoopDepth > to->maxLoopDepth) {
		to->maxLoopDepth = from->maxLoopDepth;
	}

	// 在 valueToInterval 中将 fromVal 映射到 toIdx
	valueToInterval[fromVal] = toIdx;

	// 将 from 区间标记为无效
	from->vreg = nullptr;

	// 记录代表映射
	representative_[fromVal] = toVal;
}

/// @brief 合并后重建干涉图
InterferenceGraph * RegCoalescer::rebuildInterferenceGraph(
	const std::vector<LiveInterval *> & intervals)
{
	// 计算有效区间数
	int numValid = 0;
	for (auto * interval : intervals) {
		if (interval != nullptr && interval->getVReg() != nullptr) {
			++numValid;
		}
	}

	auto * graph = new InterferenceGraph(static_cast<int>(intervals.size()));

	// 遍历所有区间对，检查是否干涉
	for (int i = 0; i < static_cast<int>(intervals.size()); ++i) {
		if (intervals[i] == nullptr || intervals[i]->getVReg() == nullptr) {
			continue;
		}
		for (int j = i + 1; j < static_cast<int>(intervals.size()); ++j) {
			if (intervals[j] == nullptr || intervals[j]->getVReg() == nullptr) {
				continue;
			}
			if (intervals[i]->overlaps(*intervals[j])) {
				graph->addEdge(i, j);
			}
		}
	}
	graph->finalizeEdges();
	return graph;
}

/// @brief 执行寄存器合并
void RegCoalescer::run(std::vector<LiveInterval *> & intervals,
                       InterferenceGraph *& graph,
                       Function * func,
                       std::unordered_map<Value *, int> & valueToInterval)
{
	if (!enabled_) {
		return;
	}

	// 迭代合并直到无新合并发生
	bool changed = true;
	while (changed) {
		changed = false;
		auto copyPairs = collectCopyPairs(func);

		for (auto & [src, dst, copyInst] : copyPairs) {
			// 跳过已消除的 copy
			if (eliminatedCopies_.find(copyInst) != eliminatedCopies_.end()) {
				continue;
			}

			// 通过代表映射找到最终代表
			while (representative_.find(src) != representative_.end()) {
				src = representative_[src];
			}
			while (representative_.find(dst) != representative_.end()) {
				dst = representative_[dst];
			}
			if (src == dst) {
				// 已经合并
				eliminatedCopies_.insert(copyInst);
				changed = true;
				continue;
			}

			if (canCoalesce(src, dst, intervals, graph, valueToInterval)) {
				mergeIntervals(src, dst, intervals, valueToInterval);
				eliminatedCopies_.insert(copyInst);
				changed = true;
			}
		}

		if (changed) {
			// 合并后重建干涉图
			auto * newGraph = rebuildInterferenceGraph(intervals);
			delete graph;
			graph = newGraph;
		}
	}
}
