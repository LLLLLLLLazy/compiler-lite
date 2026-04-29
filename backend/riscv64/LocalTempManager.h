///
/// @file LocalTempManager.h
/// @brief 动态临时寄存器管理器，用于指令选择阶段的scratch寄存器分配
///
/// 指令选择在翻译单条IR指令时需要临时寄存器（如加载左右操作数、计算地址等）。
/// t0-t6由本管理器专用，按单条IR指令内的借用/归还关系动态分配，
/// 避免硬编码某个固定scratch导致同一指令内部互相覆盖。
///
#pragma once

#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ILocRiscV64.h"

class Function;
class Instruction;
class Value;

/// @brief 动态临时寄存器管理器
///
/// 在指令选择过程中管理临时寄存器的借用与归还：
/// - borrow(): 从t0-t6中查找当前尚未借出的物理寄存器
/// - release(): 归还借用的寄存器
///
/// 所有借用必须在同一条IR指令的翻译结束前归还（即不能跨translate_*调用）。
///
class LocalTempManager {

public:
	/// @brief 构造函数
	/// @param pool 全局可用物理寄存器池（保留参数用于接口兼容；scratch固定为t0-t6）
	/// @param allocMap 全局寄存器分配映射（Value* -> RegAllocInfo）
	/// @param instNumbering 指令编号映射（Instruction* -> 编号）
	/// @param valueLiveRanges 虚拟寄存器活跃范围（Value* -> [start, end)）
	LocalTempManager(
		const std::vector<int> & pool,
		const std::unordered_map<Value *, RegAllocInfo> & allocMap,
		const std::map<Instruction *, int> & instNumbering,
		const std::unordered_map<Value *, std::pair<int, int>> & valueLiveRanges);

	/// @brief 为当前指令借用一个空闲物理寄存器
	/// @param inst 当前正在翻译的IR指令
	/// @param excludeReg 排除的物理寄存器编号（如dstReg），-1表示不排除
	/// @return 物理寄存器编号
	int borrow(Instruction * inst, int excludeReg = -1);

	/// @brief 归还借用的寄存器
	/// @param reg 物理寄存器编号
	void release(int reg);

	/// @brief 检查所有借出的寄存器是否已全部归还
	/// @return 是否全部归还
	bool allReleased() const { return borrowed.empty(); }

private:
	/// @brief scratch物理寄存器池（t0-t6）
	std::vector<int> pool;

	/// @brief 当前被借出的寄存器集合
	std::unordered_set<int> borrowed;
};
