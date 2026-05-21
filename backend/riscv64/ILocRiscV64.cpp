///
/// @file ILocRiscV64.cpp
/// @brief RISC-V64指令序列管理的实现
///

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

#include "ILocRiscV64.h"
#include "AllocaInst.h"
#include "Common.h"
#include "Function.h"
#include "LocalTempManager.h"
#include "PlatformRiscV64.h"
#include "Module.h"
#include "ConstInteger.h"
#include "ConstFloat.h"
#include "GlobalVariable.h"

namespace {

/// @brief 判断指令是否为内部标签定义指令（以.L开头的标签）
bool isInternalLabelInst(const RiscV64Inst * inst)
{
	return inst != nullptr && !inst->dead && inst->result == ":" && !inst->opcode.empty() &&
		   inst->opcode.rfind(".L", 0) == 0;
}

/// @brief 判断指令是否为引用标签的跳转/分支指令
bool isLabelReferenceInst(const RiscV64Inst * inst)
{
	return inst != nullptr && !inst->dead &&
		   (inst->opcode == "j" || inst->opcode == "jal" || inst->opcode == "beq" || inst->opcode == "bne" ||
			inst->opcode == "blt" || inst->opcode == "bge" || inst->opcode == "bltu" || inst->opcode == "bgeu");
}

/// @brief 判断指令是否为最终有效的机器指令（非dead、非注释、非标签）
/// @param inst 待判断的指令
/// @return 若为有效机器指令则返回true
bool isFinalMachineInstruction(const RiscV64Inst * inst)
{
	return inst != nullptr && !inst->dead && !inst->opcode.empty() && inst->opcode != "#" && inst->result != ":";
}

/// @brief 判断操作数是否为直接栈操作数（以(sp)或(s0)为基址）
/// @param operand 操作数字符串
/// @return 若为栈操作数则返回true
bool isDirectStackOperand(const std::string & operand)
{
	return operand.find("(sp)") != std::string::npos || operand.find("(s0)") != std::string::npos;
}

/// @brief 判断操作码是否为栈加载指令（lw/ld/flw/fld）
/// @param opcode 操作码字符串
/// @return 若为加载指令则返回true
bool isLoadOpcode(const std::string & opcode)
{
	return opcode == "lw" || opcode == "ld" || opcode == "flw" || opcode == "fld";
}

/// @brief 判断操作码是否为栈存储指令（sw/sd/fsw/fsd）
/// @param opcode 操作码字符串
/// @return 若为存储指令则返回true
bool isStoreOpcode(const std::string & opcode)
{
	return opcode == "sw" || opcode == "sd" || opcode == "fsw" || opcode == "fsd";
}

/// @brief 判断操作码是否为寄存器移动指令（mv/fmv.x.w/fmv.w.x/fsgnj.s）
/// @param opcode 操作码字符串
/// @return 若为move指令则返回true
bool isMoveOpcode(const std::string & opcode)
{
	return opcode == "mv" || opcode == "fmv.x.w" || opcode == "fmv.w.x" || opcode == "fsgnj.s";
}


int alignTo(int value, int align)
{
	if (align <= 0) {
		return value;
	}
	return ((value + align - 1) / align) * align;
}

Type * memoryObjectType(Value * val)
{
	if (auto * allocaInst = dynamic_cast<AllocaInst *>(val)) {
		return allocaInst->getAllocaType();
	}

	if (auto * globalVar = dynamic_cast<GlobalVariable *>(val)) {
		return globalVar->getValueType();
	}

	return val != nullptr ? val->getType() : nullptr;
}

/// @brief 根据寄存器分配信息动态计算所需栈帧大小
/// @param regAllocMap 寄存器分配映射表
/// @param savedFrameBytes callee-saved寄存器占用的字节数
/// @return 16字节对齐的栈帧大小
int computeFrameSize(const std::unordered_map<Value *, RegAllocInfo> & regAllocMap, int savedFrameBytes)
{
	int requiredBytes = savedFrameBytes;
	for (const auto & [_, info] : regAllocMap) {
		if (!info.hasStackSlot) {
			continue;
		}

		int slotReach = 0;
		// FP负方向偏移的栈槽：局部变量和溢出变量
		if (info.baseRegId == RISCV64_FP_REG_NO && info.offset < 0) {
			slotReach = static_cast<int>(-info.offset);
			requiredBytes = std::max(requiredBytes, slotReach);
		} else if (info.baseRegId == RISCV64_SP_REG_NO) {
			// SP正方向偏移的栈槽：超出寄存器传递的调用参数
			slotReach = static_cast<int>(info.offset);
			requiredBytes = std::max(requiredBytes, savedFrameBytes + slotReach);
		}
	}

	return alignTo(requiredBytes, 16);
}

} // namespace

