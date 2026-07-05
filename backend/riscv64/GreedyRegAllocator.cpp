///
/// @file GreedyRegAllocator.cpp
/// @brief RISCV64 Greedy寄存器分配器的实现
///
/// 基于线性扫描的Greedy寄存器分配算法实现，包括：
/// - 活跃区间分析与干涉图构建
/// - 按溢出权重排序的Greedy分配主循环
/// - 空闲寄存器分配与驱逐（evict）策略
/// - 溢出（spill）处理
///
#include "GreedyRegAllocator.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "CallInst.h"
#include "CalleeSavedFPREnabler.h"
#include "DominatorTree.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "HeuristicSpillStrategy.h"
#include "InterferenceGraph.h"
#include "LiveInterval.h"
#include "LiveIntervalAnalysis.h"
#include "LiveIntervalSplitter.h"
#include "LoopInfo.h"
#include "RegCoalescer.h"
#include "Rematerialization.h"
#include "Use.h"
#include "Value.h"

/// @brief 构造函数，初始化溢出决策策略
/// @param strategy 溢出决策策略，若为nullptr则使用默认的HeuristicSpillStrategy
/// @param enableCalleeSavedFPR 是否启用 callee-saved FPR
/// @param enableCoalesce 是否启用寄存器合并
/// @param enableSplit 是否启用活跃区间分裂
GreedyRegAllocator::GreedyRegAllocator(SpillStrategy * strategy,
                                       bool enableCalleeSavedFPR,
                                       bool enableCoalesce,
                                       bool enableSplit)
{
	if (strategy == nullptr) {
		// 未提供策略时，创建默认的启发式溢出策略
		ownedStrategy = std::make_unique<HeuristicSpillStrategy>();
		spillStrategy = ownedStrategy.get();
	} else {
		spillStrategy = strategy;
	}

	if (enableCalleeSavedFPR) {
		fprEnabler_ = std::make_unique<CalleeSavedFPREnabler>(true);
	}
	if (enableCoalesce) {
		coalescer_ = std::make_unique<RegCoalescer>(true);
	}
	if (enableSplit) {
		// maxTotalIntervals 将在 allocate() 中根据原区间数设置
		splitter_ = std::make_unique<LiveIntervalSplitter>(true, 0);
	}
}

/// @brief 对函数执行寄存器分配
/// @param func 待分配的函数
///
/// 流程：
/// 1. 构建可用寄存器池
/// 2. 执行活跃区间分析
/// 3. 运行Greedy分配算法
/// 4. 重建分配映射表
void GreedyRegAllocator::allocate(Function * func)
{
	// 清空上一次分配的状态
	allocationMap.clear();
	allocationSegments.clear();
	allocatedGprLiveRanges.clear();
	allocatedFprLiveRanges.clear();
	allocatedVectorLiveRanges.clear();
	splitTransfers.clear();
	splitStackValues.clear();
	spilledValues.clear();
	rematOnlySpills.clear();
	stats = RegAllocStats{};
	intervalToIndex.clear();
	usedCalleeSavedGprsDuringAllocation.clear();
	callInstNumbers.clear();
	splitCandidateNumbers.clear();
	liveAcrossCallPositions.clear();
	instNumbering.clear();
	valueLiveRanges.clear();
	valueUsePositions.clear();
	frameSize = 0;
	outgoingArgBytes = 0;
	availableRegs.clear();
	availableFloatRegs.clear();
	availableVectorRegs.clear();

	// 内建函数不需要寄存器分配
	if (func == nullptr || func->isBuiltin()) {
		return;
	}

	// 构建可用物理寄存器池
	availableRegs = buildRegisterPool(func);
	availableFloatRegs = buildFloatRegisterPool(func);
	availableVectorRegs = buildVectorRegisterPool(func);

	// [Callee-saved FPR] 扩展 FPR 池
	if (fprEnabler_) {
		availableFloatRegs = fprEnabler_->extendFloatPool(availableFloatRegs);
	}

	// 构建支配树和循环分析，用于计算循环深度加权的溢出权重
	DominatorTree domTree(func);
	LoopInfo loopInfo(func, &domTree);

	// 将循环深度写入BasicBlock，供后续指令选择等阶段使用
	for (auto * bb : func->getBlocks()) {
		bb->setLoopDepth(loopInfo.getLoopDepth(bb));
	}

	// 执行活跃区间分析
	LiveIntervalAnalysis analysis(func, &loopInfo);
	analysis.run();
	instNumbering = analysis.getInstNumbering();
	liveAcrossCallPositions = analysis.getLiveAcrossCallPositions();
	for (auto & [inst, num] : instNumbering) {
		if (dynamic_cast<CallInst *>(inst) != nullptr) {
			callInstNumbers.push_back(num);
		}
	}
	// instNumbering 是 std::map<Instruction*,int>，按指针序迭代得到的 callInstNumbers
	// 顺序每次编译都不同，会影响分裂点选择等顺序敏感决策。按调用位置升序排序使其确定
	std::sort(callInstNumbers.begin(), callInstNumbers.end());

	std::unordered_set<int> splitCandidateSet;
	for (auto * bb : func->getBlocks()) {
		if (bb == nullptr || bb->getLoopDepth() <= 0) {
			continue;
		}
		for (auto * inst : bb->getInstructions()) {
			auto numIt = instNumbering.find(inst);
			if (numIt != instNumbering.end()) {
				splitCandidateSet.insert(numIt->second);
				break;
			}
		}
	}
	splitCandidateNumbers.assign(splitCandidateSet.begin(), splitCandidateSet.end());
	std::sort(splitCandidateNumbers.begin(), splitCandidateNumbers.end());

	// 建立活跃区间到索引的映射
	auto & intervals = analysis.getIntervals();
	for (int i = 0; i < static_cast<int>(intervals.size()); ++i) {
		intervalToIndex[intervals[i]] = i;
	}

	// [寄存器合并] 在 Greedy 主循环之前执行
	auto * ig = analysis.getInterferenceGraph();
	std::unordered_map<Value *, int> valueToInterval;
	for (int i = 0; i < static_cast<int>(intervals.size()); ++i) {
		if (intervals[i] != nullptr && intervals[i]->getVReg() != nullptr) {
			valueToInterval[intervals[i]->getVReg()] = i;
		}
	}
	if (coalescer_) {
		coalescer_->run(intervals, ig, func, valueToInterval, instNumbering, analysis.getPreciseSegments());
		refreshCoalescedLiveAcrossCallPositions();
	}

	// 运行Greedy分配主循环
	runGreedy(intervals, ig);
	analysis.adoptInterferenceGraph(ig);
	// 从活跃区间结果重建分配映射表
	rebuildAllocationMap(intervals);

	// [Callee-saved FPR] 收集被使用的 callee-saved FPR
	if (fprEnabler_) {
		usedCalleeSavedFPRs_ = CalleeSavedFPREnabler::collectUsedCalleeSavedFPRs(allocationMap);
	}

	// [活跃区间分裂] 处理分裂点的 spill/reload
	// 分裂产生的子区间如果分配到不同寄存器或被溢出，
	// 需要在分裂点插入值的传递代码。
	// 当前实现：分裂的子区间共享同一 vreg，在指令选择阶段
	// 通过 RegAllocInfo 自然处理——左子区间和右子区间
	// 各自的寄存器分配结果会写入 allocationMap，
	// 指令选择阶段根据每条指令处的分配信息正确生成代码。
	// 若两子区间分配到不同寄存器，分裂点处会自动产生
	// 寄存器间 move（由 copy 指令或指令选择逻辑处理）。
	// 无需额外插入 spill/reload IR 指令。

	// 保存活跃性快照，供指令选择阶段的动态临时寄存器管理使用。
	// 同一 Value 可能因 split 出现多个子区间，必须取并集，不能让后一个子区间覆盖前一个。
	for (auto * interval : intervals) {
		if (interval != nullptr && interval->getVReg() != nullptr) {
			auto & range = valueLiveRanges[interval->getVReg()];
			if (range.first == 0 && range.second == 0) {
				range = {interval->getStart(), interval->getEnd()};
			} else {
				range.first = std::min(range.first, interval->getStart());
				range.second = std::max(range.second, interval->getEnd());
			}
		}
	}
	refreshCoalescedAliasAllocations();
}

