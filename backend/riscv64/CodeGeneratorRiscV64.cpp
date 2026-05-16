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
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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

/// @brief 判断叶子函数是否可以省略栈帧
/// @param func 当前函数
/// @param allocMap 寄存器分配映射表
/// @param savedRegs 需要保存的callee-saved寄存器列表
/// @param outgoingArgBytes 调用其他函数时超出寄存器传递的参数字节数
/// @return 若可以省略栈帧则返回true
///
/// 条件：无调用指令、无超出寄存器传递的参数、仅需保存FP、无栈分配的值
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

/// @brief 从当前保存列表中提取真正由寄存器分配器使用的callee-saved GPR
/// @param savedRegs 所有被保存的寄存器编号列表
/// @return 仅包含callee-saved GPR（x9/s1, x18-27/s2-s11）的子集
/// @note 用于RA统计JSON输出，记录实际占用的callee-saved GPR编号
std::vector<int> collectUsedCalleeSavedGPRs(const std::vector<int> & savedRegs)
{
	std::vector<int> used;
	for (int reg : savedRegs) {
		if (reg == 9 || (reg >= 18 && reg <= 27)) {
			used.push_back(reg);
		}
	}
	return used;
}

/// @brief 将字符串写成JSON安全形式，转义双引号、反斜杠和控制字符
/// @param value 原始字符串
/// @return 转义后的JSON安全字符串
std::string jsonEscape(const std::string & value)
{
	std::ostringstream out;
	for (unsigned char ch : value) {
		switch (ch) {
			case '"':
				out << "\\\"";
				break;
			case '\\':
				out << "\\\\";
				break;
			case '\b':
				out << "\\b";
				break;
			case '\f':
				out << "\\f";
				break;
			case '\n':
				out << "\\n";
				break;
			case '\r':
				out << "\\r";
				break;
			case '\t':
				out << "\\t";
				break;
			default:
				if (ch < 0x20) {
					const char * hex = "0123456789ABCDEF";
					out << "\\u00" << hex[(ch >> 4) & 0xF] << hex[ch & 0xF];
				} else {
					out << static_cast<char>(ch);
				}
				break;
		}
	}
	return out.str();
}

/// @brief 将整数数组写为JSON数组格式，如 [1, 2, 3]
/// @param out 输出流
/// @param values 整数数组
void writeJsonIntArray(std::ostream & out, const std::vector<int> & values)
{
	out << "[";
	for (size_t i = 0; i < values.size(); ++i) {
		if (i != 0) {
			out << ", ";
		}
		out << values[i];
	}
	out << "]";
}

} // namespace

/// @brief 构造函数
/// @param _module 待编译的IR模块
/// @param enableCalleeSavedFPR 是否启用 callee-saved FPR
/// @param enableCoalesce 是否启用寄存器合并
/// @param enableSplit 是否启用活跃区间分裂
/// @param raStatsJsonPath 若非空，则输出寄存器分配JSON统计到该路径
CodeGeneratorRiscV64::CodeGeneratorRiscV64(Module * _module,
                                           bool enableCalleeSavedFPR,
                                           bool enableCoalesce,
                                           bool enableSplit,
                                           std::string raStatsJsonPath)
	: CodeGeneratorAsm(_module),
	  greedyAllocator(nullptr, enableCalleeSavedFPR, enableCoalesce, enableSplit),
	  enableCalleeSavedFPR_(enableCalleeSavedFPR),
	  enableCoalesce_(enableCoalesce),
	  enableSplit_(enableSplit),
	  raStatsJsonPath_(std::move(raStatsJsonPath))
{}