RiscV64Inst::RiscV64Inst(
	std::string _opcode,
	std::string _result,
	std::string _arg1,
	std::string _arg2,
	std::string _cond,
	std::string _addition)
	: opcode(_opcode), cond(_cond), result(_result), arg1(_arg1), arg2(_arg2), addition(_addition), dead(false)
{}

/*
	指令内容替换
*/
void RiscV64Inst::replace(
	std::string _opcode,
	std::string _result,
	std::string _arg1,
	std::string _arg2,
	std::string _cond,
	std::string _addition)
{
	opcode = _opcode;
	result = _result;
	arg1 = _arg1;
	arg2 = _arg2;
	cond = _cond;
	addition = _addition;
}

/*
	设置为无效指令
*/
void RiscV64Inst::setDead()
{
	dead = true;
}

/*
	输出函数 - RISCV64格式
	与ARM32的区别：无条件码，内存寻址为offset(base)格式
*/
std::string RiscV64Inst::outPut()
{
	// 无用代码，什么都不输出
	if (dead) {
		return "";
	}

	// 占位指令
	if (opcode.empty()) {
		return "";
	}

	std::string ret = opcode;

	// RISCV64无条件码，不输出cond

	// 结果输出
	if (!result.empty()) {
		if (result == ":") {
			// 标签：opcode后直接跟冒号
			ret += result;
		} else {
			ret += " " + result;
		}
	}

	// 第一元参数输出
	if (!arg1.empty()) {
		ret += "," + arg1;
	}

	// 第二元参数输出
	if (!arg2.empty()) {
		ret += "," + arg2;
	}

	// 其他附加信息输出
	if (!addition.empty()) {
		ret += "," + addition;
	}

	return ret;
}

#define emit(...)                                                                                                      \
	do {                                                                                                               \
		code.push_back(new RiscV64Inst(__VA_ARGS__));                                                                  \
		++machineInstCount;                                                                                            \
	} while (0)

/// @brief 构造函数
/// @param _module 符号表
ILocRiscV64::ILocRiscV64(Module * _module)
{
	this->module = _module;
}

/// @brief 析构函数
ILocRiscV64::~ILocRiscV64()
{
	std::list<RiscV64Inst *>::iterator pIter;

	for (pIter = code.begin(); pIter != code.end(); ++pIter) {
		delete (*pIter);
	}
}

/// @brief 设置寄存器分配信息映射表
/// @param allocMap 寄存器分配信息（由GreedyRegAllocator产生）
void ILocRiscV64::setRegAllocMap(const std::unordered_map<Value *, RegAllocInfo> & allocMap)
{
	this->regAllocMap = allocMap;
}

/// @brief 获取某个Value的寄存器分配信息
/// @param val IR值
/// @return 寄存器分配信息引用
RegAllocInfo & ILocRiscV64::getRegAllocInfo(Value * val)
{
	return regAllocMap[val];
}

/// @brief 删除无用的Label指令
/// 优化策略：先收集所有被跳转指令引用的内部标签，再遍历标签指令，
/// 将未被引用的标签标记为dead，避免O(n^2)的逐标签遍历。
void ILocRiscV64::deleteUnusedLabel()
{
	// 收集所有被跳转/分支指令引用的内部标签（以".L"开头）
	std::unordered_set<std::string> referencedLabels;
	auto collectLabel = [&referencedLabels](const std::string & operand) {
		if (!operand.empty() && operand.rfind(".L", 0) == 0) {
			referencedLabels.insert(operand);
		}
	};

	// 遍历所有跳转/分支指令，收集其引用的标签
	for (RiscV64Inst * inst: code) {
		if (!isLabelReferenceInst(inst)) {
			continue;
		}

		collectLabel(inst->result);
		collectLabel(inst->arg1);
		collectLabel(inst->arg2);
		collectLabel(inst->addition);
	}

	// 将未被任何跳转指令引用的内部标签标记为dead
	for (RiscV64Inst * inst: code) {
		if (isInternalLabelInst(inst) && referencedLabels.find(inst->opcode) == referencedLabels.end()) {
			inst->setDead();
		}
	}
}