/// @brief 获取虚拟寄存器分配的物理寄存器编号
/// @param vreg 虚拟寄存器（Value*）
/// @return 物理寄存器编号，-1表示未分配
int GreedyRegAllocator::getAllocation(Value * vreg) const
{
	auto it = allocationMap.find(vreg);
	if (it == allocationMap.end()) {
		return -1;
	}
	return it->second.regId;
}

/// @brief 判断虚拟寄存器是否被溢出
/// @param vreg 虚拟寄存器（Value*）
/// @return 是否被溢出
bool GreedyRegAllocator::isSpilled(Value * vreg) const
{
	return spilledValues.find(vreg) != spilledValues.end();
}

bool GreedyRegAllocator::isRematOnlySpill(Value * vreg) const
{
	return rematOnlySpills.find(vreg) != rematOnlySpills.end();
}

/// @brief 获取溢出变量的栈槽偏移
/// @param vreg 虚拟寄存器（Value*）
/// @return 栈槽偏移量，0表示未分配栈槽
int64_t GreedyRegAllocator::getSpillSlot(Value * vreg) const
{
	auto it = allocationMap.find(vreg);
	if (it == allocationMap.end() || !it->second.hasStackSlot) {
		return 0;
	}
	return it->second.offset;
}

/// @brief 判断某个Value是否已有分配信息
/// @param val 待判断的Value
/// @return 是否已有分配
bool GreedyRegAllocator::hasAllocation(Value * val) const
{
	return allocationMap.find(val) != allocationMap.end();
}

/// @brief 判断Value是否必须分配在栈上
/// @param val 待判断的Value
/// @return 是否必须分配在栈上
///
/// AllocaInst（栈分配指令）的结果必须分配在栈上
bool GreedyRegAllocator::isForcedStackValue(Value * val)
{
	return dynamic_cast<AllocaInst *>(val) != nullptr;
}

/// @brief 获取被消除的 copy 指令集合
const std::unordered_set<Instruction *> & GreedyRegAllocator::getEliminatedCopies() const
{
	static const std::unordered_set<Instruction *> emptySet;
	if (coalescer_) {
		return coalescer_->getEliminatedCopies();
	}
	return emptySet;
}

/// @brief 获取寄存器合并后的最终代表值
/// @param value 待查询的Value
/// @return 沿着representative映射链找到的最终代表值；若未被合并则返回自身
/// @note 使用visited集合防止循环引用导致无限循环
Value * GreedyRegAllocator::getCoalescedRepresentative(Value * value) const
{
	if (value == nullptr || !coalescer_) {
		return value;
	}

	const auto & representative = coalescer_->getRepresentativeMap();
	std::unordered_set<Value *> visited;
	Value * current = value;
	while (current != nullptr) {
		auto it = representative.find(current);
		if (it == representative.end() || it->second == nullptr || !visited.insert(current).second) {
			break;
		}
		current = it->second;
	}
	return current;
}

