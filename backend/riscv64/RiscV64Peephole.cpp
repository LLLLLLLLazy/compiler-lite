#include "RiscV64Peephole.h"

#include <algorithm>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using InstList = std::list<RiscV64Inst *>;
using InstIt = InstList::iterator;

bool isLiveInst(RiscV64Inst * inst)
{
	return inst != nullptr && !inst->dead && !inst->opcode.empty();
}

/// @brief 判断是否为仅用于汇编展示的注释伪指令。
bool isCommentInst(RiscV64Inst * inst)
{
	return isLiveInst(inst) && inst->opcode == "#";
}

InstIt nextLive(InstList & code, InstIt it)
{
	if (it != code.end()) {
		++it;
	}
	while (it != code.end() && !isLiveInst(*it)) {
		++it;
	}
	return it;
}

/// @brief 获取下一条真实机器指令；跳过 dead 指令与可选的 IR 注释。
InstIt nextMachineInst(InstList & code, InstIt it)
{
	if (it != code.end()) {
		++it;
	}
	while (it != code.end() && (!isLiveInst(*it) || isCommentInst(*it))) {
		++it;
	}
	return it;
}

bool isLabel(RiscV64Inst * inst)
{
	return isLiveInst(inst) && inst->result == ":";
}

bool registerUsedAfterBeforeRedefOrBoundary(InstList & code, InstIt start, const std::string & reg);

bool isSimpleMoveOrLoad(RiscV64Inst * inst)
{
	if (!isLiveInst(inst)) {
		return false;
	}
	return inst->opcode == "mv" || inst->opcode == "li" || inst->opcode == "la";
}

bool sameInstruction(RiscV64Inst * lhs, RiscV64Inst * rhs)
{
	return lhs != nullptr && rhs != nullptr && lhs->opcode == rhs->opcode && lhs->result == rhs->result &&
		   lhs->arg1 == rhs->arg1 && lhs->arg2 == rhs->arg2 && lhs->addition == rhs->addition;
}

bool removeSelfMoves(InstList & code)
{
	bool changed = false;
	for (auto * inst : code) {
		if (!isLiveInst(inst)) {
			continue;
		}
		const bool mvSelf = inst->opcode == "mv" && inst->result == inst->arg1;
		// fmv.s/fsgnj.s rd,rs（rd==rs）是寄存器自拷贝，等价于无操作
		const bool fmvSelf = (inst->opcode == "fmv.s" || inst->opcode == "fsgnj.s") &&
		                      inst->result == inst->arg1 && inst->arg2.empty();
		const bool fsgnjSelf = inst->opcode == "fsgnj.s" && inst->result == inst->arg1 &&
		                        inst->arg1 == inst->arg2;
		if (mvSelf || fmvSelf || fsgnjSelf) {
			inst->setDead();
			changed = true;
		}
	}
	return changed;
}

bool removeConsecutiveDuplicates(InstList & code)
{
	bool changed = false;
	RiscV64Inst * prev = nullptr;
	for (auto * inst : code) {
		if (!isLiveInst(inst)) {
			continue;
		}
		if (isSimpleMoveOrLoad(inst) && isSimpleMoveOrLoad(prev) && sameInstruction(prev, inst)) {
			inst->setDead();
			changed = true;
			continue;
		}
		prev = inst;
	}
	return changed;
}

bool removeJumpToNextLabel(InstList & code)
{
	bool changed = false;
	for (auto it = code.begin(); it != code.end(); ++it) {
		auto * inst = *it;
		if (!isLiveInst(inst) || inst->opcode != "j") {
			continue;
		}

		auto next = nextLive(code, it);
		if (next != code.end() && isLabel(*next) && (*next)->opcode == inst->result) {
			inst->setDead();
			changed = true;
		}
	}
	return changed;
}

bool foldZeroSubCompare(InstList & code)
{
	bool changed = false;
	for (auto it = code.begin(); it != code.end(); ++it) {
		auto * li = *it;
		if (!isLiveInst(li) || li->opcode != "li" || li->arg1 != "0") {
			continue;
		}

		auto subIt = nextLive(code, it);
		auto cmpIt = nextLive(code, subIt);
		if (subIt == code.end() || cmpIt == code.end()) {
			continue;
		}

		auto * sub = *subIt;
		auto * cmp = *cmpIt;
		const std::string & tmp = li->result;
		if (!isLiveInst(sub) || sub->opcode != "subw" || sub->result != tmp || sub->arg2 != tmp ||
		    sub->arg1 == tmp) {
			continue;
		}
		if (!isLiveInst(cmp) || (cmp->opcode != "snez" && cmp->opcode != "seqz") || cmp->arg1 != tmp) {
			continue;
		}

		cmp->replace(cmp->opcode, cmp->result, sub->arg1);
		li->setDead();
		sub->setDead();
		changed = true;
	}
	return changed;
}

bool parseIntImmediate(const std::string & text, int & value)
{
	try {
		std::size_t parsed = 0;
		const int imm = std::stoi(text, &parsed);
		if (parsed != text.size()) {
			return false;
		}
		value = imm;
		return true;
	} catch (...) {
		return false;
	}
}

bool branchComparesRegisterWithZero(RiscV64Inst * branch,
	                                const std::string & valueReg,
	                                const std::string & zeroReg)
{
	if (!isLiveInst(branch) || (branch->opcode != "beq" && branch->opcode != "bne") || valueReg.empty() ||
	    zeroReg.empty()) {
		return false;
	}

	return (branch->result == valueReg && branch->arg1 == zeroReg) ||
	       (branch->result == zeroReg && branch->arg1 == valueReg);
}

bool isNegateSelf(RiscV64Inst * inst, const std::string & reg)
{
	return isLiveInst(inst) && inst->opcode == "subw" && inst->result == reg && inst->arg1 == "zero" &&
	       inst->arg2 == reg;
}

/// @brief 融合 fmul.s + fadd.s 为 fmadd.s（以及 fsub 变体）
///
/// 模式匹配：
///   fmul.s d, a, b
///   fadd.s e, c, d   ->  fmadd.s e, a, b, c
///   fadd.s e, d, c   ->  fmadd.s e, a, b, c
///   fsub.s e, d, c   ->  fmsub.s e, a, b, c
///   fsub.s e, c, d   ->  fnmsub.s e, a, b, c
///
/// 要求 fmul 的结果 d 在 add/sub 之间无其他活跃使用，且在 add/sub 之后也不再使用
bool fuseFMA(InstList & code)
{
	bool changed = false;
	for (auto it = code.begin(); it != code.end(); ++it) {
		auto * fmul = *it;
		if (!isLiveInst(fmul) || fmul->opcode != "fmul.s") {
			continue;
		}

		const std::string & productReg = fmul->result;
		const std::string & lhs = fmul->arg1;
		const std::string & rhs = fmul->arg2;

		auto addIt = nextLive(code, it);
		if (addIt == code.end()) {
			continue;
		}

		auto * addInst = *addIt;
		if (!isLiveInst(addInst)) {
			continue;
		}

		bool productUsedBetween = false;
		for (auto check = std::next(it); check != addIt && check != code.end(); ++check) {
			if (!isLiveInst(*check)) {
				continue;
			}
			if ((*check)->result == productReg || (*check)->arg1 == productReg || (*check)->arg2 == productReg) {
				productUsedBetween = true;
				break;
			}
		}
		if (productUsedBetween) {
			continue;
		}

		std::string fusedOpcode;
		std::string accumulateOperand;
		if (addInst->opcode == "fadd.s") {
			if (addInst->arg2 == productReg) {
				accumulateOperand = addInst->arg1;
				fusedOpcode = "fmadd.s";
			} else if (addInst->arg1 == productReg) {
				accumulateOperand = addInst->arg2;
				fusedOpcode = "fmadd.s";
			}
		} else if (addInst->opcode == "fsub.s") {
			if (addInst->arg1 == productReg) {
				accumulateOperand = addInst->arg2;
				fusedOpcode = "fmsub.s";
			} else if (addInst->arg2 == productReg) {
				accumulateOperand = addInst->arg1;
				fusedOpcode = "fnmsub.s";
			}
		}

		if (fusedOpcode.empty()) {
			continue;
		}

		if (registerUsedAfterBeforeRedefOrBoundary(code, addIt, productReg)) {
			continue;
		}

		addInst->replace(fusedOpcode, addInst->result, lhs, rhs, "", accumulateOperand);
		fmul->setDead();
		changed = true;
	}
	return changed;
}

bool isPowerOfTwo(int value)
{
	return value > 0 && (value & (value - 1)) == 0;
}

int log2PowerOfTwo(int value)
{
	int shift = 0;
	while (value > 1) {
		value >>= 1;
		++shift;
	}
	return shift;
}

bool isStoreOpcode(const std::string & opcode)
{
	return opcode == "sb" || opcode == "sh" || opcode == "sw" || opcode == "sd" || opcode == "fsw" ||
	       opcode == "fsd";
}

bool isLoadOpcode(const std::string & opcode)
{
	return opcode == "lb" || opcode == "lbu" || opcode == "lh" || opcode == "lhu" || opcode == "lw" ||
	       opcode == "lwu" || opcode == "ld" || opcode == "flw" || opcode == "fld";
}

bool isMemoryOpcode(const std::string & opcode)
{
	return isLoadOpcode(opcode) || isStoreOpcode(opcode);
}

bool parseMemoryOperand(const std::string & operand, int & offset, std::string & base)
{
	const auto open = operand.find('(');
	const auto close = operand.find(')', open == std::string::npos ? 0 : open);
	if (open == std::string::npos || close == std::string::npos || close <= open + 1 || close + 1 != operand.size()) {
		return false;
	}
	if (!parseIntImmediate(operand.substr(0, open), offset)) {
		return false;
	}

	base = operand.substr(open + 1, close - open - 1);
	return !base.empty();
}

bool isSigned12BitImmediate(int value)
{
	return value >= -2048 && value <= 2047;
}

std::string formatMemoryOperand(int offset, const std::string & base)
{
	return std::to_string(offset) + "(" + base + ")";
}

bool isBranchOpcode(const std::string & opcode)
{
	return !opcode.empty() && opcode[0] == 'b';
}

bool definesResultOperand(RiscV64Inst * inst)
{
	return isLiveInst(inst) && !inst->result.empty() && inst->result != ":" && !isStoreOpcode(inst->opcode) &&
	       !isBranchOpcode(inst->opcode) && inst->opcode != "j" && inst->opcode != "jal" &&
	       inst->opcode != "call" && inst->opcode != "ret";
}

bool operandMentionsRegister(const std::string & operand, const std::string & reg)
{
	return operand == reg || operand.find("(" + reg + ")") != std::string::npos;
}

bool operandReferencesAddressBase(const std::string & operand, const std::string & reg)
{
	return operand.find("(" + reg + ")") != std::string::npos;
}

bool replaceAddressBase(std::string & operand, const std::string & oldReg, const std::string & newReg)
{
	const std::string needle = "(" + oldReg + ")";
	const auto pos = operand.find(needle);
	if (pos == std::string::npos) {
		return false;
	}
	operand.replace(pos + 1, oldReg.size(), newReg);
	return true;
}

bool usesRegister(RiscV64Inst * inst, const std::string & reg)
{
	if (!isLiveInst(inst) || reg.empty()) {
		return false;
	}
	if (!definesResultOperand(inst) && operandMentionsRegister(inst->result, reg)) {
		return true;
	}
	return operandMentionsRegister(inst->arg1, reg) || operandMentionsRegister(inst->arg2, reg) ||
	       operandMentionsRegister(inst->addition, reg);
}

bool isControlBoundary(RiscV64Inst * inst)
{
	return isLiveInst(inst) &&
	       (isLabel(inst) || isBranchOpcode(inst->opcode) || inst->opcode == "j" || inst->opcode == "jal" ||
	        inst->opcode == "call" || inst->opcode == "ret");
}

bool isArgumentRegister(const std::string & reg)
{
	return reg.size() == 2 && reg[0] == 'a' && reg[1] >= '0' && reg[1] <= '7';
}

/// @brief 判断寄存器名是否为浮点参数寄存器（fa0-fa7）
bool isFloatArgumentRegister(const std::string & reg)
{
	return reg.size() == 3 && reg[0] == 'f' && reg[1] == 'a' && reg[2] >= '0' && reg[2] <= '7';
}

/// @brief 判断寄存器名是否为整数或浮点参数寄存器（a0-a7或fa0-fa7）
bool isCallArgumentRegister(const std::string & reg)
{
	return isArgumentRegister(reg) || isFloatArgumentRegister(reg);
}

/// @brief 判断寄存器名是否为返回值寄存器（a0或fa0）
bool isReturnValueRegister(const std::string & reg)
{
	return reg == "a0" || reg == "fa0";
}

bool isPhysicalRegisterName(const std::string & reg);

bool isCallerSavedRegister(const std::string & reg)
{
	static const std::unordered_set<std::string> callerSaved = {
		"ra",  "t0",  "t1",  "t2",  "t3",  "t4",  "t5",  "t6",
		"a0",  "a1",  "a2",  "a3",  "a4",  "a5",  "a6",  "a7",
		"ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7",
		"fa0", "fa1", "fa2", "fa3", "fa4", "fa5", "fa6", "fa7",
		"ft8", "ft9", "ft10", "ft11",
	};
	return callerSaved.find(reg) != callerSaved.end();
}

bool isCopyPropagationRegister(const std::string & reg)
{
	if (reg == "zero") {
		return true;
	}
	return isPhysicalRegisterName(reg) && reg != "ra" && reg != "sp" && reg != "gp" && reg != "tp";
}

std::string addressBaseRegister(const std::string & operand);

/// @brief 判断寄存器名是否为RISC-V64物理寄存器名
bool isPhysicalRegisterName(const std::string & reg)
{
	static const std::unordered_set<std::string> kRegs = {
		"zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "fp", "s1", "a0", "a1", "a2",
		"a3",   "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10",
		"s11",  "t3", "t4", "t5", "t6", "ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7",
		"fs0",  "fs1", "fa0", "fa1", "fa2", "fa3", "fa4", "fa5", "fa6", "fa7", "fs2", "fs3",
		"fs4",  "fs5", "fs6", "fs7", "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11",
	};
	return kRegs.find(reg) != kRegs.end();
}

/// @brief 判断指令是否隐式使用指定寄存器（call隐式使用参数寄存器，ret隐式使用返回值寄存器）
bool instructionImplicitlyUsesRegister(RiscV64Inst * inst, const std::string & reg)
{
	if (!isLiveInst(inst) || reg.empty()) {
		return false;
	}
	if (inst->opcode == "call" && isCallArgumentRegister(reg)) {
		return true;
	}
	return inst->opcode == "ret" && isReturnValueRegister(reg);
}

/// @brief 从操作数字符串中收集引用的物理寄存器名（包括内存基址寄存器）
void collectOperandRegisters(const std::string & operand, std::unordered_set<std::string> & regs)
{
	if (isPhysicalRegisterName(operand)) {
		regs.insert(operand);
	}
	const std::string base = addressBaseRegister(operand);
	if (isPhysicalRegisterName(base)) {
		regs.insert(base);
	}
}

/// @brief 收集指令显式和隐式使用的寄存器集合
std::unordered_set<std::string> instructionUseSet(RiscV64Inst * inst)
{
	std::unordered_set<std::string> regs;
	if (!isLiveInst(inst)) {
		return regs;
	}
	if (!definesResultOperand(inst)) {
		collectOperandRegisters(inst->result, regs);
	}
	collectOperandRegisters(inst->arg1, regs);
	collectOperandRegisters(inst->arg2, regs);
	collectOperandRegisters(inst->addition, regs);
	if (inst->opcode == "call") {
		for (int i = 0; i <= 7; ++i) {
			regs.insert("a" + std::to_string(i));
			regs.insert("fa" + std::to_string(i));
		}
	} else if (inst->opcode == "ret") {
		regs.insert("a0");
		regs.insert("fa0");
	}
	return regs;
}

