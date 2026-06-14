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

/// @brief 将一组可能重叠/乱序的段归并为升序的极大段集合
/// @param raw 原始段（精确段在合并后可能重复/重叠）
/// @return 升序、互不相邻重叠的极大段
std::vector<Segment> normalizeSegments(const std::vector<Segment> & raw)
{
	std::vector<Segment> segs = raw;
	std::sort(segs.begin(), segs.end(), [](const Segment & a, const Segment & b) {
		return a.start < b.start;
	});
	std::vector<Segment> merged;
	for (const auto & s : segs) {
		if (s.start >= s.end) {
			continue;
		}
		if (!merged.empty() && s.start <= merged.back().end) {
			merged.back().end = std::max(merged.back().end, s.end);
		} else {
			merged.push_back(s);
		}
	}
	return merged;
}

/// @brief 判断段集合是否与区间 [lo,hi) 有任何重叠
/// @param segs 段集合
/// @param lo 区间下界（含）
/// @param hi 区间上界（不含）
/// @return 任一段与 [lo,hi) 相交则返回 true
bool overlapsRange(const std::vector<Segment> & segs, int lo, int hi)
{
	for (const auto & s : segs) {
		if (std::max(s.start, lo) < std::min(s.end, hi)) {
			return true;
		}
	}
	return false;
}