/// @brief 将 coalesced alias 的位置查询统一回最终代表值
/// @note 遍历coalescer的representative映射，对每个被合并的alias：
///       1. 查找其最终代表值（沿映射链走到终点）
///       2. 将代表值的allocationMap、allocationSegments、valueLiveRanges
///          复制到alias，使两者共享同一位置
///       3. 同步spilledValues和splitStackValues集合
///       这确保被coalescing消除的copy指令的src/dst在后续代码生成中
///       使用完全相同的寄存器/栈槽位置
void GreedyRegAllocator::refreshCoalescedAliasAllocations()
{
	if (!coalescer_) {
		return;
	}

	for (const auto & [alias, _] : coalescer_->getRepresentativeMap()) {
		Value * representative = getCoalescedRepresentative(alias);
		if (alias == nullptr || representative == nullptr || alias == representative) {
			continue;
		}

		auto mapIt = allocationMap.find(representative);
		if (mapIt != allocationMap.end()) {
			allocationMap[alias] = mapIt->second;
		}

		auto segIt = allocationSegments.find(representative);
		if (segIt != allocationSegments.end()) {
			allocationSegments[alias] = segIt->second;
		}

		auto liveIt = valueLiveRanges.find(representative);
		if (liveIt != valueLiveRanges.end()) {
			valueLiveRanges[alias] = liveIt->second;
		}

		if (spilledValues.find(representative) != spilledValues.end()) {
			spilledValues.insert(alias);
		} else {
			spilledValues.erase(alias);
		}
		if (rematOnlySpills.find(representative) != rematOnlySpills.end()) {
			rematOnlySpills.insert(alias);
		} else {
			rematOnlySpills.erase(alias);
		}
		if (splitStackValues.find(representative) != splitStackValues.end()) {
			splitStackValues.insert(alias);
		} else {
			splitStackValues.erase(alias);
		}
	}
}

/// @brief 执行Greedy分配主循环
/// @param intervals 活跃区间列表
/// @param graph 干涉图
///
/// 按溢出权重从高到低排序活跃区间，依次尝试：
/// 1. 分配空闲寄存器
/// 2. 驱逐低权重区间并分配
/// 3. 标记为溢出
void GreedyRegAllocator::runGreedy(std::vector<LiveInterval *> & intervals, InterferenceGraph *& graph)
{
	// 设置分裂器的最大区间数限制
	if (splitter_) {
		splitter_ = std::make_unique<LiveIntervalSplitter>(true,
			static_cast<int>(intervals.size()) * 4);
	}

	auto sortPending = [](auto begin, auto end) {
		std::sort(begin, end, [](LiveInterval * lhs, LiveInterval * rhs) {
			if (lhs == rhs) {
				return false;
			}
			if (lhs == nullptr) {
				return false;
			}
			if (rhs == nullptr) {
				return true;
			}
			if (lhs->getSpillWeight() == rhs->getSpillWeight()) {
				return lhs->getStart() < rhs->getStart();
			}
			return lhs->getSpillWeight() > rhs->getSpillWeight();
		});
	};

	std::vector<LiveInterval *> workList;
	workList.reserve(intervals.size());
	std::unordered_set<LiveInterval *> queued;
	std::unordered_set<LiveInterval *> finalized;
	auto enqueue = [&](LiveInterval * item) {
		if (item == nullptr || item->getVReg() == nullptr) {
			return;
		}
		if (finalized.find(item) != finalized.end()) {
			return;
		}
		if (queued.insert(item).second) {
			workList.push_back(item);
		}
	};
	for (auto * interval : intervals) {
		enqueue(interval);
	}
	sortPending(workList.begin(), workList.end());

	std::unordered_map<LiveInterval *, int> evictionRetries;
	constexpr int maxEvictionRetries = 3;
	bool needReSort = false;

	for (size_t wi = 0; wi < workList.size(); ++wi) {
		// 分裂/驱逐后可能需要重排序未处理元素
		if (needReSort) {
			sortPending(workList.begin() + static_cast<std::ptrdiff_t>(wi), workList.end());
			needReSort = false;
		}

		auto * interval = workList[wi];
		queued.erase(interval);
		if (interval == nullptr || interval->getVReg() == nullptr) {
			continue;
		}
		if (finalized.find(interval) != finalized.end()) {
			continue;
		}
		if (interval->getPhysReg() != -1) {
			finalized.insert(interval);
			continue;
		}

		// 强制栈分配的Value直接溢出
		if (isForcedStackValue(interval->getVReg())) {
			markSpilled(interval);
			finalized.insert(interval);
			continue;
		}

		// 尝试分配空闲寄存器
		if (tryAssignFreeReg(interval, intervals, graph)) {
			finalized.insert(interval);
			continue;
		}

		// 尝试驱逐已有分配；被驱逐者重新入队，避免一次失败就直接落栈。
		if (evictionRetries[interval] <= maxEvictionRetries) {
			std::vector<LiveInterval *> evictedVictims;
			if (tryEvictAndAssign(interval, intervals, graph, &evictedVictims)) {
				finalized.insert(interval);
				for (auto * victim : evictedVictims) {
					if (victim == nullptr || victim->getVReg() == nullptr) {
						continue;
					}
					finalized.erase(victim);
					++evictionRetries[victim];
					enqueue(victim);
				}
				needReSort = !evictedVictims.empty();
				continue;
			}
		}

		// [活跃区间分裂] 尝试分裂
		if (splitter_) {
			auto splitResult = splitter_->trySplit(interval, intervals, graph,
				callInstNumbers, splitCandidateNumbers, intervalToIndex);
			if (splitResult.has_value()) {
				// 分裂成功，原区间失效，将子区间加入工作列表
				finalized.insert(interval);
				enqueue(splitResult->left);
				enqueue(splitResult->right);
				needReSort = true;
				continue;
			}
		}

		// 无法分配，标记为溢出
		markSpilled(interval);
		finalized.insert(interval);
	}
}

