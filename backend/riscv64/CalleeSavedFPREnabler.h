///
/// @file CalleeSavedFPREnabler.h
/// @brief Callee-saved FPR 启用器，将 fs0-fs11 纳入 FPR 分配池
///
/// 启用后，跨调用存活的浮点活跃区间可分配到 callee-saved FPR，
/// 减少浮点值的溢出。prologue/epilogue 中需保存/恢复被使用的
/// callee-saved FPR。
///

#pragma once

#include <unordered_map>
#include <vector>

#include "ILocRiscV64.h"

class Value;

/// @brief Callee-saved FPR 启用器
class CalleeSavedFPREnabler {

public:
	/// @brief 构造函数
	/// @param enabled 是否启用 callee-saved FPR
	explicit CalleeSavedFPREnabler(bool enabled = false);

	/// @brief 扩展浮点寄存器池，加入 callee-saved FPR
	/// @param floatRegs 现有 FPR 池（仅 caller-saved，20个）
	/// @return 扩展后的 FPR 池（32个）；若未启用则返回原池
	std::vector<int> extendFloatPool(const std::vector<int> & floatRegs) const;

	/// @brief 判断 FPR 是否为 callee-saved
	/// @param reg FPR 编号
	/// @return 是否为 callee-saved FPR
	static bool isCalleeSavedFPR(int reg);

	/// @brief 从分配映射中收集被使用的 callee-saved FPR 列表
	/// @param allocMap 寄存器分配映射表
	/// @return 被使用的 callee-saved FPR 编号列表（升序）
	static std::vector<int> collectUsedCalleeSavedFPRs(
		const std::unordered_map<Value *, RegAllocInfo> & allocMap);

private:
	bool enabled_;

	/// @brief callee-saved FPR 编号列表：fs0=8, fs1=9, fs2-fs11=18-27
	static const std::vector<int> CALLEE_SAVED_FPRS;
};