/// @brief 输出汇编
/// @param file 输出的文件指针
/// @param outputEmpty 是否输出空语句
void ILocRiscV64::outPut(FILE * file, bool outputEmpty)
{
	for (auto inst: code) {

		std::string s = inst->outPut();

		if (inst->result == ":") {
			// Label指令，不需要Tab输出
			fprintf(file, "%s\n", s.c_str());
			continue;
		}

		if (!s.empty()) {
			fprintf(file, "\t%s\n", s.c_str());
		} else if ((outputEmpty)) {
			fprintf(file, "\n");
		}
	}
}

/// @brief 获取当前的代码序列
/// @return 代码序列
std::list<RiscV64Inst *> & ILocRiscV64::getCode()
{
	return code;
}

/// @brief 收集最终静态指标
/// @return peephole之后、忽略dead/标签/注释后的汇编级指标
/// @note 统计内容包括：机器指令总数、栈load/store次数（以sp/s0为基址的lw/ld/flw/fld/sw/sd/fsw/fsd）、
///       move指令次数（mv/fmv.x.w/fmv.w.x/fsgnj.s）
RiscV64CodegenStats ILocRiscV64::collectFinalStats() const
{
	RiscV64CodegenStats stats;
	for (const auto * inst : code) {
		if (!isFinalMachineInstruction(inst)) {
			continue;
		}

		++stats.machineInstructionCount;
		if (isLoadOpcode(inst->opcode) && isDirectStackOperand(inst->arg1)) {
			++stats.stackLoadCount;
		}
		if (isStoreOpcode(inst->opcode) && isDirectStackOperand(inst->arg1)) {
			++stats.stackStoreCount;
		}
		if (isMoveOpcode(inst->opcode)) {
			++stats.moveInstructionCount;
		}
	}
	return stats;
}

/**
 * 数字变字符串，若flag为真，则变为立即数寻址（加#）
 * RISCV64中立即数不需要#前缀，但为了与ARM32接口兼容保留flag参数
 */
std::string ILocRiscV64::toStr(int num, bool flag)
{
	// RISCV64立即数不需要#前缀，直接输出数字
	return std::to_string(num);
}

/*
	产生标签（用于BasicBlock标签输出）
*/
void ILocRiscV64::label(std::string name)
{
	// .L1:
	emit(name, ":");
}

/// @brief 0个源操作数指令
/// @param op 操作码
/// @param rs 操作数
void ILocRiscV64::inst(std::string op, std::string rs)
{
	emit(op, rs);
}

/// @brief 一个操作数指令
/// @param op 操作码
/// @param rs 操作数
/// @param arg1 源操作数
void ILocRiscV64::inst(std::string op, std::string rs, std::string arg1)
{
	emit(op, rs, arg1);
}

/// @brief 两个操作数指令
/// @param op 操作码
/// @param rs 操作数
/// @param arg1 源操作数
/// @param arg2 源操作数
void ILocRiscV64::inst(std::string op, std::string rs, std::string arg1, std::string arg2)
{
	emit(op, rs, arg1, arg2);
}

///
/// @brief 注释指令，RISCV64使用#作为注释符
/// @param str 注释内容
///
void ILocRiscV64::comment(std::string str)
{
	emit("#", str);
}

/*
	加载立即数 - RISCV64使用li伪指令或lui+addiw
	对于12位有符号立即数范围(-2048~2047)，使用li伪指令(汇编器展开为addi)
	对于超出12位范围的32位常量，使用lui+addiw方式
	注意：RV64 的 lui 会将 20 位立即数左移 12 位后符号扩展到 64 位，
	若高位为 1（如立即数 2147483647 即 0x7FFFFFFF），符号扩展后会产生错误的
	64 位负值。使用 addiw 代替 addi，addiw 只取低 32 位结果再符号扩展，
	从而消除 lui 带来的错误符号扩展，保证装载的 64 位值正确。
	此前使用 lui+addi 会导致 RV64 上大立即数装载出错。
*/
void ILocRiscV64::load_imm(int rs_reg_no, int constant)
{
	if (PlatformRiscV64::constExpr(constant)) {
		// 12位有符号立即数范围内，使用li伪指令
		// li rd, imm (汇编器会展开为 addi rd, zero, imm)
		emit("li", PlatformRiscV64::regName[rs_reg_no], std::to_string(constant));
	} else {
		// 超出12位范围，使用lui+addiw。
		// RV64 的 lui 会生成符号扩展后的 64 位值；addiw 可把结果收敛回 i32 语义。
		const int32_t v32 = static_cast<int32_t>(constant);
		int32_t lo = v32 & 0xFFF;
		int32_t hi = v32 - lo;
		if (lo >= 2048) {
			lo -= 4096;
			hi += 4096;
		}
		const uint32_t luiImm = (static_cast<uint32_t>(hi >> 12)) & 0xFFFFF;

		emit("lui", PlatformRiscV64::regName[rs_reg_no], std::to_string(luiImm));
		emit("addiw", PlatformRiscV64::regName[rs_reg_no], PlatformRiscV64::regName[rs_reg_no],
			 std::to_string(lo));
	}
}