/// @brief 为活跃区间分配物理寄存器
/// @param interval 活跃区间
/// @param reg 物理寄存器编号
void GreedyRegAllocator::assignPhysicalReg(LiveInterval * interval, int reg)
{
	interval->setPhysReg(reg);
	interval->setSpilled(false);
	if (reg >= 0 && registerClassFor(interval) == RegisterClass::Gpr && isCalleeSavedGpr(reg)) {
		usedCalleeSavedGprsDuringAllocation.insert(reg);
	}
}

/// @brief 尝试为活跃区间分配空闲寄存器
/// @param interval 待分配的活跃区间
/// @param intervals 所有活跃区间
/// @param graph 干涉图
/// @return 是否分配成功
///
/// 遍历可用寄存器，找到不与当前区间干涉的寄存器即可分配
bool GreedyRegAllocator::tryAssignFreeReg(LiveInterval * interval,
	const std::vector<LiveInterval *> & intervals,
	InterferenceGraph * graph)
{
	if (graph == nullptr || registerPoolFor(interval).empty()) {
		return false;
	}

	const int node = getIntervalIndex(interval);
	if (node < 0) {
		return false;
	}

	// 获取当前区间所有干涉邻居已占用的寄存器集合
	const RegisterClass wantedClass = registerClassFor(interval);
	std::set<int> usedRegs = getInterferingRegsForClass(node, intervals, graph, wantedClass);
	for (int reg: orderedRegisterPoolFor(interval)) {
		if (!canAssignReg(interval, reg)) {
			continue;
		}
		if (usedRegs.find(reg) == usedRegs.end()) {
			assignPhysicalReg(interval, reg);
			return true;
		}
	}

	return false;
}

/// @brief 尝试驱逐已有分配并为当前区间分配寄存器
/// @param interval 待分配的活跃区间
/// @param intervals 所有活跃区间
/// @param graph 干涉图
/// @return 是否驱逐并分配成功
///
/// 遍历可用寄存器，对于每个被干涉邻居占用的寄存器：
/// - 若邻居的溢出权重 >= 当前区间权重，则该寄存器不可驱逐
/// - 否则将邻居加入驱逐候选列表
/// 若所有占用该寄存器的邻居都可驱逐，则执行驱逐并分配
bool GreedyRegAllocator::tryEvictAndAssign(LiveInterval * interval,
	std::vector<LiveInterval *> & intervals,
	InterferenceGraph * graph,
	std::vector<LiveInterval *> * evictedVictims)
{
	if (graph == nullptr || registerPoolFor(interval).empty()) {
		return false;
	}

	const int node = getIntervalIndex(interval);
	if (node < 0) {
		return false;
	}

	const RegisterClass wantedClass = registerClassFor(interval);
	for (int reg: orderedRegisterPoolFor(interval)) {
		if (!canAssignReg(interval, reg)) {
			continue;
		}

		std::vector<LiveInterval *> evictionCandidates;
		bool canUseReg = true;

		// 检查占用该寄存器的所有干涉邻居
		for (int neighborIdx: graph->getNeighbors(node)) {
			if (neighborIdx < 0 || neighborIdx >= static_cast<int>(intervals.size())) {
				continue;
			}

			auto * neighbor = intervals[neighborIdx];
			if (neighbor == nullptr || registerClassFor(neighbor) != wantedClass || neighbor->getPhysReg() != reg) {
				continue;
			}

			// 邻居权重更高，无法驱逐
			if (neighbor->getSpillWeight() >= interval->getSpillWeight()) {
				canUseReg = false;
				break;
			}

			evictionCandidates.push_back(neighbor);
		}

		if (!canUseReg || evictionCandidates.empty()) {
			continue;
		}

		// 先腾出该寄存器：把所有 victim 的物理寄存器清空，
		// 然后再把当前区间放到 reg 上。这样后续重试 victim 时，
		// 干涉图会正确反映 victim 对当前区间（持有 reg）干涉，
		// 从而避免 victim 又挑回 reg。
		for (auto * victim: evictionCandidates) {
			assignPhysicalReg(victim, -1);
		}
		assignPhysicalReg(interval, reg);

		if (evictedVictims != nullptr) {
			evictedVictims->insert(evictedVictims->end(), evictionCandidates.begin(), evictionCandidates.end());
		} else {
			for (auto * victim: evictionCandidates) {
				if (!tryAssignFreeReg(victim, intervals, graph)) {
					markSpilled(victim);
				}
			}
		}
		return true;
	}

	return false;
}

/// @brief 将活跃区间标记为溢出
/// @param interval 待溢出的活跃区间
void GreedyRegAllocator::markSpilled(LiveInterval * interval)
{
	interval->setPhysReg(-1);
	interval->setSpilled(true);
	spilledValues.insert(interval->getVReg());
}

