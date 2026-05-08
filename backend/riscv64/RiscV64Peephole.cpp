#include "RiscV64Peephole.h"

#include <list>
#include <string>
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

bool registerUsedBeforeRedef(InstList & code, InstIt start, const std::string & reg)
{
	for (auto it = nextLive(code, start); it != code.end(); it = nextLive(code, it)) {
		auto * inst = *it;
		if (usesRegister(inst, reg) || (inst->opcode == "call" && isArgumentRegister(reg))) {
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

bool instructionMentionsRegister(RiscV64Inst * inst, const std::string & reg)
{
	return isLiveInst(inst) &&
	       (operandMentionsRegister(inst->result, reg) || operandMentionsRegister(inst->arg1, reg) ||
	        operandMentionsRegister(inst->arg2, reg) || operandMentionsRegister(inst->addition, reg));
}

bool registerMentionedInFunction(const InstList & code, const std::string & reg)
{
	for (auto * inst : code) {
		if (instructionMentionsRegister(inst, reg)) {
			return true;
		}
	}
	return false;
}

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

/// @brief 仿射地址链描述：循环内形如 base + index * 2^scale 的地址计算序列
struct AffineAddressChain {
    InstIt baseMoveIt;                  ///< mv addrReg, baseReg 指令位置
    InstIt indexMoveIt;                 ///< mv tmpReg, indexReg 指令位置
    InstIt shiftIt;                     ///< slli tmpReg, tmpReg, scale 指令位置
    InstIt addIt;                       ///< add addrReg, addrReg/baseReg, tmpReg 指令位置
    std::vector<InstIt> memoryUses;     ///< 使用 addrReg 作为基址的内存访问指令列表
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
	    scale > 10) {
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
	if (stride <= 0 || stride > 2047 || addrReg == indexReg || tmpReg == indexReg || baseReg == indexReg) {
		return false;
	}

	std::vector<InstIt> memoryUses;
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
		if (!isMemoryOpcode(inst->opcode) || isStoreOpcode(inst->opcode) ||
		    (!operandReferencesAddressBase(inst->result, addrReg) &&
		     !operandReferencesAddressBase(inst->arg1, addrReg) &&
		     !operandReferencesAddressBase(inst->arg2, addrReg) &&
		     !operandReferencesAddressBase(inst->addition, addrReg))) {
			return false;
		}
		memoryUses.push_back(scan);
	}

	if (memoryUses.empty()) {
		return false;
	}

	chain.baseMoveIt = start;
	chain.indexMoveIt = indexMoveIt;
	chain.shiftIt = shiftIt;
	chain.addIt = addIt;
	chain.memoryUses = std::move(memoryUses);
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
bool assignPointerRegisters(InstList & code, std::vector<AffineAddressChain> & chains)
{
	static const std::vector<std::string> kCandidates = {"t4", "t5", "t6"};
	std::unordered_set<std::string> reserved;
	for (auto & chain : chains) {
		bool assigned = false;
		for (const auto & reg : kCandidates) {
			if (reserved.find(reg) != reserved.end() || registerMentionedInFunction(code, reg)) {
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

/// @brief 通用仿射地址递推优化：把循环内 base + i * stride 地址计算改为指针步进。
///
/// 只匹配简单 innermost 计数循环，不读取函数名/标签名语义。触发条件包括：
///   - 循环头为 blt i,bound,body + j exit，循环体末尾唯一跳回头部；
///   - i 在循环体中按 +1 更新；
///   - 地址计算形如 mv addr,base; mv tmp,i; slli tmp,tmp,k; add addr,addr,tmp；
///   - 临时指针寄存器在当前函数中完全未使用，且循环体内没有 call/额外分支。
bool reduceAffineAddressRecurrences(InstList & code)
{
	if (functionHasStore(code)) {
		return false;
	}

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
		if (chains.empty() || !assignPointerRegisters(code, chains)) {
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
		code.insert(headerInsertPos, new RiscV64Inst(loopEntryLabel, ":"));
		(*latchIt)->result = loopEntryLabel;

		for (const auto & chain : chains) {
			(*chain.baseMoveIt)->setDead();
			(*chain.indexMoveIt)->setDead();
			(*chain.shiftIt)->setDead();
			(*chain.addIt)->setDead();
			for (auto memIt : chain.memoryUses) {
				replaceMemoryBase(*memIt, chain.addrReg, chain.pointerReg);
			}
			code.insert(updateInsertIt,
			            new RiscV64Inst("addi", chain.pointerReg, chain.pointerReg, std::to_string(chain.stride)));
		}

		return true;
	}
	return changed;
}

} // namespace

bool RiscV64Peephole::run(ILocRiscV64 & iloc, int optLevel)
{
	(void) optLevel;  // 当前所有窥孔优化均不依赖优化级别
	bool changed = false;
	bool localChanged = false;
	auto & code = iloc.getCode();
	do {
		localChanged = false;
		// 仿射地址递推优化：将循环内 base+i*stride 地址计算改为指针步进
		localChanged = reduceAffineAddressRecurrences(code) || localChanged;
		localChanged = reduceMulByConst(code) || localChanged;
		localChanged = foldZeroSubCompare(code) || localChanged;
		localChanged = removeSelfMoves(code) || localChanged;
		localChanged = removeConsecutiveDuplicates(code) || localChanged;
		localChanged = removeJumpToNextLabel(code) || localChanged;
		changed = changed || localChanged;
	} while (localChanged);
	return changed;
}
