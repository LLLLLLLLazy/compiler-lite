///
/// @file LiveInterval.h
/// @brief 活跃区间数据结构，用于Greedy寄存器分配算法
///
#pragma once

#include <list>
#include <vector>

#include "Value.h"

/// @brief 区间子段，表示一个连续的存活范围
struct Segment {
	/// @brief 子段起始指令编号
	int start;

	/// @brief 子段结束指令编号
	int end;
};

/// @brief 活跃区间，每个虚拟寄存器对应一个活跃区间
/// 表示虚拟寄存器在程序中的存活范围，由一个或多个子段组成
class LiveInterval {

public:
	/// @brief 对应的IR虚拟寄存器
	Value * vreg;

	/// @brief 区间起点（所有子段中最小的start）
	int start;

	/// @brief 区间终点（所有子段中最大的end）
	int end;

	/// @brief 子段列表，当存活范围不连续时由多个子段组成
	std::list<Segment> segments;

	/// @brief 分配的物理寄存器编号，-1表示未分配（溢出到栈）
	int physReg;

	/// @brief 溢出权重，值越大越优先保留在寄存器中
	float spillWeight;

	/// @brief 使用位置列表，记录该虚拟寄存器在哪些指令位置被使用
	std::vector<int> usePositions;

	/// @brief 构造函数
	/// @param _vreg 对应的IR虚拟寄存器
	explicit LiveInterval(Value * _vreg);

	/// @brief 添加存活子段，并更新区间范围
	/// @param segStart 子段起始指令编号
	/// @param segEnd 子段结束指令编号
	void addSegment(int segStart, int segEnd);

	/// @brief 判断与另一个活跃区间是否干涉（区间重叠检查）
	/// @param other 另一个活跃区间
	/// @return true表示存在干涉（有重叠），false表示无干涉
	bool overlaps(LiveInterval & other);

	/// @brief 根据使用密度和循环深度计算溢出权重
/// 计算公式：spillWeight = (useCount / intervalLength) * pow(10, loopDepth)
	/// @param loopDepth 最深循环嵌套深度
	void calcSpillWeight(int loopDepth);

	/// @brief 添加一个使用位置
	/// @param pos 指令编号
	void addUsePosition(int pos);
};