/// @brief 收集指令定义的寄存器集合
std::unordered_set<std::string> instructionDefSet(RiscV64Inst * inst)
{
	std::unordered_set<std::string> regs;
	if (definesResultOperand(inst) && isPhysicalRegisterName(inst->result)) {
		regs.insert(inst->result);
	}
	return regs;
}

/// @brief 机器级基本块，用于局部分析的活跃性计算
struct MachineBlock {
	std::vector<RiscV64Inst *> insts;              ///< 块内指令列表
	std::vector<int> succs;                        ///< 后继块索引列表
	std::unordered_set<std::string> use;           ///< 块内向上暴露的使用（use-before-def）
	std::unordered_set<std::string> def;           ///< 块内所有定义
	std::unordered_set<std::string> liveIn;        ///< 块入口活跃寄存器集合
	std::unordered_set<std::string> liveOut;       ///< 块出口活跃寄存器集合
};

/// @brief 机器级活跃性分析结果
struct MachineLiveness {
	std::vector<MachineBlock> blocks;                           ///< 所有基本块
	std::unordered_map<RiscV64Inst *, int> instToBlock;         ///< 指令到基本块索引的映射
};

/// @brief 提取指令引用的标签名（以.L开头的操作数）
std::string referencedLabel(RiscV64Inst * inst)
{
	if (!isLiveInst(inst)) {
		return "";
	}
	for (const auto * operand : {&inst->result, &inst->arg1, &inst->arg2, &inst->addition}) {
		if (operand->rfind(".L", 0) == 0) {
			return *operand;
		}
	}
	return "";
}

/// @brief 构建机器级活跃性分析：划分基本块、计算liveIn/liveOut
/// @param code 指令列表
/// @return 包含基本块信息和活跃性分析结果的结构
MachineLiveness buildMachineLiveness(InstList & code)
{
	MachineLiveness info;
	std::unordered_map<std::string, int> labelToBlock;
	std::vector<std::string> pendingLabels;
	MachineBlock current;

	auto flushCurrent = [&]() {
		if (current.insts.empty()) {
			return;
		}
		const int idx = static_cast<int>(info.blocks.size());
		for (const auto & label : pendingLabels) {
			labelToBlock[label] = idx;
		}
		pendingLabels.clear();
		for (auto * inst : current.insts) {
			info.instToBlock[inst] = idx;
		}
		info.blocks.push_back(std::move(current));
		current = MachineBlock{};
	};

	for (auto * inst : code) {
		if (!isLiveInst(inst)) {
			continue;
		}
		if (isLabel(inst)) {
			flushCurrent();
			pendingLabels.push_back(inst->opcode);
			continue;
		}
		current.insts.push_back(inst);
		if (isControlBoundary(inst)) {
			flushCurrent();
		}
	}
	flushCurrent();

	for (int i = 0; i < static_cast<int>(info.blocks.size()); ++i) {
		auto & block = info.blocks[i];
		auto * last = block.insts.empty() ? nullptr : block.insts.back();
		auto addTarget = [&](const std::string & label) {
			auto it = labelToBlock.find(label);
			if (it != labelToBlock.end()) {
				block.succs.push_back(it->second);
			}
		};

		if (last != nullptr && isBranchOpcode(last->opcode)) {
			addTarget(referencedLabel(last));
			if (i + 1 < static_cast<int>(info.blocks.size())) {
				block.succs.push_back(i + 1);
			}
		} else if (last != nullptr && (last->opcode == "j" || last->opcode == "jal")) {
			addTarget(referencedLabel(last));
		} else if (last == nullptr || last->opcode != "ret") {
			if (i + 1 < static_cast<int>(info.blocks.size())) {
				block.succs.push_back(i + 1);
			}
		}
		std::sort(block.succs.begin(), block.succs.end());
		block.succs.erase(std::unique(block.succs.begin(), block.succs.end()), block.succs.end());

		for (auto * inst : block.insts) {
			for (const auto & reg : instructionUseSet(inst)) {
				if (block.def.find(reg) == block.def.end()) {
					block.use.insert(reg);
				}
			}
			for (const auto & reg : instructionDefSet(inst)) {
				block.def.insert(reg);
			}
		}
	}

	bool changed = true;
	while (changed) {
		changed = false;
		for (int i = static_cast<int>(info.blocks.size()) - 1; i >= 0; --i) {
			auto & block = info.blocks[i];
			std::unordered_set<std::string> newOut;
			for (int succ : block.succs) {
				newOut.insert(info.blocks[succ].liveIn.begin(), info.blocks[succ].liveIn.end());
			}
			std::unordered_set<std::string> newIn = block.use;
			for (const auto & reg : newOut) {
				if (block.def.find(reg) == block.def.end()) {
					newIn.insert(reg);
				}
			}
			if (newOut != block.liveOut || newIn != block.liveIn) {
				block.liveOut = std::move(newOut);
				block.liveIn = std::move(newIn);
				changed = true;
			}
		}
	}
	return info;
}

/// @brief 判断寄存器是否在指令所在基本块的出口活跃
bool registerLiveOutOfBlock(const MachineLiveness & info, RiscV64Inst * inst, const std::string & reg)
{
	auto it = info.instToBlock.find(inst);
	if (it == info.instToBlock.end()) {
		return true;
	}
	return info.blocks[it->second].liveOut.find(reg) != info.blocks[it->second].liveOut.end();
}

/// @brief 判断寄存器是否可作为死定义清扫的候选
///
/// 仅对调用者保存的临时/参数寄存器（t0-t6、a0-a7、ft0-ft11、fa0-fa7）做死定义消除。
/// 被调用者保存寄存器（s/fs 系列）、sp、ra、gp、tp 承载跨过程或栈帧语义，
/// 其定义即便在本函数内看似无后续使用，也可能在返回后被调用方依赖，
/// 因此一律不在此清扫，避免破坏调用约定
bool isEliminableDefRegister(const std::string & reg)
{
	static const std::unordered_set<std::string> kRemovable = {
		"t0",  "t1",  "t2",  "t3",  "t4",   "t5",   "t6",   "a0",  "a1",  "a2",  "a3",
		"a4",  "a5",  "a6",  "a7",  "ft0",  "ft1",  "ft2",  "ft3", "ft4", "ft5", "ft6",
		"ft7", "ft8", "ft9", "ft10", "ft11", "fa0", "fa1",  "fa2", "fa3", "fa4", "fa5",
		"fa6", "fa7",
	};
	return kRemovable.find(reg) != kRemovable.end();
}

/// @brief 基于机器级活跃性分析的通用死定义清扫
///
/// 对每个基本块做一次反向扫描，维护活跃寄存器集合；若某条指令定义的物理寄存器
/// 在该指令之后不再被使用（既不在块内后续使用，也不在块出口活跃集合中），
/// 且该寄存器属于可清扫集合，则将其标记为死代码
///
/// 该清扫覆盖寄存器分配与其它 peephole 改写后残留的死 mv / li / fmv.w.x /
/// 地址计算等指令，而无需为每种模式单独编写规则
bool eliminateDeadDefinitions(InstList & code)
{
	const MachineLiveness liveness = buildMachineLiveness(code);
	bool changed = false;
	for (const auto & block : liveness.blocks) {
		std::unordered_set<std::string> live = block.liveOut;
		for (auto it = block.insts.rbegin(); it != block.insts.rend(); ++it) {
			auto * inst = *it;
			if (!isLiveInst(inst)) {
				continue;
			}
			const auto defs = instructionDefSet(inst);
			const auto uses = instructionUseSet(inst);
			bool removable = !defs.empty();
			for (const auto & def : defs) {
				if (!isEliminableDefRegister(def) || live.find(def) != live.end()) {
					removable = false;
					break;
				}
			}
			if (removable) {
				inst->setDead();
				changed = true;
				continue;
			}
			for (const auto & def : defs) {
				live.erase(def);
			}
			for (const auto & use : uses) {
				live.insert(use);
			}
		}
	}
	return changed;
}

/// @brief 将 `% ±2^k ==/!= 0` 的 signed-rem 降低序列改写为位测试。
///
/// 对于零比较，`x % ±2^k` 是否为 0 只取决于 x 的低 k 位；即便 x 为负数，
/// 零判定也与 `x & ((1<<k)-1)` 相同。仅在 remainder 结果不会跨出当前
/// 条件分支继续作为真实余数使用时，才删除完整余数链。
bool foldPowerOfTwoRemainderZeroBranch(InstList & code)
{
	bool changed = false;
	// 预先构建机器级活跃性分析，用于判断余数寄存器是否跨基本块存活
	const MachineLiveness liveness = buildMachineLiveness(code);

	for (auto it = code.begin(); it != code.end(); ++it) {
		// 第一步：匹配符号掩码指令 sraiw biasReg, srcReg, 31
		// 这是编译器生成的 signed remainder 序列的起始指令，
		// 将源操作数的符号位扩展到所有位，得到 0 或 -1
		auto * signMask = *it;
		if (!isLiveInst(signMask) || signMask->opcode != "sraiw" || signMask->arg2 != "31") {
			continue;
		}

		const std::string biasReg = signMask->result;
		const std::string srcReg = signMask->arg1;
		if (biasReg.empty() || srcReg.empty()) {
			continue;
		}

		// 第二步：依次匹配后续三条指令，构成除法商的计算链：
		//   srliw biasReg, biasReg, (32-k)   -- 右移得到偏移修正值
		//   addw  quotientReg, srcReg, biasReg -- 加上偏移修正
		//   sraiw quotientReg, quotientReg, k  -- 算术右移k位得到商
		auto biasShiftIt = nextMachineInst(code, it);
		auto addIt = nextMachineInst(code, biasShiftIt);
		auto quotientShiftIt = nextMachineInst(code, addIt);
		if (biasShiftIt == code.end() || addIt == code.end() || quotientShiftIt == code.end()) {
			continue;
		}

		auto * biasShift = *biasShiftIt;
		auto * add = *addIt;
		auto * quotientShift = *quotientShiftIt;
		int biasShiftAmount = 0;
		int quotientShiftAmount = 0;
		if (!isLiveInst(biasShift) || biasShift->opcode != "srliw" || biasShift->result != biasReg ||
		    biasShift->arg1 != biasReg || !parseIntImmediate(biasShift->arg2, biasShiftAmount) ||
		    !isLiveInst(add) || add->opcode != "addw" || add->arg1 != srcReg || add->arg2 != biasReg ||
		    add->result.empty() || !isLiveInst(quotientShift) || quotientShift->opcode != "sraiw" ||
		    quotientShift->result != add->result || quotientShift->arg1 != add->result ||
		    !parseIntImmediate(quotientShift->arg2, quotientShiftAmount)) {
			continue;
		}

		const std::string quotientReg = add->result;
		// 验证移位量合法性：k 在 [1,30] 范围内，且偏移移位量 = 32 - k
		if (quotientShiftAmount <= 0 || quotientShiftAmount >= 31 ||
		    biasShiftAmount != 32 - quotientShiftAmount) {
			continue;
		}

		// 收集所有需要标记为死代码的指令迭代器
		auto currentIt = quotientShiftIt;
		std::vector<InstIt> chainIts = {it, biasShiftIt, addIt, quotientShiftIt};

		// 第三步：可选地匹配负除数的取反指令 subw q, zero, q
		// 若除数为负数（如 x % -8），编译器会在商计算后插入取反操作
		bool negativeDivisor = false;
		auto maybeNegateIt = nextMachineInst(code, currentIt);
		if (maybeNegateIt != code.end() && isNegateSelf(*maybeNegateIt, quotientReg)) {
			negativeDivisor = true;
			chainIts.push_back(maybeNegateIt);
			currentIt = maybeNegateIt;
		}

		// 第四步：匹配商乘以除数的左移指令 slliw q, q, k
		// 这是计算 quotient * divisor = quotient * 2^k 的步骤
		auto productShiftIt = nextMachineInst(code, currentIt);
		if (productShiftIt == code.end()) {
			continue;
		}
		auto * productShift = *productShiftIt;
		int productShiftAmount = 0;
		if (!isLiveInst(productShift) || productShift->opcode != "slliw" ||
		    productShift->result != quotientReg || productShift->arg1 != quotientReg ||
		    !parseIntImmediate(productShift->arg2, productShiftAmount) ||
		    productShiftAmount != quotientShiftAmount) {
			continue;
		}
		chainIts.push_back(productShiftIt);
		currentIt = productShiftIt;

		// 若除数为负数，还需匹配第二次取反指令 subw q, zero, q
		// 用于将 quotient * (-2^k) 的结果取反
		if (negativeDivisor) {
			auto secondNegateIt = nextMachineInst(code, currentIt);
			if (secondNegateIt == code.end() || !isNegateSelf(*secondNegateIt, quotientReg)) {
				continue;
			}
			chainIts.push_back(secondNegateIt);
			currentIt = secondNegateIt;
		}

		// 第五步：匹配余数计算指令 subw remainderReg, srcReg, quotientReg
		// remainder = src - quotient * divisor
		auto remainderIt = nextMachineInst(code, currentIt);
		if (remainderIt == code.end()) {
			continue;
		}
		auto * remainder = *remainderIt;
		if (!isLiveInst(remainder) || remainder->opcode != "subw" || remainder->arg1 != srcReg ||
		    remainder->arg2 != quotientReg || remainder->result.empty()) {
			continue;
		}

		const std::string remainderReg = remainder->result;
		auto afterRemainderIt = nextMachineInst(code, remainderIt);
		if (afterRemainderIt == code.end()) {
			continue;
		}

		// 余数仅用于布尔判定的情形：snez/seqz boolReg, remainderReg。
		// 对 2 的幂取模，(n % 2^k != 0) 等价于 (n & (2^k-1) != 0)，与 n 的符号无关，
		// 因此可用位掩码替换整条有符号求余链，再让原 snez/seqz 作用于掩码结果即可。
		// 安全前提：remainderReg 的真实值无人依赖——既不 live-out，也不在 snez/seqz 之后被读取。
		{
			auto * boolInst = *afterRemainderIt;
			if (isLiveInst(boolInst) && (boolInst->opcode == "snez" || boolInst->opcode == "seqz") &&
			    boolInst->arg1 == remainderReg && !boolInst->result.empty() &&
			    !registerLiveOutOfBlock(liveness, boolInst, remainderReg) &&
			    !registerUsedAfterBeforeRedefOrBoundary(code, afterRemainderIt, remainderReg)) {
				for (auto chainIt : chainIts) {
					(*chainIt)->setDead();
				}
				const int mask = (1 << quotientShiftAmount) - 1;
				if (mask <= 2047) {
					remainder->replace("andi", remainderReg, srcReg, std::to_string(mask));
				} else {
					remainder->replace("slliw", remainderReg, srcReg,
					                    std::to_string(32 - quotientShiftAmount));
				}
				changed = true;
				continue;
			}
		}

		// 第六步：匹配零比较分支指令
		// 可能的形式：beq/bne remainderReg, zeroReg, label
		// 其中 zeroReg 可能是 "zero" 寄存器，也可能是 li 加载的 0
		InstIt zeroMaterializeIt = code.end();
		InstIt branchIt = code.end();
		std::string zeroReg;
		auto * nextInst = *afterRemainderIt;
		if (isLiveInst(nextInst) && nextInst->opcode == "li" && nextInst->arg1 == "0" &&
		    !nextInst->result.empty()) {
			// 零值通过 li 指令材料化到临时寄存器
			zeroMaterializeIt = afterRemainderIt;
			zeroReg = nextInst->result;
			branchIt = nextMachineInst(code, zeroMaterializeIt);
		} else {
			// 零值直接使用硬件零寄存器 "zero"
			zeroReg = "zero";
			branchIt = afterRemainderIt;
		}
		// 安全性检查：余数寄存器不能在基本块出口仍然活跃，
		// 否则后续代码可能依赖余数的真实值而非仅判断是否为零
		if (branchIt == code.end() || !branchComparesRegisterWithZero(*branchIt, remainderReg, zeroReg) ||
		    registerLiveOutOfBlock(liveness, *branchIt, remainderReg)) {
			continue;
		}

		// 第七步：执行优化改写
		// 将整个 signed remainder 计算链标记为死代码
		for (auto chainIt : chainIts) {
			(*chainIt)->setDead();
		}

		// 将余数计算替换为位掩码操作：
		//   若掩码值 mask = (1<<k)-1 <= 2047（12位立即数范围），使用 andi 指令
		//   否则使用 slliw 左移指令替代（后续配合其他优化完成掩码计算）
		const int mask = (1 << quotientShiftAmount) - 1;
		if (mask <= 2047) {
			remainder->replace("andi", remainderReg, srcReg, std::to_string(mask));
		} else {
			remainder->replace("slliw", remainderReg, srcReg, std::to_string(32 - quotientShiftAmount));
		}

		// 若零值是通过 li 材料化的临时寄存器，且该寄存器在分支后不再活跃，
		// 则将分支指令中的临时寄存器替换为 "zero"，并删除 li 指令
		if (zeroMaterializeIt != code.end() && !registerLiveOutOfBlock(liveness, *branchIt, zeroReg)) {
			auto * branch = *branchIt;
			if (branch->result == zeroReg) {
				branch->result = "zero";
			}
			if (branch->arg1 == zeroReg) {
				branch->arg1 = "zero";
			}
			(*zeroMaterializeIt)->setDead();
		}

		changed = true;
	}

	return changed;
}

