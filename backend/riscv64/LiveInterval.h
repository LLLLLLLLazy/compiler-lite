///
/// @file LiveInterval.h
/// @brief 活跃区间数据结构，用于Greedy寄存器分配
///
/// - vreg关联SSA IR中的Value*（虚拟寄存器）
/// - 区间由多个不连续的Segment组成（支持分支/循环中的活跃范围）
/// - spillWeight考虑循环深度加权
///

#pragma once

#include <cstdint>
#include <list>
#include <vector>

class Value;

/// @brief 活跃区间的一个连续子段 [start, end)
struct Segment {
	int start; ///< 子段起始指令编号（含）
	int end;   ///< 子段结束指令编号（不含）

	Segment(int s, int e) : start(s), end(e) {}

	/// 判断两个子段是否重叠
	bool overlaps(const Segment & other) const
	{
		return start < other.end && other.start < end;
	}
};

/// @brief 活跃区间，表示一个虚拟寄存器在整个函数中的存活范围
class LiveInterval {

public:
	/// @brief 构造函数
	/// @param vreg 关联的虚拟寄存器（SSA IR中的Value*）
	explicit LiveInterval(Value * vreg);

	/// @brief 添加一个存活子段 [start, end)
	/// 自动与相邻/重叠的子段合并，并更新整体区间范围
	/// @param start 子段起始指令编号
	/// @param end 子段结束指令编号
	void addSegment(int start, int end);

	/// @brief 添加一个使用点（指令编号）
	/// 使用点用于精确计算溢出权重
	/// @param pos 指令编号
	void addUsePosition(int pos);

	/// @brief 判断两个活跃区间是否干涉（任意子段重叠）
	/// @param other 另一个活跃区间
	/// @return 是否干涉
	bool overlaps(const LiveInterval & other) const;

	/// @brief 计算溢出权重
	/// spillWeight = (useCount / intervalLength) * pow(10, loopDepth)
	/// 权重越高越不应溢出（越应分配物理寄存器）
	/// @param loopDepth 所在循环的嵌套深度（0=不在循环中）
	void calcSpillWeight(int loopDepth = 0);

	/// @brief 获取关联的虚拟寄存器
	/// @return Value*
	Value * getVReg() const { return vreg; }

	/// @brief 获取区间起始位置
	/// @return 起始指令编号
	int getStart() const { return start; }

	/// @brief 获取区间结束位置
	/// @return 结束指令编号
	int getEnd() const { return end; }

	/// @brief 获取分配的物理寄存器编号
	/// @return 物理寄存器编号，-1表示未分配
	int getPhysReg() const { return physReg; }

	/// @brief 设置分配的物理寄存器
	/// @param reg 物理寄存器编号
	void setPhysReg(int reg) { physReg = reg; }

	/// @brief 获取溢出权重
	/// @return 溢出权重
	float getSpillWeight() const { return spillWeight; }

	/// @brief 判断是否已被溢出
	/// @return 是否溢出
	bool isSpilled() const { return spilled; }

	/// @brief 标记为溢出
	/// @param spill 是否溢出
	void setSpilled(bool spill = true) { spilled = spill; }

	/// @brief 获取溢出栈偏移
	/// @return 栈偏移
	int64_t getSpillSlot() const { return spillSlot; }

	/// @brief 设置溢出栈偏移
	/// @param slot 栈偏移
	void setSpillSlot(int64_t slot) { spillSlot = slot; }

	/// @brief 获取存活子段列表
	/// @return 子段列表引用
	const std::list<Segment> & getSegments() const { return segments; }

	/// @brief 获取使用点列表
	/// @return 使用点列表引用
	const std::vector<int> & getUsePositions() const { return usePositions; }

	/// 关联的虚拟寄存器（SSA IR中的Value*）
	Value * vreg;

	/// 整体区间范围
	int start; ///< 最早起始位置
	int end;   ///< 最晚结束位置

	/// 存活子段列表（可能不连续，如分支/循环中）
	std::list<Segment> segments;

	/// 使用点列表（指令编号）
	std::vector<int> usePositions;

	/// 分配的物理寄存器编号，-1表示未分配
	int physReg;

	/// 溢出权重
	float spillWeight;

	/// 是否已被溢出
	bool spilled;

	/// 溢出栈偏移
	int64_t spillSlot;

	/// 该变量所在的最大循环深度（由LiveIntervalAnalysis根据LoopInfo填入）
	int maxLoopDepth = 0;
};
