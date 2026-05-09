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
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "AllocaInst.h"
#include "ArrayType.h"
#include "BasicBlock.h"
#include "CallInst.h"
#include "Function.h"
#include "GlobalVariable.h"
#include "ILocRiscV64.h"
#include "InstSelectorRiscV64.h"
#include "Instruction.h"
#include "LocalTempManager.h"
#include "PlatformRiscV64.h"
#include "RiscV64Peephole.h"
#include "ScratchAllocator.h"
#include "Value.h"

namespace {

/// @brief 将value向上对齐到align的倍数
/// @param value 待对齐的值
/// @param align 对齐粒度
/// @return 对齐后的值
int alignTo(int value, int align)
{
	return ((value + align - 1) / align) * align;
}

/// @brief 获取Value在栈中的对象类型
/// @param val 待查询的Value
/// @return 实际占用栈槽的对象类型
Type * stackObjectType(Value * val)
{
	if (auto * allocaInst = dynamic_cast<AllocaInst *>(val)) {
		return allocaInst->getAllocaType();
	}
	if (auto * globalVar = dynamic_cast<GlobalVariable *>(val)) {
		return globalVar->getValueType();
	}
	return val != nullptr ? val->getType() : nullptr;
}

/// @brief 计算Value在RV64栈上的自然对齐
/// @param type 栈对象类型
/// @return 需要满足的对齐字节数
int stackSlotAlignment(Type * type)
{
	if (type == nullptr) {
		return 4;
	}
	if (type->isPointerType()) {
		return 8;
	}
	if (auto * arrayType = dynamic_cast<ArrayType *>(type)) {
		return stackSlotAlignment(arrayType->getElementType());
	}
	return 4;
}

/// @brief 计算栈槽大小，按对象自然对齐并保证最小4字节
/// @param val 待计算栈槽大小的Value
/// @return 栈槽字节数
int stackSlotSize(Value * val)
{
	Type * slotType = stackObjectType(val);
	int size = 4;
	if (slotType != nullptr && slotType->getSize() > 0) {
		size = slotType->getSize();
	}
	return std::max(4, alignTo(size, stackSlotAlignment(slotType)));
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

/// @brief 判断函数中是否包含函数调用指令
/// @param func 待检查的函数
/// @return 是否包含CallInst
bool hasCallInst(Function * func)
{
	for (auto * bb: func->getBlocks()) {
		for (auto * inst: bb->getInstructions()) {
			if (dynamic_cast<CallInst *>(inst) != nullptr) {
				return true;
			}
		}
	}
	return false;
}

/// @brief 计算当前函数需要在prologue/epilogue中保存的callee-saved寄存器列表
/// @param func 当前函数
/// @param allocMap 寄存器分配映射表
/// @return 需要保存的寄存器编号列表（按栈帧中保存顺序排列）
///
/// 策略：
/// - 若函数包含调用指令，则必须保存ra（返回地址）
/// - 始终保存s0（帧指针，后端固定使用s0作为FP）
/// - 对于s1-s11（编号9,18-27），仅当寄存器分配器实际使用了该寄存器时才保存
std::vector<int> computeSavedRegs(
	Function * func,
	const std::unordered_map<Value *, RegAllocInfo> & allocMap,
	const std::unordered_map<int, std::vector<std::pair<int, int>>> & allocatedGprLiveRanges)
{
	std::vector<int> regs;
	if (hasCallInst(func)) {
		// 函数内有调用，需要保存返回地址寄存器ra
		regs.push_back(RISCV64_RA_REG_NO);
	}

	// 后端当前始终使用s0作为帧指针，必须保存
	regs.push_back(RISCV64_FP_REG_NO);

	// s1-s11的寄存器编号：s1=9, s2=18, s3=19, ..., s11=27
	const std::vector<int> calleeSavedGprs = {
		9, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
	};
	// 仅保存被寄存器分配器实际分配使用的callee-saved寄存器
	for (int reg: calleeSavedGprs) {
		bool used = false;
		for (const auto & [_, info]: allocMap) {
			if (info.hasReg() && info.regId == reg) {
				used = true;
				break;
			}
		}
		if (!used && allocatedGprLiveRanges.find(reg) != allocatedGprLiveRanges.end()) {
			used = true;
		}
		if (used) {
			regs.push_back(reg);
		}
	}

	return regs;
}

bool canOmitLeafFrame(Function * func,
                      const std::unordered_map<Value *, RegAllocInfo> & allocMap,
                      const std::vector<int> & savedRegs,
                      int outgoingArgBytes)
{
	if (hasCallInst(func) || outgoingArgBytes != 0) {
		return false;
	}
	if (savedRegs.size() != 1 || savedRegs.front() != RISCV64_FP_REG_NO) {
		return false;
	}
	for (const auto & [_, info] : allocMap) {
		if (info.hasStackSlot) {
			return false;
		}
	}
	return true;
}

} // namespace

/// @brief 构造函数
/// @param _module 待编译的IR模块
CodeGeneratorRiscV64::CodeGeneratorRiscV64(Module * _module,
                                           bool enableCalleeSavedFPR,
                                           bool enableCoalesce,
                                           bool enableSplit)
	: CodeGeneratorAsm(_module),
	  greedyAllocator(nullptr, enableCalleeSavedFPR, enableCoalesce, enableSplit)
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
	// 输出全局变量定义
	bool emittedDataSection = false;
	for (auto * var: module->getGlobalVariables()) {
		// BSS段变量使用.comm伪指令
		if (var->isInBSSSection()) {
			std::fprintf(fp, ".comm %s, %d, %d\n", var->getName().c_str(), var->getValueType()->getSize(),
				var->getAlignment());
			continue;
		}

		// 已初始化的全局变量输出到.data段
		std::fprintf(fp, ".global %s\n", var->getName().c_str());
		std::fprintf(fp, ".data\n");
		std::fprintf(fp, ".align %d\n", var->getAlignment());
		std::fprintf(fp, ".type %s, %%object\n", var->getName().c_str());
		std::fprintf(fp, ".size %s, %d\n", var->getName().c_str(), var->getValueType()->getSize());
		std::fprintf(fp, "%s:\n", var->getName().c_str());
		if (var->getInitKind() == GlobalVariable::InitKind::Float) {
			// 在整数寄存器中存储浮点数的IEEE 754位模式
			float fval = var->getInitFloatValue();
			std::uint32_t bits = 0;
			std::memcpy(&bits, &fval, sizeof(bits));
			std::fprintf(fp, ".word %u\n", bits);
		} else if (var->getInitKind() == GlobalVariable::InitKind::Int) {
			std::fprintf(fp, ".word %d\n", var->getInitIntValue());
		} else if (var->getInitKind() == GlobalVariable::InitKind::IntArray) {
			for (int32_t v : var->getInitIntArray()) {
				std::fprintf(fp, ".word %d\n", v);
			}
		} else if (var->getInitKind() == GlobalVariable::InitKind::FloatArray) {
			for (float v : var->getInitFloatArray()) {
				std::uint32_t bits = 0;
				std::memcpy(&bits, &v, sizeof(bits));
				std::fprintf(fp, ".word %u\n", bits);
			}
		}
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
/// 流程：寄存器分配 -> 指令选择(含scratch vreg创建) -> scratch分配 -> patchup -> 输出汇编
void CodeGeneratorRiscV64::genCodeSection(Function * func)
{
	// 执行寄存器分配
	registerAllocation(func);
	if (std::getenv("MINIC_RA_STATS") != nullptr) {
		const auto & s = greedyAllocator.getStats();
		std::fprintf(stderr,
		             "[ra-stats] %s assigned=%d(gpr=%d,fpr=%d) spilledIntervals=%d spilledValues=%d "
		             "reloads~=%d stores~=%d copies=%d splits=%d\n",
		             func->getName().c_str(),
		             s.assignedRegIntervals,
		             s.assignedGprIntervals,
		             s.assignedFprIntervals,
		             s.spilledIntervals,
		             s.spilledValues,
		             s.estimatedReloads,
		             s.estimatedSpillStores,
		             s.eliminatedCopies,
		             s.splitCount);
	}
	// 创建底层汇编序列，设置寄存器分配信息和栈帧大小
	ILocRiscV64 iloc(module);
	iloc.setRegAllocMap(greedyAllocator.getAllocationMap());
	// 设置当前函数需要保存的callee-saved寄存器列表
	iloc.setSavedRegs(currentSavedRegs);
	iloc.setSavedFPRs(currentSavedFPRs);
	iloc.setFrameSize(greedyAllocator.getFrameSize());

	// 执行指令选择，将IR翻译为RISC-V64汇编指令
	// 指令选择过程中创建ScratchValue（虚拟寄存器）
	InstSelectorRiscV64 instSelector(func, iloc, greedyAllocator);
	instSelector.setShowLinearIR(showLinearIR);
	instSelector.setEliminatedCopies(greedyAllocator.getEliminatedCopies());
	instSelector.run();

	// Scratch寄存器分配：为ScratchValue分配物理寄存器
	auto & scratchValues = instSelector.getScratchValues();
	if (!scratchValues.empty()) {
		ScratchAllocator scratchAlloc;
		scratchAlloc.allocate(
			scratchValues,
			greedyAllocator.getAllocationMap(),
			greedyAllocator.getValueLiveRanges(),
			greedyAllocator.getAllocatedGprLiveRanges(),
			greedyAllocator.getInstNumbering(),
			iloc.getInstToMIRange(),
			greedyAllocator.getAvailableRegs());

		// 将scratch分配结果写入allocationMap，并为spilled scratch分配栈槽
		auto & allocMap = greedyAllocator.getAllocationMap();
		for (auto & sv : scratchValues) {
			if (!sv.released) {
				continue;
			}
			auto * key = reinterpret_cast<Value *>(sv.identity);
			if (sv.spilled) {
				const int slotSize = 8;
				const int slotEnd = greedyAllocator.getFrameSize() + slotSize;
				const int newFrameSize = alignTo(slotEnd, 16);
				greedyAllocator.setFrameSize(newFrameSize);
				sv.spillSlot = -slotEnd;
				RegAllocInfo info;
				info.setStack(RISCV64_FP_REG_NO, sv.spillSlot);
				allocMap[key] = info;
			} else if (sv.physicalReg >= 0) {
				RegAllocInfo info;
				info.setReg(sv.physicalReg);
				allocMap[key] = info;
			}
		}

		// 更新栈帧大小后需要重新设置
		iloc.setFrameSize(greedyAllocator.getFrameSize());

		// Patchup：替换机器指令中的scratch寄存器编号
		iloc.patchScratchRegs(scratchValues);
	}

	RiscV64Peephole peephole;
	peephole.run(iloc, module->getOptLevel());

	// 删除未被引用的基本块标签
	iloc.deleteUnusedLabel();

	// 输出函数头部信息
	std::fprintf(fp, ".align %d\n", func->getAlignment());
	std::fprintf(fp, ".global %s\n", func->getName().c_str());
	std::fprintf(fp, ".type %s, %%function\n", func->getName().c_str());
	std::fprintf(fp, "%s:\n", func->getName().c_str());

	// 调试模式下输出每个IR值的寄存器/栈位置映射
	if (showLinearIR) {
		const auto & s = greedyAllocator.getStats();
		std::fprintf(fp,
		             "\t# RA stats: assigned=%d(gpr=%d,fpr=%d) spilledIntervals=%d spilledValues=%d "
		             "reloads~=%d stores~=%d copies=%d splits=%d\n",
		             s.assignedRegIntervals,
		             s.assignedGprIntervals,
		             s.assignedFprIntervals,
		             s.spilledIntervals,
		             s.spilledValues,
		             s.estimatedReloads,
		             s.estimatedSpillStores,
		             s.eliminatedCopies,
		             s.splitCount);
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
	// 计算当前函数需要保存的callee-saved寄存器列表
	currentSavedRegs = computeSavedRegs(func,
	                                    greedyAllocator.getAllocationMap(),
	                                    greedyAllocator.getAllocatedGprLiveRanges());
	// 收集被使用的callee-saved FPR
	currentSavedFPRs = greedyAllocator.getUsedCalleeSavedFPRs();
	// 为未分配寄存器和溢出的变量分配栈槽
	stackAlloc(func);
	if (canOmitLeafFrame(func,
	                     greedyAllocator.getAllocationMap(),
	                     currentSavedRegs,
	                     greedyAllocator.getOutgoingArgBytes()) &&
	    currentSavedFPRs.empty()) {
		currentSavedRegs.clear();
		greedyAllocator.setFrameSize(0);
	}
}

/// @brief 栈空间分配，为局部变量、溢出变量和超出寄存器传递的形参分配栈槽
/// @param func 待分配栈空间的函数
///
/// 栈帧布局（从高地址到低地址）：
/// - caller的栈帧
/// - 返回地址和callee-saved寄存器（savedFrameBytes字节）
/// - 局部变量和溢出变量（localBytes字节）
/// - 超过8个参数的调用参数（outgoingBytes字节）
///
void CodeGeneratorRiscV64::stackAlloc(Function * func)
{
	auto & allocMap = greedyAllocator.getAllocationMap();
	// 根据实际保存的callee-saved寄存器数量计算栈帧占用字节数（GPR + FPR）
	const int savedFrameBytes = static_cast<int>(currentSavedRegs.size() + currentSavedFPRs.size()) * 8;

	int localBytes = 0;
	// 为Value分配栈槽，偏移量相对于FP寄存器为负方向
	auto assignStackSlot = [&](Value * val) {
		auto & info = allocMap[val];
		if (info.hasStackSlot) {
			return;
		}

		localBytes = alignTo(localBytes, stackSlotAlignment(stackObjectType(val)));
		localBytes += stackSlotSize(val);
		info.setStack(RISCV64_FP_REG_NO, -(savedFrameBytes + localBytes));
	};

	// 为所有形参创建分配信息
	for (auto * param: func->getParams()) {
		allocMap.try_emplace(param, RegAllocInfo{});
	}

	// RISC-V ABI：整数和浮点参数使用独立的寄存器计数器。
	// 整数类型依次占用 a0-a7，浮点类型依次占用 fa0-fa7。
	// 超出对应寄存器的形参通过栈传递，位于FP正方向偏移，每个栈槽 8 字节对齐。
	{
		int intIdx = 0, floatIdx = 0, stackOffset = 0;
		for (auto * param : func->getParams()) {
			const bool isFloat = param->getType()->isFloatType();
			bool onStack = false;

			if (isFloat) {
				if (floatIdx >= 8) {
					onStack = true;
				}
				floatIdx++;
			} else {
				if (intIdx >= 8) {
					onStack = true;
				}
				intIdx++;
			}

			if (onStack) {
				auto & info = allocMap[param];
				info.regId = -1;
				info.setStack(RISCV64_FP_REG_NO, stackOffset);
				stackOffset += 8;
			}
		}
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
	const int frameSize = alignTo(savedFrameBytes + localBytes + outgoingBytes, 16);
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
	} else if (it->second.hasFloatReg()) {
		str = "\t# " + showName + ":" + PlatformRiscV64::fpRegName[it->second.regId];
	} else if (it->second.hasStackSlot) {
		str = "\t# " + showName + ":" + std::to_string(it->second.offset) + "(" +
			  PlatformRiscV64::regName[it->second.baseRegId] + ")";
	}
}
