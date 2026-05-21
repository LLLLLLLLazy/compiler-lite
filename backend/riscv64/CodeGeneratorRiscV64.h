///
/// @file CodeGeneratorRiscV64.h
/// @brief RISCV64汇编代码生成器的头文件
///
#pragma once

#include <string>
#include <vector>

#include "CodeGeneratorAsm.h"
#include "GreedyRegAllocator.h"

/// @brief RISCV64汇编代码生成器
class CodeGeneratorRiscV64 : public CodeGeneratorAsm {

public:
	/// @brief 构造函数
	/// @param module 待编译的IR模块
	/// @param enableCalleeSavedFPR 是否启用 callee-saved FPR
	/// @param enableCoalesce 是否启用寄存器合并
	/// @param enableSplit 是否启用活跃区间分裂
	/// @param raStatsJsonPath 若非空，则输出寄存器分配JSON统计到该路径
	explicit CodeGeneratorRiscV64(Module * module,
	                             bool enableCalleeSavedFPR = false,
	                             bool enableCoalesce = false,
	                             bool enableSplit = false,
	                             std::string raStatsJsonPath = "");
	~CodeGeneratorRiscV64() override = default;
	using CodeGenerator::run;

protected:
	/// @brief 产生汇编文件
	bool run() override;

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
	/// @brief 当前模块是否使用内置循环并行运行时
	bool moduleUsesMtRuntime() const;

	/// @brief 输出内置循环并行运行时汇编
	void emitMtRuntime();


	/// @brief 将本模块的寄存器分配评测指标写为JSON
	/// @return 写入是否成功；若未配置输出路径则直接返回true
	bool writeRAStatsJson() const;

	/// @brief 单个函数的RA统计报告，用于JSON输出
	struct FunctionRAReport {
		std::string functionName;              ///< 函数名
		RegAllocStats regAllocStats;           ///< 寄存器分配统计（分配区间数、溢出数等）
		int frameSize = 0;                     ///< 栈帧大小（字节）
		std::vector<int> usedCalleeSavedGPRs;  ///< 实际使用的callee-saved GPR编号
		std::vector<int> usedCalleeSavedFPRs;  ///< 实际使用的callee-saved FPR编号
		RiscV64CodegenStats codegenStats;      ///< 代码生成统计（指令数、栈访问数等）
	};


	/// @brief Greedy寄存器分配器实例
	GreedyRegAllocator greedyAllocator;

	/// @brief 当前函数需要保存的callee-saved寄存器编号
	std::vector<int> currentSavedRegs;

	/// @brief 当前函数需要保存的callee-saved FPR编号
	std::vector<int> currentSavedFPRs;

	/// @brief 当前代码生成器启用的RA配置
	bool enableCalleeSavedFPR_ = false;  ///< 是否启用callee-saved FPR分配
	bool enableCoalesce_ = false;        ///< 是否启用寄存器合并（coalescing）
	bool enableSplit_ = false;           ///< 是否启用活跃区间分裂

	/// @brief 机器可读RA统计输出路径，为空则不输出
	std::string raStatsJsonPath_;

	/// @brief 模块内所有函数的RA评测记录，在run()结束时统一写为JSON
	std::vector<FunctionRAReport> raReports_;
};