/// @brief 判断寄存器在当前块内是否先被使用（或遇到控制流边界），再被重定义
bool registerUsedBeforeRedef(InstList & code, InstIt start, const std::string & reg)
{
	for (auto it = nextLive(code, start); it != code.end(); it = nextLive(code, it)) {
		auto * inst = *it;
		if (usesRegister(inst, reg) || instructionImplicitlyUsesRegister(inst, reg)) {
			return true;
		}
		if (isControlBoundary(inst)) {
			break;
		}
		if (definesResultOperand(inst) && inst->result == reg) {
			break;
		}
	}
	return false;
}

/// @brief 判断寄存器在当前块内是否先被使用（或遇到控制流边界），再被重定义
/// @note 与registerUsedBeforeRedef逻辑相同，用于不同上下文
bool registerUsedAfterBeforeRedefOrBoundary(InstList & code, InstIt start, const std::string & reg)
{
	for (auto it = nextLive(code, start); it != code.end(); it = nextLive(code, it)) {
		auto * inst = *it;
		if (usesRegister(inst, reg) || instructionImplicitlyUsesRegister(inst, reg)) {
			return true;
		}
		if (isControlBoundary(inst)) {
			break;
		}
		if (definesResultOperand(inst) && inst->result == reg) {
			break;
		}
	}
	return false;
}

/// @brief 判断寄存器在当前块内是否会先被重定义、而不是先被使用或跨出控制流边界。
///
/// 只有在这个条件成立时，才能安全地把前面的定义视为局部死定义；
/// 若先遇到边界，则寄存器可能在后继块继续存活，局部 peephole 不做猜测。
bool registerRedefinedBeforeUseOrBoundary(InstList & code, InstIt start, const std::string & reg)
{
	for (auto it = nextLive(code, start); it != code.end(); it = nextLive(code, it)) {
		auto * inst = *it;
		if (usesRegister(inst, reg) || instructionImplicitlyUsesRegister(inst, reg)) {
			return false;
		}
		if (isControlBoundary(inst)) {
			return false;
		}
		if (definesResultOperand(inst) && inst->result == reg) {
			return true;
		}
	}
	return true;
}

/// @brief 判断指令是否在任何操作数中提及指定寄存器
bool instructionMentionsRegister(RiscV64Inst * inst, const std::string & reg)
{
	return isLiveInst(inst) &&
	       (operandMentionsRegister(inst->result, reg) || operandMentionsRegister(inst->arg1, reg) ||
	        operandMentionsRegister(inst->arg2, reg) || operandMentionsRegister(inst->addition, reg));
}

/// @brief 判断寄存器是否在整个函数中被提及
bool registerMentionedInFunction(const InstList & code, const std::string & reg)
{
	for (auto * inst : code) {
		if (instructionMentionsRegister(inst, reg)) {
			return true;
		}
	}
	return false;
}

/// @brief 判断寄存器是否在指定指令范围内被提及
bool registerMentionedInRange(InstIt begin, InstIt end, const std::string & reg)
{
	for (auto it = begin; it != end; ++it) {
		if (instructionMentionsRegister(*it, reg)) {
			return true;
		}
	}
	return false;
}

/// @brief 判断寄存器是否在指定指令范围内被定义
bool registerDefinedInRange(InstIt begin, InstIt end, const std::string & reg)
{
	for (auto it = begin; it != end; ++it) {
		auto * inst = *it;
		if (definesResultOperand(inst) && inst->result == reg) {
			return true;
		}
	}
	return false;
}

/// @brief 从内存操作数中提取基址寄存器名，如 "8(sp)" -> "sp"
std::string addressBaseRegister(const std::string & operand)
{
	const auto open = operand.find('(');
	const auto close = operand.find(')', open == std::string::npos ? 0 : open);
	if (open == std::string::npos || close == std::string::npos || close <= open + 1) {
		return "";
	}
	return operand.substr(open + 1, close - open - 1);
}

/// @brief 栈槽访问摘要，用于局部 store-load forwarding
struct StackSlotAccess {
	bool valid = false;          ///< 是否成功解析为栈槽访问
	bool isLoad = false;         ///< 是否为加载指令
	bool isStore = false;        ///< 是否为存储指令
	int width = 0;               ///< 访问宽度（字节）
	std::string opcode;          ///< 原始操作码
	std::string base;            ///< 栈基址寄存器
	std::string locationKey;      ///< 栈槽地址键，包含基址和偏移
	std::string slotKey;         ///< 栈槽键，包含访问宽度、基址和偏移
	std::string valueReg;        ///< load 的目标寄存器或 store 的源寄存器
};

int stackAccessWidth(const std::string & opcode)
{
	if (opcode == "lb" || opcode == "lbu" || opcode == "sb") return 1;
	if (opcode == "lh" || opcode == "lhu" || opcode == "sh") return 2;
	if (opcode == "lw" || opcode == "lwu" || opcode == "sw" || opcode == "flw" || opcode == "fsw") return 4;
	if (opcode == "ld" || opcode == "sd" || opcode == "fld" || opcode == "fsd") return 8;
	return 0;
}

bool isMatchingGprStackLoadStore(const StackSlotAccess & store, const StackSlotAccess & load)
{
	return store.valid && load.valid && store.isStore && load.isLoad && store.slotKey == load.slotKey &&
	       ((store.opcode == "sd" && load.opcode == "ld") || (store.opcode == "sw" && load.opcode == "lw"));
}
bool isStackBaseRegister(const std::string & reg)
{
	return reg == "sp" || reg == "s0" || reg == "fp";
}

/// @brief 解析形如 off(sp) 或 off(s0) 的栈操作数
bool parseStackMemoryOperand(const std::string & operand, std::string & base, int & offset)
{
	const auto open = operand.find('(');
	const auto close = operand.find(')', open == std::string::npos ? 0 : open);
	if (open == std::string::npos || close == std::string::npos || close <= open + 1 || close + 1 != operand.size()) {
		return false;
	}

	base = operand.substr(open + 1, close - open - 1);
	if (!isStackBaseRegister(base)) {
		return false;
	}

	try {
		std::size_t parsed = 0;
		offset = std::stoi(operand.substr(0, open), &parsed);
		return parsed == open;
	} catch (...) {
		return false;
	}
}

/// @brief 提取直接以 sp/s0/fp 为基址的栈槽访问
StackSlotAccess decodeStackSlotAccess(RiscV64Inst * inst)
{
	StackSlotAccess access;
	if (!isLiveInst(inst) || !isMemoryOpcode(inst->opcode)) {
		return access;
	}

	std::string base;
	int offset = 0;
	if (!parseStackMemoryOperand(inst->arg1, base, offset)) {
		return access;
	}

	access.valid = true;
	access.isLoad = isLoadOpcode(inst->opcode);
	access.isStore = isStoreOpcode(inst->opcode);
	access.opcode = inst->opcode;
	access.width = stackAccessWidth(inst->opcode);
	if (access.width <= 0) {
		return StackSlotAccess{};
	}
	access.base = base;
	access.locationKey = base + ":" + std::to_string(offset);
	access.slotKey = std::to_string(access.width) + ":" + access.locationKey;
	access.valueReg = inst->result;
	return access;
}

/// @brief 栈槽级块活跃性信息
struct StackSlotBlockLiveness {
	std::vector<MachineBlock> blocks;                           ///< 复用机器基本块划分和后继
	std::unordered_map<RiscV64Inst *, int> instToBlock;         ///< 指令到块编号
};

/// @brief 构建栈槽级活跃性，用于判断 store 是否跨基本块有后续 load
StackSlotBlockLiveness buildStackSlotLiveness(InstList & code)
{
	StackSlotBlockLiveness info;
	MachineLiveness machine = buildMachineLiveness(code);
	info.blocks = std::move(machine.blocks);
	info.instToBlock = std::move(machine.instToBlock);

	for (auto & block : info.blocks) {
		block.use.clear();
		block.def.clear();
		block.liveIn.clear();
		block.liveOut.clear();
		for (auto * inst : block.insts) {
			StackSlotAccess access = decodeStackSlotAccess(inst);
			if (!access.valid) {
				continue;
			}
			if (access.isLoad && block.def.find(access.slotKey) == block.def.end()) {
				block.use.insert(access.slotKey);
			}
			if (access.isStore && (access.opcode == "sd" || access.opcode == "sw")) {
				block.def.insert(access.slotKey);
			}
		}
	}

	bool changed = true;
	while (changed) {
		changed = false;
		for (int i = static_cast<int>(info.blocks.size()) - 1; i >= 0; --i) {
			auto & block = info.blocks[i];
			std::unordered_set<std::string> newOut;
			for (int succ : block.succs) {
				newOut.insert(info.blocks[succ].liveIn.begin(), info.blocks[succ].liveIn.end());
			}
			std::unordered_set<std::string> newIn = block.use;
			for (const auto & slot : newOut) {
				if (block.def.find(slot) == block.def.end()) {
					newIn.insert(slot);
				}
			}
			if (newOut != block.liveOut || newIn != block.liveIn) {
				block.liveOut = std::move(newOut);
				block.liveIn = std::move(newIn);
				changed = true;
			}
		}
	}

	return info;
}

/// @brief 判断栈槽在当前块剩余部分是否仍可能被读取
bool stackSlotUsedLaterInBlock(InstList & code, InstIt start, const std::string & slotKey)
{
	for (auto it = nextLive(code, start); it != code.end(); it = nextLive(code, it)) {
		auto * inst = *it;
		if (isControlBoundary(inst)) {
			break;
		}

		StackSlotAccess access = decodeStackSlotAccess(inst);
		if (access.valid && access.slotKey == slotKey) {
			if (access.isLoad) {
				return true;
			}
			if (access.isStore && (access.opcode == "sd" || access.opcode == "sw")) {
				return false;
			}
			return true;
		}

		if (!access.valid && isLoadOpcode(inst->opcode)) {
			return true;
		}
	}
	return false;
}

/// @brief 判断栈槽是否在 store 所在块出口活跃
bool stackSlotLiveOutOfBlock(const StackSlotBlockLiveness & liveness, RiscV64Inst * inst, const std::string & slotKey)
{
	auto it = liveness.instToBlock.find(inst);
	if (it == liveness.instToBlock.end()) {
		return true;
	}
	return liveness.blocks[it->second].liveOut.find(slotKey) != liveness.blocks[it->second].liveOut.end();
}

/// @brief 转发同一基本块内的 64 位栈槽 store-load 往返
///
/// 匹配：
///   sd src, slot
///   ...
///   ld dst, slot
///
/// 若中间没有控制流边界、同槽写或可能改写该栈槽的未知 store，
/// 则把 ld 改为寄存器 move；若 src 在中间被重定义，则在 store 后补一个早期 move。
/// 当栈槽在后续块不活跃且当前块后续也不再读取时，原 store 也可删除
bool forwardStackStoreLoads(InstList & code)
{
	bool changed = false;

	for (auto it = code.begin(); it != code.end(); ++it) {
		auto * store = *it;
		StackSlotAccess storeAccess = decodeStackSlotAccess(store);
		if (!storeAccess.valid || !storeAccess.isStore || storeAccess.valueReg.empty() ||
		    (storeAccess.opcode != "sd" && storeAccess.opcode != "sw")) {
			continue;
		}

		bool sourceAvailable = true;
		for (auto scan = nextLive(code, it); scan != code.end(); scan = nextLive(code, scan)) {
			auto * inst = *scan;
			if (isControlBoundary(inst)) {
				break;
			}

			if (definesResultOperand(inst) && inst->result == storeAccess.base) {
				break;
			}

			StackSlotAccess access = decodeStackSlotAccess(inst);
			if (access.valid && access.isStore && access.locationKey == storeAccess.locationKey &&
			    access.slotKey != storeAccess.slotKey) {
				break;
			}
			if (access.valid && access.slotKey == storeAccess.slotKey) {
				if (access.isStore) {
					break;
				}
				if (!isMatchingGprStackLoadStore(storeAccess, access) || access.valueReg.empty()) {
					break;
				}

				const std::string dst = access.valueReg;
				if (sourceAvailable) {
					inst->replace("mv", dst, storeAccess.valueReg);
				} else {
					if (dst == storeAccess.valueReg || registerMentionedInRange(nextLive(code, it), scan, dst)) {
						break;
					}
					code.insert(std::next(it), new RiscV64Inst("mv", dst, storeAccess.valueReg));
					inst->setDead();
				}

				// 只做寄存器转发；保留原 store，避免在宽度别名、跨块或 ABI 栈槽
				// 情况下误删仍可能被读取的栈写。
				changed = true;
				break;
			}

			if (!access.valid && isStoreOpcode(inst->opcode)) {
				break;
			}

			if (definesResultOperand(inst) && inst->result == storeAccess.valueReg) {
				sourceAvailable = false;
			}
		}
	}

	return changed;
}

void invalidateCachedStackRegister(std::unordered_map<std::string, std::string> & cachedRegs,
                                   const std::string & reg)
{
	if (reg.empty()) {
		return;
	}
	for (auto it = cachedRegs.begin(); it != cachedRegs.end();) {
		if (it->second == reg) {
			it = cachedRegs.erase(it);
		} else {
			++it;
		}
	}
}

void invalidateCachedStackLocation(std::unordered_map<std::string, std::string> & cachedRegs,
                                    const std::string & locationKey)
{
	if (locationKey.empty()) {
		return;
	}
	const std::string suffix = ":" + locationKey;
	for (auto it = cachedRegs.begin(); it != cachedRegs.end();) {
		if (it->first.size() >= suffix.size() &&
		    it->first.compare(it->first.size() - suffix.size(), suffix.size(), suffix) == 0) {
			it = cachedRegs.erase(it);
		} else {
			++it;
		}
	}
}

