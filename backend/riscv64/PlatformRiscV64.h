///
/// @file PlatformRiscV64.h
/// @brief RISC-V64平台相关头文件
///
#pragma once

#include <string>

#include "RegVariable.h"

// 在操作过程中临时借助的寄存器为RISCV64_TMP_REG_NO
#define RISCV64_TMP_REG_NO 5

// 栈寄存器SP和FP
#define RISCV64_SP_REG_NO 2
#define RISCV64_FP_REG_NO 8

// 函数跳转寄存器RA
#define RISCV64_LX_REG_NO 1

// 零寄存器X0
#define RISCV64_ZERO_REG_NO 0

// RA寄存器X1
#define RISCV64_RA_REG_NO 1

// A0寄存器X10
#define RISCV64_A0_REG_NO 10

/// @brief RISC-V64平台信息
class PlatformRiscV64 {

public:
	/// @brief 判断是否是合法的立即数表达式
	/// @param num
	/// @return
	static bool constExpr(int num);

	/// @brief 判定是否是合法的偏移
	/// @param num
	/// @return
	static bool isDisp(int num);

	/// @brief 判断是否是合法的寄存器名
	/// @param name 寄存器名字
	/// @return 是否是
	static bool isReg(std::string name);

	/// @brief 最大寄存器数目
	static const int maxRegNum = 32;

	/// @brief 可使用的通用寄存器个数
	static const int maxUsableRegNum = 25;

	/// @brief 寄存器的名字，按真实x0-x31顺序对应ABI名字
	static const std::string regName[maxRegNum];

	/// @brief 对寄存器分配Value，记录位置
	static RegVariable * intRegVal[PlatformRiscV64::maxRegNum];
};
