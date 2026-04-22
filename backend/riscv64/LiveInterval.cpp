///
/// @file LiveInterval.cpp
/// @brief 活跃区间数据结构实现
///

#include "LiveInterval.h"

#include <algorithm>
#include <cmath>

/// @brief 构造函数
/// @param _vreg 对应的IR虚拟寄存器
LiveInterval::LiveInterval(Value * _vreg)
	: vreg(_vreg), start(INT32_MAX), end(INT32_MIN), physReg(-1), spillWeight(0.0f)
{
}

/// @brief 添加存活子段，并更新区间范围
/// @param segStart 子段起始指令编号
/// @param segEnd 子段结束指令编号
void LiveInterval::addSegment(int segStart, int segEnd)
{
	segments.push_back({segStart, segEnd});

	// 更新区间范围
	if (segStart < start) {
		start = segStart;
	}
	if (segEnd > end) {
		end = segEnd;
	}
}

/// @brief 判断与另一个活跃区间是否干涉（区间重叠检查）
/// 通过比较两个区间的子段列表，检查是否存在重叠的子段
/// @param other 另一个活跃区间
/// @return true表示存在干涉（有重叠），false表示无干涉
bool LiveInterval::overlaps(LiveInterval & other)
{
	// 快速检查：如果整体区间不重叠，则子段一定不重叠
	if (this->end <= other.start || other.end <= this->start) {
		return false;
	}

	// 逐对比较子段，检查是否存在重叠
	for (auto & seg1 : this->segments) {
		for (auto & seg2 : other.segments) {
			// 两个子段重叠条件：seg1.start < seg2.end && seg2.start < seg1.end
			if (seg1.start < seg2.end && seg2.start < seg1.end) {
				return true;
			}
		}
	}

	return false;
}

/// @brief 根据使用密度和循环深度计算溢出权重
/// 计算公式：spillWeight = (useCount / intervalLength) * pow(10, loopDepth)
/// useCount: 使用次数（usePositions的大小）
/// intervalLength: 区间长度（end - start）
/// loopDepth: 最深循环嵌套深度
/// @param loopDepth 最深循环嵌套深度
void LiveInterval::calcSpillWeight(int loopDepth)
{
	int useCount = static_cast<int>(usePositions.size());
	int intervalLength = end - start;

	if (intervalLength <= 0) {
		// 区间长度为0或负数，使用默认权重
		spillWeight = static_cast<float>(useCount);
		return;
	}

	// 基础权重 = 使用次数 / 区间长度（使用密度）
	float baseWeight = static_cast<float>(useCount) / static_cast<float>(intervalLength);

	// 循环加权 = 基础权重 * 10^循环深度
	spillWeight = baseWeight * static_cast<float>(std::pow(10, loopDepth));
}

/// @brief 添加一个使用位置
/// @param pos 指令编号
void LiveInterval::addUsePosition(int pos)
{
	usePositions.push_back(pos);
}