///
/// 在无控制流边界、无未知store、缓存寄存器未被重定义的直线区间内，
/// 将第二次及后续 `lw/ld dst, slot` 改写为 `mv dst, cachedReg`。
bool eliminateRedundantStackReloads(InstList & code)
{
	bool changed = false;
	std::unordered_map<std::string, std::string> cachedRegs;

	for (auto it = code.begin(); it != code.end(); ++it) {
		auto * inst = *it;
		if (!isLiveInst(inst)) {
			continue;
		}
		if (isControlBoundary(inst)) {
			cachedRegs.clear();
			continue;
		}

		StackSlotAccess access = decodeStackSlotAccess(inst);
		if (access.valid && access.isStore) {
			invalidateCachedStackLocation(cachedRegs, access.locationKey);
			if ((access.opcode == "sd" || access.opcode == "sw") && !access.valueReg.empty()) {
				cachedRegs[access.slotKey] = access.valueReg;
			}
			continue;
		}

		if (access.valid && access.isLoad && (access.opcode == "ld" || access.opcode == "lw") &&
		    !access.valueReg.empty()) {
			const std::string dst = access.valueReg;
			auto cachedIt = cachedRegs.find(access.slotKey);
			if (cachedIt != cachedRegs.end() && !cachedIt->second.empty()) {
				const std::string src = cachedIt->second;
				invalidateCachedStackRegister(cachedRegs, dst);
				if (dst == src) {
					inst->setDead();
				} else {
					inst->replace("mv", dst, src);
				}
				cachedRegs[access.slotKey] = dst;
				changed = true;
				continue;
			}

			invalidateCachedStackRegister(cachedRegs, dst);
			cachedRegs[access.slotKey] = dst;
			continue;
		}

		if (!access.valid && isStoreOpcode(inst->opcode)) {
			cachedRegs.clear();
			continue;
		}
		if (definesResultOperand(inst)) {
			invalidateCachedStackRegister(cachedRegs, inst->result);
			if (inst->result == "sp" || inst->result == "s0" || inst->result == "fp") {
				cachedRegs.clear();
			}
		}
	}

	return changed;
}

/// @brief 在指定范围内选择一个未被使用的浮点临时寄存器（ft4-ft7）
std::string chooseFreeFloatTemp(InstIt begin, InstIt end)
{
	static const std::vector<std::string> kCandidates = {"ft4", "ft5", "ft6", "ft7"};
	for (const auto & reg : kCandidates) {
		if (!registerMentionedInRange(begin, end, reg)) {
			return reg;
		}
	}
	return "";
}

/// @brief 判断函数中是否包含存储指令
bool functionHasStore(const InstList & code)
{
	for (auto * inst : code) {
		if (isLiveInst(inst) && isStoreOpcode(inst->opcode)) {
			return true;
		}
	}
	return false;
}

/// @brief 强度消减：将乘以小常量的 mulw 替换为移位-加/减序列
///
/// 模式匹配：
///   li t, N
///   mulw d, s, t   →   移位-加/减序列，N ∈ [2..15]
///
/// 示例：
///   N=3  → slliw d,s,1; addw d,d,s     （2*s + s）
///   N=5  → slliw d,s,2; addw d,d,s     （4*s + s）
///   N=7  → slliw d,s,3; subw d,d,s     （8*s - s）
///   N=15 → slliw d,s,4; subw d,d,s     （16*s - s）
///
/// 对于 2 的幂直接用左移，仅对 2^k±1 生成等价的移位+加减。
bool reduceMulByConst(InstList & code)
{
	bool changed = false;
	for (auto it = code.begin(); it != code.end(); ++it) {
		auto * li = *it;
		if (!isLiveInst(li) || li->opcode != "li") {
			continue;
		}

		// Parse the immediate value
		int imm = 0;
		try {
			imm = std::stoi(li->arg1);
		} catch (...) {
			continue;
		}
		// 扩展范围：支持更多2^k±1模式，以及负数
		const int absImm = imm < 0 ? -imm : imm;
		if (absImm < 2 || absImm > 127) {
			continue;
		}

		auto mulIt = nextLive(code, it);
		if (mulIt == code.end()) {
			continue;
		}
		auto * mul = *mulIt;
		if (!isLiveInst(mul) || mul->opcode != "mulw") {
			continue;
		}

		const std::string d = mul->result;
		const std::string s = mul->arg1;
		const std::string t = mul->arg2;
		const std::string constReg = li->result;

		// The li target must be one of the mulw operands
		if (t != constReg && s != constReg) {
			continue;
		}

		// Determine which operand is the variable (not the constant)
		if (s == constReg && t == constReg) {
			continue;
		}
		const std::string var = (t == constReg) ? s : t;

		// 扩展的强度削减模式：支持所有2^k±1形式以及负数
		int shift = 0;
		std::string followOp;
		bool needNegate = imm < 0;  // 负数乘法需要最后取反

		if (isPowerOfTwo(absImm)) {
			// 2的幂：直接移位
			shift = log2PowerOfTwo(absImm);
		} else if (isPowerOfTwo(absImm - 1)) {
			// 2^k + 1: 例如 3=2+1, 5=4+1, 9=8+1, 17=16+1, ...
			shift = log2PowerOfTwo(absImm - 1);
			followOp = "addw";  // (x << k) + x
		} else if (isPowerOfTwo(absImm + 1)) {
			// 2^k - 1: 例如 7=8-1, 15=16-1, 31=32-1, 63=64-1, ...
			shift = log2PowerOfTwo(absImm + 1);
			followOp = "subw";  // (x << k) - x
		} else {
			continue;  // 不支持的模式
		}

		if (!followOp.empty() && d == var) {
			continue;
		}
		if (constReg != d && registerUsedBeforeRedef(code, mulIt, constReg)) {
			continue;
		}

		// Find the position to insert new instructions
		auto insertPos = mulIt;
		++insertPos;

		li->setDead();
		mul->setDead();
		code.insert(insertPos, new RiscV64Inst("slliw", d, var, std::to_string(shift)));
		if (!followOp.empty()) {
			code.insert(insertPos, new RiscV64Inst(followOp, d, d, var));
		}
		// 负数乘法：生成 -result
		if (needNegate) {
			code.insert(insertPos, new RiscV64Inst("subw", d, "zero", d));
		}

		changed = true;
	}
	return changed;
}

bool foldUnitStepIncrements(InstList & code)
{
	bool changed = false;
	for (auto it = code.begin(); it != code.end(); ++it) {
		auto * li = *it;
		if (!isLiveInst(li) || li->opcode != "li" || li->arg1 != "1" || li->result.empty()) {
			continue;
		}

		auto addIt = nextLive(code, it);
		auto mvIt = nextLive(code, addIt);
		if (addIt == code.end() || mvIt == code.end()) {
			continue;
		}

		auto * add = *addIt;
		auto * mv = *mvIt;
		if (!isLiveInst(add) || add->opcode != "addw" || add->result.empty()) {
			continue;
		}

		std::string indexReg;
		if (add->arg1 != li->result && add->arg2 == li->result) {
			indexReg = add->arg1;
		} else if (add->arg2 != li->result && add->arg1 == li->result) {
			indexReg = add->arg2;
		} else {
			continue;
		}

		if (add->result == indexReg) {
			if (registerUsedAfterBeforeRedefOrBoundary(code, addIt, li->result)) {
				continue;
			}
			li->setDead();
			add->replace("addiw", indexReg, indexReg, "1");
			changed = true;
			continue;
		}

		if (!isLiveInst(mv) || mv->opcode != "mv" || mv->result != indexReg || mv->arg1 != add->result) {
			continue;
		}
		if (registerUsedAfterBeforeRedefOrBoundary(code, mvIt, li->result)) {
			continue;
		}

		li->setDead();
		add->replace("addiw", indexReg, indexReg, "1");
		mv->setDead();
		changed = true;
	}
	return changed;
}

/// @brief 将紧邻访存的地址临时寄存器折叠进12位访存偏移。
///
///   addi tmp, base, imm
///   lw   dst, 0(tmp)      ->   lw dst, imm(base)
///
/// store 同理，但当 tmp 同时作为待写入值时跳过，避免改变 store 的数据值。
bool foldAddiAddressIntoMemoryOffset(InstList & code)
{
	bool changed = false;
	for (auto it = code.begin(); it != code.end(); ++it) {
		auto * addi = *it;
		if (!isLiveInst(addi) || addi->opcode != "addi" || addi->result.empty() || addi->arg1.empty()) {
			continue;
		}

		const std::string tmpReg = addi->result;
		const std::string baseReg = addi->arg1;
		if (tmpReg == baseReg) {
			continue;
		}

		int addOffset = 0;
		if (!parseIntImmediate(addi->arg2, addOffset)) {
			continue;
		}

		auto memIt = nextMachineInst(code, it);
		if (memIt == code.end()) {
			continue;
		}
		auto * mem = *memIt;
		if (!isLiveInst(mem) || !isMemoryOpcode(mem->opcode)) {
			continue;
		}

		int memOffset = 0;
		std::string memBase;
		if (!parseMemoryOperand(mem->arg1, memOffset, memBase) || memBase != tmpReg) {
			continue;
		}

		const int foldedOffset = addOffset + memOffset;
		if (!isSigned12BitImmediate(foldedOffset)) {
			continue;
		}

		if (isStoreOpcode(mem->opcode) && mem->result == tmpReg) {
			continue;
		}

		const bool memRedefinesTmp = isLoadOpcode(mem->opcode) && mem->result == tmpReg;
		// 若 tmp 不是被这条 load 覆盖，删除 addi 前必须证明 tmp 在当前基本块内会被重新定义。
		// 只检查“后面是否直接使用”不够：tmp 可能跨控制流边界在后继块使用，例如：
		//   addi a2,a1,4; lw t0,0(a2); ...; blt ..., .L; .L: mv a1,a2
		// 遇到边界时无法做局部证明，保守跳过。
		if (!memRedefinesTmp && !registerRedefinedBeforeUseOrBoundary(code, memIt, tmpReg)) {
			continue;
		}

		mem->arg1 = formatMemoryOperand(foldedOffset, baseReg);
		addi->setDead();
		changed = true;
	}
	return changed;
}

/// @brief 判断指令是否匹配指定的操作码、结果寄存器和操作数（空字符串表示不检查）
bool isInst(RiscV64Inst * inst,
            const std::string & opcode,
            const std::string & result = "",
            const std::string & arg1 = "",
            const std::string & arg2 = "")
{
	if (!isLiveInst(inst) || inst->opcode != opcode) {
		return false;
	}
	if (!result.empty() && inst->result != result) {
		return false;
	}
	if (!arg1.empty() && inst->arg1 != arg1) {
		return false;
	}
	if (!arg2.empty() && inst->arg2 != arg2) {
		return false;
	}
	return true;
}

bool parseSmallNonNegativeInteger(const std::string & text, int & value)
{
	try {
		value = std::stoi(text);
	} catch (...) {
		return false;
	}
	return value >= 0;
}

/// @brief 在循环体中查找索引变量的单位步长更新指令
///
/// 匹配两种模式：
///   1. addi/addiw indexReg, indexReg, 1
///   2. li tmpReg, 1; addw indexReg, indexReg, tmpReg（或 addw indexReg, tmpReg, indexReg）
/// @param code 指令列表
/// @param bodyBegin 循环体起始迭代器
/// @param latchIt 循环 latch 迭代器
/// @param indexReg 循环索引寄存器
/// @param insertBefore [out] 找到的更新指令位置，用于后续插入指针步进指令
/// @return true 表示找到单位步长更新
bool findUnitStepUpdate(InstList & code, InstIt bodyBegin, InstIt latchIt, const std::string & indexReg, InstIt & insertBefore)
{
	for (auto it = bodyBegin; it != latchIt && it != code.end(); it = nextLive(code, it)) {
		auto * inst = *it;
		if (!isLiveInst(inst)) {
			continue;
		}

		if ((inst->opcode == "addi" || inst->opcode == "addiw") && inst->result == indexReg &&
		    inst->arg1 == indexReg && inst->arg2 == "1") {
			insertBefore = it;
			return true;
		}

		if (!isInst(inst, "li") || inst->arg1 != "1") {
			continue;
		}

		const std::string oneReg = inst->result;
		auto addIt = nextLive(code, it);
		if (addIt == code.end()) {
			continue;
		}
		auto * add = *addIt;
		if (!isLiveInst(add) || add->opcode != "addw") {
			continue;
		}
		const bool addIndexOne = add->arg1 == indexReg && add->arg2 == oneReg;
		const bool addOneIndex = add->arg1 == oneReg && add->arg2 == indexReg;
		if (!addIndexOne && !addOneIndex) {
			continue;
		}

		if (add->result == indexReg) {
			insertBefore = it;
			return true;
		}

		for (auto scan = nextLive(code, addIt); scan != latchIt && scan != code.end(); scan = nextLive(code, scan)) {
			auto * scanInst = *scan;
			if (!isLiveInst(scanInst)) {
				continue;
			}
			if (isInst(scanInst, "mv", indexReg, add->result)) {
				insertBefore = it;
				return true;
			}
			if (definesResultOperand(scanInst) && scanInst->result == add->result) {
				break;
			}
		}
	}
	return false;
}

/// @brief 将内存指令中的地址基寄存器替换为新寄存器
/// @param inst 待修改的指令
/// @param oldReg 旧基寄存器
/// @param newReg 新基寄存器
/// @return true 表示至少替换了一处
bool replaceMemoryBase(RiscV64Inst * inst, const std::string & oldReg, const std::string & newReg)
{
	if (!isLiveInst(inst) || !isMemoryOpcode(inst->opcode)) {
		return false;
	}

	bool changed = false;
	changed = replaceAddressBase(inst->result, oldReg, newReg) || changed;
	changed = replaceAddressBase(inst->arg1, oldReg, newReg) || changed;
	changed = replaceAddressBase(inst->arg2, oldReg, newReg) || changed;
	changed = replaceAddressBase(inst->addition, oldReg, newReg) || changed;
	return changed;
}

/// @brief 将普通操作数中对寄存器的直接使用替换为新寄存器，不改写定义位置。
bool replaceRegisterUse(RiscV64Inst * inst, const std::string & oldReg, const std::string & newReg)
{
	if (!isLiveInst(inst)) {
		return false;
	}

	bool changed = false;
	if (!definesResultOperand(inst) && inst->result == oldReg) {
		inst->result = newReg;
		changed = true;
	}
	if (inst->arg1 == oldReg) {
		inst->arg1 = newReg;
		changed = true;
	}
	if (inst->arg2 == oldReg) {
		inst->arg2 = newReg;
		changed = true;
	}
	if (inst->addition == oldReg) {
		inst->addition = newReg;
		changed = true;
	}
	return changed;
}

bool replaceUsesBeforeRedef(InstIt begin, InstIt end, const std::string & oldReg, const std::string & newReg)
{
	bool changed = false;
	for (auto it = begin; it != end; ++it) {
		auto * inst = *it;
		if (!isLiveInst(inst)) {
			continue;
		}
		if (definesResultOperand(inst) && inst->result == oldReg) {
			break;
		}
		changed = replaceRegisterUse(inst, oldReg, newReg) || changed;
	}
	return changed;
}

/// @brief 将块内局部物化后的唯一 move 折叠回物化指令。
///
/// 匹配：
///   li/la tmp, X
///   ...
///   mv dst, tmp
///
/// 仅当 tmp 的第一个后续提及就是该 move、dst 在两者之间未被提及，
/// 且 tmp 在 move 后会先被重定义而不是跨出块边界时才改写。
/// @brief 折叠立即数/地址材料化后的冗余move
///
/// 匹配模式：
///   li/la src, imm     // 材料化立即数或地址到src
///   ...
///   mv dst, src        // 随后move到dst
///
/// 若src在li/la与mv之间未被其他指令使用，且dst在两者之间未出现，
/// 且src在mv之后会被重定义（即mv是src的唯一消费），则将li/la的目标
/// 直接改为dst，消除冗余move。
bool foldMaterializationMoves(InstList & code)
{
	bool changed = false;
	for (auto it = code.begin(); it != code.end(); ++it) {
		auto * materialize = *it;
		if (!isLiveInst(materialize) || (materialize->opcode != "li" && materialize->opcode != "la") ||
		    materialize->result.empty()) {
			continue;
		}

		const std::string src = materialize->result;
		for (auto scan = nextLive(code, it); scan != code.end(); scan = nextLive(code, scan)) {
			auto * inst = *scan;
			if (isControlBoundary(inst)) {
				break;
			}
			if (!instructionMentionsRegister(inst, src)) {
				continue;
			}

			if (inst->opcode != "mv" || inst->arg1 != src || inst->result.empty() || inst->result == src) {
				break;
			}

			const std::string dst = inst->result;
			auto betweenBegin = nextLive(code, it);
			if (registerMentionedInRange(betweenBegin, scan, dst) ||
			    !registerRedefinedBeforeUseOrBoundary(code, scan, src)) {
				break;
			}

			materialize->result = dst;
			inst->setDead();
			changed = true;
			break;
		}
	}
	return changed;
}

