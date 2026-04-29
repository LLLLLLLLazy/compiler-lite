///
/// @file LocalTempManager.cpp
/// @brief 动态临时寄存器管理器的实现
///
#include "LocalTempManager.h"

#include <cstdio>

#include "Instruction.h"
#include "PlatformRiscV64.h"
#include "Value.h"

/// @brief 构造函数
/// @param pool 可用物理寄存器池（保留参数用于接口兼容）
/// @param allocMap 全局寄存器分配映射
/// @param instNumbering 指令编号映射
/// @param valueLiveRanges 虚拟寄存器活跃范围
LocalTempManager::LocalTempManager(
	const std::vector<int> & globalPool,
	const std::unordered_map<Value *, RegAllocInfo> & allocMap,
	const std::map<Instruction *, int> & instNumbering,
	const std::unordered_map<Value *, std::pair<int, int>> & valueLiveRanges)
	: pool({RISCV64_TMP_REG_NO, 6, 7, 28, 29, 30, 31})
{
	(void) globalPool;
	(void) allocMap;
	(void) instNumbering;
	(void) valueLiveRanges;
}

/// @brief 为当前指令借用一个空闲物理寄存器
/// @param inst 当前正在翻译的IR指令
/// @param excludeReg 排除的物理寄存器编号（如dstReg）
/// @return 物理寄存器编号
///
/// 遍历专用scratch寄存器池，找到第一个当前尚未借出的寄存器。
/// t0-t6不参与全局寄存器分配，因此无需查询虚拟寄存器活跃区间。
int LocalTempManager::borrow(Instruction * inst, int excludeReg)
{
	(void) inst;

	for (int reg : pool) {
		if (reg == excludeReg) {
			continue;
		}
		if (borrowed.find(reg) != borrowed.end()) {
			continue;
		}
		borrowed.insert(reg);
		return reg;
	}

	// 极端情况：所有寄存器都被借出或排除，不应发生
	std::fprintf(stderr, "LocalTempManager: 无可用的临时寄存器！\n");
	return excludeReg >= 0 ? excludeReg : pool[0];
}

/// @brief 归还借用的寄存器
/// @param reg 物理寄存器编号
void LocalTempManager::release(int reg)
{
	borrowed.erase(reg);
}
