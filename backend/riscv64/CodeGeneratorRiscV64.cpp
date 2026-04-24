///
/// @file CodeGeneratorRiscV64.cpp
/// @brief RISCV64汇编代码生成器的实现
///
/// 负责将SSA IR编译为RISC-V64汇编代码，包括：
/// - 寄存器分配（通过GreedyRegAllocator）
/// - 指令选择（通过InstSelectorRiscV64）
/// - 栈帧布局计算与栈槽分配
/// - 汇编代码输出
///
#include "CodeGeneratorRiscV64.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "CallInst.h"
#include "Function.h"
#include "GlobalVariable.h"
#include "ILocRiscV64.h"
#include "InstSelectorRiscV64.h"
#include "Instruction.h"
#include "PlatformRiscV64.h"
#include "Value.h"

namespace {

/// @brief 保存的callee-saved寄存器占用的栈帧字节数
/// RISC-V64的callee-saved寄存器：ra, s0-s11, 共13个64位寄存器 = 104字节
constexpr int kSavedFrameBytes = 104;

/// @brief 将value向上对齐到align的倍数
/// @param value 待对齐的值
/// @param align 对齐粒度
/// @return 对齐后的值
int alignTo(int value, int align)
{
	return ((value + align - 1) / align) * align;
}

/// @brief 计算栈槽大小（按4字节对齐，最小4字节）
/// @param val 待计算栈槽大小的Value
/// @return 栈槽字节数
int stackSlotSize(Value * val)
{
	int size = 4;
	if (auto * allocaInst = dynamic_cast<AllocaInst *>(val)) {
		// AllocaInst按其分配类型的大小计算
		size = allocaInst->getAllocaType()->getSize();
	} else if (val != nullptr && val->getType() != nullptr && val->getType()->getSize() > 0) {
		// 其他Value按其类型大小计算
		size = val->getType()->getSize();
	}
	return std::max(4, alignTo(size, 4));
}

/// @brief 计算函数中所有调用指令的最大参数个数
/// @param func 待计算的函数
/// @return 最大参数个数
int maxCallArgCount(Function * func)
{
	int result = 0;
	for (auto * bb: func->getBlocks()) {
		for (auto * inst: bb->getInstructions()) {
			if (auto * call = dynamic_cast<CallInst *>(inst)) {
				result = std::max(result, call->getArgCount());
			}
		}
	}
	return result;
}

} // namespace

/// @brief 构造函数
/// @param _module 待编译的IR模块
CodeGeneratorRiscV64::CodeGeneratorRiscV64(Module * _module) : CodeGeneratorAsm(_module)
{}

/// @brief 生成汇编文件头部，输出RISC-V64架构属性
void CodeGeneratorRiscV64::genHeader()
{
	std::fprintf(fp, "%s\n", "# .arch riscv");
	std::fprintf(fp, "%s\n", "# .option pic0");
	std::fprintf(fp, "%s\n", ".attribute arch, \"rv64gc\"");
	std::fprintf(fp, "%s\n", ".option nopic");
}

/// @brief 生成数据段，输出全局变量定义
void CodeGeneratorRiscV64::genDataSection()
{
	std::fprintf(fp, ".text\n");

	bool emittedDataSection = false;
	for (auto * var: module->getGlobalVariables()) {
		// BSS段变量使用.comm伪指令
		if (var->isInBSSSection()) {
			std::fprintf(fp, ".comm %s, %d, %d\n", var->getName().c_str(), var->getType()->getSize(),
				var->getAlignment());
			continue;
		}

		// 已初始化的全局变量输出到.data段
		std::fprintf(fp, ".global %s\n", var->getName().c_str());
		std::fprintf(fp, ".data\n");
		std::fprintf(fp, ".align %d\n", var->getAlignment());
		std::fprintf(fp, ".type %s, %%object\n", var->getName().c_str());
		std::fprintf(fp, ".size %s, %d\n", var->getName().c_str(), var->getType()->getSize());
		std::fprintf(fp, "%s:\n", var->getName().c_str());
		std::fprintf(fp, ".word %d\n", var->getInitIntValue());
		emittedDataSection = true;
	}

	// 数据段结束后回到代码段
	if (emittedDataSection) {
		std::fprintf(fp, ".text\n");
	}
}

/// @brief 生成函数的代码段
/// @param func 待生成的函数
///
/// 流程：寄存器分配 -> 指令选择 -> 删除无用标签 -> 输出汇编
void CodeGeneratorRiscV64::genCodeSection(Function * func)
{
	// 执行寄存器分配
	registerAllocation(func);

	// 创建底层汇编序列，设置寄存器分配信息和栈帧大小
	ILocRiscV64 iloc(module);
	iloc.setRegAllocMap(greedyAllocator.getAllocationMap());
	iloc.setFrameSize(greedyAllocator.getFrameSize());

	// 执行指令选择，将IR翻译为RISC-V64汇编指令
	InstSelectorRiscV64 instSelector(func, iloc, greedyAllocator);
	instSelector.setShowLinearIR(showLinearIR);
	instSelector.run();

	// 删除未被引用的基本块标签
	iloc.deleteUnusedLabel();

	// 输出函数头部信息
	std::fprintf(fp, ".align %d\n", func->getAlignment());
	std::fprintf(fp, ".global %s\n", func->getName().c_str());
	std::fprintf(fp, ".type %s, %%function\n", func->getName().c_str());
	std::fprintf(fp, "%s:\n", func->getName().c_str());

	// 调试模式下输出每个IR值的寄存器/栈位置映射
	if (showLinearIR) {
		for (auto & [val, info]: greedyAllocator.getAllocationMap()) {
			(void) info;
			std::string str;
			getIRValueStr(val, str);
			if (!str.empty()) {
				std::fprintf(fp, "%s\n", str.c_str());
			}
		}
	}

	// 输出汇编指令序列
	iloc.outPut(fp);
	std::fprintf(fp, ".size %s, .-%s\n", func->getName().c_str(), func->getName().c_str());
}