/// @brief 折叠零值材料化后的整数 store
///
/// 匹配：
///   li tmp, 0
///   ...
///   sw/sd tmp, addr
///
/// RISC-V store 可以直接使用 zero 寄存器作为源操作数，
/// 若 tmp 在 store 后不再使用，则删除对应 li
bool foldZeroStores(InstList & code)
{
	bool changed = false;
	for (auto it = code.begin(); it != code.end(); ++it) {
		auto * li = *it;
		if (!isLiveInst(li) || li->opcode != "li" || li->arg1 != "0" || li->result.empty()) {
			continue;
		}

		const std::string zeroReg = li->result;
		for (auto scan = nextLive(code, it); scan != code.end(); scan = nextLive(code, scan)) {
			auto * inst = *scan;
			if (isControlBoundary(inst)) {
				break;
			}
			if (!instructionMentionsRegister(inst, zeroReg)) {
				continue;
			}

			const bool integerStore = inst->opcode == "sb" || inst->opcode == "sh" || inst->opcode == "sw" ||
			                          inst->opcode == "sd";
			const bool storeUsesZeroReg = integerStore && inst->result == zeroReg &&
			                          !operandMentionsRegister(inst->arg1, zeroReg) &&
			                          !operandMentionsRegister(inst->arg2, zeroReg) &&
			                          !operandMentionsRegister(inst->addition, zeroReg);
			if (!storeUsesZeroReg) {
				break;
			}

			inst->result = "zero";
			if (!registerUsedAfterBeforeRedefOrBoundary(code, scan, zeroReg)) {
				li->setDead();
			}
			changed = true;
			break;
		}
	}
	return changed;
}

/// @brief 判断指令是否为从寄存器到寄存器的move（mv、fmv.s或fsgnj.s同寄存器形式）
/// @param inst 待判断的指令
/// @param dst [out] 目标寄存器名
/// @param src [out] 源寄存器名
/// @return 若为move指令则返回true
bool isMoveFromRegister(RiscV64Inst * inst, std::string & dst, std::string & src)
{
	if (!isLiveInst(inst) || inst->result.empty()) {
		return false;
	}
	if (inst->opcode == "mv" && !inst->arg1.empty()) {
		dst = inst->result;
		src = inst->arg1;
		return true;
	}
	// fmv.s rd,rs（标准化后的形式）
	if (inst->opcode == "fmv.s" && !inst->arg1.empty() && inst->arg2.empty()) {
		dst = inst->result;
		src = inst->arg1;
		return true;
	}
	// fsgnj.s rd,rs,rs（未标准化的遗留形式）
	if (inst->opcode == "fsgnj.s" && !inst->arg1.empty() && inst->arg1 == inst->arg2) {
		dst = inst->result;
		src = inst->arg1;
		return true;
	}
	return false;
}

/// @brief 将局部唯一消费的 producer 直接改写到 copy 目标寄存器。
///
/// 匹配：
///   def tmp, ...
///   ...
///   mv/fsgnj.s dst, tmp
///
/// 只在同一基本块内改写；tmp 的第一次后续提及必须就是该 copy，
/// dst 在 producer 与 copy 之间不能出现，copy 后 tmp 在本块内也不能
/// 再被读取。这样把“计算到 tmp 再搬到 dst”收成“直接计算到 dst”。
bool retargetSingleUseDefinitions(InstList & code)
{
	bool changed = false;
	const MachineLiveness liveness = buildMachineLiveness(code);
	for (auto it = code.begin(); it != code.end(); ++it) {
		auto * def = *it;
		if (!definesResultOperand(def) || def->result.empty()) {
			continue;
		}

		const std::string tmp = def->result;
		for (auto scan = nextLive(code, it); scan != code.end(); scan = nextLive(code, scan)) {
			auto * inst = *scan;
			if (isControlBoundary(inst)) {
				break;
			}
			if (!instructionMentionsRegister(inst, tmp)) {
				continue;
			}

			std::string dst;
			std::string src;
			if (!isMoveFromRegister(inst, dst, src) || src != tmp || dst.empty() || dst == tmp) {
				break;
			}

			const auto betweenBegin = nextLive(code, it);
			if (registerMentionedInRange(betweenBegin, scan, dst) ||
			    registerUsedAfterBeforeRedefOrBoundary(code, scan, tmp) ||
			    registerLiveOutOfBlock(liveness, inst, tmp)) {
				break;
			}

			def->result = dst;
			inst->setDead();
			changed = true;
			break;
		}
	}
	return changed;
}

/// @brief 在单个基本块内传播寄存器拷贝（mv 或 fsgnj.s rd,rs,rs）的目标寄存器使用。
///
/// `mv/fsgnj.s dst, src` 之后，直到 dst/src 被重定义或遇到控制流边界前，
/// 后续读取 dst 等价于读取 src。这里会同时改写普通 operand 与内存地址基寄存器。
bool propagateMoveUses(InstList & code)
{
	bool changed = false;
	for (auto it = code.begin(); it != code.end(); ++it) {
		auto * move = *it;
		std::string dst;
		std::string src;
		if (!isMoveFromRegister(move, dst, src) || dst.empty() || src.empty() || dst == src) {
			continue;
		}

		for (auto scan = nextLive(code, it); scan != code.end(); scan = nextLive(code, scan)) {
			auto * inst = *scan;
			if (isControlBoundary(inst)) {
				break;
			}

			if (usesRegister(inst, dst)) {
				changed = replaceRegisterUse(inst, dst, src) || changed;
				if (isMemoryOpcode(inst->opcode)) {
					changed = replaceMemoryBase(inst, dst, src) || changed;
				}
			}

			const bool redefinesDst = definesResultOperand(inst) && inst->result == dst;
			const bool redefinesSrc = definesResultOperand(inst) && inst->result == src;
			if (redefinesDst || redefinesSrc) {
				break;
			}
		}
	}
	return changed;
}

bool rewriteCopyUsesInBlock(const MachineBlock & block,
                            std::size_t startIndex,
                            const std::string & dst,
                            const std::string & src,
                            bool & reachesBlockEnd)
{
	bool changed = false;
	reachesBlockEnd = true;
	for (std::size_t i = startIndex; i < block.insts.size(); ++i) {
		auto * inst = block.insts[i];
		if (!isLiveInst(inst) || isCommentInst(inst)) {
			continue;
		}

		if (inst->opcode == "call") {
			reachesBlockEnd = false;
			break;
		}

		if (usesRegister(inst, dst)) {
			changed = replaceRegisterUse(inst, dst, src) || changed;
			if (isMemoryOpcode(inst->opcode)) {
				changed = replaceMemoryBase(inst, dst, src) || changed;
			}
		}

		const bool redefinesDst = definesResultOperand(inst) && inst->result == dst;
		const bool redefinesSrc = src != "zero" && definesResultOperand(inst) && inst->result == src;
		if (redefinesDst || redefinesSrc || inst->opcode == "ret") {
			reachesBlockEnd = false;
			break;
		}
	}
	return changed;
}

/// @brief 沿 CFG 传播寄存器 copy，覆盖入口 copy 后跨分支使用的场景。
///
/// 单基本块的 propagateMoveUses 会在条件分支处停止，因此像
/// `fsgnj.s ft0,fa0,fa0; ...; beq ...; ret-paths-use-ft0` 这样的形参
/// 保护 copy 会残留。这里做一个非常保守的 available-copy 转发：只有当
/// 某个块的所有前驱都已经保持 `dst == src`，且路径上没有 call 或重定义
/// dst/src 时，才改写该块内 dst 的读取。
bool propagateMoveUsesAcrossBlocks(InstList & code)
{
	bool changed = false;
	const MachineLiveness liveness = buildMachineLiveness(code);
	if (liveness.blocks.empty()) {
		return false;
	}

	std::vector<std::vector<int>> preds(liveness.blocks.size());
	for (int i = 0; i < static_cast<int>(liveness.blocks.size()); ++i) {
		for (int succ : liveness.blocks[i].succs) {
			if (succ >= 0 && succ < static_cast<int>(preds.size())) {
				preds[succ].push_back(i);
			}
		}
	}

	for (auto * move : code) {
		std::string dst;
		std::string src;
		if (!isMoveFromRegister(move, dst, src) || dst.empty() || src.empty() || dst == src) {
			continue;
		}
		if (!isCopyPropagationRegister(dst) || !isCopyPropagationRegister(src)) {
			continue;
		}

		auto blockIt = liveness.instToBlock.find(move);
		if (blockIt == liveness.instToBlock.end()) {
			continue;
		}

		const int startBlock = blockIt->second;
		const auto & startInsts = liveness.blocks[startBlock].insts;
		auto moveInBlock = std::find(startInsts.begin(), startInsts.end(), move);
		if (moveInBlock == startInsts.end()) {
			continue;
		}

		std::vector<bool> availableOut(liveness.blocks.size(), false);
		bool startOut = false;
		changed = rewriteCopyUsesInBlock(liveness.blocks[startBlock],
		                                  static_cast<std::size_t>(std::distance(startInsts.begin(), moveInBlock) + 1),
		                                  dst,
		                                  src,
		                                  startOut) ||
		          changed;
		availableOut[startBlock] = startOut;

		bool stateChanged = true;
		while (stateChanged) {
			stateChanged = false;
			for (int blockIndex = 0; blockIndex < static_cast<int>(liveness.blocks.size()); ++blockIndex) {
				if (blockIndex == startBlock || preds[blockIndex].empty()) {
					continue;
				}

				bool availableIn = true;
				for (int pred : preds[blockIndex]) {
					availableIn = availableIn && availableOut[pred];
				}
				if (!availableIn) {
					continue;
				}

				bool blockOut = false;
				changed = rewriteCopyUsesInBlock(liveness.blocks[blockIndex], 0, dst, src, blockOut) || changed;
				if (blockOut != availableOut[blockIndex]) {
					availableOut[blockIndex] = blockOut;
					stateChanged = true;
				}
			}
		}
	}

	return changed;
}

/// @brief 删除只在本基本块内被后续重定义覆盖的死寄存器拷贝（mv 或 fsgnj.s rd,rs,rs）。
bool removeDeadMoves(InstList & code)
{
	bool changed = false;
	for (auto it = code.begin(); it != code.end(); ++it) {
		auto * move = *it;
		std::string dst;
		std::string src;
		if (!isMoveFromRegister(move, dst, src) || dst.empty() || src.empty() || dst == src) {
			continue;
		}
		if (registerRedefinedBeforeUseOrBoundary(code, it, dst)) {
			move->setDead();
			changed = true;
		}
	}
	return changed;
}

/// @brief 删除“结果寄存器在全函数范围内已死”的纯定义指令（无内存/控制副作用的寄存器赋值）。
///
/// 与 removeDeadMoves（仅块内）不同，这里借助机器级活跃性分析判断结果是否 live-out，
/// 因此能删除跨调用/分支后仍无人使用的死定义，例如：
///   - 经跨过程常量传播特化后，函数仍保存却再不使用的形参拷贝（mv t0,a0 / fsgnj.s ft1,fa2,fa2）；
///   - 浮点常量去重后遗留、其 GPR 不再被读取的 lui/li 馈给指令。
/// 仅处理已知纯净的操作码，避免误删带副作用或多定义的指令。
bool removeDeadPureDefs(InstList & code)
{
	static const std::unordered_set<std::string> pureOps = {
		"mv", "fsgnj.s", "fsgnjn.s", "fsgnjx.s", "fmv.s", "fmv.w.x", "fmv.x.w", "li", "lui",
	};

	bool changed = false;
	const MachineLiveness liveness = buildMachineLiveness(code);
	for (auto it = code.begin(); it != code.end(); ++it) {
		auto * inst = *it;
		if (!definesResultOperand(inst) || inst->result.empty() || !isPhysicalRegisterName(inst->result)) {
			continue;
		}
		if (pureOps.find(inst->opcode) == pureOps.end()) {
			continue;
		}
		// 结果寄存器既不在块内（重定义/边界前）被使用，也不 live-out ⇒ 该定义已死。
		if (!registerUsedAfterBeforeRedefOrBoundary(code, it, inst->result) &&
		    !registerLiveOutOfBlock(liveness, inst, inst->result)) {
			inst->setDead();
			changed = true;
		}
	}
	return changed;
}

/// @brief 仿射地址链描述：循环内形如 base + index * 2^scale 的地址计算序列
struct AffineAddressChain {
    InstIt baseMoveIt;                  ///< mv addrReg, baseReg 指令位置
    InstIt indexMoveIt;                 ///< mv tmpReg, indexReg 指令位置
    InstIt shiftIt;                     ///< slli tmpReg, tmpReg, scale 指令位置
    InstIt addIt;                       ///< add addrReg, addrReg/baseReg, tmpReg 指令位置
    std::vector<InstIt> rewriteUses;    ///< 使用 addrReg 作为地址基址或派生地址源的指令列表
    std::string baseReg;                ///< 数组基址寄存器
    std::string addrReg;                ///< 计算出的地址寄存器
    std::string tmpReg;                 ///< 中间临时寄存器
    std::string pointerReg;             ///< 分配的指针步进寄存器
    int scale = 0;                      ///< 左移位数（元素大小以2的幂表示）
    int stride = 0;                     ///< 步进字节数 = 2^scale
};

/// @brief 匹配循环体内的仿射地址计算链
///
/// 识别形如以下指令序列：
///   mv addrReg, baseReg        // 加载数组基址
///   mv tmpReg, indexReg        // 复制循环索引
///   slli tmpReg, tmpReg, k     // 计算字节偏移 = index * 2^k
///   add addrReg, addrReg, tmpReg  // 最终地址 = base + offset
/// 并收集后续使用 addrReg 作为内存基址的 load 指令。
/// @param code 指令列表
/// @param start 起始扫描位置（应为 mv addrReg, baseReg）
/// @param latchIt 循环 latch 位置
/// @param indexReg 循环索引寄存器名
/// @param chain [out] 匹配成功的仿射地址链
/// @return true 表示成功匹配一条完整的仿射地址链
bool matchAffineAddressChain(InstList & code,
                             InstIt start,
                             InstIt latchIt,
                             const std::string & indexReg,
                             AffineAddressChain & chain)
{
	auto * baseMove = *start;
	if (!isInst(baseMove, "mv") || baseMove->result.empty() || baseMove->arg1.empty()) {
		return false;
	}

	auto indexMoveIt = nextLive(code, start);
	auto shiftIt = nextLive(code, indexMoveIt);
	auto addIt = nextLive(code, shiftIt);
	if (indexMoveIt == code.end() || shiftIt == code.end() || addIt == code.end()) {
		return false;
	}

	auto * indexMove = *indexMoveIt;
	auto * shift = *shiftIt;
	auto * add = *addIt;

	const std::string addrReg = baseMove->result;
	const std::string baseReg = baseMove->arg1;
	if (!isInst(indexMove, "mv") || indexMove->arg1 != indexReg || indexMove->result.empty()) {
		return false;
	}
	const std::string tmpReg = indexMove->result;

	int scale = 0;
	if (!isInst(shift, "slli", tmpReg, tmpReg) || !parseSmallNonNegativeInteger(shift->arg2, scale) ||
	    scale > 15) {
		return false;
	}

	if (!isInst(add, "add", addrReg)) {
		return false;
	}
	const bool addAddrTmp = add->arg1 == addrReg && add->arg2 == tmpReg;
	const bool addBaseTmp = add->arg1 == baseReg && add->arg2 == tmpReg;
	const bool addTmpAddr = add->arg1 == tmpReg && add->arg2 == addrReg;
	const bool addTmpBase = add->arg1 == tmpReg && add->arg2 == baseReg;
	if (!addAddrTmp && !addBaseTmp && !addTmpAddr && !addTmpBase) {
		return false;
	}

	const int stride = 1 << scale;
	if (stride <= 0 || stride > 32767 || addrReg == indexReg || tmpReg == indexReg || baseReg == indexReg) {
		return false;
	}

	std::vector<InstIt> rewriteUses;
	for (auto scan = nextLive(code, addIt); scan != code.end() && scan != latchIt; scan = nextLive(code, scan)) {
		auto * inst = *scan;
		if (!isLiveInst(inst)) {
			continue;
		}

		if (definesResultOperand(inst) && inst->result == addrReg) {
			break;
		}
		if (definesResultOperand(inst) && inst->result == tmpReg) {
			break;
		}

		const bool usesAddr = usesRegister(inst, addrReg);
		const bool usesTmp = usesRegister(inst, tmpReg);
		if (usesTmp) {
			return false;
		}
		if (!usesAddr) {
			continue;
		}

		const bool memoryBaseUse =
		    isMemoryOpcode(inst->opcode) &&
		    (operandReferencesAddressBase(inst->result, addrReg) ||
		     operandReferencesAddressBase(inst->arg1, addrReg) ||
		     operandReferencesAddressBase(inst->arg2, addrReg) ||
		     operandReferencesAddressBase(inst->addition, addrReg));
		const bool derivedAddressUse =
		    isInst(inst, "mv") && inst->arg1 == addrReg && inst->result != indexReg && inst->result != tmpReg;
		if (!memoryBaseUse && !derivedAddressUse) {
		    return false;
		}
		rewriteUses.push_back(scan);
	}

	if (rewriteUses.empty()) {
		return false;
	}

	chain.baseMoveIt = start;
	chain.indexMoveIt = indexMoveIt;
	chain.shiftIt = shiftIt;
	chain.addIt = addIt;
	chain.rewriteUses = std::move(rewriteUses);
	chain.baseReg = baseReg;
	chain.addrReg = addrReg;
	chain.tmpReg = tmpReg;
	chain.scale = scale;
	chain.stride = stride;
	return true;
}