/// @brief 加载符号地址 - RISCV64使用la伪指令
/// @param rs_reg_no 结果寄存器编号
/// @param name 符号名
void ILocRiscV64::load_symbol(int rs_reg_no, std::string name)
{
	// la rd, symbol (伪指令，汇编器展开为auipc+addi)
	emit("la", PlatformRiscV64::regName[rs_reg_no], name);
}

/// @brief 基址寻址加载 - RISCV64格式: lw rs, offset(base)
/// @param rs_reg_no 结果寄存器
/// @param base_reg_no 基址寄存器
/// @param offset 偏移
void ILocRiscV64::load_base(int rs_reg_no, int base_reg_no, int offset, bool wide)
{
	std::string rsReg = PlatformRiscV64::regName[rs_reg_no];
	std::string base = PlatformRiscV64::regName[base_reg_no];
	const char * loadOp = wide ? "ld" : "lw";

	if (PlatformRiscV64::isDisp(offset)) {
		// 有效的偏移常量，RISCV64内存寻址格式: offset(base)
		// lw/ld rs, offset(base)
		std::string mem = std::to_string(offset) + "(" + base + ")";
		emit(loadOp, rsReg, mem);
	} else {
		// 偏移超出12位范围，先加载偏移到寄存器，再用add计算地址
		load_imm(rs_reg_no, offset);
		emit("add", rsReg, base, rsReg);
		emit(loadOp, rsReg, "0(" + rsReg + ")");
	}
}

/// @brief 从基址寻址内存加载单精度float到FPR。
void ILocRiscV64::load_float_base(int fd_reg_no, int base_reg_no, int offset, int tmp_reg_no)
{
	std::string fdReg = PlatformRiscV64::fpRegName[fd_reg_no];
	std::string base = PlatformRiscV64::regName[base_reg_no];

	if (PlatformRiscV64::isDisp(offset)) {
		emit("flw", fdReg, std::to_string(offset) + "(" + base + ")");
	} else {
		load_imm(tmp_reg_no, offset);
		emit("add", PlatformRiscV64::regName[tmp_reg_no], base, PlatformRiscV64::regName[tmp_reg_no]);
		emit("flw", fdReg, "0(" + PlatformRiscV64::regName[tmp_reg_no] + ")");
	}
}

/// @brief 基址寻址存储 - RISCV64格式: sw src, offset(base)
/// @param src_reg_no 源寄存器
/// @param base_reg_no 基址寄存器
/// @param disp 偏移
/// @param tmp_reg_no 可能需要临时寄存器编号
void ILocRiscV64::store_base(int src_reg_no, int base_reg_no, int disp, int tmp_reg_no, bool wide)
{
	std::string src = PlatformRiscV64::regName[src_reg_no];
	std::string base = PlatformRiscV64::regName[base_reg_no];
	const char * storeOp = wide ? "sd" : "sw";

	if (PlatformRiscV64::isDisp(disp)) {
		// 有效的偏移常量，RISCV64内存寻址格式: offset(base)
		// sw/sd src, offset(base)
		std::string mem = std::to_string(disp) + "(" + base + ")";
		emit(storeOp, src, mem);
	} else {
		// 偏移超出12位范围，先加载偏移到临时寄存器，再用add计算地址
		load_imm(tmp_reg_no, disp);
		emit("add", PlatformRiscV64::regName[tmp_reg_no], base, PlatformRiscV64::regName[tmp_reg_no]);
		emit(storeOp, src, "0(" + PlatformRiscV64::regName[tmp_reg_no] + ")");
	}
}

/// @brief 将FPR中的单精度float保存到基址寻址内存。
void ILocRiscV64::store_float_base(int fs_reg_no, int base_reg_no, int disp, int tmp_reg_no)
{
	std::string fsReg = PlatformRiscV64::fpRegName[fs_reg_no];
	std::string base = PlatformRiscV64::regName[base_reg_no];

	if (PlatformRiscV64::isDisp(disp)) {
		emit("fsw", fsReg, std::to_string(disp) + "(" + base + ")");
	} else {
		load_imm(tmp_reg_no, disp);
		emit("add", PlatformRiscV64::regName[tmp_reg_no], base, PlatformRiscV64::regName[tmp_reg_no]);
		emit("fsw", fsReg, "0(" + PlatformRiscV64::regName[tmp_reg_no] + ")");
	}
}