/// @brief 构建可用物理寄存器池
/// @param func 当前函数
/// @return 可用寄存器编号列表
///
/// 使用RISC-V ABI中可分配的GPR：
/// - caller-saved: t0-t6, a0-a7
/// - callee-saved: s1-s11
///
/// 保留寄存器：zero, ra, sp, gp, tp, s0/fp。
std::vector<int> GreedyRegAllocator::buildRegisterPool(Function * func) const
{
	(void) func;

	// t3-t4 (28-29) 保留为scratch寄存器，t5-t6 (30-31) 参与全局分配
	std::vector<int> regs = {
		5,  // t0
		6,  // t1
		7,  // t2
		10, // a0
		11, // a1
		12, // a2
		13, // a3
		14, // a4
		15, // a5
		16, // a6
		17, // a7
		9,  // s1
		18, // s2
		19, // s3
		20, // s4
		21, // s5
		22, // s6
		23, // s7
		24, // s8
		25, // s9
		26, // s10
		27, // s11
		30, // t5
		31, // t6
	};

	return regs;
}

/// @brief 构建FPR分配池。
///
/// 当前只启用caller-saved FPR。ft10/ft11 保留给指令选择阶段作为临时 FPR，
/// 避免极端浮点压力下没有 scratch 寄存器可用于 reload/store。
std::vector<int> GreedyRegAllocator::buildFloatRegisterPool(Function * func) const
{
	(void) func;

	// 首轮FP寄存器分配只使用caller-saved FPR，保留 ft10/ft11 给指令选择临时使用。
	std::vector<int> regs = {
		0,  // ft0
		1,  // ft1
		2,  // ft2
		3,  // ft3
		4,  // ft4
		5,  // ft5
		6,  // ft6
		7,  // ft7
		10, // fa0
		11, // fa1
		12, // fa2
		13, // fa3
		14, // fa4
		15, // fa5
		16, // fa6
		17, // fa7
		28, // ft8
		29, // ft9
	};

	return regs;
}

/// @brief 构建 RVV 向量寄存器分配池。
///
/// v0 预留给后续 mask/predicate 指令，v30-v31 预留给指令选择阶段处理向量溢出和临时值。
std::vector<int> GreedyRegAllocator::buildVectorRegisterPool(Function * func) const
{
	(void) func;

	std::vector<int> regs;
	for (int reg = 1; reg <= 29; ++reg) {
		regs.push_back(reg);
	}
	return regs;
}

/// @brief 判断活跃区间是否对应float SSA值。
bool GreedyRegAllocator::isFloatInterval(LiveInterval * interval)
{
	Value * value = interval != nullptr ? interval->getVReg() : nullptr;
	return value != nullptr && value->getType() != nullptr && value->getType()->isFloatType();
}

/// @brief 判断活跃区间是否对应 RVV 向量 SSA 值。
bool GreedyRegAllocator::isVectorInterval(LiveInterval * interval)
{
	Value * value = interval != nullptr ? interval->getVReg() : nullptr;
	return value != nullptr && value->getType() != nullptr && value->getType()->isVectorType();
}

/// @brief 根据 Value 类型映射到对应的物理寄存器文件。
GreedyRegAllocator::RegisterClass GreedyRegAllocator::registerClassFor(LiveInterval * interval)
{
	if (isVectorInterval(interval)) {
		return RegisterClass::Vector;
	}
	if (isFloatInterval(interval)) {
		return RegisterClass::Fpr;
	}
	return RegisterClass::Gpr;
}

/// @brief 根据区间类型选择GPR、FPR或VR寄存器池。
const std::vector<int> & GreedyRegAllocator::registerPoolFor(LiveInterval * interval) const
{
	switch (registerClassFor(interval)) {
		case RegisterClass::Fpr:
			return availableFloatRegs;
		case RegisterClass::Vector:
			return availableVectorRegs;
		case RegisterClass::Gpr:
		default:
			return availableRegs;
	}
}

std::vector<int> GreedyRegAllocator::orderedRegisterPoolFor(LiveInterval * interval) const
{
	const auto & pool = registerPoolFor(interval);
	if (registerClassFor(interval) != RegisterClass::Gpr || pool.empty()) {
		return pool;
	}

	std::vector<int> ordered = pool;
	const bool crossesCall = intervalCrossesCall(interval);
	const bool hotPrefersCalleeSaved = shouldPreferCalleeSaved(interval);
	auto originalOrder = [&](int reg) {
		auto it = std::find(pool.begin(), pool.end(), reg);
		return it == pool.end() ? static_cast<int>(pool.size()) : static_cast<int>(it - pool.begin());
	};
	auto score = [&](int reg) {
		int cost = originalOrder(reg);
		const bool calleeSaved = isCalleeSavedGpr(reg);
		if (crossesCall) {
			// canAssignReg 会拒绝 caller-saved；这里仍把它们排到最后，方便驱逐评估更快命中合法寄存器。
			return calleeSaved ? (usedCalleeSavedGprsDuringAllocation.count(reg) ? cost : cost + 8) : cost + 10000;
		}
		if (!calleeSaved) {
			return cost;
		}
		// 打开一个新的 s-reg 需要 prologue/epilogue 一次性保存恢复；已打开的 s-reg 成本接近普通寄存器。
		const int openCost = usedCalleeSavedGprsDuringAllocation.count(reg) ? 2 : 32;
		const int hotBonus = hotPrefersCalleeSaved ? 28 : 0;
		return cost + openCost - hotBonus;
	};
	std::stable_sort(ordered.begin(), ordered.end(), [&](int lhs, int rhs) {
		return score(lhs) < score(rhs);
	});
	return ordered;
}

bool GreedyRegAllocator::isCalleeSavedGpr(int reg)
{
	return reg == 9 || (reg >= 18 && reg <= 27);
}