/// @brief 判断循环体是否包含不安全的控制流或调用指令
///
/// 若循环体中存在标签、call、ret、分支或跳转指令，
/// 则认为该循环体结构过于复杂，不适合进行仿射地址优化。
/// @param code 指令列表
/// @param bodyBegin 循环体起始位置
/// @param latchIt 循环 latch 位置
/// @return true 表示循环体包含不安全指令
bool loopBodyHasUnsafeControlOrCall(InstList & code, InstIt bodyBegin, InstIt latchIt)
{
	for (auto it = bodyBegin; it != latchIt && it != code.end(); it = nextLive(code, it)) {
		auto * inst = *it;
		if (!isLiveInst(inst)) {
			continue;
		}
		if (isLabel(inst) || inst->opcode == "call" || inst->opcode == "ret" || isBranchOpcode(inst->opcode) ||
		    inst->opcode == "j") {
			return true;
		}
	}
	return false;
}

/// @brief 生成唯一的派生标签名（在原标签后追加 _lsr 后缀）
/// @param code 指令列表
/// @param headerLabel 循环头标签名
/// @return 唯一的派生标签名，无法生成时返回空串
std::string makeUniqueDerivedLabel(const InstList & code, const std::string & headerLabel)
{
	for (int suffix = 0; suffix < 1000; ++suffix) {
		std::string candidate = headerLabel + "_lsr" + std::to_string(suffix);
		bool exists = false;
		for (auto * inst : code) {
			if (isLiveInst(inst) && isLabel(inst) && inst->opcode == candidate) {
				exists = true;
				break;
			}
		}
		if (!exists) {
			return candidate;
		}
	}
	return "";
}

/// @brief 为各仿射地址链分配互不冲突的指针步进寄存器
///
/// 从候选临时寄存器集合 {t4, t5, t6} 中选取在当前函数中未使用的寄存器，
/// 分配给各仿射地址链作为指针步进寄存器。
/// @param code 指令列表
/// @param chains 仿射地址链列表
/// @return true 表示所有链都成功分配了寄存器
bool assignPointerRegisters(InstList & code, InstIt firstLiveRangeIt, std::vector<AffineAddressChain> & chains)
{
	static const std::vector<std::string> kCandidates = {"t4", "t5", "t6"};
	std::unordered_set<std::string> reserved;
	for (auto & chain : chains) {
		bool assigned = false;
		for (const auto & reg : kCandidates) {
			if (reserved.find(reg) != reserved.end() || registerMentionedInRange(firstLiveRangeIt, code.end(), reg)) {
				continue;
			}
			chain.pointerReg = reg;
			reserved.insert(reg);
			assigned = true;
			break;
		}
		if (!assigned) {
			return false;
		}
	}
	return true;
}

/// @brief 统计整个函数中以分支或跳转方式引用某标签的次数
int countLabelBranchReferences(const InstList & code, const std::string & label)
{
	int count = 0;
	for (auto * inst : code) {
		if (!isLiveInst(inst)) {
			continue;
		}
		if ((isBranchOpcode(inst->opcode) || inst->opcode == "j" || inst->opcode == "jal") &&
		    referencedLabel(inst) == label) {
			++count;
		}
	}
	return count;
}

/// @brief 将循环体内重复物化的 float 常量提升到循环外
///
/// 后端对 ConstFloat 不分配寄存器，每次引用都会重新生成
///   lui R, hi; addiw R, R, lo; fmv.w.x F, R
/// 序列。当该序列位于循环体内且常量在循环内不变时，可将其整体提升到循环
/// 前导块（preheader），使常量在整个循环执行期间只物化一次
///
/// 安全条件：
/// - 循环结构为已识别的 header/body/latch 单回边循环
/// - 目标 FPR F 在循环体内仅由该 fmv.w.x 定义一次
/// - 中间 GPR R 在循环体内只在该三指令序列中出现
/// - 循环头标签在全函数中仅被 latch 回边引用一次，且头部由前导块顺序进入，
///   以保证提升点支配整个循环
bool hoistLoopInvariantFloatConstants(InstList & code)
{
	for (auto headerIt = code.begin(); headerIt != code.end(); ++headerIt) {
		auto * header = *headerIt;
		if (!isLabel(header)) {
			continue;
		}

		auto branchIt = nextLive(code, headerIt);
		auto exitJumpIt = nextLive(code, branchIt);
		if (branchIt == code.end() || exitJumpIt == code.end()) {
			continue;
		}

		auto * branch = *branchIt;
		if (!isLiveInst(branch) || branch->opcode != "blt" || branch->arg2.empty() || !isInst(*exitJumpIt, "j")) {
			continue;
		}

		const std::string headerLabel = header->opcode;
		const std::string bodyLabel = branch->arg2;
		InstIt bodyLabelIt = code.end();
		for (auto it = exitJumpIt; it != code.end(); ++it) {
			if (isLabel(*it) && (*it)->opcode == bodyLabel) {
				bodyLabelIt = it;
				break;
			}
		}
		if (bodyLabelIt == code.end()) {
			continue;
		}

		auto bodyBegin = nextLive(code, bodyLabelIt);
		InstIt latchIt = code.end();
		for (auto it = bodyBegin; it != code.end(); it = nextLive(code, it)) {
			if (it != bodyBegin && isLabel(*it)) {
				break;
			}
			if (isInst(*it, "j", headerLabel)) {
				latchIt = it;
				break;
			}
		}
		if (latchIt == code.end() || loopBodyHasUnsafeControlOrCall(code, bodyBegin, latchIt)) {
			continue;
		}

		// 仅当循环头只被 latch 回边引用一次时，提升到头标签前才支配整个循环
		if (countLabelBranchReferences(code, headerLabel) != 1) {
			continue;
		}
		// 提升点前驱必须顺序落入循环头：前一条真实指令不能是无条件跳转或返回
		InstIt prevIt = code.end();
		for (auto scan = headerIt; scan != code.begin();) {
			--scan;
			if (isLiveInst(*scan) && !isCommentInst(*scan)) {
				prevIt = scan;
				break;
			}
		}
		if (prevIt == code.end()) {
			continue;
		}
		auto * prev = *prevIt;
		if (isLabel(prev) || prev->opcode == "j" || prev->opcode == "ret") {
			continue;
		}

		// 在循环体内查找 float 常量物化序列 lui R,hi; addiw R,R,lo; fmv.w.x F,R
		for (auto luiIt = bodyBegin; luiIt != latchIt && luiIt != code.end(); luiIt = nextMachineInst(code, luiIt)) {
			auto * lui = *luiIt;
			if (!isInst(lui, "lui") || lui->result.empty()) {
				continue;
			}
			auto addiIt = nextMachineInst(code, luiIt);
			auto fmvIt = nextMachineInst(code, addiIt);
			if (addiIt == code.end() || fmvIt == code.end()) {
				continue;
			}
			auto * addi = *addiIt;
			auto * fmv = *fmvIt;
			const std::string tmpReg = lui->result;
			if (!isInst(addi, "addiw", tmpReg, tmpReg) || !isInst(fmv, "fmv.w.x") || fmv->arg1 != tmpReg ||
			    fmv->result.empty()) {
				continue;
			}
			const std::string fpReg = fmv->result;

			// fpReg 在循环体内只能由该 fmv 定义一次；tmpReg 只能出现在该三指令序列中
			bool blocked = false;
			for (auto scan = bodyBegin; scan != latchIt && scan != code.end(); scan = nextMachineInst(code, scan)) {
				if (scan == luiIt || scan == addiIt || scan == fmvIt) {
					continue;
				}
				auto * inst = *scan;
				if ((definesResultOperand(inst) && inst->result == fpReg) ||
				    instructionMentionsRegister(inst, tmpReg)) {
					blocked = true;
					break;
				}
			}
			if (blocked) {
				continue;
			}

			// 将三指令序列复制到循环头标签前的前导块，并删除循环体内的原序列
			code.insert(headerIt, new RiscV64Inst(lui->opcode, lui->result, lui->arg1, lui->arg2));
			code.insert(headerIt, new RiscV64Inst(addi->opcode, addi->result, addi->arg1, addi->arg2));
			code.insert(headerIt, new RiscV64Inst(fmv->opcode, fmv->result, fmv->arg1, fmv->arg2));
			lui->setDead();
			addi->setDead();
			fmv->setDead();
			return true;
		}
	}

	return false;
}

/// @brief 返回条件分支操作码的逻辑取反（操作数顺序不变）；无已知取反则返回空串。
std::string invertedBranchOpcode(const std::string & op)
{
	if (op == "beq") return "bne";
	if (op == "bne") return "beq";
	if (op == "blt") return "bge";
	if (op == "bge") return "blt";
	if (op == "bltu") return "bgeu";
	if (op == "bgeu") return "bltu";
	if (op == "bgt") return "ble";
	if (op == "ble") return "bgt";
	if (op == "bgtu") return "bleu";
	if (op == "bleu") return "bgtu";
	return "";
}

/// @brief 反转“条件分支 + 跨越式无条件跳转”以省去 j 指令。
///
/// 形如：
///   bCC rs1, rs2, L1     # 条件成立跳到 L1（L1 紧跟在 j 之后）
///   j   L2               # 否则跳到 L2
///   L1:                  # 直落目标
/// 等价改写为：
///   b!CC rs1, rs2, L2    # 条件不成立才跳 L2，否则直落进 L1
///   L1:
/// 删除一条无条件跳转、消除一个多余基本块边，且语义完全等价。
/// @brief 将“j X；X 块仅含一条条件分支”的跳转穿透为条件分支本身
///
/// while 形循环的 latch 以 `j header` 回跳、header 仅含一条条件分支时，
/// 每轮迭代需要两次控制转移（j + 条件分支）。把目标块的条件分支复制到
/// 跳转点（j X → bcc a,b,T; j F，其中 F 为 X 的显式或直落后继），迭代
/// 路径缩短为一次控制转移，效果等价于循环旋转后的分支形态。
/// 复制的分支只读寄存器、目标块不含其它指令，寄存器状态经 j 原样到达，
/// 改写恒安全；F==X 的自环无法推进，跳过。
bool threadJumpToConditionBlock(InstList & code)
{
	bool changed = false;
	std::unordered_map<std::string, InstIt> labelPos;
	for (auto it = code.begin(); it != code.end(); ++it) {
		if (isLabel(*it)) {
			labelPos[(*it)->opcode] = it;
		}
	}

	for (auto it = code.begin(); it != code.end(); ++it) {
		auto * jump = *it;
		if (!isLiveInst(jump) || jump->opcode != "j" || jump->result.empty()) {
			continue;
		}
		auto found = labelPos.find(jump->result);
		if (found == labelPos.end()) {
			continue;
		}
		auto branchIt = nextMachineInst(code, found->second);
		if (branchIt == code.end() || !isLiveInst(*branchIt) || !isBranchOpcode((*branchIt)->opcode) ||
		    (*branchIt)->arg2.empty()) {
			continue;
		}
		auto afterIt = nextMachineInst(code, branchIt);
		if (afterIt == code.end()) {
			continue;
		}
		std::string falseLabel;
		if ((*afterIt)->opcode == "j" && !(*afterIt)->result.empty()) {
			falseLabel = (*afterIt)->result;
		} else if (isLabel(*afterIt)) {
			falseLabel = (*afterIt)->opcode;
		} else {
			continue;
		}
		if (falseLabel == jump->result) {
			continue;
		}

		auto * branch = *branchIt;
		jump->replace(branch->opcode, branch->result, branch->arg1, branch->arg2);
		code.insert(std::next(it), new RiscV64Inst("j", falseLabel));
		changed = true;
	}
	return changed;
}

bool invertBranchOverJump(InstList & code)
{
	bool changed = false;
	for (auto it = code.begin(); it != code.end(); ++it) {
		auto * branch = *it;
		if (!isLiveInst(branch) || !isBranchOpcode(branch->opcode) || branch->arg2.empty()) {
			continue;
		}
		const std::string inv = invertedBranchOpcode(branch->opcode);
		if (inv.empty()) {
			continue;
		}
		auto jumpIt = nextLive(code, it);
		if (jumpIt == code.end() || !isLiveInst(*jumpIt) || (*jumpIt)->opcode != "j" ||
		    (*jumpIt)->result.empty()) {
			continue;
		}
		auto labelIt = nextLive(code, jumpIt);
		if (labelIt == code.end() || !isLabel(*labelIt) || (*labelIt)->opcode != branch->arg2) {
			continue;
		}
		branch->replace(inv, branch->result, branch->arg1, (*jumpIt)->result);
		(*jumpIt)->setDead();
		changed = true;
	}
	return changed;
}

/// @brief 判断 reg 在 start 之后是否“先被重定义、且重定义前未被使用”（即此前的值已死）。
///
/// 仅当确实先遇到重定义时返回 true；遇到使用或控制边界(此时 reg 可能 live-out)都保守返回 false。
/// 与 registerUsedAfterBeforeRedefOrBoundary 不同：后者把“到达边界”也并入 false，无法区分
/// “已死”与“可能 live-out”，故这里单独实现严格版本，用于安全删除一条产生 reg 的指令。
bool registerDeadByRedefBeforeUse(InstList & code, InstIt start, const std::string & reg)
{
	for (auto it = nextLive(code, start); it != code.end(); it = nextLive(code, it)) {
		auto * inst = *it;
		if (usesRegister(inst, reg) || instructionImplicitlyUsesRegister(inst, reg)) {
			return false;
		}
		if (isControlBoundary(inst)) {
			return false;
		}
		if (definesResultOperand(inst) && inst->result == reg) {
			return true;
		}
	}
	return false;
}

