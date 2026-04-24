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
#include "Function.h"
#include "HeuristicSpillStrategy.h"
#include "InterferenceGraph.h"
#include "LiveInterval.h"
#include "LiveIntervalAnalysis.h"
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
	frameSize = 0;
	outgoingArgBytes = 0;

	// 内建函数不需要寄存器分配
	if (func == nullptr || func->isBuiltin()) {
		return;
	}

	// 构建可用物理寄存器池
	availableRegs = buildRegisterPool(func);

	// 执行活跃区间分析
	LiveIntervalAnalysis analysis(func);
	analysis.run();

	// 建立活跃区间到索引的映射
	auto & intervals = analysis.getIntervals();
	for (int i = 0; i < static_cast<int>(intervals.size()); ++i) {
		intervalToIndex[intervals[i]] = i;
	}

	// 运行Greedy分配主循环
	runGreedy(intervals, analysis.getInterferenceGraph());
	// 从活跃区间结果重建分配映射表
	rebuildAllocationMap(intervals);
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
	if (graph == nullptr || availableRegs.empty()) {
		return false;
	}

	const int node = getIntervalIndex(interval);
	if (node < 0) {
		return false;
	}

	// 获取当前区间所有干涉邻居已占用的寄存器集合
	std::set<int> usedRegs = graph->getInterferingRegs(node, intervals);
	for (int reg: availableRegs) {
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
	if (graph == nullptr || availableRegs.empty()) {
		return false;
	}

	const int node = getIntervalIndex(interval);
	if (node < 0) {
		return false;
	}

	for (int reg: availableRegs) {
		std::vector<LiveInterval *> evictionCandidates;
		bool canUseReg = true;

		// 检查占用该寄存器的所有干涉邻居
		for (int neighborIdx: graph->getNeighbors(node)) {
			if (neighborIdx < 0 || neighborIdx >= static_cast<int>(intervals.size())) {
				continue;
			}

			auto * neighbor = intervals[neighborIdx];
			if (neighbor == nullptr || neighbor->getPhysReg() != reg) {
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
/// 默认使用callee-saved寄存器s1-s11（编号9, 18-27）
/// 若函数不含调用指令，还可使用caller-saved寄存器a0-a7和t0-t5
std::vector<int> GreedyRegAllocator::buildRegisterPool(Function * func) const
{
	const bool hasCall = functionHasCall(func);

	// callee-saved寄存器：s1-s11，在函数调用后仍保持值
	std::vector<int> regs = {
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
	};

	// 无函数调用时，caller-saved寄存器也可使用
	if (!hasCall) {
		regs.insert(regs.end(), {10, 11, 12, 13, 14, 15, 16, 17, 28, 29, 30, 31});
	}

	return regs;
}

/// @brief 判断函数是否包含函数调用指令
/// @param func 待判断的函数
/// @return 是否包含调用
bool GreedyRegAllocator::functionHasCall(Function * func) const
{
	for (auto * bb: func->getBlocks()) {
		for (auto * inst: bb->getInstructions()) {
			if (dynamic_cast<CallInst *>(inst) != nullptr) {
				return true;
			}
		}
	}
	return false;
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
			info.setReg(interval->getPhysReg());
		}
		allocationMap[interval->getVReg()] = info;
	}
}
