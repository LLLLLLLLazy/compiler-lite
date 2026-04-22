///
/// @file PlatformRiscV64.cpp
/// @brief RISC-V64平台相关实现
///

#include "PlatformRiscV64.h"

#include "IntegerType.h"

const std::string PlatformRiscV64::regName[PlatformRiscV64::maxRegNum] = {
	"zero", // x0, 恒为0
	"ra",   // x1, 返回地址寄存器
	"sp",   // x2, 栈指针寄存器
	"gp",   // x3, 全局指针寄存器
	"tp",   // x4, 线程指针寄存器
	"t0",   // x5, 临时寄存器
	"t1",   // x6, 临时寄存器
	"t2",   // x7, 临时寄存器
	"s0",   // x8, 帧指针寄存器(s0/fp)
	"s1",   // x9, 被调用者保存寄存器
	"a0",   // x10, 返回值/第1个参数
	"a1",   // x11, 第2个参数
	"a2",   // x12, 第3个参数
	"a3",   // x13, 第4个参数
	"a4",   // x14, 第5个参数
	"a5",   // x15, 第6个参数
	"a6",   // x16, 第7个参数
	"a7",   // x17, 第8个参数
	"s2",   // x18, 被调用者保存寄存器
	"s3",   // x19, 被调用者保存寄存器
	"s4",   // x20, 被调用者保存寄存器
	"s5",   // x21, 被调用者保存寄存器
	"s6",   // x22, 被调用者保存寄存器
	"s7",   // x23, 被调用者保存寄存器
	"s8",   // x24, 被调用者保存寄存器
	"s9",   // x25, 被调用者保存寄存器
	"s10",  // x26, 被调用者保存寄存器
	"s11",  // x27, 被调用者保存寄存器
	"t3",   // x28, 临时寄存器
	"t4",   // x29, 临时寄存器
	"t5",   // x30, 临时寄存器
	"t6",   // x31, 临时寄存器
};

RegVariable * PlatformRiscV64::intRegVal[PlatformRiscV64::maxRegNum] = {
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[0], 0),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[1], 1),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[2], 2),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[3], 3),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[4], 4),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[5], 5),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[6], 6),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[7], 7),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[8], 8),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[9], 9),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[10], 10),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[11], 11),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[12], 12),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[13], 13),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[14], 14),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[15], 15),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[16], 16),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[17], 17),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[18], 18),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[19], 19),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[20], 20),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[21], 21),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[22], 22),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[23], 23),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[24], 24),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[25], 25),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[26], 26),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[27], 27),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[28], 28),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[29], 29),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[30], 30),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscV64::regName[31], 31),
};

/// @brief 判断是否是合法的立即数表达式
/// RISC-V I-type: 12位有符号立即数, 范围 -2048 ~ 2047
/// @param num
/// @return
bool PlatformRiscV64::constExpr(int num)
{
	return num >= -2048 && num <= 2047;
}

/// @brief 判定是否是合法的偏移
/// 与constExpr一致，用于load/store的offset(base)寻址
/// @param num
/// @return
bool PlatformRiscV64::isDisp(int num)
{
	return num >= -2048 && num <= 2047;
}

/// @brief 判断是否是合法的寄存器名
/// 支持两种命名: x编号(x0-x31)和ABI名称(zero/ra/sp/gp/tp/t0-t6/s0-s11/a0-a7/fp)
/// @param name 寄存器名字
/// @return 是否是
bool PlatformRiscV64::isReg(std::string name)
{
	return name == "x0" || name == "x1" || name == "x2" || name == "x3" || name == "x4" || name == "x5" ||
		   name == "x6" || name == "x7" || name == "x8" || name == "x9" || name == "x10" || name == "x11" ||
		   name == "x12" || name == "x13" || name == "x14" || name == "x15" || name == "x16" || name == "x17" ||
		   name == "x18" || name == "x19" || name == "x20" || name == "x21" || name == "x22" || name == "x23" ||
		   name == "x24" || name == "x25" || name == "x26" || name == "x27" || name == "x28" || name == "x29" ||
		   name == "x30" || name == "x31" || name == "zero" || name == "ra" || name == "sp" || name == "gp" ||
		   name == "tp" || name == "t0" || name == "t1" || name == "t2" || name == "s0" || name == "fp" ||
		   name == "s1" || name == "a0" || name == "a1" || name == "a2" || name == "a3" || name == "a4" ||
		   name == "a5" || name == "a6" || name == "a7" || name == "s2" || name == "s3" || name == "s4" ||
		   name == "s5" || name == "s6" || name == "s7" || name == "s8" || name == "s9" || name == "s10" ||
		   name == "s11" || name == "t3" || name == "t4" || name == "t5" || name == "t6";
}
