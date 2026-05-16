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

namespace {

/// @brief 判断两个活跃区间是否仅在指定窗口内重叠
/// @param srcInterval 源值的活跃区间
/// @param dstInterval 目标值的活跃区间
/// @param windowStart 窗口起始位置（含）
/// @param windowEnd 窗口结束位置（不含）
/// @return 若两区间有重叠且所有重叠段都完全落在[windowStart, windowEnd)内则返回true
/// @note 用于判断copy指令的src/dst干涉是否仅限于copy本身这一拍，
///       若是则该干涉不应阻止coalescing
bool onlyOverlapsInsideWindow(const LiveInterval * srcInterval,
                              const LiveInterval * dstInterval,
                              int windowStart,
                              int windowEnd)
{
	if (srcInterval == nullptr || dstInterval == nullptr || windowStart >= windowEnd) {
		return false;
	}

	bool sawOverlap = false;
	for (const auto & srcSeg : srcInterval->getSegments()) {
		for (const auto & dstSeg : dstInterval->getSegments()) {
			const int overlapStart = std::max(srcSeg.start, dstSeg.start);
			const int overlapEnd = std::min(srcSeg.end, dstSeg.end);
			if (overlapStart >= overlapEnd) {
				continue;
			}
			sawOverlap = true;
			if (overlapStart < windowStart || overlapEnd > windowEnd) {
				return false;
			}
		}
	}
	return sawOverlap;
}

bool isLocalComputedSource(Value * src, Instruction * copyInst)
{
	auto * srcInst = dynamic_cast<Instruction *>(src);
	return srcInst != nullptr && dynamic_cast<CopyInst *>(srcInst) == nullptr && copyInst != nullptr &&
	       srcInst->getParentBlock() != nullptr && srcInst->getParentBlock() == copyInst->getParentBlock();
}

} // namespace

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
                               const std::unordered_map<Value *, int> & valueToInterval,
                               Instruction * copyInst,
                               const std::map<Instruction *, int> & instNumbering)
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
	if (!isLocalComputedSource(src, copyInst)) {
		return false;
	}

	// copy 的 src 使用与 dst 定义若仅在 copy 本身这一拍相接，可共享寄存器；
	// 这是 coalescing 想消除的那条 move，不应被当作真实干涉。
	// 若干涉图报告src/dst干涉，进一步检查：若重叠仅限于copy指令位置这一拍，
	// 则该干涉是copy本身造成的"伪干涉"，仍可合并；否则拒绝合并。
	if (graph != nullptr && graph->hasInterference(srcIdx, dstIdx)) {
		auto copyPosIt = instNumbering.find(copyInst);
		if (copyPosIt == instNumbering.end()) {
			// 无法定位copy指令位置，保守拒绝合并
			return false;
		}
		const int copyPos = copyPosIt->second;

		if (onlyOverlapsInsideWindow(intervals[srcIdx], intervals[dstIdx], copyPos, copyPos + 1)) {
			return true;
		}

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
/// @param intervals 活跃区间列表
/// @param graph 干涉图（可能被重建）
/// @param func 当前函数
/// @param valueToInterval Value到活跃区间索引的映射
/// @param instNumbering 指令编号映射，用于判断copy位置的伪干涉
void RegCoalescer::run(std::vector<LiveInterval *> & intervals,
                       InterferenceGraph *& graph,
                       Function * func,
                       std::unordered_map<Value *, int> & valueToInterval,
                       const std::map<Instruction *, int> & instNumbering)
{
	if (!enabled_) {
		return;
	}

	// GreedyRegAllocator 会按函数复用同一个 coalescer；每次运行都应从
	// 当前函数的 copy 图重新开始，避免上一函数的代表映射泄漏进来。
	eliminatedCopies_.clear();
	representative_.clear();

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

			if (canCoalesce(src, dst, intervals, graph, valueToInterval, copyInst, instNumbering)) {
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