/// @brief 浮点常量材料化的局部去重。
///
/// 指令选择会在“每一次使用浮点常量”时各自材料化一遍，例如：
///   lui     t3, IMM
///   fmv.w.x fd, t3        （或 fmv.w.x fd, zero 表示 0.0）
/// 在同一直线区间(中间无标签/分支/跳转/调用)内，若某个 FPR 已持有同一常量且未被覆写，
/// 则后续把同一常量再次材料化到“同一寄存器”属于纯冗余，可直接删除（并连带删除紧邻、
/// 且其结果随后即死的 lui/li 馈给指令）。该变换不改变任何数值，只消除重复指令，
/// 尤其能消除循环展开后反复重载的同一常量（如牛顿迭代里每步都重载的 0.5）。
bool dedupRedundantFloatConstMaterialize(InstList & code)
{
	bool changed = false;
	std::unordered_map<std::string, std::string> fprHolds;   // FPR -> 当前持有的常量键
	std::unordered_map<std::string, std::string> gprConst;   // GPR -> 由 lui/li 装载的常量键
	std::unordered_map<std::string, InstIt> gprConstAt;      // GPR -> 对应 lui/li 指令位置

	for (auto it = code.begin(); it != code.end(); ++it) {
		auto * inst = *it;
		if (!isLiveInst(inst)) {
			continue;
		}
		if (isControlBoundary(inst)) {
			// 跨基本块/调用：所有缓存失效（调用会破坏 caller-saved 寄存器）
			fprHolds.clear();
			gprConst.clear();
			gprConstAt.clear();
			continue;
		}

		const std::string & op = inst->opcode;

		if ((op == "lui" || op == "li") && isPhysicalRegisterName(inst->result)) {
			gprConst[inst->result] = op + ":" + inst->arg1;
			gprConstAt[inst->result] = it;
			continue;
		}

		if (op == "fmv.w.x") {
			const std::string fd = inst->result;
			const std::string src = inst->arg1;
			std::string key;
			InstIt feederIt = code.end();
			if (src == "zero") {
				key = "#zero";
			} else {
				auto kIt = gprConst.find(src);
				if (kIt != gprConst.end()) {
					key = kIt->second;
					auto aIt = gprConstAt.find(src);
					if (aIt != gprConstAt.end()) {
						feederIt = aIt->second;
					}
				}
			}

			if (!key.empty()) {
				auto held = fprHolds.find(fd);
				if (held != fprHolds.end() && held->second == key) {
					// fd 已持有同一常量且未被覆写：本次材料化纯冗余，删除之
					// 注意：先判定馈给指令是否紧邻(用 nextLive)，再 setDead，
					// 否则 setDead 后 nextLive 会跳过本条已死指令导致邻接判定失败。
					const bool feederAdjacent =
						feederIt != code.end() && nextLive(code, feederIt) == it;
					inst->setDead();
					changed = true;
					// 紧邻且其结果寄存器随后即死的 lui/li 馈给指令也一并删除
					if (feederAdjacent && registerDeadByRedefBeforeUse(code, it, src)) {
						(*feederIt)->setDead();
					}
					continue;  // fprHolds[fd] 保持为 key 不变
				}
				fprHolds[fd] = key;
				continue;
			}
			// 未知来源（真正的整型→浮点位拷贝等）：fd 不再持有已知常量
			fprHolds.erase(fd);
			continue;
		}

		// 普通指令：若覆写了某寄存器，则其缓存失效
		if (definesResultOperand(inst)) {
			fprHolds.erase(inst->result);
			gprConst.erase(inst->result);
			gprConstAt.erase(inst->result);
		}
	}

	return changed;
}

/// @brief 缓存循环内对同一浮点地址的重复 load。
///
/// 匹配形如：循环前 fsw v,0(p)，循环体中反复 flw x,0(p)，且 p 在循环体内不被重定义、
/// 循环体不精确写同一地址。该优化只替换完全相同地址文本，不做激进别名推断。
bool cacheLoopInvariantFloatLoads(InstList & code)
{
	for (auto headerIt = code.begin(); headerIt != code.end(); ++headerIt) {
		auto * header = *headerIt;
		if (!isLabel(header)) {
			continue;
		}

		auto branchIt = nextLive(code, headerIt);
		auto exitJumpIt = nextLive(code, branchIt);
		if (branchIt == code.end() || exitJumpIt == code.end()) {
			continue;
		}

		auto * branch = *branchIt;
		if (!isLiveInst(branch) || branch->opcode != "blt" || branch->arg2.empty() || !isInst(*exitJumpIt, "j")) {
			continue;
		}

		const std::string headerLabel = header->opcode;
		const std::string bodyLabel = branch->arg2;
		InstIt bodyLabelIt = code.end();
		for (auto it = exitJumpIt; it != code.end(); ++it) {
			if (isLabel(*it) && (*it)->opcode == bodyLabel) {
				bodyLabelIt = it;
				break;
			}
		}
		if (bodyLabelIt == code.end()) {
			continue;
		}

		auto bodyBegin = nextLive(code, bodyLabelIt);
		InstIt latchIt = code.end();
		for (auto it = bodyBegin; it != code.end(); it = nextLive(code, it)) {
			if (it != bodyBegin && isLabel(*it)) {
				break;
			}
			if (isInst(*it, "j", headerLabel)) {
				latchIt = it;
				break;
			}
		}
		if (latchIt == code.end() || loopBodyHasUnsafeControlOrCall(code, bodyBegin, latchIt)) {
			continue;
		}

		for (auto loadIt = bodyBegin; loadIt != latchIt && loadIt != code.end(); loadIt = nextLive(code, loadIt)) {
			auto * load = *loadIt;
			if (!isLiveInst(load) || load->opcode != "flw" || load->result.empty() || load->arg1.empty()) {
				continue;
			}

			const std::string address = load->arg1;
			const std::string baseReg = addressBaseRegister(address);
			if (baseReg.empty() || registerDefinedInRange(bodyBegin, latchIt, baseReg)) {
				continue;
			}

			bool writesSameAddress = false;
			for (auto scan = bodyBegin; scan != latchIt && scan != code.end(); scan = nextLive(code, scan)) {
				auto * inst = *scan;
				if (isLiveInst(inst) && (inst->opcode == "fsw" || inst->opcode == "sw" || inst->opcode == "sd") &&
				    inst->arg1 == address) {
					writesSameAddress = true;
					break;
				}
			}
			if (writesSameAddress) {
				continue;
			}

			InstIt searchStart = headerIt;
			for (auto scan = headerIt; scan != code.begin();) {
				--scan;
				if (isInst(*scan, "j", headerLabel)) {
					searchStart = scan;
					break;
				}
			}

			InstIt storeIt = code.end();
			for (auto scan = searchStart; scan != code.begin();) {
				--scan;
				auto * inst = *scan;
				if (isLabel(inst)) {
					break;
				}
				if (!isLiveInst(inst)) {
					continue;
				}
				if (definesResultOperand(inst) && inst->result == baseReg) {
					break;
				}
				if (inst->opcode == "fsw" && inst->arg1 == address) {
					storeIt = scan;
					break;
				}
			}
			if (storeIt == code.end() || (*storeIt)->result.empty()) {
				continue;
			}

			const std::string cacheReg = chooseFreeFloatTemp(storeIt, code.end());
			if (cacheReg.empty()) {
				continue;
			}

			auto firstUseIt = std::next(loadIt);
			if (!replaceUsesBeforeRedef(firstUseIt, latchIt, load->result, cacheReg)) {
				continue;
			}

			code.insert(std::next(storeIt), new RiscV64Inst("fsgnj.s", cacheReg, (*storeIt)->result, (*storeIt)->result));
			load->setDead();
			return true;
		}
	}

	return false;
}

bool registerUpdatedInLoop(InstList & code, InstIt bodyBegin, InstIt latchIt, const std::string & reg)
{
	for (auto it = bodyBegin; it != latchIt && it != code.end(); it = nextLive(code, it)) {
		auto * inst = *it;
		if (!isLiveInst(inst)) {
			continue;
		}
		if ((inst->opcode == "add" || inst->opcode == "addi") && inst->result == reg && inst->arg1 == reg) {
			return true;
		}
	}
	return false;
}

bool replaceAddressBaseUntilRedef(InstList & code,
                                  InstIt begin,
                                  InstIt end,
                                  const std::string & oldReg,
                                  const std::string & newReg)
{
	bool changed = false;
	for (auto it = begin; it != end && it != code.end(); it = nextLive(code, it)) {
		auto * inst = *it;
		if (!isLiveInst(inst)) {
			continue;
		}
		if (definesResultOperand(inst) && inst->result == oldReg) {
			break;
		}
		if (isMemoryOpcode(inst->opcode)) {
			changed = replaceMemoryBase(inst, oldReg, newReg) || changed;
		}
	}
	return changed;
}

bool rewriteAddressUsesUntilRedef(InstList & code,
                                  InstIt begin,
                                  InstIt end,
                                  const std::string & oldReg,
                                  const std::string & newReg)
{
	bool changed = false;
	for (auto it = begin; it != end && it != code.end(); it = nextLive(code, it)) {
		auto * inst = *it;
		if (!isLiveInst(inst)) {
			continue;
		}
		if (definesResultOperand(inst) && inst->result == oldReg) {
			break;
		}
		if (isMemoryOpcode(inst->opcode)) {
			changed = replaceMemoryBase(inst, oldReg, newReg) || changed;
			continue;
		}
		if (isInst(inst, "mv") && inst->arg1 == oldReg) {
			changed = replaceRegisterUse(inst, oldReg, newReg) || changed;
		}
	}
	return changed;
}

/// @brief 将内层循环中不变的列偏移合入递推指针初值。
///
/// LSR 后常见形态为：
///   mv addr,rowPtr; mv tmp,col; slli tmp,tmp,k; add addr,addr,tmp; flw/fsw ...,0(addr)
/// 若 col 在循环内不变且 rowPtr 按 stride 递推，则把 col 偏移移到循环入口前，
/// 循环体内直接使用 rowPtr 作为内存基址。
bool foldInvariantAddressOffsetsIntoRecurrences(InstList & code)
{
	for (auto headerIt = code.begin(); headerIt != code.end(); ++headerIt) {
		auto * header = *headerIt;
		if (!isLabel(header)) {
			continue;
		}

		auto branchIt = nextLive(code, headerIt);
		auto exitJumpIt = nextLive(code, branchIt);
		if (branchIt == code.end() || exitJumpIt == code.end()) {
			continue;
		}
		auto * branch = *branchIt;
		if (!isLiveInst(branch) || branch->opcode != "blt" || branch->arg2.empty() || !isInst(*exitJumpIt, "j")) {
			continue;
		}

		const std::string headerLabel = header->opcode;
		const std::string bodyLabel = branch->arg2;
		InstIt bodyLabelIt = code.end();
		for (auto it = exitJumpIt; it != code.end(); ++it) {
			if (isLabel(*it) && (*it)->opcode == bodyLabel) {
				bodyLabelIt = it;
				break;
			}
		}
		if (bodyLabelIt == code.end()) {
			continue;
		}

		auto bodyBegin = nextLive(code, bodyLabelIt);
		InstIt latchIt = code.end();
		for (auto it = bodyBegin; it != code.end(); it = nextLive(code, it)) {
			if (it != bodyBegin && isLabel(*it)) {
				break;
			}
			if (isInst(*it, "j", headerLabel)) {
				latchIt = it;
				break;
			}
		}
		if (latchIt == code.end() || loopBodyHasUnsafeControlOrCall(code, bodyBegin, latchIt)) {
			continue;
		}

		for (auto it = bodyBegin; it != latchIt && it != code.end(); it = nextLive(code, it)) {
			auto * baseMove = *it;
			if (!isInst(baseMove, "mv") || baseMove->result.empty() || baseMove->arg1.empty()) {
				continue;
			}
			const std::string addrReg = baseMove->result;
			const std::string pointerReg = baseMove->arg1;
			if (registerDefinedInRange(bodyBegin, it, pointerReg) ||
			    !registerUpdatedInLoop(code, bodyBegin, latchIt, pointerReg)) {
				continue;
			}

			auto indexMoveIt = nextLive(code, it);
			auto shiftIt = nextLive(code, indexMoveIt);
			auto addIt = nextLive(code, shiftIt);
			if (indexMoveIt == code.end() || shiftIt == code.end() || addIt == code.end()) {
				continue;
			}
			auto * indexMove = *indexMoveIt;
			auto * shift = *shiftIt;
			auto * add = *addIt;
			if (!isInst(indexMove, "mv") || indexMove->result.empty() || indexMove->arg1.empty()) {
				continue;
			}
			const std::string tmpReg = indexMove->result;
			const std::string offsetReg = indexMove->arg1;
			if (registerDefinedInRange(bodyBegin, latchIt, offsetReg)) {
				continue;
			}

			int scale = 0;
			if (!isInst(shift, "slli", tmpReg, tmpReg) || !parseSmallNonNegativeInteger(shift->arg2, scale)) {
				continue;
			}
			if (!isInst(add, "add", addrReg)) {
				continue;
			}
			const bool addAddrTmp = add->arg1 == addrReg && add->arg2 == tmpReg;
			const bool addTmpAddr = add->arg1 == tmpReg && add->arg2 == addrReg;
			if (!addAddrTmp && !addTmpAddr) {
				continue;
			}

			if (!replaceAddressBaseUntilRedef(code, nextLive(code, addIt), latchIt, addrReg, pointerReg)) {
				continue;
			}

			code.insert(headerIt, new RiscV64Inst("mv", tmpReg, offsetReg));
			code.insert(headerIt, new RiscV64Inst("slli", tmpReg, tmpReg, std::to_string(scale)));
			code.insert(headerIt, new RiscV64Inst("add", pointerReg, pointerReg, tmpReg));
			baseMove->setDead();
			indexMove->setDead();
			shift->setDead();
			add->setDead();
			return true;
		}
	}

	return false;
}