/// @brief 寄存器Mov操作 - RISCV64使用mv伪指令
/// @param rs_reg_no 结果寄存器
/// @param src_reg_no 源寄存器
void ILocRiscV64::mov_reg(int rs_reg_no, int src_reg_no)
{
	emit("mv", PlatformRiscV64::regName[rs_reg_no], PlatformRiscV64::regName[src_reg_no]);
}

/// @brief FPR到FPR复制。RISC-V没有fmv.s三操作数编码，使用fsgnj.s rd,rs,rs。
void ILocRiscV64::fmov_reg(int fd_reg_no, int fs_reg_no)
{
	emit("fsgnj.s", PlatformRiscV64::fpRegName[fd_reg_no], PlatformRiscV64::fpRegName[fs_reg_no],
	     PlatformRiscV64::fpRegName[fs_reg_no]);
}

/// @brief 加载变量到寄存器，保证将变量放到reg中
/// @param rs_reg_no 结果寄存器
/// @param src_var 源操作数
void ILocRiscV64::load_var(int rs_reg_no, Value * src_var)
{
	auto it = regAllocMap.find(src_var);
	load_var(rs_reg_no, src_var, it != regAllocMap.end() ? it->second : RegAllocInfo{});
}

void ILocRiscV64::load_var(int rs_reg_no, Value * src_var, const RegAllocInfo & info)
{
	Type * valueType = memoryObjectType(src_var);
	const bool wide = valueType->isPointerType();
	if (Instanceof(constVal, ConstInteger *, src_var)) {
		// 若src_var是常量，则直接加载常量值
		load_imm(rs_reg_no, constVal->getVal());

	} else if (Instanceof(constVal, ConstFloat *, src_var)) {
		// float常量：加载IEEE 754位模式作为立即数
		std::uint32_t bits = constVal->getBitPattern();
		load_imm(rs_reg_no, static_cast<int32_t>(bits));

	} else if (Instanceof(globalVar, GlobalVariable *, src_var)) {
		// 全局变量：la加载地址 + lw加载值
		load_symbol(rs_reg_no, globalVar->getName());
		emit(wide ? "ld" : "lw", PlatformRiscV64::regName[rs_reg_no], "0(" + PlatformRiscV64::regName[rs_reg_no] + ")");

	} else {
		if (info.hasReg()) {
			// 分配了寄存器，直接mov
			int32_t src_regId = info.regId;
			if (src_regId != rs_reg_no) {
				// 寄存器不一样才需要mv操作
				emit("mv", PlatformRiscV64::regName[rs_reg_no], PlatformRiscV64::regName[src_regId]);
			}
		} else if (info.hasFloatReg()) {
			emit("fmv.x.w", PlatformRiscV64::regName[rs_reg_no], PlatformRiscV64::fpRegName[info.regId]);
		} else if (info.hasStackSlot) {
			// 在栈上分配了空间，从栈加载
			load_base(rs_reg_no, info.baseRegId, info.offset, wide);
		} else {
			// 未找到分配信息，可能是AllocaInst的结果（指针值）
			// 指针值需要通过栈地址加载
			minic_log(LOG_ERROR, "ILocRiscV64::load_var: 未找到变量分配信息");
		}
	}
}

/// @brief 加载float值到FPR。
///
/// 常量和GPR分配的float值通过fmv.w.x搬运位模式；栈和全局变量使用flw。
void ILocRiscV64::load_float_var(int fd_reg_no, Value * src_var, int tmp_reg_no)
{
	auto it = regAllocMap.find(src_var);
	load_float_var(fd_reg_no, src_var, tmp_reg_no, it != regAllocMap.end() ? it->second : RegAllocInfo{});
}

