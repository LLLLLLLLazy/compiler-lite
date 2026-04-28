#pragma once

#include <list>
#include <string>
#include <unordered_map>

#include "Module.h"
#include "Value.h"

#define Instanceof(res, type, var) auto res = dynamic_cast<type>(var)

/// @brief 寄存器分配信息（适配SSA IR）
///
/// 新的SSA IR的Value类没有getRegId()/getMemoryAddr()方法，
/// 因此寄存器分配和栈位置信息通过此辅助结构管理。
/// GreedyRegAllocator在分配完成后，为每个Value填充此信息。
///
struct RegAllocInfo {
	/// @brief 分配的物理寄存器编号，-1表示未分配（在栈上）
	int32_t regId = -1;

	/// @brief 栈帧基址寄存器编号（通常为RISCV64_FP_REG_NO或RISCV64_SP_REG_NO）
	int32_t baseRegId = -1;

	/// @brief 栈帧偏移量（相对于baseRegId）
	int64_t offset = 0;

	/// @brief 是否在栈上分配了空间
	bool hasStackSlot = false;

	/// @brief 判断是否分配了寄存器
	bool hasReg() const { return regId != -1; }

	/// @brief 设置寄存器分配
	void setReg(int32_t reg) { regId = reg; }

	/// @brief 设置栈位置
	void setStack(int32_t base, int64_t off)
	{
		baseRegId = base;
		offset = off;
		hasStackSlot = true;
	}
};

/// @brief 底层汇编指令：RISC-V64
struct RiscV64Inst {

	/// @brief 操作码
	std::string opcode;

	/// @brief 条件（RISC-V不使用，保留兼容）
	std::string cond;

	/// @brief 结果
	std::string result;

	/// @brief 源操作数1
	std::string arg1;

	/// @brief 源操作数2
	std::string arg2;

	/// @brief 附加信息
	std::string addition;

	/// @brief 标识指令是否无效
	bool dead;

	/// @brief 构造函数
	RiscV64Inst(
		std::string op,
		std::string rs = "",
		std::string s1 = "",
		std::string s2 = "",
		std::string cond = "",
		std::string extra = "");

	/// @brief 指令更新
	void replace(
		std::string op,
		std::string rs = "",
		std::string s1 = "",
		std::string s2 = "",
		std::string cond = "",
		std::string extra = "");

	/// @brief 设置死指令
	void setDead();

	/// @brief 指令字符串输出函数
	std::string outPut();
};

/// @brief 底层汇编序列-RISC-V64
///
/// 适配SSA IR：
/// - load_var/store_var通过RegAllocInfo获取寄存器/栈位置信息
/// - 不再依赖Value::getRegId()/Value::getMemoryAddr()
/// - 支持BasicBlock标签输出
///
class ILocRiscV64 {

	/// @brief RISC-V64汇编序列
	std::list<RiscV64Inst *> code;

	/// @brief 符号表
	Module * module;

	/// @brief 寄存器分配信息映射表（Value* -> RegAllocInfo）
	/// 由CodeGeneratorRiscV64在寄存器分配后填充
	std::unordered_map<Value *, RegAllocInfo> regAllocMap;

	/// @brief 已计算好的函数栈帧大小
	int frameSize = 0;

public:
	/// @brief 构造函数
	/// @param _module 符号表-模块
	ILocRiscV64(Module * _module);

	/// @brief 析构函数
	~ILocRiscV64();

	/// @brief 设置寄存器分配信息映射表
	/// @param allocMap 寄存器分配信息（由GreedyRegAllocator产生）
	void setRegAllocMap(const std::unordered_map<Value *, RegAllocInfo> & allocMap);

	/// @brief 获取某个Value的寄存器分配信息
	/// @param val IR值
	/// @return 寄存器分配信息引用
	RegAllocInfo & getRegAllocInfo(Value * val);

	/// @brief 设置函数栈帧大小
	/// @param size 栈帧字节数
	void setFrameSize(int size)
	{
		frameSize = size;
	}

	/// @brief 获取函数栈帧大小
	/// @return 栈帧字节数
	int getFrameSize() const
	{
		return frameSize;
	}

	/// @brief 注释指令，RISCV64使用#作为注释符
	/// @param str 注释内容
	void comment(std::string str);

