///
/// @file InstSelectorRiscV64.h
/// @brief RISCV64结构化IR指令选择器的头文件
///
#pragma once

#include <map>
#include <string>

#include "Function.h"
#include "GreedyRegAllocator.h"
#include "ILocRiscV64.h"
#include "Instruction.h"
#include "LocalTempManager.h"

class BasicBlock;
class Value;

/// @brief RISCV64结构化IR指令选择器
///
/// 将SSA IR指令逐条翻译为RISC-V64汇编指令，包括：
/// - 算术逻辑指令（add, sub, mul, div, mod）
/// - 内存指令（alloca, load, store）
/// - 控制流指令（br, cond_br, ret, call）
/// - 类型转换指令（zext, copy, phi）
/// - 函数prologue/epilogue生成
///
class InstSelectorRiscV64 {

public:
	/// @brief 构造函数
	/// @param func 待翻译的函数
	/// @param iloc 底层汇编序列
	/// @param allocator 寄存器分配器
	InstSelectorRiscV64(Function * func, ILocRiscV64 & iloc, GreedyRegAllocator & allocator);
	~InstSelectorRiscV64() = default;

	/// @brief 执行指令选择，遍历函数的所有指令并翻译
	void run();

	/// @brief 设置是否输出线性IR（调试用）
	/// @param show 是否输出
	void setShowLinearIR(bool show)
	{
		showLinearIR = show;
	}

private:
	struct OperandReg {
		int reg = -1;
		bool borrowed = false;
	};

	/// @brief 指令翻译处理函数类型
	typedef void (InstSelectorRiscV64::*translate_handler)(Instruction *);

	/// @brief 根据指令操作码分派到对应的翻译函数
	/// @param inst IR指令
	void translate(Instruction * inst);

	/// @brief 输出IR指令的文本表示（调试用）
	/// @param inst IR指令
	void outputIRInstruction(Instruction * inst);

	/// @brief 翻译alloca指令（栈空间分配）
	void translate_alloca(Instruction * inst);
	/// @brief 翻译load指令（内存读取）
	void translate_load(Instruction * inst);
	/// @brief 翻译store指令（内存写入）
	void translate_store(Instruction * inst);
	/// @brief 翻译add指令（加法）
	void translate_add(Instruction * inst);
	/// @brief 翻译sub指令（减法）
	void translate_sub(Instruction * inst);
	/// @brief 翻译mul指令（乘法）
	void translate_mul(Instruction * inst);
	/// @brief 翻译div指令（除法）
	void translate_div(Instruction * inst);
	/// @brief 翻译mod指令（取模）
	void translate_mod(Instruction * inst);
	/// @brief 翻译icmp指令（整数比较）
	void translate_icmp(Instruction * inst);
	/// @brief 翻译br指令（无条件跳转）
	void translate_br(Instruction * inst);
	/// @brief 翻译cond_br指令（条件跳转）
	void translate_cond_br(Instruction * inst);
	/// @brief 翻译ret指令（函数返回）
	void translate_ret(Instruction * inst);
	/// @brief 翻译call指令（函数调用）
	void translate_call(Instruction * inst);
	/// @brief 翻译phi指令（φ节点，SSA合并）
	void translate_phi(Instruction * inst);
	/// @brief 翻译zext指令（零扩展）
	void translate_zext(Instruction * inst);
	/// @brief 翻译copy指令（寄存器复制）
	void translate_copy(Instruction * inst);
	/// @brief 翻译元素地址计算指令
	void translate_gep(Instruction * inst);
	/// @brief 翻译二元运算指令的通用实现
	/// @param inst IR指令
	/// @param op RISC-V汇编操作码（如"add", "sub"）
	void translate_binary(Instruction * inst, const std::string & op);

	/// @brief 生成形参从a0-a7到分配寄存器的移动指令
	void emitFormalParamMoves();
	/// @brief 生成函数epilogue（恢复callee-saved寄存器并返回）
	void emitEpilogue();
	/// @brief 生成64位加载指令（sd/ld）
	/// @param reg 寄存器名
	/// @param offset 栈偏移
	/// @param tmpReg 临时寄存器编号（用于大偏移地址计算）
	void emitLoad64(const std::string & reg, int offset, int tmpReg);
	/// @brief 生成栈指针调整指令
	/// @param amount 调整量
	/// @param tmpReg 临时寄存器编号（用于大偏移地址计算）
	void emitStackAdjust(int amount, int tmpReg);

	/// @brief 获取Value分配的结果寄存器编号
	/// @param val IR值
	/// @return 物理寄存器编号
	int getResultReg(Value * val) const;

	/// @brief 获取只读操作数所在寄存器，必要时借用临时寄存器加载
	/// @param val 操作数
	/// @param inst 当前IR指令
	/// @param excludeReg 借用临时寄存器时需要排除的寄存器
	/// @param preferredReg 可直接承载该操作数的首选寄存器
	/// @return 操作数寄存器及是否需要释放
	OperandReg loadOperand(Value * val, Instruction * inst, int excludeReg = -1, int preferredReg = -1);

	/// @brief 释放通过loadOperand借用的临时寄存器
	void releaseOperand(const OperandReg & operand);

	/// @brief 将寄存器值存储到Value的目标位置
	/// @param val 目标Value
	/// @param srcReg 源寄存器编号
	/// @param inst 当前IR指令（用于临时寄存器借用时的活跃性查询）
	void storeResult(Value * val, int srcReg, Instruction * inst = nullptr);

	/// @brief 生成基本块对应的标签名
	/// @param bb 基本块
	/// @return 标签名字符串
	std::string blockLabel(BasicBlock * bb) const;

	/// @brief 清理标签名中的非法字符
	/// @param text 原始文本
	/// @return 清理后的标签名
	std::string sanitizeLabelPart(const std::string & text) const;

	/// @brief 待翻译的函数
	Function * func = nullptr;

	/// @brief 底层汇编序列引用
	ILocRiscV64 & iloc;

	/// @brief 寄存器分配器引用
	GreedyRegAllocator & allocator;

	/// @brief IR操作码到翻译处理函数的映射表
	std::map<IRInstOperator, translate_handler> translatorHandlers;

	/// @brief 是否输出线性IR（调试用）
	bool showLinearIR = false;

	/// @brief 动态临时寄存器管理器
	LocalTempManager tempMgr;
};