bool GreedyRegAllocator::shouldPreferCalleeSaved(LiveInterval * interval) const
{
	if (interval == nullptr || isForcedStackValue(interval->getVReg())) {
		return false;
	}
	if (intervalCrossesCall(interval)) {
		return true;
	}
	if (interval->maxLoopDepth <= 0) {
		return false;
	}

	const int useCount = static_cast<int>(interval->getUsePositions().size());
	const int length = std::max(1, interval->getEnd() - interval->getStart());
	if (interval->maxLoopDepth >= 2 && useCount >= 2) {
		return true;
	}
	// 一次callee-saved保存/恢复约两条指令；只有使用密度较高的热区间才优先占用s寄存器。
	return interval->maxLoopDepth >= 1 && useCount >= 4 && length <= 96;
}

/// @brief 收集同寄存器文件内的干涉寄存器。
///
/// GPR、FPR和VR都用0-31编号，编号相同不代表同一个物理资源，因此干涉集合必须按类别过滤。
std::set<int> GreedyRegAllocator::getInterferingRegsForClass(
	int node,
	const std::vector<LiveInterval *> & intervals,
	InterferenceGraph * graph,
	RegisterClass wantedClass) const
{
	std::set<int> regs;
	if (graph == nullptr) {
		return regs;
	}

	for (int neighborIdx : graph->getNeighbors(node)) {
		if (neighborIdx < 0 || neighborIdx >= static_cast<int>(intervals.size())) {
			continue;
		}

		auto * neighbor = intervals[neighborIdx];
		if (neighbor == nullptr || registerClassFor(neighbor) != wantedClass) {
			continue;
		}

		const int physReg = neighbor->getPhysReg();
		if (physReg != -1) {
			regs.insert(physReg);
		}
	}

	return regs;
}

/// @brief 判断物理寄存器是否为调用者保存寄存器
/// @param reg 物理寄存器编号
/// @return 是否会被普通函数调用 clobber
bool GreedyRegAllocator::isCallerSavedReg(int reg)
{
	return (reg >= 5 && reg <= 7) || (reg >= 10 && reg <= 17) || (reg >= 28 && reg <= 31);
}

/// @brief 判断FPR是否为caller-saved。
///
/// FPR编号按f0-f31计算，和GPR编号不能共用isCallerSavedReg的区间规则。
static bool isCallerSavedFloatReg(int reg)
{
	return (reg >= 0 && reg <= 7) || (reg >= 10 && reg <= 17) || (reg >= 28 && reg <= 31);
}

static bool sameRegAllocInfo(const RegAllocInfo & lhs, const RegAllocInfo & rhs)
{
	return lhs.regId == rhs.regId &&
	       lhs.baseRegId == rhs.baseRegId &&
	       lhs.offset == rhs.offset &&
	       lhs.hasStackSlot == rhs.hasStackSlot &&
	       lhs.isFloatReg == rhs.isFloatReg &&
	       lhs.isVectorReg == rhs.isVectorReg;
}

/// @brief 判断活跃区间是否在调用返回后仍需保持值
/// @param interval 活跃区间
/// @return 是否在调用点需要保持值
bool GreedyRegAllocator::intervalCrossesCall(LiveInterval * interval) const
{
	if (interval == nullptr || liveAcrossCallPositions.empty()) {
		return false;
	}

	Value * vreg = interval->getVReg();
	auto it = liveAcrossCallPositions.find(vreg);
	if (it == liveAcrossCallPositions.end()) {
		return false;
	}

	for (int callNum : it->second) {
		for (const auto & seg : interval->getSegments()) {
			if (seg.start <= callNum && callNum < seg.end) {
				return true;
			}
		}
	}

	return false;
}

void GreedyRegAllocator::refreshCoalescedLiveAcrossCallPositions()
{
	if (!coalescer_ || liveAcrossCallPositions.empty()) {
		return;
	}

	std::vector<std::pair<Value *, std::unordered_set<int>>> updates;
	for (const auto & [value, positions] : liveAcrossCallPositions) {
		Value * representative = getCoalescedRepresentative(value);
		if (representative != nullptr && representative != value) {
			updates.push_back({representative, positions});
		}
	}

	for (const auto & [representative, positions] : updates) {
		auto & destination = liveAcrossCallPositions[representative];
		destination.insert(positions.begin(), positions.end());
	}
}

/// @brief 判断某物理寄存器能否分配给指定活跃区间
/// @param interval 活跃区间
/// @param reg 物理寄存器编号
/// @return 是否可分配
bool GreedyRegAllocator::canAssignReg(LiveInterval * interval, int reg) const
{
	if (isVectorInterval(interval)) {
		// 当前尚未保存/恢复跨调用的向量寄存器，跨 call 的向量值统一走栈槽。
		(void) reg;
		return !intervalCrossesCall(interval);
	}
	if (isFloatInterval(interval)) {
		// 首轮FPR池只含caller-saved寄存器；跨调用值必须溢出，避免被callee clobber。
		if (!isCallerSavedFloatReg(reg)) {
			return true;
		}
		return !intervalCrossesCall(interval);
	}
	if (!isCallerSavedReg(reg)) {
		return true;
	}
	return !intervalCrossesCall(interval);
}

/// @brief 获取活跃区间在排序列表中的索引
/// @param interval 活跃区间
/// @return 索引，-1表示未找到
int GreedyRegAllocator::getIntervalIndex(LiveInterval * interval) const
{
	auto it = intervalToIndex.find(interval);
	if (it == intervalToIndex.end()) {
		return -1;
	}
	return it->second;
}