void ILocRiscV64::load_float_var(int fd_reg_no, Value * src_var, int tmp_reg_no, const RegAllocInfo & info)
{
	if (Instanceof(constVal, ConstFloat *, src_var)) {
		std::uint32_t bits = constVal->getBitPattern();
		load_imm(tmp_reg_no, static_cast<int32_t>(bits));
		emit("fmv.w.x", PlatformRiscV64::fpRegName[fd_reg_no], PlatformRiscV64::regName[tmp_reg_no]);

	} else if (Instanceof(globalVar, GlobalVariable *, src_var)) {
		load_symbol(tmp_reg_no, globalVar->getName());
		emit("flw", PlatformRiscV64::fpRegName[fd_reg_no], "0(" + PlatformRiscV64::regName[tmp_reg_no] + ")");

	} else {
		if (info.hasFloatReg()) {
			if (info.regId != fd_reg_no) {
				fmov_reg(fd_reg_no, info.regId);
			}
		} else if (info.hasReg()) {
			emit("fmv.w.x", PlatformRiscV64::fpRegName[fd_reg_no], PlatformRiscV64::regName[info.regId]);
		} else if (info.hasStackSlot) {
			load_float_base(fd_reg_no, info.baseRegId, info.offset, tmp_reg_no);
		} else {
			minic_log(LOG_ERROR, "ILocRiscV64::load_float_var: 未找到变量分配信息");
		}
	}
}

/// @brief 加载变量地址到寄存器
/// @param rs_reg_no 结果寄存器
/// @param var 变量
void ILocRiscV64::lea_var(int rs_reg_no, Value * var)
{
	// 通过regAllocMap查找栈位置信息
	auto it = regAllocMap.find(var);
	if (it != regAllocMap.end() && it->second.hasStackSlot) {
		leaStack(rs_reg_no, it->second.baseRegId, it->second.offset);
	} else if (Instanceof(globalVar, GlobalVariable *, var)) {
		// 全局变量地址
		load_symbol(rs_reg_no, globalVar->getName());
	} else {
		minic_log(LOG_ERROR, "ILocRiscV64::lea_var: 未找到变量栈位置信息");
	}
}

/// @brief 保存寄存器到变量，保证将计算结果保存到变量
/// 适配SSA IR：通过regAllocMap获取寄存器/栈位置信息
/// @param src_reg_no 源寄存器
/// @param dest_var  目标变量
/// @param tmp_reg_no 第三方寄存器
void ILocRiscV64::store_var(int src_reg_no, Value * dest_var, int tmp_reg_no)
{
	auto it = regAllocMap.find(dest_var);
	store_var(src_reg_no, dest_var, tmp_reg_no, it != regAllocMap.end() ? it->second : RegAllocInfo{});
}

void ILocRiscV64::store_var(int src_reg_no, Value * dest_var, int tmp_reg_no, const RegAllocInfo & info)
{
	Type * valueType = memoryObjectType(dest_var);
	const bool wide = valueType->isPointerType();
	if (Instanceof(globalVar, GlobalVariable *, dest_var)) {
		// 全局变量：la加载地址 + sw存储值
		load_symbol(tmp_reg_no, globalVar->getName());
		emit(wide ? "sd" : "sw", PlatformRiscV64::regName[src_reg_no], "0(" + PlatformRiscV64::regName[tmp_reg_no] + ")");

	} else {
		if (info.hasReg()) {
			// 分配了寄存器，直接mov
			int dest_reg_id = info.regId;
			if (src_reg_no != dest_reg_id) {
				emit("mv", PlatformRiscV64::regName[dest_reg_id], PlatformRiscV64::regName[src_reg_no]);
			}
		} else if (info.hasFloatReg()) {
			emit("fmv.w.x", PlatformRiscV64::fpRegName[info.regId], PlatformRiscV64::regName[src_reg_no]);
		} else if (info.hasStackSlot) {
			// 在栈上分配了空间，存储到栈
			store_base(src_reg_no, info.baseRegId, info.offset, tmp_reg_no, wide);
		} else {
			minic_log(LOG_ERROR, "ILocRiscV64::store_var: 未找到变量分配信息");
		}
	}
}

/// @brief 将FPR中的float值保存到目标Value。
///
/// 目标可能是FPR、GPR位模式、栈槽或全局变量，按RegAllocInfo选择路径。
void ILocRiscV64::store_float_var(int fs_reg_no, Value * dest_var, int tmp_reg_no)
{
	auto it = regAllocMap.find(dest_var);
	store_float_var(fs_reg_no, dest_var, tmp_reg_no, it != regAllocMap.end() ? it->second : RegAllocInfo{});
}

