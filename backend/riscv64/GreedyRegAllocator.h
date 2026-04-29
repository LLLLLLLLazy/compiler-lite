///
/// @file GreedyRegAllocator.h
/// @brief RISCV64 Greedy寄存器分配器的头文件
///
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ILocRiscV64.h"
#include "SpillStrategy.h"

class Function;
class Instruction;
class InterferenceGraph;
class LiveInterval;
class Value;

/// @brief RISCV64 Greedy寄存器分配器
///
/// 基于线性扫描的Greedy寄存器分配算法，流程：
/// 1. 计算活跃区间
/// 2. 构建干涉图
/// 3. 按活跃区间起点排序，依次尝试分配物理寄存器
/// 4. 若无空闲寄存器，尝试驱逐已有分配（evict）
/// 5. 驱逐失败则标记为溢出（spill）
///
class GreedyRegAllocator {

public:
	/// @brief 构造函数
	/// @param strategy 溢出决策策略，若为nullptr则使用默认的HeuristicSpillStrategy
	explicit GreedyRegAllocator(SpillStrategy * strategy = nullptr);

	/// @brief 对函数执行寄存器分配
	/// @param func 待分配的函数
	void allocate(Function * func);

	/// @brief 获取虚拟寄存器分配的物理寄存器编号
	/// @param vreg 虚拟寄存器（Value*）
	/// @return 物理寄存器编号，-1表示未分配
	int getAllocation(Value * vreg) const;

	/// @brief 判断虚拟寄存器是否被溢出
	/// @param vreg 虚拟寄存器（Value*）
	/// @return 是否被溢出
	bool isSpilled(Value * vreg) const;

	/// @brief 获取溢出变量的栈槽偏移
	/// @param vreg 虚拟寄存器（Value*）
	/// @return 栈槽偏移量
	int64_t getSpillSlot(Value * vreg) const;

	/// @brief 获取完整的寄存器分配映射表（只读）
	/// @return Value* -> RegAllocInfo 的映射表
	const std::unordered_map<Value *, RegAllocInfo> & getAllocationMap() const
	{
		return allocationMap;
	}

	/// @brief 获取完整的寄存器分配映射表（可修改）
	/// @return Value* -> RegAllocInfo 的映射表
	std::unordered_map<Value *, RegAllocInfo> & getAllocationMap()
	{
		return allocationMap;
	}

	/// @brief 判断某个Value是否已有分配信息
	/// @param val 待判断的Value
	/// @return 是否已有分配
	bool hasAllocation(Value * val) const;

	/// @brief 获取栈帧大小
	/// @return 栈帧字节数
	int getFrameSize() const
	{
		return frameSize;
	}

	/// @brief 设置栈帧大小
	/// @param size 栈帧字节数
	void setFrameSize(int size)
	{
		frameSize = size;
	}

	/// @brief 获取超出寄存器传递的调用参数占用字节数
	/// @return 字节数
	int getOutgoingArgBytes() const
	{
		return outgoingArgBytes;
	}

	/// @brief 设置超出寄存器传递的调用参数占用字节数
	/// @param bytes 字节数
	void setOutgoingArgBytes(int bytes)
	{
		outgoingArgBytes = bytes;
	}

	/// @brief 获取可用寄存器池
	/// @return 寄存器编号列表
	const std::vector<int> & getAvailableRegs() const
	{
		return availableRegs;
	}

	/// @brief 获取指令编号映射（Instruction* -> 编号）
	/// @return 指令编号映射
	const std::map<class Instruction *, int> & getInstNumbering() const
	{
		return instNumbering;
	}

	/// @brief 获取虚拟寄存器活跃范围映射（Value* -> [start, end)）
	/// @return 活跃范围映射
	const std::unordered_map<class Value *, std::pair<int, int>> & getValueLiveRanges() const
	{
		return valueLiveRanges;
	}

	/// @brief 判断Value是否必须分配在栈上（如AllocaInst、全局变量等）
	/// @param val 待判断的Value
	/// @return 是否必须分配在栈上
	static bool isForcedStackValue(Value * val);

private:
	/// @brief 执行Greedy分配主循环
	/// @param intervals 按起点排序的活跃区间列表
	/// @param graph 干涉图
	void runGreedy(std::vector<LiveInterval *> & intervals, InterferenceGraph * graph);

	/// @brief 为活跃区间分配物理寄存器
	/// @param interval 活跃区间
	/// @param reg 物理寄存器编号
	void assignPhysicalReg(LiveInterval * interval, int reg);

	/// @brief 尝试为活跃区间分配空闲寄存器
	/// @param interval 待分配的活跃区间
	/// @param intervals 所有活跃区间
	/// @param graph 干涉图
	/// @return 是否分配成功
	bool tryAssignFreeReg(LiveInterval * interval,
		const std::vector<LiveInterval *> & intervals,
		InterferenceGraph * graph);

	/// @brief 尝试驱逐已有分配并为当前区间分配寄存器
	/// @param interval 待分配的活跃区间
	/// @param intervals 所有活跃区间
	/// @param graph 干涉图
	/// @return 是否驱逐并分配成功
	bool tryEvictAndAssign(LiveInterval * interval,
		std::vector<LiveInterval *> & intervals,
		InterferenceGraph * graph);

	/// @brief 将活跃区间标记为溢出
	/// @param interval 待溢出的活跃区间
	void markSpilled(LiveInterval * interval);

	/// @brief 构建可用物理寄存器池
	/// @param func 当前函数
	/// @return 可用寄存器编号列表
	std::vector<int> buildRegisterPool(Function * func) const;

	/// @brief 判断函数是否包含函数调用
	/// @param func 待判断的函数
	/// @return 是否包含调用
	bool functionHasCall(Function * func) const;

	/// @brief 获取活跃区间在排序列表中的索引
	/// @param interval 活跃区间
	/// @return 索引
	int getIntervalIndex(LiveInterval * interval) const;

	/// @brief 从活跃区间列表重建分配映射表
	/// @param intervals 活跃区间列表
	void rebuildAllocationMap(const std::vector<LiveInterval *> & intervals);

	/// @brief 拥有的溢出策略（当外部未提供时使用默认策略）
	std::unique_ptr<SpillStrategy> ownedStrategy;

	/// @brief 溢出决策策略指针
	SpillStrategy * spillStrategy = nullptr;

	/// @brief 可用物理寄存器编号列表
	std::vector<int> availableRegs;

	/// @brief 寄存器分配映射表：Value* -> RegAllocInfo
	std::unordered_map<Value *, RegAllocInfo> allocationMap;

	/// @brief 被溢出的Value集合
	std::unordered_set<Value *> spilledValues;

	/// @brief 活跃区间到排序列表索引的映射
	std::unordered_map<LiveInterval *, int> intervalToIndex;

	/// @brief 栈帧大小
	int frameSize = 0;

	/// @brief 超出寄存器传递的调用参数占用字节数
	int outgoingArgBytes = 0;

	/// @brief 指令编号映射（Instruction* -> 编号），用于局部临时寄存器分配
	std::map<class Instruction *, int> instNumbering;

	/// @brief 虚拟寄存器活跃范围（Value* -> [start, end)），用于局部临时寄存器分配
	std::unordered_map<class Value *, std::pair<int, int>> valueLiveRanges;
};