RegAllocInfo GreedyRegAllocator::getAllocationInfoAt(Value * value, int instNum) const
{
	if (value == nullptr) {
		return RegAllocInfo{};
	}

	if (splitStackValues.find(value) != splitStackValues.end()) {
		auto mapIt = allocationMap.find(value);
		return mapIt != allocationMap.end() ? mapIt->second : RegAllocInfo{};
	}

	auto segIt = allocationSegments.find(value);
	if (segIt != allocationSegments.end() && instNum >= 0) {
		for (const auto & segment : segIt->second) {
			if (segment.start <= instNum && instNum < segment.end) {
				if (segment.info.hasAnyReg()) {
					return segment.info;
				}
				auto mapIt = allocationMap.find(value);
				return mapIt != allocationMap.end() ? mapIt->second : RegAllocInfo{};
			}
		}
	}

	auto it = allocationMap.find(value);
	if (it == allocationMap.end()) {
		return RegAllocInfo{};
	}
	return it->second;
}

RegAllocInfo GreedyRegAllocator::getAllocationInfo(Value * value, Instruction * inst) const
{
	if (inst == nullptr) {
		return getAllocationInfoAt(value, 0);
	}

	auto it = instNumbering.find(inst);
	if (it == instNumbering.end()) {
		return getAllocationInfoAt(value, -1);
	}
	return getAllocationInfoAt(value, it->second);
}

RegAllocInfo GreedyRegAllocator::getRegisterHoldingValueAt(Value * value, int instNum) const
{
	if (value == nullptr || instNum < 0) {
		return RegAllocInfo{};
	}
	// 仅当存在覆盖 instNum 且分配了寄存器的活跃区间时，才认为该寄存器此刻仍持有 value。
	// 绝不回退到 allocationMap（可能是已被其它值复用的 home 寄存器），否则重新物化
	// 会从错误寄存器读到别的值。
	auto segIt = allocationSegments.find(value);
	if (segIt == allocationSegments.end()) {
		return RegAllocInfo{};
	}
	for (const auto & segment : segIt->second) {
		if (segment.start <= instNum && instNum < segment.end && segment.info.hasAnyReg()) {
			return segment.info;
		}
	}
	return RegAllocInfo{};
}

RegAllocInfo GreedyRegAllocator::getRegisterHoldingValueAt(Value * value, Instruction * inst) const
{
	if (inst == nullptr) {
		return RegAllocInfo{};
	}
	auto it = instNumbering.find(inst);
	if (it == instNumbering.end()) {
		return RegAllocInfo{};
	}
	return getRegisterHoldingValueAt(value, it->second);
}

bool GreedyRegAllocator::canRematerializeAt(Value * value, int instNum, int depth) const
{
	if (value == nullptr || instNum < 0 || depth > 2 ||
	    !RiscV64Rematerialization::isCheapRematerializable(value, depth)) {
		return false;
	}

	auto operandAvailable = [&](Value * operand) {
		return getRegisterHoldingValueAt(operand, instNum).hasReg() || canRematerializeAt(operand, instNum, depth + 1);
	};

	if (auto * gep = dynamic_cast<GetElementPtrInst *>(value)) {
		return operandAvailable(gep->getBasePointer()) && operandAvailable(gep->getIndexOperand());
	}

	if (auto * binary = dynamic_cast<BinaryInst *>(value)) {
		switch (binary->getOp()) {
			case IRInstOperator::IRINST_OP_ADD_I:
				return operandAvailable(binary->getLHS()) && operandAvailable(binary->getRHS());
			case IRInstOperator::IRINST_OP_SHL_I:
				return operandAvailable(binary->getLHS());
			default:
				return false;
		}
	}

	return true;
}

bool GreedyRegAllocator::canOmitSpillSlotForRemat(Value * value) const
{
	if (!RiscV64Rematerialization::isCheapRematerializable(value)) {
		return false;
	}

	for (Use * use : value->getUseList()) {
		auto * inst = use != nullptr ? dynamic_cast<Instruction *>(use->getUser()) : nullptr;
		if (inst == nullptr) {
			return false;
		}
		auto posIt = instNumbering.find(inst);
		if (posIt == instNumbering.end() || !canRematerializeAt(value, posIt->second)) {
			return false;
		}
	}
	return true;
}

