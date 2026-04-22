///
/// @file PlatformRiscV64.h
/// @brief RISC-V64平台相关头文件
///
#pragma once

#include <string>

#include "RegVariable.h"

// 在操作过程中临时借助的寄存器为RISCV64_TMP_REG_NO (t0 = x5)
#define RISCV64_TMP_REG_NO 5

// 栈寄存器SP和FP
#define RISCV64_SP_REG_NO 2
#define RISCV64_FP_REG_NO 8

// 函数跳转寄存器RA (x1)
#define RISCV64_LX_REG_NO 1

// 零寄存器X0
#define RISCV64_ZERO_REG_NO 0

// RA寄存器X1
#define RISCV64_RA_REG_NO 1

// A0寄存器X10 (返回值/第1个参数)
#define RISCV64_A0_REG_NO 10

/// @brief RISC-V64平台信息
class PlatformRiscV64 {

public:
	/// @brief 判断是否是合法的12位有符号立即数表达式
	/// RISC-V I-type立即数范围: -2048 ~ 2047
	/// @param num 立即数
	/// @return 是否合法
	static bool constExpr(int num);

	/// @brief 判定是否是合法的偏移（与constExpr一致，用于内存寻址偏移）
	/// @param num 偏移量
	/// @return 是否合法
	static bool isDisp(int num);

	/// @brief 判断是否是合法的寄存器名（支持ABI名称和x编号两种形式）
	/// @param name 寄存器名字
	/// @return 是否是合法寄存器名
	static bool isReg(std::string name);

	/// @brief 最大寄存器数目（x0-x31共32个）
	static const int maxRegNum = 32;

	/// @brief 可分配的通用寄存器个数
	/// 排除: x0(zero), x1(ra), x2(sp), x3(gp), x4(tp) = 5个保留寄存器
	/// 可用: x5-x31 = 27个通用寄存器
	/// 其中x8(s0/fp)由帧指针专用，实际可分配为26个，但此处统计包含fp
	static const int maxUsableRegNum = 27;

	/// @brief 寄存器的名字，按真实x0-x31顺序对应ABI名字
	static const std::string regName[maxRegNum];

	/// @brief 对每个物理寄存器创建一个RegVariable，供后端调用约定适配使用
	static RegVariable * intRegVal[PlatformRiscV64::maxRegNum];
};
