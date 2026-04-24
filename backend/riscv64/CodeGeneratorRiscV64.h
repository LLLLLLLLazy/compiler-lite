///
/// @file CodeGeneratorRiscV64.h
/// @brief RISCV64汇编代码生成器的头文件
///
#pragma once

#include <string>

#include "CodeGeneratorAsm.h"
#include "GreedyRegAllocator.h"

/// @brief RISCV64汇编代码生成器
class CodeGeneratorRiscV64 : public CodeGeneratorAsm {

public:
	/// @brief 构造函数
	/// @param module 待编译的IR模块
	explicit CodeGeneratorRiscV64(Module * module);
	~CodeGeneratorRiscV64() override = default;
	using CodeGenerator::run;

protected:
	/// @brief 生成汇编文件头部（架构属性等）
	void genHeader() override;

	/// @brief 生成数据段（全局变量）
	void genDataSection() override;

	/// @brief 生成代码段（函数体）
	/// @param func 待生成的函数
	void genCodeSection(Function * func) override;

	/// @brief 执行寄存器分配
	/// @param func 待分配寄存器的函数
	void registerAllocation(Function * func) override;

	/// @brief 栈空间分配，为局部变量和溢出变量分配栈槽
	/// @param func 待分配栈空间的函数
	void stackAlloc(Function * func);

	/// @brief 调整函数调用指令（RISC-V64暂不需要额外调整）
	/// @param func 待调整的函数
	void adjustFuncCallInsts(Function * func);

	/// @brief 调整形参指令（RISC-V64暂不需要额外调整）
	/// @param func 待调整的函数
	void adjustFormalParamInsts(Function * func);

	/// @brief 获取IR值的字符串表示（用于调试输出，显示寄存器/栈位置）
	/// @param val IR值
	/// @param str 输出的字符串
	void getIRValueStr(Value * val, std::string & str);

private:
	/// @brief Greedy寄存器分配器实例
	GreedyRegAllocator greedyAllocator;
};