/// @brief 从活跃区间列表重建分配映射表
/// @param intervals 活跃区间列表
///
/// 将每个活跃区间的物理寄存器分配结果写入allocationMap
void GreedyRegAllocator::rebuildAllocationMap(const std::vector<LiveInterval *> & intervals)
{
	allocationMap.clear();
	allocationSegments.clear();
	allocatedGprLiveRanges.clear();
	allocatedFprLiveRanges.clear();
	splitTransfers.clear();
	splitStackValues.clear();
	spilledValues.clear();
	rematOnlySpills.clear();
	valueUsePositions.clear();
	int estimatedReloads = 0;
	int estimatedSpillStores = 0;

	for (auto * interval: intervals) {
		if (interval == nullptr || interval->getVReg() == nullptr) {
			continue;
		}

		Value * value = interval->getVReg();
		RegAllocInfo info;
		if (interval->getPhysReg() != -1) {
			// 同一个 regId 在不同寄存器文件中含义不同，回填时必须保留类别标记。
			if (isVectorInterval(interval)) {
				info.setVectorReg(interval->getPhysReg());
				allocatedVectorLiveRanges[interval->getPhysReg()].push_back({interval->getStart(), interval->getEnd()});
			} else if (isFloatInterval(interval)) {
				info.setFloatReg(interval->getPhysReg());
				allocatedFprLiveRanges[interval->getPhysReg()].push_back({interval->getStart(), interval->getEnd()});
			} else {
				info.setReg(interval->getPhysReg());
				allocatedGprLiveRanges[interval->getPhysReg()].push_back({interval->getStart(), interval->getEnd()});
			}
		} else {
			spilledValues.insert(value);
		}

		valueUsePositions[value].insert(valueUsePositions[value].end(), interval->getUsePositions().begin(),
		                               interval->getUsePositions().end());
		allocationSegments[value].push_back({interval->getStart(), interval->getEnd(), info});
	}

	for (auto & [_, ranges] : allocatedGprLiveRanges) {
		std::sort(ranges.begin(), ranges.end());
	}
	for (auto & [_, ranges] : allocatedFprLiveRanges) {
		std::sort(ranges.begin(), ranges.end());
	}
	for (auto & [_, ranges] : allocatedVectorLiveRanges) {
		std::sort(ranges.begin(), ranges.end());
	}

	auto eraseAllocatedRange = [](auto & rangesByReg, int reg, std::pair<int, int> range) {
		auto regIt = rangesByReg.find(reg);
		if (regIt == rangesByReg.end()) {
			return;
		}
		auto & ranges = regIt->second;
		ranges.erase(std::remove(ranges.begin(), ranges.end(), range), ranges.end());
		if (ranges.empty()) {
			rangesByReg.erase(regIt);
		}
	};
	auto dropPublishedRange = [&](const RegAllocSegment & segment) {
		std::pair<int, int> range{segment.start, segment.end};
		if (segment.info.hasReg()) {
			eraseAllocatedRange(allocatedGprLiveRanges, segment.info.regId, range);
		} else if (segment.info.hasFloatReg()) {
			eraseAllocatedRange(allocatedFprLiveRanges, segment.info.regId, range);
		} else if (segment.info.hasVectorReg()) {
			eraseAllocatedRange(allocatedVectorLiveRanges, segment.info.regId, range);
		}
	};

	for (auto & [value, segments] : allocationSegments) {
		std::sort(segments.begin(), segments.end(), [](const RegAllocSegment & lhs, const RegAllocSegment & rhs) {
			if (lhs.start == rhs.start) {
				return lhs.end < rhs.end;
			}
			return lhs.start < rhs.start;
		});

		bool hasSpilledSegment = false;
		bool hasLocationChange = false;
		bool hasBaselineLocation = false;
		RegAllocInfo baselineLocation;
		for (const auto & segment : segments) {
			if (!segment.info.hasAnyReg()) {
				hasSpilledSegment = true;
			}
			if (!hasBaselineLocation) {
				baselineLocation = segment.info;
				hasBaselineLocation = true;
			} else if (!sameRegAllocInfo(baselineLocation, segment.info)) {
				hasLocationChange = true;
			}
		}

		if (hasSpilledSegment) {
			allocationMap[value] = RegAllocInfo{};
			spilledValues.insert(value);
		} else if (!segments.empty()) {
			allocationMap[value] = segments.front().info;
		}

		bool hasBoundaryTransfer = false;
		for (std::size_t i = 1; i < segments.size(); ++i) {
			const auto & prev = segments[i - 1];
			const auto & curr = segments[i];
			if (prev.end == curr.start && !sameRegAllocInfo(prev.info, curr.info)) {
				// 分裂点两侧位置不同，指令选择阶段需要在该编号处补搬运。
				splitTransfers.push_back({value, curr.start});
				hasBoundaryTransfer = true;
			}
		}
		if (hasBoundaryTransfer || hasLocationChange) {
			// Split copies are only inserted at linear adjacent boundaries. If the
			// same Value has different locations across CFG-separated segments
			// (including partial spill), use one canonical stack slot so every
			// definition and use sees the same storage. Once canonical stack is chosen,
			// do not publish unused split physical ranges to scratch liveness or
			// callee-saved save decisions.
			allocationMap[value] = RegAllocInfo{};
			splitStackValues.insert(value);
			spilledValues.insert(value);
			for (auto & segment : segments) {
				dropPublishedRange(segment);
				segment.info = RegAllocInfo{};
			}
			Value * stackValue = value;
			splitTransfers.erase(std::remove_if(splitTransfers.begin(), splitTransfers.end(),
				[stackValue](const RegAllocSplitTransfer & transfer) { return transfer.value == stackValue; }),
				splitTransfers.end());
		}
	}

	for (Value * value : spilledValues) {
		if (canOmitSpillSlotForRemat(value)) {
			rematOnlySpills.insert(value);
			continue;
		}
		auto useIt = valueUsePositions.find(value);
		if (useIt != valueUsePositions.end()) {
			estimatedReloads += static_cast<int>(useIt->second.size());
		}
		++estimatedSpillStores;
	}

	rebuildStats();
	stats.estimatedReloads = estimatedReloads;
	stats.estimatedSpillStores = estimatedSpillStores;
	refreshCoalescedAliasAllocations();
}

void GreedyRegAllocator::rebuildStats()
{
	stats = RegAllocStats{};
	stats.eliminatedCopies = coalescer_ ? static_cast<int>(coalescer_->getEliminatedCopies().size()) : 0;
	stats.splitCount = splitTransfers.size();
	stats.spilledValues = static_cast<int>(spilledValues.size());

	for (const auto & [_, segments] : allocationSegments) {
		for (const auto & segment : segments) {
			if (segment.info.hasReg()) {
				++stats.assignedRegIntervals;
				++stats.assignedGprIntervals;
			} else if (segment.info.hasFloatReg()) {
				++stats.assignedRegIntervals;
				++stats.assignedFprIntervals;
			} else if (segment.info.hasVectorReg()) {
				++stats.assignedRegIntervals;
				++stats.assignedVrIntervals;
			} else {
				++stats.spilledIntervals;
			}
		}
	}

	// reload/store 估算需要 LiveInterval 的 usePositions，由 rebuildAllocationMap
	// 在 LiveIntervalAnalysis 析构前填充。
}