/// @brief 执行寄存器分配，包括Greedy分配和栈空间分配
/// @param func 待分配寄存器的函数
void CodeGeneratorRiscV64::registerAllocation(Function * func)
{
	// 内建函数不需要寄存器分配
	if (func == nullptr || func->isBuiltin()) {
		return;
	}

	// 执行Greedy寄存器分配
	greedyAllocator.allocate(func);
	// 调整函数调用和形参指令（RISC-V64暂不需要额外调整）
	adjustFuncCallInsts(func);
	adjustFormalParamInsts(func);
	// 为未分配寄存器和溢出的变量分配栈槽
	stackAlloc(func);
}

/// @brief 栈空间分配，为局部变量、溢出变量和超出寄存器传递的形参分配栈槽
/// @param func 待分配栈空间的函数
///
/// 栈帧布局（从高地址到低地址）：
/// - caller的栈帧
/// - 返回地址和callee-saved寄存器（kSavedFrameBytes字节）
/// - 局部变量和溢出变量（localBytes字节）
/// - 超过8个参数的调用参数（outgoingBytes字节）
///
void CodeGeneratorRiscV64::stackAlloc(Function * func)
{
	auto & allocMap = greedyAllocator.getAllocationMap();

	int localBytes = 0;
	// 为Value分配栈槽，偏移量相对于FP寄存器为负方向
	auto assignStackSlot = [&](Value * val) {
		auto & info = allocMap[val];
		if (info.hasStackSlot) {
			return;
		}

		localBytes += stackSlotSize(val);
		info.setStack(RISCV64_FP_REG_NO, -(kSavedFrameBytes + localBytes));
	};

	// 为所有形参创建分配信息
	for (auto * param: func->getParams()) {
		allocMap.try_emplace(param, RegAllocInfo{});
	}

	// 超过8个寄存器参数(a0-a7)的形参通过栈传递，位于FP正方向偏移
	for (int i = 8; i < static_cast<int>(func->getParams().size()); ++i) {
		auto * param = func->getParams()[i];
		auto & info = allocMap[param];
		info.regId = -1;
		info.setStack(RISCV64_FP_REG_NO, (i - 8) * 8);
	}

	// 遍历所有指令，为AllocaInst和有结果值的指令创建分配信息
	for (auto * bb: func->getBlocks()) {
		for (auto * inst: bb->getInstructions()) {
			if (dynamic_cast<AllocaInst *>(inst) != nullptr) {
				assignStackSlot(inst);
				continue;
			}

			if (inst->hasResultValue()) {
				allocMap.try_emplace(inst, RegAllocInfo{});
			}
		}
	}

	// 为强制栈分配、未分配寄存器或被溢出的变量分配栈槽
	for (auto & [val, info]: allocMap) {
		if (val == nullptr) {
			continue;
		}

		if (GreedyRegAllocator::isForcedStackValue(val) || info.regId == -1 || greedyAllocator.isSpilled(val)) {
			assignStackSlot(val);
		}
	}

	// 计算栈帧总大小：callee-saved + 局部变量 + 超出寄存器传递的调用参数，16字节对齐
	const int maxArgs = maxCallArgCount(func);
	const int outgoingBytes = maxArgs > 8 ? (maxArgs - 8) * 8 : 0;
	const int frameSize = alignTo(kSavedFrameBytes + localBytes + outgoingBytes, 16);
	greedyAllocator.setOutgoingArgBytes(outgoingBytes);
	greedyAllocator.setFrameSize(frameSize);
}

/// @brief 调整函数调用指令（RISC-V64暂不需要额外调整）
/// @param func 待调整的函数
void CodeGeneratorRiscV64::adjustFuncCallInsts(Function * func)
{
	(void) func;
}

/// @brief 调整形参指令（RISC-V64暂不需要额外调整）
/// @param func 待调整的函数
void CodeGeneratorRiscV64::adjustFormalParamInsts(Function * func)
{
	(void) func;
}

/// @brief 获取IR值的字符串表示，用于调试输出
/// @param val IR值
/// @param str 输出的字符串，格式为 "# name:reg" 或 "# name:offset(base)"
///
/// 显示IR值对应的寄存器编号或栈位置信息，便于调试时查看分配结果
void CodeGeneratorRiscV64::getIRValueStr(Value * val, std::string & str)
{
	auto & allocMap = greedyAllocator.getAllocationMap();
	auto it = allocMap.find(val);
	if (it == allocMap.end()) {
		return;
	}

	// 构造显示名称：优先使用 name:irName 格式
	std::string showName;
	if (!val->getName().empty() && !val->getIRName().empty()) {
		showName = val->getName() + ":" + val->getIRName();
	} else if (!val->getIRName().empty()) {
		showName = val->getIRName();
	} else {
		showName = val->getName();
	}

	if (showName.empty()) {
		return;
	}

	// 根据分配结果输出寄存器或栈位置
	if (it->second.hasReg()) {
		str = "\t# " + showName + ":" + PlatformRiscV64::regName[it->second.regId];
	} else if (it->second.hasStackSlot) {
		str = "\t# " + showName + ":" + std::to_string(it->second.offset) + "(" +
			  PlatformRiscV64::regName[it->second.baseRegId] + ")";
	}
}