/// @brief 通用仿射地址递推优化：把循环内 base + i * stride 地址计算改为指针步进。
///
/// 只匹配简单 innermost 计数循环，不读取函数名/标签名语义。触发条件包括：
///   - 循环头为 blt i,bound,body + j exit，循环体末尾唯一跳回头部；
///   - i 在循环体中按 +1 更新；
///   - 地址计算形如 mv addr,base; mv tmp,i; slli tmp,tmp,k; add addr,addr,tmp；
///   - 临时指针寄存器在当前函数中完全未使用，且循环体内没有 call/额外分支。
bool reduceAffineAddressRecurrences(InstList & code)
{
	bool changed = false;
	for (auto headerIt = code.begin(); headerIt != code.end(); ++headerIt) {
		auto * header = *headerIt;
		if (!isLabel(header)) {
			continue;
		}

		auto branchIt = nextLive(code, headerIt);
		auto exitJumpIt = nextLive(code, branchIt);
		if (branchIt == code.end() || exitJumpIt == code.end()) {
			continue;
		}

		auto * branch = *branchIt;
		if (!isLiveInst(branch) || branch->opcode != "blt" || branch->result.empty() || branch->arg2.empty() ||
		    !isInst(*exitJumpIt, "j")) {
			continue;
		}

		const std::string headerLabel = header->opcode;
		const std::string bodyLabel = branch->arg2;
		const std::string indexReg = branch->result;

		InstIt bodyLabelIt = code.end();
		for (auto it = exitJumpIt; it != code.end(); ++it) {
			if (isLabel(*it) && (*it)->opcode == bodyLabel) {
				bodyLabelIt = it;
				break;
			}
		}
		if (bodyLabelIt == code.end()) {
			continue;
		}

		auto bodyBegin = nextLive(code, bodyLabelIt);
		InstIt latchIt = code.end();
		for (auto it = bodyBegin; it != code.end(); it = nextLive(code, it)) {
			if (it != bodyBegin && isLabel(*it)) {
				break;
			}
			if (isInst(*it, "j", headerLabel)) {
				latchIt = it;
				break;
			}
		}
		if (latchIt == code.end() || loopBodyHasUnsafeControlOrCall(code, bodyBegin, latchIt)) {
			continue;
		}

		InstIt updateInsertIt = code.end();
		if (!findUnitStepUpdate(code, bodyBegin, latchIt, indexReg, updateInsertIt)) {
			continue;
		}

		std::vector<AffineAddressChain> chains;
		for (auto it = bodyBegin; it != latchIt && it != code.end(); it = nextLive(code, it)) {
			AffineAddressChain chain;
			if (matchAffineAddressChain(code, it, latchIt, indexReg, chain)) {
				chains.push_back(std::move(chain));
			}
		}
		if (chains.empty() || !assignPointerRegisters(code, headerIt, chains)) {
			continue;
		}

		const std::string loopEntryLabel = makeUniqueDerivedLabel(code, headerLabel);
		if (loopEntryLabel.empty()) {
			continue;
		}

		auto headerInsertPos = headerIt;
		++headerInsertPos;
		for (const auto & chain : chains) {
			code.insert(headerInsertPos, new RiscV64Inst("slli", chain.pointerReg, indexReg, std::to_string(chain.scale)));
			code.insert(headerInsertPos, new RiscV64Inst("add", chain.pointerReg, chain.baseReg, chain.pointerReg));
		}
		int sharedLargeStride = 0;
		bool canShareLargeStride = true;
		for (const auto & chain : chains) {
			if (chain.stride <= 2047) {
				continue;
			}
			if (sharedLargeStride == 0) {
				sharedLargeStride = chain.stride;
			} else if (sharedLargeStride != chain.stride) {
				canShareLargeStride = false;
			}
		}

		std::string sharedStrideReg;
		if (sharedLargeStride > 0 && canShareLargeStride) {
			static const std::vector<std::string> kCandidates = {"t4", "t5", "t6"};
			std::unordered_set<std::string> pointerRegs;
			for (const auto & chain : chains) {
				pointerRegs.insert(chain.pointerReg);
			}
			for (const auto & reg : kCandidates) {
				if (pointerRegs.find(reg) == pointerRegs.end() &&
				    !registerMentionedInRange(headerIt, code.end(), reg)) {
					sharedStrideReg = reg;
					break;
				}
			}
			if (!sharedStrideReg.empty()) {
				code.insert(headerInsertPos, new RiscV64Inst("li", sharedStrideReg, std::to_string(sharedLargeStride)));
			}
		}
		code.insert(headerInsertPos, new RiscV64Inst(loopEntryLabel, ":"));
		(*latchIt)->result = loopEntryLabel;

		for (const auto & chain : chains) {
			(*chain.baseMoveIt)->setDead();
			(*chain.indexMoveIt)->setDead();
			(*chain.shiftIt)->setDead();
			(*chain.addIt)->setDead();
			for (auto useIt : chain.rewriteUses) {
				if (isMemoryOpcode((*useIt)->opcode)) {
					replaceMemoryBase(*useIt, chain.addrReg, chain.pointerReg);
				} else {
					replaceRegisterUse(*useIt, chain.addrReg, chain.pointerReg);
				}
			}
			rewriteAddressUsesUntilRedef(code, nextLive(code, chain.addIt), latchIt, chain.addrReg, chain.pointerReg);
			if (chain.stride <= 2047) {
				code.insert(updateInsertIt,
				            new RiscV64Inst("addi", chain.pointerReg, chain.pointerReg, std::to_string(chain.stride)));
			} else if (!sharedStrideReg.empty() && chain.stride == sharedLargeStride) {
				code.insert(updateInsertIt, new RiscV64Inst("add", chain.pointerReg, chain.pointerReg, sharedStrideReg));
			} else {
				code.insert(updateInsertIt, new RiscV64Inst("li", chain.tmpReg, std::to_string(chain.stride)));
				code.insert(updateInsertIt, new RiscV64Inst("add", chain.pointerReg, chain.pointerReg, chain.tmpReg));
			}
		}

		return true;
	}
	return changed;
}

/// @brief 标准化浮点符号注入指令为更清晰的伪指令形式
///
/// RISC-V使用fsgnj系列指令实现浮点copy/negate/abs：
///   - fsgnj.s  rd,rs,rs 等价于 fmv.s  rd,rs（符号拷贝 = 整体拷贝）
///   - fsgnjn.s rd,rs,rs 等价于 fneg.s rd,rs（符号取反）
///   - fsgnjx.s rd,rs,rs 等价于 fabs.s rd,rs（符号异或自己 = 清除符号位）
///
/// 标准化为伪指令形式可提升可读性，且为后续优化提供统一模式。
bool normalizeFsgnjInstructions(InstList & code)
{
	bool changed = false;
	for (auto * inst : code) {
		if (!isLiveInst(inst)) {
			continue;
		}

		// fsgnj.s rd,rs,rs → fmv.s rd,rs
		if (inst->opcode == "fsgnj.s" && !inst->arg1.empty() && inst->arg1 == inst->arg2) {
			inst->opcode = "fmv.s";
			inst->arg2 = "";
			changed = true;
		}
		// fsgnjn.s rd,rs,rs → fneg.s rd,rs
		else if (inst->opcode == "fsgnjn.s" && !inst->arg1.empty() && inst->arg1 == inst->arg2) {
			inst->opcode = "fneg.s";
			inst->arg2 = "";
			changed = true;
		}
		// fsgnjx.s rd,rs,rs → fabs.s rd,rs
		else if (inst->opcode == "fsgnjx.s" && !inst->arg1.empty() && inst->arg1 == inst->arg2) {
			inst->opcode = "fabs.s";
			inst->arg2 = "";
			changed = true;
		}
	}
	return changed;
}

/// @brief 消除 andi x,y,1 后的冗余 snez 指令
///
/// 模式：
///   andi dst, src, 1
///   snez tmp, dst
///   beq/bne tmp, zero, label
///
/// 优化为：
///   andi dst, src, 1
///   beq/bne dst, zero, label
///
/// 原因：andi x,y,1 的结果已经是 0 或 1，snez 是冗余的。
bool foldRedundantSnezAfterAndi(InstList & code)
{
	bool changed = false;
	for (auto it = code.begin(); it != code.end(); ++it) {
		auto * andi = *it;
		if (!isLiveInst(andi) || andi->opcode != "andi") {
			continue;
		}
		// 检查是否为 andi x,y,1
		if (andi->addition != "1") {
			continue;
		}

		// 查找下一条指令
		auto nextIt = nextMachineInst(code, it);
		if (nextIt == code.end()) {
			continue;
		}
		auto * snez = *nextIt;
		if (!isLiveInst(snez) || snez->opcode != "snez") {
			continue;
		}
		// 检查 snez 的源是否为 andi 的目标
		if (snez->arg1 != andi->result) {
			continue;
		}

		// 查找使用 snez 结果的指令
		std::string snezDst = snez->result;
		std::string andiDst = andi->result;

		// 检查 andi 的结果在 snez 后是否还被使用
		bool andiUsedAfterSnez = false;
		for (auto checkIt = std::next(nextIt); checkIt != code.end(); ++checkIt) {
			auto * inst = *checkIt;
			if (!isLiveInst(inst)) {
				continue;
			}
			// 如果遇到标签，停止检查（可能跨基本块）
			if (isLabel(inst)) {
				break;
			}
			// 如果 andi 结果被重定义，停止
			if (inst->result == andiDst) {
				break;
			}
			// 检查是否使用了 andi 的结果
			if (inst->arg1 == andiDst || inst->arg2 == andiDst) {
				andiUsedAfterSnez = true;
				break;
			}
		}

		// 如果 andi 结果还被使用，不能优化（需要保留两个值）
		if (andiUsedAfterSnez) {
			continue;
		}

		// 替换所有使用 snez 结果的地方为 andi 结果
		bool replacedAny = false;
		for (auto replIt = std::next(nextIt); replIt != code.end(); ++replIt) {
			auto * inst = *replIt;
			if (!isLiveInst(inst)) {
				continue;
			}
			// 遇到标签停止（基本块边界）
			if (isLabel(inst)) {
				break;
			}
			// 如果 snez 目标被重定义，停止
			if (inst->result == snezDst) {
				break;
			}
			// 替换使用
			if (inst->arg1 == snezDst) {
				inst->arg1 = andiDst;
				replacedAny = true;
			}
			if (inst->arg2 == snezDst) {
				inst->arg2 = andiDst;
				replacedAny = true;
			}
		}

		if (replacedAny) {
			snez->setDead();
			changed = true;
		}
	}
	return changed;
}

/// @brief 合并函数尾部的多个 ret 指令，减少代码大小
///
/// 模式：
///   label1:
///     ret
///   label2:
///     ret
///
/// 优化为：
///   label1:
///   label2:
///     ret
///
/// 将跳转到第一个 ret 的分支重定向到最后一个 ret，然后删除冗余的 ret。
bool mergeDuplicateReturns(InstList & code)
{
	bool changed = false;

	// 查找所有 ret 指令
	std::vector<InstIt> retInstructions;
	for (auto it = code.begin(); it != code.end(); ++it) {
		if (isLiveInst(*it) && (*it)->opcode == "ret") {
			retInstructions.push_back(it);
		}
	}

	// 如果只有一个 ret，无需优化
	if (retInstructions.size() <= 1) {
		return false;
	}

	// 选择最后一个 ret 作为统一的返回点
	InstIt canonicalRetIt = retInstructions.back();

	// 查找紧邻每个 ret 之前的标签
	std::unordered_map<std::string, InstIt> labelToRet;
	for (auto retIt : retInstructions) {
		// 向前查找标签
		auto labelIt = retIt;
		while (labelIt != code.begin()) {
			--labelIt;
			if (isLiveInst(*labelIt)) {
				if (isLabel(*labelIt)) {
					labelToRet[(*labelIt)->opcode] = retIt;
				}
				break; // 遇到第一个活跃指令就停止
			}
		}
	}

	// 找到跳转到各个 ret 的分支指令，重定向到统一的返回点
	std::string canonicalRetLabel;
	// 为统一返回点创建或找到标签
	auto labelIt = canonicalRetIt;
	while (labelIt != code.begin()) {
		--labelIt;
		if (isLiveInst(*labelIt)) {
			if (isLabel(*labelIt)) {
				canonicalRetLabel = (*labelIt)->opcode;
			}
			break;
		}
	}

	// 如果没有找到标签，创建一个
	if (canonicalRetLabel.empty()) {
		canonicalRetLabel = ".L_unified_return";
		code.insert(canonicalRetIt, new RiscV64Inst(canonicalRetLabel, ":"));
	}

	// 重定向所有跳转到其他 ret 的分支
	for (const auto & entry : labelToRet) {
		const std::string & label = entry.first;
		InstIt retIt = entry.second;

		// 跳过统一返回点自己
		if (retIt == canonicalRetIt) {
			continue;
		}

		// 检查：如果删除这个 ret，会不会导致 fall-through 到下一条指令？
		// 需要跳过所有紧邻的标签，找到下一条实际执行的指令
		auto nextIt = retIt;
		++nextIt;
		bool canDelete = true;

		// 跳过紧邻的标签
		while (nextIt != code.end() && isLiveInst(*nextIt) && isLabel(*nextIt)) {
			++nextIt;
		}

		// 如果找到了非标签的指令，检查是否是另一个 ret
		if (nextIt != code.end() && isLiveInst(*nextIt)) {
			// 如果下一条指令不是 ret，说明会 fall-through 到其他代码，不能删除
			if ((*nextIt)->opcode != "ret") {
				canDelete = false;
			}
		}

		if (!canDelete) {
			// 不能删除这个 ret，跳过
			continue;
		}

		// 查找所有跳转到这个标签的指令
		for (auto it = code.begin(); it != code.end(); ++it) {
			auto * inst = *it;
			if (!isLiveInst(inst)) {
				continue;
			}

			// 处理无条件跳转
			if (inst->opcode == "j" && inst->result == label) {
				inst->result = canonicalRetLabel;
				changed = true;
			}
			// 处理条件分支
			else if (isBranchOpcode(inst->opcode) && !inst->arg2.empty() && inst->arg2 == label) {
				inst->arg2 = canonicalRetLabel;
				changed = true;
			}
		}

		// 删除这个冗余的 ret
		(*retIt)->setDead();
		changed = true;
	}

	return changed;
}

} // namespace

bool RiscV64Peephole::run(ILocRiscV64 & iloc, int optLevel, bool enableCoalesceRetargeting)
{
	bool changed = false;
	bool localChanged = false;
	auto & code = iloc.getCode();
	do {
		localChanged = false;
		// FMA融合优化：将 fmul.s + fadd.s/fsub.s 融合为 fmadd.s/fmsub.s
		// 仅在O2及以上启用，因为FMA比分开fmul+fadd精度更高，会导致浮点结果差异
		if (optLevel > 1) {
			localChanged = fuseFMA(code) || localChanged;
		}
		localChanged = cacheLoopInvariantFloatLoads(code) || localChanged;
		// 将循环体内重复物化的 float 常量提升到循环前导块，只物化一次
		localChanged = hoistLoopInvariantFloatConstants(code) || localChanged;
		// 浮点常量材料化去重：删除直线区间内对同一寄存器重复材料化的同一常量
		localChanged = dedupRedundantFloatConstMaterialize(code) || localChanged;
		// 标准化浮点符号注入指令：fsgnj.s rd,rs,rs → fmv.s; fsgnjn.s rd,rs,rs → fneg.s
		localChanged = normalizeFsgnjInstructions(code) || localChanged;		// 仿射地址递推优化：将循环内 base+i*stride 地址计算改为指针步进
		localChanged = reduceAffineAddressRecurrences(code) || localChanged;
		localChanged = foldInvariantAddressOffsetsIntoRecurrences(code) || localChanged;
		localChanged = reduceMulByConst(code) || localChanged;
		localChanged = foldUnitStepIncrements(code) || localChanged;
		localChanged = foldAddiAddressIntoMemoryOffset(code) || localChanged;
		// 2的幂取模零值分支优化：将 x % ±2^k ==/!= 0 的余数计算链替换为位掩码测试
		localChanged = foldPowerOfTwoRemainderZeroBranch(code) || localChanged;
		localChanged = foldZeroSubCompare(code) || localChanged;
		// 消除 andi x,y,1 后的冗余 snez 指令
		localChanged = foldRedundantSnezAfterAndi(code) || localChanged;
		// 折叠立即数/地址材料化后的冗余move：li x,imm; mv y,x -> li y,imm
		localChanged = foldMaterializationMoves(code) || localChanged;
		// 折叠零值 store，减少局部数组清零中的 li 0 序列
		localChanged = foldZeroStores(code) || localChanged;
		// 转发同块内 32/64 位GPR栈槽 store-load，消除地址临时值绕栈往返
		localChanged = forwardStackStoreLoads(code) || localChanged;
		// 消除同一直线区间内对同一栈槽的重复 GPR reload
		localChanged = eliminateRedundantStackReloads(code) || localChanged;
		// coalesce专属优化：将局部唯一消费的producer直接改写到copy目标寄存器
		if (enableCoalesceRetargeting) {
			localChanged = retargetSingleUseDefinitions(code) || localChanged;
		}
		// 在单基本块内传播mv的目标寄存器使用，消除冗余move链
		localChanged = propagateMoveUses(code) || localChanged;
		// 沿 CFG 传播可用 copy，覆盖入口 copy 穿过条件分支后被使用的场景
		localChanged = propagateMoveUsesAcrossBlocks(code) || localChanged;
		// 删除只在本基本块内被后续重定义覆盖的死move
		localChanged = removeDeadMoves(code) || localChanged;
		// 借助机器级活跃性删除全函数范围内已死的纯定义（含特化后遗留的形参拷贝、去重后遗留的 lui/li）
		localChanged = removeDeadPureDefs(code) || localChanged;
		localChanged = removeSelfMoves(code) || localChanged;
		localChanged = removeConsecutiveDuplicates(code) || localChanged;
		localChanged = removeJumpToNextLabel(code) || localChanged;
		// 基于活跃性分析的通用死定义清扫，消除跨控制流边界仍可证明无用的定义
		localChanged = eliminateDeadDefinitions(code) || localChanged;
		// 将 j 到“仅含一条条件分支”块的跳转穿透为条件分支，缩短循环回边路径
		localChanged = threadJumpToConditionBlock(code) || localChanged;
		// 反转”条件分支 + 跨越式 j”以删除多余无条件跳转
		localChanged = invertBranchOverJump(code) || localChanged;
		// 合并多个 ret 指令到统一返回点
		localChanged = mergeDuplicateReturns(code) || localChanged;		changed = changed || localChanged;
	} while (localChanged);
	return changed;
}
