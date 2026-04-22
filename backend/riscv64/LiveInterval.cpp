///
/// @file LiveInterval.cpp
/// @brief 活跃区间数据结构的实现
///
/// vreg关联Value*，区间由Segment组成
///

#include <algorithm>
#include <cmath>
#include <limits>

#include "LiveInterval.h"
#include "Value.h"

/// @brief 构造函数
/// @param vreg 关联的虚拟寄存器（SSA IR中的Value*）
LiveInterval::LiveInterval(Value * vreg)
	: vreg(vreg), start(std::numeric_limits<int>::max()), end(std::numeric_limits<int>::min()),
	  physReg(-1), spillWeight(0.0f), spilled(false), spillSlot(0)
{}

/// @brief 添加一个存活子段 [start, end)
/// 自动与相邻/重叠的子段合并，并更新整体区间范围
void LiveInterval::addSegment(int segStart, int segEnd)
{
	if (segStart >= segEnd) {
		return;
	}

	// 更新整体区间范围
	if (segStart < start) {
		start = segStart;
	}
	if (segEnd > end) {
		end = segEnd;
	}

	// 尝试与已有子段合并
	bool merged = false;
	for (auto it = segments.begin(); it != segments.end(); ++it) {
		// 检查是否与当前子段重叠或相邻
		if (segStart <= it->end && it->start <= segEnd) {
			// 合并：扩展当前子段范围
			if (segStart < it->start) {
				it->start = segStart;
			}
			if (segEnd > it->end) {
				it->end = segEnd;
			}
			merged = true;

			// 继续检查是否可以与后续子段合并（合并后可能跨越了原本不连续的子段）
			auto nextIt = std::next(it);
			while (nextIt != segments.end()) {
				if (it->end >= nextIt->start) {
					// 可以合并
					if (nextIt->end > it->end) {
						it->end = nextIt->end;
					}
					nextIt = segments.erase(nextIt);
				} else {
					break;
				}
			}
			break;
		}
	}

	if (!merged) {
		// 找到合适的插入位置（保持按start排序）
		auto insertPos = segments.begin();
		while (insertPos != segments.end() && insertPos->start < segStart) {
			++insertPos;
		}
		segments.insert(insertPos, Segment(segStart, segEnd));
	}
}

/// @brief 添加一个使用点
void LiveInterval::addUsePosition(int pos)
{
	usePositions.push_back(pos);
}

/// @brief 判断两个活跃区间是否干涉（任意子段重叠）
bool LiveInterval::overlaps(const LiveInterval & other) const
{
	// 快速检查：整体区间不重叠则肯定不干涉
	if (start >= other.end || other.start >= end) {
		return false;
	}

	// 逐对检查子段重叠
	for (const auto & seg : segments) {
		for (const auto & otherSeg : other.segments) {
			if (seg.overlaps(otherSeg)) {
				return true;
			}
		}
	}
	return false;
}

/// @brief 计算溢出权重
/// spillWeight = (useCount / intervalLength) * pow(10, loopDepth)
void LiveInterval::calcSpillWeight(int loopDepth)
{
	if (segments.empty()) {
		spillWeight = 0.0f;
		return;
	}

	const int intervalLength = std::max(1, end - start);
	const int useCount = static_cast<int>(usePositions.size());

	// spillWeight = (useCount / length) * 10^loopDepth
	float density = static_cast<float>(useCount) / static_cast<float>(intervalLength);
	float loopFactor = std::pow(10.0f, static_cast<float>(loopDepth));
	spillWeight = density * loopFactor;
}