void ILocRiscV64::store_float_var(int fs_reg_no, Value * dest_var, int tmp_reg_no, const RegAllocInfo & info)
{
	if (Instanceof(globalVar, GlobalVariable *, dest_var)) {
		load_symbol(tmp_reg_no, globalVar->getName());
		emit("fsw", PlatformRiscV64::fpRegName[fs_reg_no], "0(" + PlatformRiscV64::regName[tmp_reg_no] + ")");

	} else {
		if (info.hasFloatReg()) {
			if (fs_reg_no != info.regId) {
				fmov_reg(info.regId, fs_reg_no);
			}
		} else if (info.hasReg()) {
			emit("fmv.x.w", PlatformRiscV64::regName[info.regId], PlatformRiscV64::fpRegName[fs_reg_no]);
		} else if (info.hasStackSlot) {
			store_float_base(fs_reg_no, info.baseRegId, info.offset, tmp_reg_no);
		} else {
			minic_log(LOG_ERROR, "ILocRiscV64::store_float_var: 未找到变量分配信息");
		}
	}
}

/// @brief 加载栈内变量地址
/// @param rs_reg_no 结果寄存器号
/// @param base_reg_no 基址寄存器
/// @param off 偏移
void ILocRiscV64::leaStack(int rs_reg_no, int base_reg_no, int off)
{
	std::string rs_reg_name = PlatformRiscV64::regName[rs_reg_no];
	std::string base_reg_name = PlatformRiscV64::regName[base_reg_no];

	if (PlatformRiscV64::constExpr(off))
		// addi rs, base, offset
		emit("addi", rs_reg_name, base_reg_name, std::to_string(off));
	else {
		// li rs, offset
		load_imm(rs_reg_no, off);

		// add rs, base, rs
		emit("add", rs_reg_name, base_reg_name, rs_reg_name);
	}
}

/// @brief 函数内栈内空间分配（局部变量、形参变量、函数参数传值，或不能寄存器分配的临时变量等）
/// RISCV64 prologue: addi sp,sp,-framesize; 保存必要callee-saved寄存器; addi s0,sp,framesize
/// @param func 函数
/// @param tmp_reg_no 临时寄存器编号
void ILocRiscV64::allocStack(Function * func, int tmp_reg_no)
{
	(void) func;
	// 优先使用已计算的栈帧大小，否则动态计算
	// 根据实际需要保存的callee-saved寄存器数量计算占用字节数
	const int savedFrameBytes = static_cast<int>(savedRegs.size()) * 8;
	const int currentFrameSize = frameSize > 0 ? frameSize : computeFrameSize(regAllocMap, savedFrameBytes);
	if (currentFrameSize == 0 && savedRegs.empty()) {
		return;
	}

	// RISCV64 prologue:
	// addi sp, sp, -framesize
	if (PlatformRiscV64::constExpr(-currentFrameSize)) {
		emit("addi", "sp", "sp", std::to_string(-currentFrameSize));
	} else {
		// 偏移超出12位有符号立即数范围，先加载到临时寄存器
		load_imm(tmp_reg_no, -currentFrameSize);
		emit("add", "sp", "sp", PlatformRiscV64::regName[tmp_reg_no]);
	}

	// 逐个保存callee-saved寄存器到栈帧顶部
	// savedRegs由CodeGeneratorRiscV64根据实际使用情况计算得出，仅保存必要的寄存器
	for (int i = 0; i < static_cast<int>(savedRegs.size()); ++i) {
		int offset = currentFrameSize - (i + 1) * 8;
		// 通过寄存器编号查找对应的寄存器名称
		const std::string & regName = PlatformRiscV64::regName[savedRegs[i]];
		if (PlatformRiscV64::isDisp(offset)) {
			emit("sd", regName, std::to_string(offset) + "(sp)");
		} else {
			// 偏移超出12位范围，通过临时寄存器计算地址
			load_imm(tmp_reg_no, offset);
			emit("add", PlatformRiscV64::regName[tmp_reg_no], "sp", PlatformRiscV64::regName[tmp_reg_no]);
			emit("sd", regName, "0(" + PlatformRiscV64::regName[tmp_reg_no] + ")");
		}
	}

	// 保存callee-saved FPR到栈帧（紧随GPR保存区之后）
	// 使用fsd指令保存双精度浮点值（RISC-V64使用fsd/fld）
	for (int i = 0; i < static_cast<int>(savedFPRs.size()); ++i) {
		int offset = currentFrameSize - (static_cast<int>(savedRegs.size()) + i + 1) * 8;
		const std::string & fpReg = PlatformRiscV64::fpRegName[savedFPRs[i]];
		if (PlatformRiscV64::isDisp(offset)) {
			emit("fsd", fpReg, std::to_string(offset) + "(sp)");
		} else {
			load_imm(tmp_reg_no, offset);
			emit("add", PlatformRiscV64::regName[tmp_reg_no], "sp", PlatformRiscV64::regName[tmp_reg_no]);
			emit("fsd", fpReg, "0(" + PlatformRiscV64::regName[tmp_reg_no] + ")");
		}
	}

	// addi s0, sp, framesize - 设置帧指针
	if (PlatformRiscV64::constExpr(currentFrameSize)) {
		emit("addi", "s0", "sp", std::to_string(currentFrameSize));
	} else {
		load_imm(tmp_reg_no, currentFrameSize);
		emit("add", "s0", "sp", PlatformRiscV64::regName[tmp_reg_no]);
	}
}