int registerClassOf(Type * type)
{
	if (type == nullptr) {
		return -1;
	}
	if (type->isVectorType()) {
		return 2;
	}
	if (type->isFloatType()) {
		return 1;
	}
	return 0;
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
	// 类型必须兼容：GPR/FPR/VR 不能跨寄存器类合并。
	if (src->getType() == nullptr || dst->getType() == nullptr) {
		return false;
	}
	if (registerClassOf(src->getType()) != registerClassOf(dst->getType())) {
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
	// 原有保守快速路径：同块本地计算源，按保守区间判伪干涉。
	// 这条路径行为不变，覆盖局部 producer temp 的 destructive-update 合并。
	if (isLocalComputedSource(src, copyInst)) {
		if (graph == nullptr || !graph->hasInterference(srcIdx, dstIdx)) {
			return true;
		}
		auto copyPosIt = instNumbering.find(copyInst);
		if (copyPosIt != instNumbering.end() &&
		    onlyOverlapsInsideWindow(intervals[srcIdx], intervals[dstIdx], copyPosIt->second, copyPosIt->second + 1)) {
			return true;
		}
	}

	// 精确（hole-aware）干涉判断：保守循环扩展会让循环携带累加器（header phi）
	// 与 merge 值处处假干涉，导致跨块 phi-copy 无法合并。改用扩展前的精确活跃段，
	// 并排除连接 src/dst 的 copy 那一拍的伪干涉，即可安全合并这类累加器 copy，
	// 实现原地累加（addw a1,a1,t 而非 mv 对）
	return !preciseInterferes(src, dst, instNumbering);
}

/// @brief 基于精确区间判断 src/dst 是否真正干涉（hole-aware）
bool RegCoalescer::preciseInterferes(Value * src, Value * dst,
                                     const std::map<Instruction *, int> & instNumbering)
{
	auto sIt = preciseSegments_.find(src);
	auto dIt = preciseSegments_.find(dst);
	if (sIt == preciseSegments_.end() || dIt == preciseSegments_.end()) {
		// 缺精确信息，保守认为干涉，不合并
		return true;
	}

	// 收集 src/dst 两个合并类的定义点位置。在某条指令定义 X 类时，若另一类 Y 在此
	// 被读且就此死亡（copy 的 d=s、两地址的 d=s op x 都是这种形态），二者持有可共享
	// 的值，这一拍的重叠属伪干涉应排除；真正的干涉会体现在定义点之后仍存活的位置上，
	// 那些位置不是定义点，会被下面如实判为干涉。这样累加器的 phi-cycle（含
	// merge=phi+add 的 `t136=t113+prod`）也能被识别为可合并
	std::unordered_set<int> benignPositions;
	if (func_ != nullptr) {
		for (auto * bb : func_->getBlocks()) {
			for (auto * inst : bb->getInstructions()) {
				Value * def = nullptr;
				if (auto * copy = dynamic_cast<CopyInst *>(inst)) {
					def = copy->getDst() != nullptr ? copy->getDst() : static_cast<Value *>(copy);
				} else if (inst->hasResultValue()) {
					def = inst;
				}
				if (def == nullptr) {
					continue;
				}
				while (representative_.find(def) != representative_.end()) {
					def = representative_[def];
				}
				if (def == src || def == dst) {
					auto pit = instNumbering.find(inst);
					if (pit != instNumbering.end()) {
						benignPositions.insert(pit->second);
					}
				}
			}
		}
	}

	// 精确段重叠判定：copy 与两地址 op 的 def 周期重叠恒为单拍 [p,p+1)，
	// 且 p 是 src/dst 的定义点。任何长度 >1 的重叠都意味着二者多拍共存=真干涉；
	// 单拍重叠也仅当落在定义点时才属伪干涉。这样既能合并累加器，又不会被
	// 连续定义点误判放过真正的共存
	for (const auto & a : sIt->second) {
		for (const auto & b : dIt->second) {
			const int os = std::max(a.start, b.start);
			const int oe = std::min(a.end, b.end);
			if (os >= oe) {
				continue;
			}
			if (oe - os > 1 || benignPositions.find(os) == benignPositions.end()) {
				return true;
			}
		}
	}

	// 回边携带空洞守卫：精确段在循环回边处会留下空洞（被携带值实际仍活跃，
	// 保守扩展段能覆盖）。若另一类外来跨越该空洞，二者真干涉，但被空洞掩盖。
	// 这是 5e406a0 错误合并不同循环变量（如 calculator 的 c 与 ii）的根因。
	// 逐原始成员判定（而非合并类并集），避免把两个不同成员之间的良性间隙误判
	auto srcMemIt = classMembers_.find(src);
	auto dstMemIt = classMembers_.find(dst);
	static const std::vector<Value *> kEmptyMembers;
	const std::vector<Value *> & srcMembers = srcMemIt != classMembers_.end() ? srcMemIt->second : kEmptyMembers;
	const std::vector<Value *> & dstMembers = dstMemIt != classMembers_.end() ? dstMemIt->second : kEmptyMembers;
	if (spansCarriedHole(srcMembers, dIt->second) ||
	    spansCarriedHole(dstMembers, sIt->second)) {
		return true;
	}

	return false;
}

/// @brief 回边携带空洞守卫的实现（逐原始成员）
bool RegCoalescer::spansCarriedHole(const std::vector<Value *> & holedMembers,
                                    const std::vector<Segment> & spannerSegs) const
{
	for (Value * owner : holedMembers) {
		auto precIt = originalPrecise_.find(owner);
		if (precIt == originalPrecise_.end()) {
			continue;
		}
		const std::vector<Segment> ownSegs = normalizeSegments(precIt->second);
		if (ownSegs.size() < 2) {
			continue; // 该成员自身无空洞
		}
		auto consIt = conservativeSegments_.find(owner);
		const std::vector<Segment> & ownCons =
			consIt != conservativeSegments_.end() ? consIt->second : precIt->second;

		auto defIt = defPositions_.find(owner);

		// 逐个空洞（该成员自身相邻极大段之间的间隙）检查 spanner 段的插入方式
		for (size_t i = 0; i + 1 < ownSegs.size(); ++i) {
			const int gapStart = ownSegs[i].end;
			const int gapEnd = ownSegs[i + 1].start;
			if (gapStart >= gapEnd) {
				continue;
			}
			// 空洞末端是该值的新定义点：空洞前的旧值确已死亡，寄存器空闲，
			// 保守扩展只是反向多填了定义点前的循环体，并非回边携带，可放行
			if (defIt != defPositions_.end() && defIt->second.count(gapEnd) > 0) {
				continue;
			}
			for (const auto & sp : spannerSegs) {
				const int os = std::max(sp.start, gapStart);
				const int oe = std::min(sp.end, gapEnd);
				if (os >= oe) {
					continue; // 不插入此空洞
				}
				// 接力（carrier）：spanner 跨越空洞两端，与两侧 holed 段在定义点
				// 接力传值（累加器形态）。其两侧重叠已被主重叠循环按伪干涉放行
				if (sp.start < gapStart && sp.end > gapEnd) {
					continue;
				}
				// 外来（foreign）跨越：仅当该成员的保守扩展段覆盖被插入区段（即
				// 空洞是回边携带、成员实际活跃）时才判真干涉。否则空洞是菱形分支
				// 死区，寄存器确实空闲，可安全放行
				if (overlapsRange(ownCons, os, oe)) {
					return true;
				}
			}
		}
	}
	return false;
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

	// 精确段同步并入代表，供同一轮内后续 copy 的精确干涉判断使用
	auto fromSegIt = preciseSegments_.find(fromVal);
	if (fromSegIt != preciseSegments_.end()) {
		auto & toSegs = preciseSegments_[toVal];
		toSegs.insert(toSegs.end(), fromSegIt->second.begin(), fromSegIt->second.end());
	}

	// 合并类成员集合同步并入代表，回边携带判定需逐原始成员检查（用各自的
	// originalPrecise_/conservativeSegments_），故只并成员列表、不并段数据
	auto fromMemIt = classMembers_.find(fromVal);
	if (fromMemIt != classMembers_.end()) {
		auto & toMem = classMembers_[toVal];
		toMem.insert(toMem.end(), fromMemIt->second.begin(), fromMemIt->second.end());
	}

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
                       const std::map<Instruction *, int> & instNumbering,
                       const std::unordered_map<Value *, std::vector<Segment>> & preciseSegments)
{
	if (!enabled_) {
		return;
	}

	// GreedyRegAllocator 会按函数复用同一个 coalescer；每次运行都应从
	// 当前函数的 copy 图重新开始，避免上一函数的代表映射泄漏进来。
	eliminatedCopies_.clear();
	representative_.clear();
	func_ = func;
	preciseSegments_ = preciseSegments;
	originalPrecise_ = preciseSegments;

	// 快照每个原始值的保守循环扩展段（合并前），用于回边携带判定：
	// 精确段在循环回边处会留下空洞（被携带值实际仍活跃），保守段能覆盖这类空洞。
	// 这里按原始值保存、不随合并并入代表，便于逐来源精确判定携带空洞。
	// 同时初始化每个值的合并类成员集合（初始为自身单元素）。
	conservativeSegments_.clear();
	classMembers_.clear();
	for (const auto & [val, idx] : valueToInterval) {
		classMembers_[val] = {val};
		if (idx >= 0 && idx < static_cast<int>(intervals.size()) && intervals[idx] != nullptr) {
			auto & dst = conservativeSegments_[val];
			for (const auto & seg : intervals[idx]->getSegments()) {
				dst.push_back(seg);
			}
		}
	}

	// 记录每个原始值的定义点编号。回边携带空洞守卫据此区分：空洞末端 gapEnd 若是
	// 该值的新定义，则空洞前的旧值确实已死、寄存器空闲（保守扩展只是反向多填了
	// 定义点之前的循环体），可安全放行；若 gapEnd 非定义（live-in 续命），才是回边携带
	defPositions_.clear();
	if (func != nullptr) {
		for (auto * bb : func->getBlocks()) {
			for (auto * inst : bb->getInstructions()) {
				Value * def = nullptr;
				if (auto * copy = dynamic_cast<CopyInst *>(inst)) {
					def = copy->getDst() != nullptr ? copy->getDst() : static_cast<Value *>(copy);
				} else if (inst->hasResultValue()) {
					def = inst;
				}
				if (def == nullptr) {
					continue;
				}
				auto pit = instNumbering.find(inst);
				if (pit != instNumbering.end()) {
					defPositions_[def].insert(pit->second);
				}
			}
		}
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