/// @brief 产生汇编文件
/// @return 始终返回true
///
/// 依次输出：文件头部 -> 数据段 -> 代码段 -> 内置循环并行运行时（若需要）
bool CodeGeneratorRiscV64::run()
{
	genHeader();
	genDataSection();
	CodeGeneratorAsm::genCodeSection();
	// 若模块中调用了循环并行运行时函数，则输出对应的汇编实现
	if (moduleUsesMtRuntime()) {
		emitMtRuntime();
	}
	if (!writeRAStatsJson()) {
		return false;
	}
	return true;
}

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
	peephole.run(iloc, module->getOptLevel(), enableCoalesce_);

	// 删除未被引用的基本块标签
	iloc.deleteUnusedLabel();

	// 收集当前函数的RA统计报告（仅在JSON输出启用时）
	if (!raStatsJsonPath_.empty()) {
		FunctionRAReport report;
		report.functionName = func->getName();
		report.regAllocStats = greedyAllocator.getStats();
		report.frameSize = iloc.getFrameSize();
		report.usedCalleeSavedGPRs = collectUsedCalleeSavedGPRs(currentSavedRegs);
		report.usedCalleeSavedFPRs = currentSavedFPRs;
		// 收集peephole优化后的最终汇编级指标（指令数、栈访问数、move数）
		report.codegenStats = iloc.collectFinalStats();
		raReports_.push_back(std::move(report));
	}

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
		std::unordered_set<Value *> scratchKeys;
		for (auto & sv: scratchValues) {
			scratchKeys.insert(reinterpret_cast<Value *>(sv.identity));
		}
		for (auto & [val, info]: greedyAllocator.getAllocationMap()) {
			(void) info;
			if (scratchKeys.find(val) != scratchKeys.end()) {
				continue;
			}
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

/// @brief 将本模块的寄存器分配评测指标写为JSON
/// @return 写入是否成功；若未配置输出路径则直接返回true
/// @note JSON结构包含schema版本、目标架构、RA配置（callee-saved FPR/coalesce/split）
///       以及每个函数的RA统计（分配区间数、溢出数、消除copy数、栈帧大小等）
///       和代码生成统计（机器指令数、栈load/store数、move指令数）
bool CodeGeneratorRiscV64::writeRAStatsJson() const
{
	if (raStatsJsonPath_.empty()) {
		return true;
	}

	std::ofstream out(raStatsJsonPath_, std::ios::out | std::ios::trunc);
	if (!out.is_open()) {
		return false;
	}

	out << "{\n";
	out << "  \"schema_version\": 1,\n";
	out << "  \"target\": \"RISCV64\",\n";
	out << "  \"config\": {\n";
	out << "    \"callee_saved_fpr\": " << (enableCalleeSavedFPR_ ? "true" : "false") << ",\n";
	out << "    \"coalesce\": " << (enableCoalesce_ ? "true" : "false") << ",\n";
	out << "    \"split\": " << (enableSplit_ ? "true" : "false") << "\n";
	out << "  },\n";
	out << "  \"functions\": [\n";

	for (size_t i = 0; i < raReports_.size(); ++i) {
		const auto & report = raReports_[i];
		const auto & stats = report.regAllocStats;
		const auto & codegen = report.codegenStats;
		out << "    {\n";
		out << "      \"name\": \"" << jsonEscape(report.functionName) << "\",\n";
		out << "      \"assigned_reg_intervals\": " << stats.assignedRegIntervals << ",\n";
		out << "      \"assigned_gpr_intervals\": " << stats.assignedGprIntervals << ",\n";
		out << "      \"assigned_fpr_intervals\": " << stats.assignedFprIntervals << ",\n";
		out << "      \"spilled_intervals\": " << stats.spilledIntervals << ",\n";
		out << "      \"spilled_values\": " << stats.spilledValues << ",\n";
		out << "      \"estimated_reloads\": " << stats.estimatedReloads << ",\n";
		out << "      \"estimated_spill_stores\": " << stats.estimatedSpillStores << ",\n";
		out << "      \"eliminated_copies\": " << stats.eliminatedCopies << ",\n";
		out << "      \"split_count\": " << stats.splitCount << ",\n";
		out << "      \"frame_size\": " << report.frameSize << ",\n";
		out << "      \"used_callee_saved_gpr\": ";
		writeJsonIntArray(out, report.usedCalleeSavedGPRs);
		out << ",\n";
		out << "      \"used_callee_saved_fpr\": ";
		writeJsonIntArray(out, report.usedCalleeSavedFPRs);
		out << ",\n";
		out << "      \"machine_instruction_count\": " << codegen.machineInstructionCount << ",\n";
		out << "      \"stack_load_count\": " << codegen.stackLoadCount << ",\n";
		out << "      \"stack_store_count\": " << codegen.stackStoreCount << ",\n";
		out << "      \"move_instruction_count\": " << codegen.moveInstructionCount << "\n";
		out << "    }";
		if (i + 1 != raReports_.size()) {
			out << ",";
		}
		out << "\n";
	}

	out << "  ]\n";
	out << "}\n";
	return out.good();
}

/// @brief 判断当前模块是否使用了内置循环并行运行时函数
/// @return 若模块中存在对 __mtstart/__mtend/__mtstart4 等函数的调用则返回true
bool CodeGeneratorRiscV64::moduleUsesMtRuntime() const
{
	if (module == nullptr) {
		return false;
	}

	for (auto * func : module->getFunctionList()) {
		if (func == nullptr || func->isBuiltin()) {
			continue;
		}
		for (auto * bb : func->getBlocks()) {
			for (auto * inst : bb->getInstructions()) {
				auto * call = dynamic_cast<CallInst *>(inst);
				if (call == nullptr || call->getCallee() == nullptr) {
					continue;
				}
				const std::string & name = call->getCallee()->getName();
				if (name == "__mtstart" || name == "__mtend" ||
				    name == "__mtstart4" || name == "__mtthreadcount4" ||
				    name == "__mtstorei32" || name == "__mtloadi32" ||
				    name == "__mtend4") {
					return true;
				}
			}
		}
	}

	return false;
}

/// @brief 输出内置循环并行运行时汇编
///
/// 生成以下运行时函数的RISC-V64汇编实现：
/// - __mtstart: 启动1路并行线程（clone系统调用），复制当前栈帧到独立栈
/// - __mtend: 等待并行线程完成（自旋等待done标志）
/// - __mtstart4: 启动4路并行线程，使用门控同步确保所有线程同时开始执行
/// - __mtthreadcount4: 返回当前并行线程数
/// - __mtstorei32: 线程安全地存储32位结果到共享结果数组
/// - __mtloadi32: 线程安全地从共享结果数组加载32位结果
/// - __mtend4: 4路并行结束，子线程标记完成并退出，主线程等待所有子线程
void CodeGeneratorRiscV64::emitMtRuntime()
{
	// 每个并行线程的栈大小：16MB
	constexpr int kThreadStackBytes = 16 * 1024 * 1024;
	// clone标志位组合，共享父进程的地址空间和文件描述符
	constexpr int kCloneFlags = 0x100 | 0x200 | 0x400 | 0x800 | 0x10000 | 0x40000;

	// ---- BSS段：单线程并行运行时的全局变量 ----
	std::fprintf(fp, "\n.bss\n");
	std::fprintf(fp, ".align 4\n");
	std::fprintf(fp, "__minic_mt_done:\n");		// 并行完成标志
	std::fprintf(fp, "\t.word 0\n");
	std::fprintf(fp, ".align 4\n");
	std::fprintf(fp, "__minic_mt_stack:\n");	// 并行线程的独立栈空间
	std::fprintf(fp, "\t.zero %d\n", kThreadStackBytes);
	std::fprintf(fp, "__minic_mt_stack_end:\n");
	std::fprintf(fp, "\n.text\n");

	// ---- __mtstart: 启动1路并行线程 ----
	// 将当前栈帧复制到独立栈，然后通过clone系统调用创建线程
	std::fprintf(fp, ".align 2\n");
	std::fprintf(fp, ".global __mtstart\n");
	std::fprintf(fp, ".type __mtstart, %%function\n");
	std::fprintf(fp, "__mtstart:\n");
	// 计算当前栈帧大小并复制到并行线程的独立栈
	std::fprintf(fp, "\tmv t0,sp\n");
	std::fprintf(fp, "\tmv t1,s0\n");
	std::fprintf(fp, "\tsub t2,t1,t0\n");
	std::fprintf(fp, "\tla t3,__minic_mt_stack_end\n");
	std::fprintf(fp, "\tsub t4,t3,t2\n");
	std::fprintf(fp, "\tmv t5,t0\n");
	std::fprintf(fp, "\tmv t6,t4\n");
	std::fprintf(fp, ".L_minic_mt_copy_loop:\n");
	std::fprintf(fp, "\tbgeu t5,t1,.L_minic_mt_copy_done\n");
	std::fprintf(fp, "\tld a5,0(t5)\n");
	std::fprintf(fp, "\tsd a5,0(t6)\n");
	std::fprintf(fp, "\taddi t5,t5,8\n");
	std::fprintf(fp, "\taddi t6,t6,8\n");
	std::fprintf(fp, "\tj .L_minic_mt_copy_loop\n");
	std::fprintf(fp, ".L_minic_mt_copy_done:\n");
	// 清除完成标志，确保内存可见性后发起clone系统调用
	std::fprintf(fp, "\tla a5,__minic_mt_done\n");
	std::fprintf(fp, "\tsw zero,0(a5)\n");
	std::fprintf(fp, "\tfence rw,rw\n");
	// clone系统调用：创建共享地址空间的线程
	std::fprintf(fp, "\tli a0,%d\n", kCloneFlags);
	std::fprintf(fp, "\tmv a1,t4\n");
	std::fprintf(fp, "\tli a2,0\n");
	std::fprintf(fp, "\tli a3,0\n");
	std::fprintf(fp, "\tli a4,0\n");
	std::fprintf(fp, "\tli a7,220\n");
	std::fprintf(fp, "\tecall\n");
	// 父线程：clone返回子线程TID（>0），直接返回0
	std::fprintf(fp, "\tbeqz a0,.L_minic_mt_child\n");
	std::fprintf(fp, "\tblt a0,zero,.L_minic_mt_clone_failed\n");
	std::fprintf(fp, "\tli a0,0\n");
	std::fprintf(fp, "\tret\n");
	// clone失败：设置done标志为1，返回0
	std::fprintf(fp, ".L_minic_mt_clone_failed:\n");
	std::fprintf(fp, "\tla a5,__minic_mt_done\n");
	std::fprintf(fp, "\tli a4,1\n");
	std::fprintf(fp, "\tsw a4,0(a5)\n");
	std::fprintf(fp, "\tli a0,0\n");
	std::fprintf(fp, "\tret\n");
	// 子线程：设置FP为独立栈顶，返回1表示子线程身份
	std::fprintf(fp, ".L_minic_mt_child:\n");
	std::fprintf(fp, "\tmv s0,t3\n");
	std::fprintf(fp, "\tli a0,1\n");
	std::fprintf(fp, "\tret\n");
	std::fprintf(fp, ".size __mtstart, .-__mtstart\n");

	// ---- __mtend: 等待并行线程完成 ----
	// 子线程(a0!=0)：设置done标志后通过exit系统调用退出
	// 父线程(a0==0)：自旋等待done标志变为非零
	std::fprintf(fp, ".align 2\n");
	std::fprintf(fp, ".global __mtend\n");
	std::fprintf(fp, ".type __mtend, %%function\n");
	std::fprintf(fp, "__mtend:\n");
	std::fprintf(fp, "\tbnez a0,.L_minic_mt_child_exit\n");
	std::fprintf(fp, "\tla t0,__minic_mt_done\n");
	std::fprintf(fp, ".L_minic_mt_parent_wait:\n");
	std::fprintf(fp, "\tfence r,rw\n");
	std::fprintf(fp, "\tlw t1,0(t0)\n");
	std::fprintf(fp, "\tbeqz t1,.L_minic_mt_parent_wait\n");
	std::fprintf(fp, "\tret\n");
	std::fprintf(fp, ".L_minic_mt_child_exit:\n");
	std::fprintf(fp, "\tla t0,__minic_mt_done\n");
	std::fprintf(fp, "\tli t1,1\n");
	std::fprintf(fp, "\tfence rw,w\n");
	std::fprintf(fp, "\tsw t1,0(t0)\n");
	std::fprintf(fp, "\tfence rw,rw\n");
	std::fprintf(fp, "\tli a0,0\n");
	std::fprintf(fp, "\tli a7,93\n");
	std::fprintf(fp, "\tecall\n");
	std::fprintf(fp, ".size __mtend, .-__mtend\n");

	// ---- 4路并行运行时的全局变量 ----
	// 每个并行线程的独立栈大小（与2路版本保持一致）
	constexpr int kThreadStackBytes4 = 16 * 1024 * 1024;

	std::fprintf(fp, "\n.bss\n");
	std::fprintf(fp, ".align 4\n");
	std::fprintf(fp, "__minic_mt4_gate:\n");			// 门控同步标志：0=未启动, 1=启动, -1=失败
	std::fprintf(fp, "\t.word 0\n");
	std::fprintf(fp, ".align 4\n");
	std::fprintf(fp, "__minic_mt4_thread_count:\n");	// 实际并行线程数
	std::fprintf(fp, "\t.word 0\n");
	std::fprintf(fp, ".align 4\n");
	std::fprintf(fp, "__minic_mt4_done:\n");			// 各线程完成标志数组（4个int32）
	std::fprintf(fp, "\t.zero 16\n");
	std::fprintf(fp, ".align 4\n");
	std::fprintf(fp, "__minic_mt4_result:\n");			// 各线程结果数组（4个int32）
	std::fprintf(fp, "\t.zero 16\n");
	std::fprintf(fp, ".align 4\n");
	std::fprintf(fp, "__minic_mt4_stack1:\n");			// 线程1的独立栈空间
	std::fprintf(fp, "\t.zero %d\n", kThreadStackBytes4);
	std::fprintf(fp, "__minic_mt4_stack1_end:\n");
	std::fprintf(fp, ".align 4\n");
	std::fprintf(fp, "__minic_mt4_stack2:\n");			// 线程2的独立栈空间
	std::fprintf(fp, "\t.zero %d\n", kThreadStackBytes4);
	std::fprintf(fp, "__minic_mt4_stack2_end:\n");
	std::fprintf(fp, ".align 4\n");
	std::fprintf(fp, "__minic_mt4_stack3:\n");			// 线程3的独立栈空间
	std::fprintf(fp, "\t.zero %d\n", kThreadStackBytes4);
	std::fprintf(fp, "__minic_mt4_stack3_end:\n");
	std::fprintf(fp, "\n.text\n");

	// ---- __mtstart4: 启动4路并行线程 ----
	// 初始化门控和结果数组，依次clone 3个子线程，然后设置线程数为4并打开门控
	std::fprintf(fp, ".align 2\n");
	std::fprintf(fp, ".global __mtstart4\n");
	std::fprintf(fp, ".type __mtstart4, %%function\n");
	std::fprintf(fp, "__mtstart4:\n");
	// 初始化：清除门控标志，设置线程数为1（仅主线程），清零完成和结果数组
	std::fprintf(fp, "\tla a5,__minic_mt4_gate\n");
	std::fprintf(fp, "\tsw zero,0(a5)\n");
	std::fprintf(fp, "\tla a5,__minic_mt4_thread_count\n");
	std::fprintf(fp, "\tli a4,1\n");
	std::fprintf(fp, "\tsw a4,0(a5)\n");
	std::fprintf(fp, "\tla a5,__minic_mt4_done\n");
	std::fprintf(fp, "\tsw zero,0(a5)\n");
	std::fprintf(fp, "\tsw zero,4(a5)\n");
	std::fprintf(fp, "\tsw zero,8(a5)\n");
	std::fprintf(fp, "\tsw zero,12(a5)\n");
	std::fprintf(fp, "\tla a5,__minic_mt4_result\n");
	std::fprintf(fp, "\tsw zero,0(a5)\n");
	std::fprintf(fp, "\tsw zero,4(a5)\n");
	std::fprintf(fp, "\tsw zero,8(a5)\n");
	std::fprintf(fp, "\tsw zero,12(a5)\n");
	std::fprintf(fp, "\tfence rw,rw\n");

	// 为指定线程ID复制栈帧并clone子线程的辅助lambda
	auto emitCloneChild = [&](int tid, const char * stackEndLabel) {
		// 计算栈帧大小并定位子线程独立栈的底部
		std::fprintf(fp, "\tli t0,%d\n", tid);			// t0 = 线程ID
		std::fprintf(fp, "\tla t1,%s\n", stackEndLabel);	// t1 = 子线程栈顶
		std::fprintf(fp, "\tmv t2,sp\n");			// t2 = 父线程sp
		std::fprintf(fp, "\tmv t3,s0\n");			// t3 = 父线程fp
		std::fprintf(fp, "\tsub t4,t3,t2\n");			// t4 = 栈帧大小
		std::fprintf(fp, "\tsub t5,t1,t4\n");			// t5 = 子线程栈底
		std::fprintf(fp, "\tmv t6,t2\n");			// t6 = 源地址（父线程栈底）
		std::fprintf(fp, "\tmv a2,t5\n");			// a2 = 目标地址（子线程栈底）
		// 逐8字节复制栈帧内容到子线程独立栈
		std::fprintf(fp, ".L_minic_mt4_copy_%d:\n", tid);
		std::fprintf(fp, "\tbgeu t6,t3,.L_minic_mt4_copy_done_%d\n", tid);
		std::fprintf(fp, "\tld a3,0(t6)\n");
		std::fprintf(fp, "\tsd a3,0(a2)\n");
		std::fprintf(fp, "\taddi t6,t6,8\n");
		std::fprintf(fp, "\taddi a2,a2,8\n");
		std::fprintf(fp, "\tj .L_minic_mt4_copy_%d\n", tid);
		std::fprintf(fp, ".L_minic_mt4_copy_done_%d:\n", tid);
		// 调用clone系统调用创建共享地址空间的子线程
		std::fprintf(fp, "\tli a0,%d\n", kCloneFlags);	// clone标志
		std::fprintf(fp, "\tmv a1,t5\n");			// 子线程栈底
		std::fprintf(fp, "\tli a2,0\n");
		std::fprintf(fp, "\tli a3,0\n");
		std::fprintf(fp, "\tli a4,0\n");
		std::fprintf(fp, "\tli a7,220\n");			// 系统调用号：clone
		std::fprintf(fp, "\tecall\n");
		// 父线程进入子线程入口，子线程（a0==0）跳转到公共子线程标签
		std::fprintf(fp, "\tbeqz a0,.L_minic_mt4_child\n");
		// clone失败（a0<0）跳转到统一失败处理
		std::fprintf(fp, "\tblt a0,zero,.L_minic_mt4_clone_failed\n");
	};

	// 依次为线程1、2、3复制栈帧并clone
	emitCloneChild(1, "__minic_mt4_stack1_end");
	emitCloneChild(2, "__minic_mt4_stack2_end");
	emitCloneChild(3, "__minic_mt4_stack3_end");

	// 所有子线程clone成功：设置线程数为4，打开门控标志，主线程返回0
	std::fprintf(fp, "\tla a5,__minic_mt4_thread_count\n");
	std::fprintf(fp, "\tli a4,4\n");
	std::fprintf(fp, "\tsw a4,0(a5)\n");
	std::fprintf(fp, "\tfence rw,rw\n");
	std::fprintf(fp, "\tla a5,__minic_mt4_gate\n");
	std::fprintf(fp, "\tli a4,1\n");
	std::fprintf(fp, "\tsw a4,0(a5)\n");
	std::fprintf(fp, "\tfence rw,rw\n");
	std::fprintf(fp, "\tli a0,0\n");
	std::fprintf(fp, "\tret\n");
	// clone失败：设置门控为-1，通知子线程取消，主线程返回0
	std::fprintf(fp, ".L_minic_mt4_clone_failed:\n");
	std::fprintf(fp, "\tla a5,__minic_mt4_gate\n");
	std::fprintf(fp, "\tli a4,-1\n");
	std::fprintf(fp, "\tsw a4,0(a5)\n");
	std::fprintf(fp, "\tfence rw,rw\n");
	std::fprintf(fp, "\tli a0,0\n");
	std::fprintf(fp, "\tret\n");
	// 子线程入口：设置FP为独立栈顶，自旋等待门控标志变为非零
	// 门控为1时开始执行（返回线程ID），门控为-1时取消执行（exit退出）
	std::fprintf(fp, ".L_minic_mt4_child:\n");
	std::fprintf(fp, "\tmv s0,t1\n");
	std::fprintf(fp, "\tla a5,__minic_mt4_gate\n");
	std::fprintf(fp, ".L_minic_mt4_child_wait:\n");
	std::fprintf(fp, "\tfence r,rw\n");
	std::fprintf(fp, "\tlw a4,0(a5)\n");
	std::fprintf(fp, "\tbeqz a4,.L_minic_mt4_child_wait\n");
	std::fprintf(fp, "\tblt a4,zero,.L_minic_mt4_child_cancel\n");
	std::fprintf(fp, "\tmv a0,t0\n");
	std::fprintf(fp, "\tret\n");
	std::fprintf(fp, ".L_minic_mt4_child_cancel:\n");
	std::fprintf(fp, "\tli a0,0\n");
	std::fprintf(fp, "\tli a7,93\n");
	std::fprintf(fp, "\tecall\n");
	std::fprintf(fp, ".size __mtstart4, .-__mtstart4\n");

	// ---- __mtthreadcount4: 返回当前并行线程数 ----
	std::fprintf(fp, ".align 2\n");
	std::fprintf(fp, ".type __mtthreadcount4, %%function\n");
	std::fprintf(fp, "__mtthreadcount4:\n");
	std::fprintf(fp, "\tla t0,__minic_mt4_thread_count\n");
	std::fprintf(fp, "\tlw a0,0(t0)\n");
	std::fprintf(fp, "\tret\n");
	std::fprintf(fp, ".size __mtthreadcount4, .-__mtthreadcount4\n");

	// ---- __mtstorei32: 线程安全地存储32位结果 ----
	// a0=结果索引, a1=结果值；写入后执行fence确保可见性
	std::fprintf(fp, ".align 2\n");
	std::fprintf(fp, ".type __mtstorei32, %%function\n");
	std::fprintf(fp, "__mtstorei32:\n");
	std::fprintf(fp, "\tla t0,__minic_mt4_result\n");
	std::fprintf(fp, "\tslli t1,a0,2\n");
	std::fprintf(fp, "\tadd t0,t0,t1\n");
	std::fprintf(fp, "\tsw a1,0(t0)\n");
	std::fprintf(fp, "\tfence rw,w\n");
	std::fprintf(fp, "\tret\n");
	std::fprintf(fp, ".size __mtstorei32, .-__mtstorei32\n");

	// ---- __mtloadi32: 线程安全地加载32位结果 ----
	// a0=结果索引；加载前执行fence确保读到最新值
	std::fprintf(fp, ".align 2\n");
	std::fprintf(fp, ".type __mtloadi32, %%function\n");
	std::fprintf(fp, "__mtloadi32:\n");
	std::fprintf(fp, "\tla t0,__minic_mt4_result\n");
	std::fprintf(fp, "\tslli t1,a0,2\n");
	std::fprintf(fp, "\tadd t0,t0,t1\n");
	std::fprintf(fp, "\tfence r,rw\n");
	std::fprintf(fp, "\tlw a0,0(t0)\n");
	std::fprintf(fp, "\tret\n");
	std::fprintf(fp, ".size __mtloadi32, .-__mtloadi32\n");

	// ---- __mtend4: 4路并行结束 ----
	// 子线程(a0!=0)：在done数组中标记完成，然后exit退出
	// 父线程(a0==0)：依次等待所有子线程的done标志变为1
	std::fprintf(fp, ".align 2\n");
	std::fprintf(fp, ".type __mtend4, %%function\n");
	std::fprintf(fp, "__mtend4:\n");
	std::fprintf(fp, "\tbnez a0,.L_minic_mt4_child_exit\n");
	std::fprintf(fp, "\tla t0,__minic_mt4_thread_count\n");
	std::fprintf(fp, "\tlw t1,0(t0)\n");
	std::fprintf(fp, "\tli t2,1\n");
	std::fprintf(fp, "\tla t3,__minic_mt4_done\n");
	std::fprintf(fp, ".L_minic_mt4_parent_next:\n");
	std::fprintf(fp, "\tbge t2,t1,.L_minic_mt4_parent_done\n");
	std::fprintf(fp, "\tslli t4,t2,2\n");
	std::fprintf(fp, "\tadd t5,t3,t4\n");
	std::fprintf(fp, ".L_minic_mt4_parent_wait:\n");
	std::fprintf(fp, "\tfence r,rw\n");
	std::fprintf(fp, "\tlw t6,0(t5)\n");
	std::fprintf(fp, "\tbeqz t6,.L_minic_mt4_parent_wait\n");
	std::fprintf(fp, "\taddi t2,t2,1\n");
	std::fprintf(fp, "\tj .L_minic_mt4_parent_next\n");
	std::fprintf(fp, ".L_minic_mt4_parent_done:\n");
	std::fprintf(fp, "\tret\n");
	std::fprintf(fp, ".L_minic_mt4_child_exit:\n");
	std::fprintf(fp, "\tla t0,__minic_mt4_done\n");
	std::fprintf(fp, "\tslli t1,a0,2\n");
	std::fprintf(fp, "\tadd t0,t0,t1\n");
	std::fprintf(fp, "\tli t2,1\n");
	std::fprintf(fp, "\tfence rw,w\n");
	std::fprintf(fp, "\tsw t2,0(t0)\n");
	std::fprintf(fp, "\tfence rw,rw\n");
	std::fprintf(fp, "\tli a0,0\n");
	std::fprintf(fp, "\tli a7,93\n");
	std::fprintf(fp, "\tecall\n");
	std::fprintf(fp, ".size __mtend4, .-__mtend4\n");
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
	// 栈分配完成后，将coalesced alias的分配信息回填到代表值，
	// 确保被合并的值与代表值共享同一栈槽/寄存器位置
	greedyAllocator.refreshCoalescedAliasAllocations();
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

	// 为强制栈分配、未分配寄存器或被溢出的变量分配栈槽。
	// coalesced alias 与其代表共享同一 canonical 栈槽，避免 eliminated copy
	// 在栈分配阶段又被拆回两个独立位置。
	std::vector<Value *> stackSlotCandidates;
	for (auto & [val, info]: allocMap) {
		if (val == nullptr) {
			continue;
		}

		if (GreedyRegAllocator::isForcedStackValue(val) || info.regId == -1 || greedyAllocator.isSpilled(val)) {
			stackSlotCandidates.push_back(val);
		}
	}
	for (auto * val : stackSlotCandidates) {
		Value * representative = greedyAllocator.getCoalescedRepresentative(val);
		if (representative != nullptr && representative != val) {
			allocMap.try_emplace(representative, RegAllocInfo{});
			assignStackSlot(representative);
			allocMap[val] = allocMap[representative];
			continue;
		}
		assignStackSlot(val);
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
