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
#include <set>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "CallInst.h"
#include "DominatorTree.h"
#include "Function.h"
#include "HeuristicSpillStrategy.h"
#include "InterferenceGraph.h"
#include "LiveInterval.h"
#include "LiveIntervalAnalysis.h"
#include "LoopInfo.h"
#include "PlatformRiscV64.h"
#include "Value.h"

/// @brief 构造函数，初始化溢出决策策略
/// @param strategy 溢出决策策略，若为nullptr则使用默认的HeuristicSpillStrategy
GreedyRegAllocator::GreedyRegAllocator(SpillStrategy * strategy)
{
	if (strategy == nullptr) {
		// 未提供策略时，创建默认的启发式溢出策略
		ownedStrategy = std::make_unique<HeuristicSpillStrategy>();
		spillStrategy = ownedStrategy.get();
	} else {
		spillStrategy = strategy;
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
	spilledValues.clear();
	intervalToIndex.clear();
	callInstNumbers.clear();
	instNumbering.clear();
	valueLiveRanges.clear();
	frameSize = 0;
	outgoingArgBytes = 0;
	availableRegs.clear();
	availableFloatRegs.clear();

	// 内建函数不需要寄存器分配
	if (func == nullptr || func->isBuiltin()) {
		return;
	}

	// 构建可用物理寄存器池
	availableRegs = buildRegisterPool(func);
	availableFloatRegs = buildFloatRegisterPool(func);

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
	for (auto & [inst, num] : instNumbering) {
		if (dynamic_cast<CallInst *>(inst) != nullptr) {
			callInstNumbers.push_back(num);
		}
	}

	// 建立活跃区间到索引的映射
	auto & intervals = analysis.getIntervals();
	for (int i = 0; i < static_cast<int>(intervals.size()); ++i) {
		intervalToIndex[intervals[i]] = i;
	}

	// 运行Greedy分配主循环
	runGreedy(intervals, analysis.getInterferenceGraph());
	// 从活跃区间结果重建分配映射表
	rebuildAllocationMap(intervals);

	// 保存活跃性快照，供指令选择阶段的动态临时寄存器管理使用
	for (auto * interval : intervals) {
		if (interval != nullptr && interval->getVReg() != nullptr) {
			valueLiveRanges[interval->getVReg()] = {interval->getStart(), interval->getEnd()};
		}
	}
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

/// @brief 执行Greedy分配主循环
/// @param intervals 活跃区间列表
/// @param graph 干涉图
///
/// 按溢出权重从高到低排序活跃区间，依次尝试：
/// 1. 分配空闲寄存器
/// 2. 驱逐低权重区间并分配
/// 3. 标记为溢出
void GreedyRegAllocator::runGreedy(std::vector<LiveInterval *> & intervals, InterferenceGraph * graph)
{
	// 按溢出权重降序排列，权重相同则按起点升序
	std::vector<LiveInterval *> workList = intervals;
	std::sort(workList.begin(), workList.end(), [](LiveInterval * lhs, LiveInterval * rhs) {
		if (lhs->getSpillWeight() == rhs->getSpillWeight()) {
			return lhs->getStart() < rhs->getStart();
		}
		return lhs->getSpillWeight() > rhs->getSpillWeight();
	});

	for (auto * interval: workList) {
		if (interval == nullptr || interval->getVReg() == nullptr) {
			continue;
		}

		// 强制栈分配的Value直接溢出
		if (isForcedStackValue(interval->getVReg())) {
			markSpilled(interval);
			continue;
		}

		// 尝试分配空闲寄存器
		if (tryAssignFreeReg(interval, intervals, graph)) {
			continue;
		}

		// 尝试驱逐已有分配
		if (tryEvictAndAssign(interval, intervals, graph)) {
			continue;
		}

		// 无法分配，标记为溢出
		markSpilled(interval);
	}
}

/// @brief 为活跃区间分配物理寄存器
/// @param interval 活跃区间
/// @param reg 物理寄存器编号
void GreedyRegAllocator::assignPhysicalReg(LiveInterval * interval, int reg)
{
	interval->setPhysReg(reg);
	interval->setSpilled(false);
	spilledValues.erase(interval->getVReg());
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
	const bool wantFloat = isFloatInterval(interval);
	std::set<int> usedRegs = getInterferingRegsForClass(node, intervals, graph, wantFloat);
	for (int reg: registerPoolFor(interval)) {
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
	InterferenceGraph * graph)
{
	if (graph == nullptr || registerPoolFor(interval).empty()) {
		return false;
	}

	const int node = getIntervalIndex(interval);
	if (node < 0) {
		return false;
	}

	const bool wantFloat = isFloatInterval(interval);
	for (int reg: registerPoolFor(interval)) {
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
			if (neighbor == nullptr || isFloatInterval(neighbor) != wantFloat || neighbor->getPhysReg() != reg) {
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

		// 驱逐所有占用该寄存器的低权重邻居
		for (auto * victim: evictionCandidates) {
			markSpilled(victim);
		}
		assignPhysicalReg(interval, reg);
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
/// 当前只启用caller-saved FPR。跨调用存活的float值会被溢出到栈上，
/// 从而避免同时引入fs0-fs11的prologue/epilogue保存恢复逻辑。
std::vector<int> GreedyRegAllocator::buildFloatRegisterPool(Function * func) const
{
	(void) func;

	// 首轮FP寄存器分配只使用caller-saved FPR，避免新增fs*保存/恢复逻辑。
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
		30, // ft10
		31, // ft11
	};

	return regs;
}

/// @brief 判断活跃区间是否对应float SSA值。
bool GreedyRegAllocator::isFloatInterval(LiveInterval * interval)
{
	Value * value = interval != nullptr ? interval->getVReg() : nullptr;
	return value != nullptr && value->getType() != nullptr && value->getType()->isFloatType();
}

/// @brief 根据区间类型选择GPR或FPR寄存器池。
const std::vector<int> & GreedyRegAllocator::registerPoolFor(LiveInterval * interval) const
{
	return isFloatInterval(interval) ? availableFloatRegs : availableRegs;
}

/// @brief 收集同寄存器文件内的干涉寄存器。
///
/// GPR和FPR都用0-31编号，编号相同不代表同一个物理资源，因此干涉集合必须按类别过滤。
std::set<int> GreedyRegAllocator::getInterferingRegsForClass(
	int node,
	const std::vector<LiveInterval *> & intervals,
	InterferenceGraph * graph,
	bool wantFloat) const
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
		if (neighbor == nullptr || isFloatInterval(neighbor) != wantFloat) {
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

/// @brief 判断活跃区间是否覆盖任一函数调用点
/// @param interval 活跃区间
/// @return 是否在调用点需要保持值
bool GreedyRegAllocator::intervalCrossesCall(LiveInterval * interval) const
{
	if (interval == nullptr || callInstNumbers.empty()) {
		return false;
	}

	Value * vreg = interval->getVReg();
	for (int callNum : callInstNumbers) {
		// call指令自身的返回值定义在调用完成之后，不与该调用的clobber冲突。
		if (auto * inst = dynamic_cast<Instruction *>(vreg); inst != nullptr) {
			auto it = instNumbering.find(inst);
			if (it != instNumbering.end() && it->second == callNum && dynamic_cast<CallInst *>(inst) != nullptr) {
				continue;
			}
		}

		for (const auto & seg : interval->getSegments()) {
			if (seg.start <= callNum && callNum < seg.end) {
				return true;
			}
		}
	}

	return false;
}

/// @brief 判断某物理寄存器能否分配给指定活跃区间
/// @param interval 活跃区间
/// @param reg 物理寄存器编号
/// @return 是否可分配
bool GreedyRegAllocator::canAssignReg(LiveInterval * interval, int reg) const
{
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

/// @brief 从活跃区间列表重建分配映射表
/// @param intervals 活跃区间列表
///
/// 将每个活跃区间的物理寄存器分配结果写入allocationMap
void GreedyRegAllocator::rebuildAllocationMap(const std::vector<LiveInterval *> & intervals)
{
	for (auto * interval: intervals) {
		if (interval == nullptr || interval->getVReg() == nullptr) {
			continue;
		}

		RegAllocInfo info;
		if (interval->getPhysReg() != -1) {
			if (isFloatInterval(interval)) {
				info.setFloatReg(interval->getPhysReg());
			} else {
				info.setReg(interval->getPhysReg());
			}
		}
		allocationMap[interval->getVReg()] = info;
	}
}
