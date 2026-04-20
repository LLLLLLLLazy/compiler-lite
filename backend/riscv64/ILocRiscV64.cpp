///
/// @file ILocRiscV64.cpp
/// @brief RISC-V64指令序列管理的实现
///

#include <cstdio>
#include <string>

#include "ILocRiscV64.h"
#include "Common.h"
#include "Function.h"
#include "PlatformRiscV64.h"
#include "Module.h"

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

#define emit(...) code.push_back(new RiscV64Inst(__VA_ARGS__))

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

/// @brief 删除无用的Label指令
void ILocRiscV64::deleteUnusedLabel()
{
	std::list<RiscV64Inst *> labelInsts;
	for (RiscV64Inst * inst: code) {
		if ((!inst->dead) && (inst->opcode[0] == '.') && (inst->result == ":")) {
			labelInsts.push_back(inst);
		}
	}

	// 检测Label指令是否在被使用，也就是是否有跳转到该Label的指令
	// 如果没有使用，则设置为dead
	for (RiscV64Inst * labelInst: labelInsts) {
		bool labelUsed = false;

		for (RiscV64Inst * inst: code) {
			// RISCV64跳转指令：j(无条件跳转)、beq/bne/blt/bge/bltu/bgeu(条件跳转)、call、jal、jalr
			if ((!inst->dead) && (inst->result == labelInst->opcode)) {
				// 检查是否是跳转/分支指令引用了该标签
				if (inst->opcode == "j" || inst->opcode == "jal" || inst->opcode == "beq" ||
					inst->opcode == "bne" || inst->opcode == "blt" || inst->opcode == "bge" ||
					inst->opcode == "bltu" || inst->opcode == "bgeu" || inst->opcode == "call") {
					labelUsed = true;
					break;
				}
			}
		}

		if (!labelUsed) {
			labelInst->setDead();
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
	产生标签
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
	加载立即数 - RISCV64使用li伪指令或lui+addi
	对于12位有符号立即数范围(-2048~2047)，使用li伪指令(汇编器展开为addi)
	对于超出12位范围的32位常量，使用lui+addi方式
*/
void ILocRiscV64::load_imm(int rs_reg_no, int constant)
{
	if (PlatformRiscV64::constExpr(constant)) {
		// 12位有符号立即数范围内，使用li伪指令
		// li rd, imm (汇编器会展开为 addi rd, zero, imm)
		emit("li", PlatformRiscV64::regName[rs_reg_no], std::to_string(constant));
	} else {
		// 超出12位范围，使用lui+addi
		// lui加载高20位，addi加载低12位
		uint32_t val = (uint32_t)constant;
		uint32_t upper = (val + 0x800) >> 12; // 考虑低12位符号扩展的修正, 立即数低12位大于0x7FF时addi会识别成负数, 将高20位加1再加上addi识别的负数得到正确解
		int32_t lower = val - (upper << 12);

		emit("lui", PlatformRiscV64::regName[rs_reg_no], std::to_string(upper));
		emit("addi", PlatformRiscV64::regName[rs_reg_no], PlatformRiscV64::regName[rs_reg_no],
			 std::to_string(lower));
	}
}

/// @brief 加载符号地址 - RISCV64使用la伪指令
/// @param rs_reg_no 结果寄存器编号
/// @param name 符号名
void ILocRiscV64::load_symbol(int rs_reg_no, std::string name)
{
	// la rd, symbol (伪指令，汇编器展开为auipc+addi)
	// symbol 为全局变量或函数名，标签名，汇编器会处理符号地址的计算
	emit("la", PlatformRiscV64::regName[rs_reg_no], name);
}

/// @brief 基址寻址加载 - RISCV64格式: lw rs, offset(base)
/// @param rs_reg_no 结果寄存器
/// @param base_reg_no 基址寄存器
/// @param offset 偏移
void ILocRiscV64::load_base(int rs_reg_no, int base_reg_no, int offset)
{
	std::string rsReg = PlatformRiscV64::regName[rs_reg_no];
	std::string base = PlatformRiscV64::regName[base_reg_no];

	if (PlatformRiscV64::isDisp(offset)) {
		// 有效的偏移常量，RISCV64内存寻址格式: offset(base)
		// lw rs, offset(base)
		std::string mem = std::to_string(offset) + "(" + base + ")";
		emit("lw", rsReg, mem);
	} else {
		// 偏移超出12位范围，先加载偏移到寄存器，再用add计算地址
		// li rs, offset
		load_imm(rs_reg_no, offset);

		// add rs, base, rs
		emit("add", rsReg, base, rsReg);

		// lw rs, 0(rs)
		emit("lw", rsReg, "0(" + rsReg + ")");
	}
}

/// @brief 基址寻址存储 - RISCV64格式: sw src, offset(base)
/// @param src_reg_no 源寄存器
/// @param base_reg_no 基址寄存器
/// @param disp 偏移
/// @param tmp_reg_no 可能需要临时寄存器编号
void ILocRiscV64::store_base(int src_reg_no, int base_reg_no, int disp, int tmp_reg_no)
{
	std::string src = PlatformRiscV64::regName[src_reg_no];
	std::string base = PlatformRiscV64::regName[base_reg_no];

	if (PlatformRiscV64::isDisp(disp)) {
		// 有效的偏移常量，RISCV64内存寻址格式: offset(base)
		// sw src, offset(base)
		std::string mem = std::to_string(disp) + "(" + base + ")";
		emit("sw", src, mem);
	} else {
		// 偏移超出12位范围，先加载偏移到临时寄存器，再用add计算地址
		// li tmp, disp
		load_imm(tmp_reg_no, disp);

		// add tmp, base, tmp
		emit("add", PlatformRiscV64::regName[tmp_reg_no], base, PlatformRiscV64::regName[tmp_reg_no]);

		// sw src, 0(tmp)
		emit("sw", src, "0(" + PlatformRiscV64::regName[tmp_reg_no] + ")");
	}
}

/// @brief 寄存器Mov操作 - RISCV64使用mv伪指令
/// @param rs_reg_no 结果寄存器
/// @param src_reg_no 源寄存器
void ILocRiscV64::mov_reg(int rs_reg_no, int src_reg_no)
{
	emit("mv", PlatformRiscV64::regName[rs_reg_no], PlatformRiscV64::regName[src_reg_no]);
}

/// @brief 加载变量到寄存器，保证将变量放到reg中
/// @param rs_reg_no 结果寄存器
/// @param src_var 源操作数
void ILocRiscV64::load_var(int rs_reg_no, Value * src_var)
{

	if (Instanceof(constVal, ConstInt *, src_var)) {
		// 若src_var是常量，则直接加载常量值

		load_imm(rs_reg_no, constVal->getVal());
	} else if (src_var->getRegId() != -1) { 
		// 若src_var是寄存器变量，则直接mov到目标寄存器

		int32_t src_regId = src_var->getRegId();

		if (src_regId != rs_reg_no) { 
			// 寄存器不一样才需要mov操作
			// mv rd, rs | 这里有优化空间——消除冗余mv
			emit("mv", PlatformRiscV64::regName[rs_reg_no], PlatformRiscV64::regName[src_regId]);
		}
	} else if (Instanceof(globalVar, GlobalVariable *, src_var)) {
		// 全局变量

		// 加载全局变量的地址
		// la t0, global_var
		load_symbol(rs_reg_no, globalVar->getName());

		// lw t0, 0(t0) - 加载全局变量的值
		emit("lw", PlatformRiscV64::regName[rs_reg_no], "0(" + PlatformRiscV64::regName[rs_reg_no] + ")");

	} else {

		// 栈+偏移的寻址方式

		// 栈帧偏移
		int32_t var_baseRegId = -1;
		int64_t var_offset = -1;

		bool result = src_var->getMemoryAddr(&var_baseRegId, &var_offset);
		if (!result) {
			minic_log(LOG_ERROR, "BUG");
		}

		// lw rs, offset(base)
		load_base(rs_reg_no, var_baseRegId, var_offset);
	}
}

/// @brief 加载变量地址到寄存器
/// @param rs_reg_no 结果寄存器
/// @param var 变量
void ILocRiscV64::lea_var(int rs_reg_no, Value * var)
{
	// 被加载的变量肯定不是常量！
	// 被加载的变量肯定不是寄存器变量！

	// 目前只考虑局部变量

	// 栈帧偏移
	int32_t var_baseRegId = -1;
	int64_t var_offset = -1;

	bool result = var->getMemoryAddr(&var_baseRegId, &var_offset);
	if (!result) {
		minic_log(LOG_ERROR, "BUG");
	}

	// addi rs, base, offset
	leaStack(rs_reg_no, var_baseRegId, var_offset);
}

/// @brief 保存寄存器到变量，保证将计算结果保存到变量
/// @param src_reg_no 源寄存器
/// @param dest_var  变量
/// @param tmp_reg_no 第三方寄存器
void ILocRiscV64::store_var(int src_reg_no, Value * dest_var, int tmp_reg_no)
{
	// 被保存目标变量肯定不是常量

	if (dest_var->getRegId() != -1) {

		// 寄存器变量

		// -1表示非寄存器，其他表示寄存器的索引值
		int dest_reg_id = dest_var->getRegId();

		// 寄存器不一样才需要mv操作
		if (src_reg_no != dest_reg_id) {

			// mv rd, rs | 这里有优化空间——消除冗余mv
			emit("mv", PlatformRiscV64::regName[dest_reg_id], PlatformRiscV64::regName[src_reg_no]);
		}

	} else if (Instanceof(globalVar, GlobalVariable *, dest_var)) {
		// 全局变量

		// 加载符号的地址到临时寄存器
		// la tmp, global_var
		load_symbol(tmp_reg_no, globalVar->getName());

		// sw src, 0(tmp)
		emit("sw", PlatformRiscV64::regName[src_reg_no], "0(" + PlatformRiscV64::regName[tmp_reg_no] + ")");

	} else {

		// 对于局部变量，则直接从栈基址+偏移寻址

		// 栈帧偏移
		int32_t dest_baseRegId = -1;
		int64_t dest_offset = -1;

		bool result = dest_var->getMemoryAddr(&dest_baseRegId, &dest_offset);
		if (!result) {
			minic_log(LOG_ERROR, "BUG");
		}

		// sw src, offset(base)
		store_base(src_reg_no, dest_baseRegId, dest_offset, tmp_reg_no);
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
/// RISCV64 prologue: addi sp,sp,-framesize; sd ra,off(sp); sd s0,off(sp); addi s0,sp,framesize
/// @param func 函数
/// @param tmp_reg_no 临时寄存器编号
void ILocRiscV64::allocStack(Function * func, int tmp_reg_no)
{
	// 计算栈帧大小
	int off = func->getMaxDep();

	// 不需要在栈内额外分配空间，则什么都不做
	if (0 == off) {
		return;
	}

	// RISCV64 prologue:
	// addi sp, sp, -framesize
	// 取负值作为偏移量，让栈指针向下增长(低地址)framesize字节
	if (PlatformRiscV64::constExpr(-off)) {
		emit("addi", "sp", "sp", std::to_string(-off));
	} else {
		// 偏移超出12位范围
		load_imm(tmp_reg_no, -off);
		emit("add", "sp", "sp", PlatformRiscV64::regName[tmp_reg_no]);
	}

	// sd ra, framesize-8(sp) - 保存返回地址
	int ra_offset = off - 8;
	if (PlatformRiscV64::isDisp(ra_offset)) {
		emit("sd", "ra", std::to_string(ra_offset) + "(sp)");
	} else {
		load_imm(tmp_reg_no, ra_offset);
		emit("add", PlatformRiscV64::regName[tmp_reg_no], "sp", PlatformRiscV64::regName[tmp_reg_no]);
		emit("sd", "ra", "0(" + PlatformRiscV64::regName[tmp_reg_no] + ")");
	}

	// sd s0, framesize-16(sp) - 保存帧指针
	int fp_offset = off - 16;
	if (PlatformRiscV64::isDisp(fp_offset)) {
		emit("sd", "s0", std::to_string(fp_offset) + "(sp)");
	} else {
		load_imm(tmp_reg_no, fp_offset);
		emit("add", PlatformRiscV64::regName[tmp_reg_no], "sp", PlatformRiscV64::regName[tmp_reg_no]);
		emit("sd", "s0", "0(" + PlatformRiscV64::regName[tmp_reg_no] + ")");
	}

	// addi s0, sp, framesize - 设置帧指针
	if (PlatformRiscV64::constExpr(off)) {
		emit("addi", "s0", "sp", std::to_string(off));
	} else {
		load_imm(tmp_reg_no, off);
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
	emit("");
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
	// 此处为空实现，与ARM32的ldr_args对称
	// ARM32的ldr_args也是空实现，参数加载在CodeGeneratorArm32中处理
}
