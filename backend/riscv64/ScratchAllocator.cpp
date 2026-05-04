///
/// @file ScratchAllocator.cpp
/// @brief Scratch虚拟寄存器分配器的实现
///

#include "ScratchAllocator.h"

#include <algorithm>

#include "Instruction.h"
#include "LocalTempManager.h"
#include "Value.h"

/// @brief 为所有scratch值分配物理寄存器
void ScratchAllocator::allocate(
	std::vector<ScratchValue> & scratchValues,
	const std::unordered_map<Value *, RegAllocInfo> & allocMap,
	const std::unordered_map<Value *, std::pair<int, int>> & valueLiveRanges,
	const std::map<Instruction *, int> & instNumbering,
	const std::unordered_map<Instruction *, std::pair<int, int>> & instToMIRange,
	const std::vector<int> & _availRegs)
{
	if (scratchValues.empty()) {
		return;
	}

	this->availRegs = _availRegs;
	this->scratchValuesPtr = &scratchValues;

	// 构建 IR指令编号 → 机器指令范围 的映射
	// instNumbering: Instruction* -> IR编号
	// instToMIRange: Instruction* -> [miStart, miEnd)
	// 合并得到: IR编号 -> [miStart, miEnd)
	std::vector<std::pair<int, int>> irNumToMIRange; // 索引 = IR指令编号
	int maxIRNum = 0;
	for (const auto & [inst, num] : instNumbering) {
		maxIRNum = std::max(maxIRNum, num);
	}
	irNumToMIRange.resize(maxIRNum + 1, {-1, -1});
	for (const auto & [inst, num] : instNumbering) {
		auto miIt = instToMIRange.find(inst);
		if (miIt != instToMIRange.end()) {
			irNumToMIRange[num] = miIt->second;
		}
	}

	// 构建每个物理寄存器的机器指令活跃范围
	// 对每个分配了物理寄存器的IR值V:
	//   V的IR活跃范围 = [irStart, irEnd)
	//   V的机器指令活跃范围 = irNumToMIRange[irStart..irEnd-1] 的并集
	regIRLiveRanges.clear();
	for (const auto & [value, info] : allocMap) {
		if (!info.hasReg()) {
			continue;
		}
		auto vlrIt = valueLiveRanges.find(value);
		if (vlrIt == valueLiveRanges.end()) {
			continue;
		}
		int irStart = vlrIt->second.first;
		int irEnd = vlrIt->second.second;

		// 收集 [irStart, irEnd) 范围内所有IR指令的机器指令范围
		int miStart = -1;
		int miEnd = -1;
		for (int irNum = irStart; irNum < irEnd; ++irNum) {
			if (irNum >= 0 && irNum < static_cast<int>(irNumToMIRange.size())) {
				const auto & miRange = irNumToMIRange[irNum];
				if (miRange.first >= 0) {
					if (miStart < 0 || miRange.first < miStart) {
						miStart = miRange.first;
					}
					if (miRange.second > miEnd) {
						miEnd = miRange.second;
					}
				}
			}
		}

		if (miStart >= 0 && miEnd > miStart) {
			regIRLiveRanges[info.regId].push_back({miStart, miEnd});
		}
	}

	// 对每个寄存器的活跃范围排序并合并重叠区间
	for (auto & [reg, ranges] : regIRLiveRanges) {
		std::sort(ranges.begin(), ranges.end());
		// 合并重叠区间
		std::vector<std::pair<int, int>> merged;
		for (const auto & r : ranges) {
			if (!merged.empty() && merged.back().second >= r.first) {
				merged.back().second = std::max(merged.back().second, r.second);
			} else {
				merged.push_back(r);
			}
		}
		ranges = std::move(merged);
	}

	// 按 borrowPos 排序 scratch 值的索引
	std::vector<int> sortedIndices(scratchValues.size());
	for (int i = 0; i < static_cast<int>(scratchValues.size()); ++i) {
		sortedIndices[i] = i;
	}
	std::sort(sortedIndices.begin(), sortedIndices.end(), [&](int a, int b) {
		return scratchValues[a].borrowPos < scratchValues[b].borrowPos;
	});

	// 逐个分配 scratch 值
	// borrowImpl 已通过 liveness-aware 逻辑选择了不与活跃IR值冲突的物理寄存器，
	// 因此直接使用 originalPhysReg，无需重新分配（避免 patchScratchRegs 的字符串替换问题）。
	for (auto & sv : scratchValues) {
		if (!sv.released || sv.borrowPos < 0 || sv.releasePos <= sv.borrowPos) {
			continue;
		}

		// 验证 originalPhysReg 在 borrow 范围内是否与 IR 值冲突
		if (!isRegOccupiedByIR(sv.originalPhysReg, sv.borrowPos, sv.releasePos)) {
			sv.physicalReg = sv.originalPhysReg;
		} else {
			// 理论上不应该发生（borrowImpl 已检查过），标记为 spill
			sv.spilled = true;
		}
	}
}

/// @brief 检查物理寄存器在机器指令范围[pos, end)内是否被IR值占用
bool ScratchAllocator::isRegOccupiedByIR(int reg, int pos, int end) const
{
	auto it = regIRLiveRanges.find(reg);
	if (it == regIRLiveRanges.end()) {
		return false;
	}

	const auto & ranges = it->second;
	// 二分查找第一个 start >= pos 的区间
	auto iter = std::lower_bound(ranges.begin(), ranges.end(), pos,
		[](const std::pair<int, int> & seg, int val) { return seg.first < val; });

	// 检查前一个区间是否与 [pos, end) 重叠
	if (iter != ranges.begin()) {
		const auto & prev = *(iter - 1);
		if (prev.second > pos) {
			return true;
		}
	}

	// 检查当前及后续区间
	for (; iter != ranges.end() && iter->first < end; ++iter) {
		return true;
	}

	return false;
}

/// @brief 检查物理寄存器在机器指令范围[pos, end)内是否被其他scratch值占用
bool ScratchAllocator::isRegOccupiedByScratch(int reg, int pos, int end, int excludeIdx) const
{
	if (scratchValuesPtr == nullptr) {
		return false;
	}

	for (int i = 0; i < static_cast<int>(scratchValuesPtr->size()); ++i) {
		if (i == excludeIdx) {
			continue;
		}
		const auto & other = (*scratchValuesPtr)[i];
		if (other.physicalReg != reg || other.spilled) {
			continue;
		}
		if (other.borrowPos < end && other.releasePos > pos) {
			return true;
		}
	}
	return false;
}
