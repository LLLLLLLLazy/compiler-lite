///
/// @file CalleeSavedFPREnabler.cpp
/// @brief Callee-saved FPR 启用器的实现
///

#include "CalleeSavedFPREnabler.h"

#include <algorithm>

/// @brief callee-saved FPR 编号列表
/// fs0=8, fs1=9, fs2=18, fs3=19, fs4=20, fs5=21,
/// fs6=22, fs7=23, fs8=24, fs9=25, fs10=26, fs11=27
const std::vector<int> CalleeSavedFPREnabler::CALLEE_SAVED_FPRS = {
	8, 9, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
};

/// @brief 构造函数
/// @param enabled 是否启用 callee-saved FPR
CalleeSavedFPREnabler::CalleeSavedFPREnabler(bool enabled)
	: enabled_(enabled)
{
}

/// @brief 扩展浮点寄存器池，加入 callee-saved FPR
/// @param floatRegs 现有 FPR 池
/// @return 扩展后的 FPR 池；若未启用则返回原池
std::vector<int> CalleeSavedFPREnabler::extendFloatPool(const std::vector<int> & floatRegs) const
{
	if (!enabled_) {
		return floatRegs;
	}

	std::vector<int> extended = floatRegs;
	for (int reg : CALLEE_SAVED_FPRS) {
		extended.push_back(reg);
	}
	return extended;
}

/// @brief 判断 FPR 是否为 callee-saved
/// @param reg FPR 编号
/// @return 是否为 callee-saved FPR
bool CalleeSavedFPREnabler::isCalleeSavedFPR(int reg)
{
	for (int r : CALLEE_SAVED_FPRS) {
		if (r == reg) {
			return true;
		}
	}
	return false;
}

/// @brief 从分配映射中收集被使用的 callee-saved FPR 列表
/// @param allocMap 寄存器分配映射表
/// @return 被使用的 callee-saved FPR 编号列表（升序）
std::vector<int> CalleeSavedFPREnabler::collectUsedCalleeSavedFPRs(
	const std::unordered_map<Value *, RegAllocInfo> & allocMap)
{
	std::vector<int> usedFPRs;
	for (const auto & [_, info] : allocMap) {
		if (info.hasFloatReg() && isCalleeSavedFPR(info.regId)) {
			usedFPRs.push_back(info.regId);
		}
	}
	// 去重并排序
	std::sort(usedFPRs.begin(), usedFPRs.end());
	usedFPRs.erase(std::unique(usedFPRs.begin(), usedFPRs.end()), usedFPRs.end());
	return usedFPRs;
}