/// @brief 调用函数fun - RISCV64使用call伪指令
/// @param name 函数名
void ILocRiscV64::call_fun(std::string name)
{
	// call name (伪指令，汇编器展开为auipc+jalr)
	emit("call", name);
}

/// @brief NOP操作
void ILocRiscV64::nop()
{
	emit("nop");
}

///
/// @brief 无条件跳转指令 - RISCV64使用j伪指令
/// @param label 目标Label名称
///
void ILocRiscV64::jump(std::string label)
{
	emit("j", label);
}

/// @brief 加载函数的参数到寄存器
/// RISCV64: 前8个参数通过a0-a7传递，第9个起通过栈传递
/// @param fun 函数
void ILocRiscV64::ldr_args(Function * fun)
{
	// RISCV64的参数加载由CodeGeneratorRiscV64::adjustFormalParamInsts处理
}

/// @brief 记录IR指令对应的机器指令范围
/// @param inst IR指令
/// @param start 该IR指令翻译开始时的机器指令计数
void ILocRiscV64::recordMIRange(Instruction * inst, int start)
{
	if (inst != nullptr) {
		instToMIRange[inst] = {start, machineInstCount};
	}
}

/// @brief 替换字符串中的寄存器名（带边界检查）
/// @param text 待替换的字符串
/// @param oldReg 旧寄存器名
/// @param newReg 新寄存器名
/// @return 是否发生替换
static bool replaceRegName(std::string & text, const std::string & oldReg, const std::string & newReg)
{
	bool replaced = false;
	std::string::size_type pos = 0;
	while ((pos = text.find(oldReg, pos)) != std::string::npos) {
		// 检查前一个字符是否为寄存器名边界（非字母数字非下划线）
		if (pos > 0 && (std::isalnum(text[pos - 1]) || text[pos - 1] == '_')) {
			pos += oldReg.size();
			continue;
		}
		// 检查后一个字符是否为寄存器名边界
		auto endPos = pos + oldReg.size();
		if (endPos < text.size() && (std::isalnum(text[endPos]) || text[endPos] == '_')) {
			pos = endPos;
			continue;
		}
		text.replace(pos, oldReg.size(), newReg);
		pos += newReg.size();
		replaced = true;
	}
	return replaced;
}

/// @brief 替换机器指令中的scratch寄存器编号
///
/// 遍历所有机器指令，对scratch值的[borrowPos, releasePos)范围内的指令，
/// 将原始物理寄存器名替换为新分配的物理寄存器名。
///
/// @param scratchValues scratch值列表
void ILocRiscV64::patchScratchRegs(const std::vector<ScratchValue> & scratchValues)
{
	// 构建机器指令的线性列表（跳过dead指令的占位）
	std::vector<RiscV64Inst *> instVec;
	instVec.reserve(code.size());
	for (auto * inst : code) {
		instVec.push_back(inst);
	}

	for (const auto & sv : scratchValues) {
		if (sv.spilled || sv.originalPhysReg < 0 || sv.physicalReg < 0) {
			continue;
		}
		if (sv.originalPhysReg == sv.physicalReg) {
			continue; // 分配了相同的寄存器，无需patch
		}

		const std::string & oldName = PlatformRiscV64::regName[sv.originalPhysReg];
		const std::string & newName = PlatformRiscV64::regName[sv.physicalReg];

		// 替换 [borrowPos, releasePos) 范围内的机器指令
		int miIdx = 0;
		for (auto * inst : code) {
			if (miIdx >= sv.borrowPos && miIdx < sv.releasePos) {
				replaceRegName(inst->opcode, oldName, newName);
				replaceRegName(inst->result, oldName, newName);
				replaceRegName(inst->arg1, oldName, newName);
				replaceRegName(inst->arg2, oldName, newName);
				replaceRegName(inst->addition, oldName, newName);
			}
			if (miIdx >= sv.releasePos) {
				break;
			}
			++miIdx;
		}
	}
}
