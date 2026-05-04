///
/// @file ScratchAllocator.h
/// @brief Scratch虚拟寄存器的第二遍分配器
///
/// 在指令选择完成后，为ScratchValue分配物理寄存器或溢出到栈。
///

#pragma once

#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ILocRiscV64.h"

class Instruction;
struct ScratchValue;
class Value;

/// @brief Scratch虚拟寄存器分配器
///
/// 指令选择阶段创建的ScratchValue在此阶段分配物理寄存器。
/// 分配依据：ScratchValue的机器指令范围[borrowPos, releasePos)内，
/// 找到不与已分配IR值或其他scratch值冲突的物理寄存器。
///
class ScratchAllocator {

public:
	/// @brief 为所有scratch值分配物理寄存器
	/// @param scratchValues 待分配的scratch值列表
	/// @param allocMap 主分配器的分配结果 (Value* -> RegAllocInfo)
	/// @param valueLiveRanges IR值的活跃范围 (Value* -> [IR start, IR end))
	/// @param instNumbering IR指令→编号映射
	/// @param instToMIRange IR指令→机器指令范围映射
	/// @param availRegs 可用物理寄存器池
	void allocate(
		std::vector<ScratchValue> & scratchValues,
		const std::unordered_map<Value *, RegAllocInfo> & allocMap,
		const std::unordered_map<Value *, std::pair<int, int>> & valueLiveRanges,
		const std::map<Instruction *, int> & instNumbering,
		const std::unordered_map<Instruction *, std::pair<int, int>> & instToMIRange,
		const std::vector<int> & availRegs);

private:
	/// @brief 检查物理寄存器在机器指令范围[pos, end)内是否被IR值占用
	bool isRegOccupiedByIR(int reg, int pos, int end) const;

	/// @brief 检查物理寄存器在机器指令范围[pos, end)内是否被其他scratch值占用
	bool isRegOccupiedByScratch(int reg, int pos, int end, int excludeIdx) const;

	/// @brief 可用物理寄存器池
	std::vector<int> availRegs;

	/// @brief 物理寄存器→机器指令活跃范围列表 (由IR值产生)
	/// reg -> [(miStart, miEnd), ...] 按miStart排序
	std::unordered_map<int, std::vector<std::pair<int, int>>> regIRLiveRanges;

	/// @brief 当前处理的scratchValues引用
	std::vector<ScratchValue> * scratchValuesPtr = nullptr;
};
