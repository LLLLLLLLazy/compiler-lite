///
/// @file LocalTempManager.cpp
/// @brief 动态临时寄存器管理器的实现
///
#include "LocalTempManager.h"

#include <algorithm>
#include <cstdio>

#include "Instruction.h"
#include "PlatformRiscV64.h"
#include "Value.h"

namespace {

std::vector<int> buildScratchPool(const std::vector<int> & globalPool)
{
	const std::vector<int> preferred = {
		5, 6, 7, 28, 29, 30, 31,       // t0-t6
		10, 11, 12, 13, 14, 15, 16, 17, // a0-a7
		9, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, // s1-s11
	};

	std::vector<int> result;
	for (int reg : preferred) {
		if (std::find(globalPool.begin(), globalPool.end(), reg) != globalPool.end()) {
			result.push_back(reg);
		}
	}

	if (result.empty()) {
		result = {RISCV64_TMP_REG_NO, 6, 7, 28, 29, 30, 31};
	}
	return result;
}

bool isArgumentReg(int reg)
{
	return reg >= RISCV64_A0_REG_NO && reg < RISCV64_A0_REG_NO + 8;
}

} // namespace

/// @brief 构造函数
/// @param pool 可用物理寄存器池（保留参数用于接口兼容）
/// @param allocMap 全局寄存器分配映射
/// @param instNumbering 指令编号映射
/// @param valueLiveRanges 虚拟寄存器活跃范围
LocalTempManager::LocalTempManager(
	const std::vector<int> & globalPool,
	const std::unordered_map<Value *, RegAllocInfo> & _allocMap,
	const std::map<Instruction *, int> & _instNumbering,
	const std::unordered_map<Value *, std::pair<int, int>> & _valueLiveRanges)
	: pool(buildScratchPool(globalPool)), allocMap(_allocMap), instNumbering(_instNumbering),
	  valueLiveRanges(_valueLiveRanges)
{}

/// @brief 为当前指令借用一个空闲物理寄存器
/// @param inst 当前正在翻译的IR指令
/// @param excludeReg 排除的物理寄存器编号（如dstReg）
/// @return 物理寄存器编号
///
/// 遍历scratch寄存器池，找到第一个当前尚未借出且不承载live值的寄存器。
int LocalTempManager::borrow(Instruction * inst, int excludeReg)
{
	return borrowImpl(inst, excludeReg, false);
}

/// @brief 在当前IR指令的源操作数已经消费后借用临时寄存器
/// @param inst 当前正在翻译的IR指令
/// @param excludeReg 排除的物理寄存器编号
/// @return 物理寄存器编号
int LocalTempManager::borrowAfterUses(Instruction * inst, int excludeReg)
{
	return borrowImpl(inst, excludeReg, true);
}

/// @brief 临时寄存器借用的通用实现
int LocalTempManager::borrowImpl(Instruction * inst, int excludeReg, bool afterUses)
{
	const int instNum = currentInstNum(inst);

	for (int reg : pool) {
		if (reg == excludeReg) {
			continue;
		}
		if (borrowed.find(reg) != borrowed.end()) {
			continue;
		}
		if (inst == nullptr && isArgumentReg(reg)) {
			continue;
		}
		if (instNum >= 0 && isLiveAllocatedReg(reg, instNum, afterUses)) {
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

/// @brief 查询当前指令编号
/// @param inst 当前IR指令
/// @return 指令编号，找不到则返回-1
int LocalTempManager::currentInstNum(Instruction * inst) const
{
	if (inst == nullptr) {
		return -1;
	}
	auto it = instNumbering.find(inst);
	if (it == instNumbering.end()) {
		return -1;
	}
	return it->second;
}

/// @brief 判断物理寄存器在某条IR指令处是否承载live值
/// @param reg 物理寄存器编号
/// @param instNum 指令编号
/// @param afterUses 是否允许复用最后一次使用在当前指令的寄存器
/// @return 是否不可作为临时寄存器借用
bool LocalTempManager::isLiveAllocatedReg(int reg, int instNum, bool afterUses) const
{
	for (const auto & [value, info] : allocMap) {
		if (value == nullptr || !info.hasReg() || info.regId != reg) {
			continue;
		}

		auto liveIt = valueLiveRanges.find(value);
		if (liveIt == valueLiveRanges.end()) {
			continue;
		}

		const auto & [start, end] = liveIt->second;
		if (afterUses) {
			if (start <= instNum && instNum + 1 < end) {
				return true;
			}
		} else if (start <= instNum && instNum < end) {
			return true;
		}
	}
	return false;
}