	/// @brief 数字变字符串，RISCV64立即数不需要#前缀
	/// @param num 立即数
	/// @param flag 是否加#（保留兼容，RISCV64忽略）
	/// @return 字符串
	std::string toStr(int num, bool flag = true);

	/// @brief 获取当前的代码序列
	/// @return 代码序列
	std::list<RiscV64Inst *> & getCode();

	/// @brief 加载立即数 li rd, imm 或 lui+addi
	/// @param rs_reg_no 结果寄存器号
	/// @param num 立即数
	void load_imm(int rs_reg_no, int num);

	/// @brief 加载符号地址 la rd, symbol
	/// @param rs_reg_no 结果寄存器号
	/// @param name Label名字
	void load_symbol(int rs_reg_no, std::string name);

	/// @brief Load指令，基址寻址 lw rs, offset(base)
	/// @param rs_reg_no 结果寄存器
	/// @param base_reg_no 基址寄存器
	/// @param disp 偏移
	void load_base(int rs_reg_no, int base_reg_no, int disp, bool wide = false);

	/// @brief Store指令，基址寻址 sw src, offset(base)
	/// @param src_reg_no 源寄存器
	/// @param base_reg_no 基址寄存器
	/// @param disp 偏移
	/// @param tmp_reg_no 可能需要临时寄存器编号
	void store_base(int src_reg_no, int base_reg_no, int disp, int tmp_reg_no, bool wide = false);

	/// @brief 标签指令（用于BasicBlock标签输出）
	/// @param name 标签名
	void label(std::string name);

	/// @brief 0个源操作数指令
	void inst(std::string op, std::string rs);

	/// @brief 一个操作数指令
	void inst(std::string op, std::string rs, std::string arg1);

	/// @brief 两个操作数指令
	void inst(std::string op, std::string rs, std::string arg1, std::string arg2);

	/// @brief 加载变量到寄存器
	/// 适配SSA IR：通过regAllocMap获取寄存器/栈位置信息
	/// @param rs_reg_no 结果寄存器
	/// @param src_var 源操作数（Value*）
	void load_var(int rs_reg_no, Value * src_var);

	/// @brief 加载变量地址到寄存器
	/// @param rs_reg_no 结果寄存器
	/// @param var 变量
	void lea_var(int rs_reg_no, Value * var);

	/// @brief 加载栈内变量地址
	/// @param rs_reg_no 结果寄存器号
	/// @param base_reg_no 基址寄存器
	/// @param off 偏移
	void leaStack(int rs_reg_no, int base_reg_no, int offset);

	/// @brief 保存寄存器到变量
	/// 适配SSA IR：通过regAllocMap获取寄存器/栈位置信息
	/// @param src_reg_no 源寄存器号
	/// @param dest_var 目标变量（Value*）
	/// @param tmp_reg_no 临时寄存器编号
	void store_var(int src_reg_no, Value * dest_var, int tmp_reg_no);

	/// @brief 寄存器Mov操作 mv rd, rs
	/// @param rs_reg_no 结果寄存器
	/// @param src_reg_no 源寄存器
	void mov_reg(int rs_reg_no, int src_reg_no);

	/// @brief 调用函数fun call name
	/// @param name 函数名
	void call_fun(std::string name);

	/// @brief 分配栈帧（RISCV64 prologue）
	/// addi sp,sp,-framesize; sd ra,off(sp); sd s0,off(sp); addi s0,sp,framesize
	/// @param func 函数
	/// @param tmp_reg_no 临时寄存器编号
	void allocStack(Function * func, int tmp_reg_no);

	/// @brief 加载函数的参数到寄存器（空实现，由CodeGeneratorRiscV64处理）
	/// @param fun 函数
	void ldr_args(Function * fun);

	/// @brief NOP操作
	void nop();

	/// @brief 无条件跳转指令 j label
	/// @param label 目标Label名称
	void jump(std::string label);

	/// @brief 输出汇编
	/// @param file 输出的文件指针
	/// @param outputEmpty 是否输出空语句
	void outPut(FILE * file, bool outputEmpty = false);

	/// @brief 删除无用的Label指令
	void deleteUnusedLabel();
};
