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
		if (isLiveInst(inst) && inst->opcode == "mv" && inst->result == inst->arg1) {
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

bool isMemoryOpcode(const std::string & opcode)
{
	return opcode == "lb" || opcode == "lbu" || opcode == "lh" || opcode == "lhu" || opcode == "lw" ||
	       opcode == "lwu" || opcode == "ld" || opcode == "flw" || opcode == "fld" || isStoreOpcode(opcode);
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
		if (imm < 2 || imm > 15) {
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

		int shift = 0;
		std::string followOp;
		if (isPowerOfTwo(imm)) {
			shift = log2PowerOfTwo(imm);
		} else if (imm == 3 || imm == 5 || imm == 9) {
			shift = log2PowerOfTwo(imm - 1);
			followOp = "addw";
		} else if (imm == 7 || imm == 15) {
			shift = log2PowerOfTwo(imm + 1);
			followOp = "subw";
		} else {
			continue;
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

/// @brief 判断指令是否为从寄存器到寄存器的move（mv或fsgnj.s同寄存器形式）
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

/// @brief 在单个基本块内传播 mv 的目标寄存器使用。
///
/// `mv dst, src` 之后，直到 dst/src 被重定义或遇到控制流边界前，
/// 后续读取 dst 等价于读取 src。这里会同时改写普通 operand 与内存地址基寄存器。
bool propagateMoveUses(InstList & code)
{
	bool changed = false;
	for (auto it = code.begin(); it != code.end(); ++it) {
		auto * move = *it;
		if (!isLiveInst(move) || move->opcode != "mv" || move->result.empty() || move->arg1.empty() ||
		    move->result == move->arg1) {
			continue;
		}

		const std::string dst = move->result;
		const std::string src = move->arg1;
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

/// @brief 删除只在本基本块内被后续重定义覆盖的死 move。
bool removeDeadMoves(InstList & code)
{
	bool changed = false;
	for (auto it = code.begin(); it != code.end(); ++it) {
		auto * move = *it;
		if (!isLiveInst(move) || move->opcode != "mv" || move->result.empty() || move->arg1.empty() ||
		    move->result == move->arg1) {
			continue;
		}
		if (registerRedefinedBeforeUseOrBoundary(code, it, move->result)) {
			move->setDead();
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
		// 仿射地址递推优化：将循环内 base+i*stride 地址计算改为指针步进
		localChanged = reduceAffineAddressRecurrences(code) || localChanged;
		localChanged = foldInvariantAddressOffsetsIntoRecurrences(code) || localChanged;
		localChanged = reduceMulByConst(code) || localChanged;
		localChanged = foldUnitStepIncrements(code) || localChanged;
		localChanged = foldZeroSubCompare(code) || localChanged;
		// 折叠立即数/地址材料化后的冗余move：li x,imm; mv y,x -> li y,imm
		localChanged = foldMaterializationMoves(code) || localChanged;
		// coalesce专属优化：将局部唯一消费的producer直接改写到copy目标寄存器
		if (enableCoalesceRetargeting) {
			localChanged = retargetSingleUseDefinitions(code) || localChanged;
		}
		// 在单基本块内传播mv的目标寄存器使用，消除冗余move链
		localChanged = propagateMoveUses(code) || localChanged;
		// 删除只在本基本块内被后续重定义覆盖的死move
		localChanged = removeDeadMoves(code) || localChanged;
		localChanged = removeSelfMoves(code) || localChanged;
		localChanged = removeConsecutiveDuplicates(code) || localChanged;
		localChanged = removeJumpToNextLabel(code) || localChanged;
		changed = changed || localChanged;
	} while (localChanged);
	return changed;
}
