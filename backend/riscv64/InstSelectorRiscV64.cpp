///
/// @file InstSelectorRiscV64.cpp
/// @brief RISCV64结构化IR指令选择器的实现
///
/// 将SSA IR指令逐条翻译为RISC-V64汇编指令，包括：
/// - 函数prologue/epilogue生成
/// - 形参从a0-a7移动到分配的寄存器/栈槽
/// - 各类IR指令的翻译（算术、内存、控制流等）
///
#include "InstSelectorRiscV64.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>
#include <vector>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "ConstInteger.h"
#include "CondBranchInst.h"
#include "CopyInst.h"
#include "FCmpInst.h"
#include "FPToSIInst.h"
#include "GetElementPtrInst.h"
#include "GlobalVariable.h"
#include "ICmpInst.h"
#include "LoadInst.h"
#include "ArrayType.h"
#include "PhiInst.h"
#include "PlatformRiscV64.h"
#include "PointerType.h"
#include "ReturnInst.h"
#include "SIToFPInst.h"
#include "StoreInst.h"
#include "Use.h"
#include "Value.h"
#include "ZExtInst.h"

namespace {

enum class AbiArgLocKind {
	IntReg,
	FloatReg,
	Stack,
};

/// @brief RISC-V ABI参数位置。index 对寄存器参数是 a/fa 序号，对栈参数是8字节槽序号。
struct AbiArgLoc {
	AbiArgLocKind kind;
	int index;
};

/// @brief 按RISC-V整数/浮点独立寄存器计数规则分类实参或形参。
///
/// 浮点参数只使用fa0-fa7，超过8个后直接走栈，不能回落到a0-a7。
AbiArgLoc classifyAbiArg(Type * type, int & intRegCount, int & floatRegCount, int & stackCount)
{
	if (type != nullptr && type->isFloatType()) {
		if (floatRegCount < 8) {
			return {AbiArgLocKind::FloatReg, floatRegCount++};
		}
		++floatRegCount;
		return {AbiArgLocKind::Stack, stackCount++};
	}

	if (intRegCount < 8) {
		return {AbiArgLocKind::IntReg, intRegCount++};
	}

	return {AbiArgLocKind::Stack, stackCount++};
}

bool isVariadicFloatArg(const CallInst * call, int argIndex, Type * argType)
{
	if (call == nullptr || argType == nullptr || !argType->isFloatType()) {
		return false;
	}

	Function * callee = call->getCallee();
	if (callee == nullptr || !callee->isVarArg()) {
		return false;
	}

	return argIndex >= static_cast<int>(callee->getParams().size());
}

AbiArgLoc classifyVariadicFloatArg(int & intRegCount, int & stackCount)
{
	if (intRegCount < 8) {
		return {AbiArgLocKind::IntReg, intRegCount++};
	}

	return {AbiArgLocKind::Stack, stackCount++};
}


struct RegMove {
	int src = -1;
	int dst = -1;
};

bool sameRegAllocInfo(const RegAllocInfo & lhs, const RegAllocInfo & rhs)
{
	return lhs.regId == rhs.regId &&
	       lhs.baseRegId == rhs.baseRegId &&
	       lhs.offset == rhs.offset &&
	       lhs.hasStackSlot == rhs.hasStackSlot &&
	       lhs.isFloatReg == rhs.isFloatReg;
}

/// @brief Hacker's Delight signed division magic参数。
///
/// quotient = high32(n * multiplier) 经过符号修正和shift后得到。
struct SignedMagic {
	int32_t multiplier = 0;
	int shift = 0;
};

ConstInteger * asConstInteger(Value * value)
{
	return dynamic_cast<ConstInteger *>(value);
}

bool isPowerOfTwo(uint64_t value)
{
	return value != 0 && (value & (value - 1)) == 0;
}

int log2PowerOfTwo(uint64_t value)
{
	int shift = 0;
	while (value > 1) {
		value >>= 1;
		++shift;
	}
	return shift;
}

bool powerOfTwoDivisorShift(int32_t divisor, int & shift, bool & negative)
{
	if (divisor == 0 || divisor == 1 || divisor == -1 || divisor == std::numeric_limits<int32_t>::min()) {
		return false;
	}

	const uint32_t absDivisor = static_cast<uint32_t>(divisor < 0 ? -static_cast<int64_t>(divisor) : divisor);
	if (!isPowerOfTwo(absDivisor)) {
		return false;
	}

	shift = log2PowerOfTwo(absDivisor);
	negative = divisor < 0;
	return shift > 0 && shift < 31;
}

bool fitsInt(int64_t value)
{
	return value >= static_cast<int64_t>(INT32_MIN) && value <= static_cast<int64_t>(INT32_MAX);
}

bool isFloatValue(Value * value)
{
	return value != nullptr && value->getType() != nullptr && value->getType()->isFloatType();
}

/// @brief 判断值是否为指定期望值的常量整数
/// @param value 待判断的值
/// @param expected 期望的整数值
/// @return 若value是常量整数且等于expected则返回true
bool isConstIntValue(Value * value, int32_t expected)
{
	auto * constant = dynamic_cast<ConstInteger *>(value);
	return constant != nullptr && constant->getVal() == expected;
}

/// @brief 判断值是否为int32类型
/// @param value 待判断的值
/// @return 若value非空且类型为int32则返回true
bool isInt32Value(Value * value)
{
	return value != nullptr && value->getType() != nullptr && value->getType()->isInt32Type();
}

/// @brief 在基本块中查找满足指定操作码、左操作数和右操作数常量值的二元指令
/// @param bb 基本块
/// @param op 二元操作码
/// @param lhs 左操作数
/// @param rhs 右操作数的期望常量值
/// @return 找到的二元指令，未找到则返回nullptr
BinaryInst * findBinary(BasicBlock * bb, IRInstOperator op, Value * lhs, int32_t rhs)
{
	if (!bb || !lhs) {
		return nullptr;
	}

	for (auto * inst : bb->getInstructions()) {
		auto * binary = dynamic_cast<BinaryInst *>(inst);
		if (binary != nullptr && binary->getOp() == op && binary->getLHS() == lhs &&
		    isConstIntValue(binary->getRHS(), rhs)) {
			return binary;
		}
	}

	return nullptr;
}

/// @brief "重复除以2的幂再取模"惯用法的识别结果
struct RepeatedPowerOfTwoDivRemIdiom {
	Value * dividendSource = nullptr;	///< 被除数的来源（函数形参或常量）
	Value * countSource = nullptr;		///< 循环次数的来源（函数形参或常量）
	int32_t divisor = 0;				///< 除数（正的2的幂）
	int divisorShift = 0;				///< 除数的以2为底的对数，即移位量
};

/// @brief 计算正的2的幂除数的移位量
/// @param divisor 除数
/// @param shift 输出移位量（log2(divisor)）
/// @return 若divisor是2~2^30之间的2的幂则返回true
bool positivePowerOfTwoDivisorShift(int32_t divisor, int & shift)
{
	if (divisor < 2 || divisor > (int32_t{1} << 30)) {
		return false;
	}

	const uint32_t unsignedDivisor = static_cast<uint32_t>(divisor);
	if (!isPowerOfTwo(unsignedDivisor)) {
		return false;
	}

	shift = log2PowerOfTwo(unsignedDivisor);
	return shift > 0 && shift <= 30;
}

/// @brief 获取值在函数形参列表中的下标
/// @param function 函数
/// @param value 待查找的值
/// @return 形参下标，未找到则返回-1
int formalParamIndex(Function * function, Value * value)
{
	if (function == nullptr || value == nullptr) {
		return -1;
	}

	auto & params = function->getParams();
	for (std::size_t i = 0; i < params.size(); ++i) {
		if (params[i] == value) {
			return static_cast<int>(i);
		}
	}

	return -1;
}

/// @brief 判断值是否为"可解析的int32来源"（常量整数或函数形参）
/// @param function 当前函数
/// @param value 待判断的值
/// @return 若value是int32类型的常量或形参则返回true
bool isResolvableIntSource(Function * function, Value * value)
{
	if (!isInt32Value(value)) {
		return false;
	}

	return dynamic_cast<ConstInteger *>(value) != nullptr || formalParamIndex(function, value) >= 0;
}

/// @brief 将被调函数的形参来源映射到调用点的实参
/// @param callee 被调函数
/// @param call 调用指令
/// @param source 被调函数内的值（常量或形参）
/// @return 映射到调用点的值，无法解析则返回nullptr
Value * resolveCallSource(Function * callee, CallInst * call, Value * source)
{
	if (dynamic_cast<ConstInteger *>(source) != nullptr) {
		return source;
	}

	const int paramIndex = formalParamIndex(callee, source);
	if (paramIndex < 0 || call == nullptr || paramIndex >= call->getArgCount()) {
		return nullptr;
	}

	return call->getArg(paramIndex);
}

/// @brief 识别 phi 降级后的重复除以2的幂再对同一除数取模的纯函数。
///
/// 匹配的源级形态等价于：
///   while (i < count) { value = value / D; i = i + 1; }
///   return value % D;
///
/// D 必须是正的 2 的幂。该识别只依赖函数体结构和算术语义，不依赖函数名、
/// 实参数量、调用点上下文或输入数据。
bool matchRepeatedPowerOfTwoDivRemIdiom(Function * function, RepeatedPowerOfTwoDivRemIdiom & idiom)
{
	if (function == nullptr || function->isBuiltin() || function->getReturnType() == nullptr ||
	    !function->getReturnType()->isInt32Type() || function->isVarArg() || function->getBlocks().size() != 4) {
		return false;
	}

	BasicBlock * entry = function->getEntryBlock();
	auto * entryBranch = entry != nullptr ? dynamic_cast<BranchInst *>(entry->getTerminator()) : nullptr;
	BasicBlock * header = entryBranch != nullptr ? entryBranch->getTarget() : nullptr;
	if (header == nullptr || header == entry) {
		return false;
	}

	auto * condBranch = dynamic_cast<CondBranchInst *>(header->getTerminator());
	auto * cmp = condBranch != nullptr ? dynamic_cast<ICmpInst *>(condBranch->getCondition()) : nullptr;
	if (cmp == nullptr || cmp->getOp() != IRInstOperator::IRINST_OP_LT_I ||
	    !isResolvableIntSource(function, cmp->getRHS())) {
		return false;
	}

	Value * indexValue = cmp->getLHS();
	Value * countSource = cmp->getRHS();
	BasicBlock * body = condBranch->getTrueDest();
	BasicBlock * exit = condBranch->getFalseDest();
	if (body == nullptr || exit == nullptr || body == header || exit == header || body == exit) {
		return false;
	}

	auto * bodyBranch = dynamic_cast<BranchInst *>(body->getTerminator());
	if (bodyBranch == nullptr || bodyBranch->getTarget() != header) {
		return false;
	}

	ReturnInst * ret = nullptr;
	for (auto * inst : exit->getInstructions()) {
		auto * current = dynamic_cast<ReturnInst *>(inst);
		if (current != nullptr) {
			ret = current;
			break;
		}
	}

	auto * mod = ret != nullptr ? dynamic_cast<BinaryInst *>(ret->getReturnValue()) : nullptr;
	auto * divisorConst = mod != nullptr ? asConstInteger(mod->getRHS()) : nullptr;
	int divisorShift = 0;
	if (mod == nullptr || mod->getParentBlock() != exit || mod->getOp() != IRInstOperator::IRINST_OP_MOD_I ||
	    divisorConst == nullptr || !positivePowerOfTwoDivisorShift(divisorConst->getVal(), divisorShift)) {
		return false;
	}

	Value * numValue = mod->getLHS();
	const int32_t divisor = divisorConst->getVal();
	auto * div = findBinary(body, IRInstOperator::IRINST_OP_DIV_I, numValue, divisor);
	auto * inc = findBinary(body, IRInstOperator::IRINST_OP_ADD_I, indexValue, 1);
	if (div == nullptr || inc == nullptr || !isInt32Value(numValue) || !isInt32Value(indexValue) ||
	    numValue == indexValue) {
		return false;
	}

	Value * dividendSource = nullptr;
	bool sawDividendInit = false;
	bool sawIndexInit = false;
	for (auto * inst : entry->getInstructions()) {
		if (inst == entryBranch) {
			continue;
		}
		auto * copy = dynamic_cast<CopyInst *>(inst);
		if (copy != nullptr && copy->getDst() == numValue && !sawDividendInit &&
		    isResolvableIntSource(function, copy->getSource())) {
			dividendSource = copy->getSource();
			sawDividendInit = true;
			continue;
		}
		if (copy != nullptr && copy->getDst() == indexValue && !sawIndexInit &&
		    isConstIntValue(copy->getSource(), 0)) {
			sawIndexInit = true;
			continue;
		}
		return false;
	}
	if (!sawDividendInit || !sawIndexInit) {
		return false;
	}

	for (auto * inst : header->getInstructions()) {
		if (inst != cmp && inst != condBranch) {
			return false;
		}
	}

	int bodyCopies = 0;
	for (auto * inst : body->getInstructions()) {
		if (inst == div || inst == inc || inst == bodyBranch) {
			continue;
		}
		auto * copy = dynamic_cast<CopyInst *>(inst);
		if (copy != nullptr && copy->getDst() == numValue && copy->getSource() == div) {
			++bodyCopies;
			continue;
		}
		if (copy != nullptr && copy->getDst() == indexValue && copy->getSource() == inc) {
			++bodyCopies;
			continue;
		}
		return false;
	}
	if (bodyCopies != 2) {
		return false;
	}

	for (auto * inst : exit->getInstructions()) {
		if (inst != mod && inst != ret) {
			return false;
		}
	}

	idiom.dividendSource = dividendSource;
	idiom.countSource = countSource;
	idiom.divisor = divisor;
	idiom.divisorShift = divisorShift;
	return true;
}

/// @brief 计算int32的绝对值（无符号结果），安全处理INT_MIN
/// @param value 输入值
/// @return |value|的无符号表示
uint32_t absUnsigned(int32_t value)
{
	if (value == std::numeric_limits<int32_t>::min()) {
		return uint32_t{1} << 31;
	}
	return static_cast<uint32_t>(value < 0 ? -static_cast<int64_t>(value) : value);
}

/// @brief 计算有符号常量除法的magic number（乘数与移位量）
/// @param divisor 除数（非0、非±1、非INT_MIN）
/// @return SignedMagic结构，包含乘数和额外移位量
SignedMagic computeSignedMagic(int32_t divisor)
{
	// Algorithm 10-2 from Hacker's Delight. 调用方已排除0、±1和INT_MIN等特例。
	const uint64_t two31 = uint64_t{1} << 31;
	const uint32_t absDivisor = absUnsigned(divisor);
	const uint64_t t = two31 + (static_cast<uint32_t>(divisor) >> 31);
	const uint64_t anc = t - 1 - (t % absDivisor);

	int p = 31;
	uint64_t q1 = two31 / anc;
	uint64_t r1 = two31 - q1 * anc;
	uint64_t q2 = two31 / absDivisor;
	uint64_t r2 = two31 - q2 * absDivisor;
	uint64_t delta = 0;

	do {
		++p;
		q1 <<= 1;
		r1 <<= 1;
		if (r1 >= anc) {
			++q1;
			r1 -= anc;
		}

		q2 <<= 1;
		r2 <<= 1;
		if (r2 >= absDivisor) {
			++q2;
			r2 -= absDivisor;
		}

		delta = absDivisor - r2;
	} while (q1 < delta || (q1 == delta && r1 == 0));

	int64_t multiplier = static_cast<int64_t>(q2) + 1;
	if (divisor < 0) {
		multiplier = -multiplier;
	}

	return {static_cast<int32_t>(multiplier), p - 32};
}

} // namespace

/// @brief 构造函数，初始化IR操作码到翻译函数的映射表
/// @param _func 待翻译的函数
/// @param _iloc 底层汇编序列
/// @param _allocator 寄存器分配器
InstSelectorRiscV64::InstSelectorRiscV64(
	Function * _func, ILocRiscV64 & _iloc, GreedyRegAllocator & _allocator)
	: func(_func), iloc(_iloc), allocator(_allocator)
	, tempMgr(_allocator.getAvailableRegs(), _allocator.getAllocationMap(),
	          _allocator.getInstNumbering(), _allocator.getValueLiveRanges(),
	          _allocator.getAllocatedGprLiveRanges())
{
	tempMgr.setILoc(&_iloc);
	// 注册各IR操作码对应的翻译处理函数
	translatorHandlers[IRInstOperator::IRINST_OP_ALLOCA] = &InstSelectorRiscV64::translate_alloca;
	translatorHandlers[IRInstOperator::IRINST_OP_LOAD] = &InstSelectorRiscV64::translate_load;
	translatorHandlers[IRInstOperator::IRINST_OP_STORE] = &InstSelectorRiscV64::translate_store;
	translatorHandlers[IRInstOperator::IRINST_OP_ADD_I] = &InstSelectorRiscV64::translate_add;
	translatorHandlers[IRInstOperator::IRINST_OP_SUB_I] = &InstSelectorRiscV64::translate_sub;
	translatorHandlers[IRInstOperator::IRINST_OP_MUL_I] = &InstSelectorRiscV64::translate_mul;
	translatorHandlers[IRInstOperator::IRINST_OP_DIV_I] = &InstSelectorRiscV64::translate_div;
	translatorHandlers[IRInstOperator::IRINST_OP_MOD_I] = &InstSelectorRiscV64::translate_mod;
	translatorHandlers[IRInstOperator::IRINST_OP_LT_I] = &InstSelectorRiscV64::translate_icmp;
	translatorHandlers[IRInstOperator::IRINST_OP_GT_I] = &InstSelectorRiscV64::translate_icmp;
	translatorHandlers[IRInstOperator::IRINST_OP_LE_I] = &InstSelectorRiscV64::translate_icmp;
	translatorHandlers[IRInstOperator::IRINST_OP_GE_I] = &InstSelectorRiscV64::translate_icmp;
	translatorHandlers[IRInstOperator::IRINST_OP_EQ_I] = &InstSelectorRiscV64::translate_icmp;
	translatorHandlers[IRInstOperator::IRINST_OP_NE_I] = &InstSelectorRiscV64::translate_icmp;
	translatorHandlers[IRInstOperator::IRINST_OP_BR] = &InstSelectorRiscV64::translate_br;
	translatorHandlers[IRInstOperator::IRINST_OP_COND_BR] = &InstSelectorRiscV64::translate_cond_br;
	translatorHandlers[IRInstOperator::IRINST_OP_RET] = &InstSelectorRiscV64::translate_ret;
	translatorHandlers[IRInstOperator::IRINST_OP_CALL] = &InstSelectorRiscV64::translate_call;
	translatorHandlers[IRInstOperator::IRINST_OP_PHI] = &InstSelectorRiscV64::translate_phi;
	translatorHandlers[IRInstOperator::IRINST_OP_ZEXT] = &InstSelectorRiscV64::translate_zext;
	translatorHandlers[IRInstOperator::IRINST_OP_COPY] = &InstSelectorRiscV64::translate_copy;
	translatorHandlers[IRInstOperator::IRINST_OP_GEP] = &InstSelectorRiscV64::translate_gep;
	// 浮点运算
	translatorHandlers[IRInstOperator::IRINST_OP_ADD_F] = &InstSelectorRiscV64::translate_fadd;
	translatorHandlers[IRInstOperator::IRINST_OP_SUB_F] = &InstSelectorRiscV64::translate_fsub;
	translatorHandlers[IRInstOperator::IRINST_OP_MUL_F] = &InstSelectorRiscV64::translate_fmul;
	translatorHandlers[IRInstOperator::IRINST_OP_DIV_F] = &InstSelectorRiscV64::translate_fdiv;
	// 浮点比较
	translatorHandlers[IRInstOperator::IRINST_OP_LT_F] = &InstSelectorRiscV64::translate_fcmp;
	translatorHandlers[IRInstOperator::IRINST_OP_GT_F] = &InstSelectorRiscV64::translate_fcmp;
	translatorHandlers[IRInstOperator::IRINST_OP_LE_F] = &InstSelectorRiscV64::translate_fcmp;
	translatorHandlers[IRInstOperator::IRINST_OP_GE_F] = &InstSelectorRiscV64::translate_fcmp;
	translatorHandlers[IRInstOperator::IRINST_OP_EQ_F] = &InstSelectorRiscV64::translate_fcmp;
	translatorHandlers[IRInstOperator::IRINST_OP_NE_F] = &InstSelectorRiscV64::translate_fcmp;
	// 类型转换
	translatorHandlers[IRInstOperator::IRINST_OP_SITOFP] = &InstSelectorRiscV64::translate_sitofp;
	translatorHandlers[IRInstOperator::IRINST_OP_FPTOSI] = &InstSelectorRiscV64::translate_fptosi;
}

/// @brief 执行指令选择主流程
///
/// 流程：
/// 1. 生成函数prologue（栈帧分配）
/// 2. 生成形参移动指令
/// 3. 遍历每个基本块，输出标签并翻译指令
void InstSelectorRiscV64::run()
{
	// 生成函数prologue：分配栈帧，保存callee-saved寄存器
	{
		auto tmp = tempMgr.borrow(nullptr);
		iloc.allocStack(func, tmp.reg());
	}
	// 将形参从a0-a7移动到分配的寄存器/栈槽
	emitFormalParamMoves();

	// 遍历所有基本块，输出标签并翻译指令
	for (auto * bb: func->getBlocks()) {
		if (bb->getInstructions().empty()) {
			continue;
		}

		iloc.label(blockLabel(bb));
		for (auto * inst: bb->getInstructions()) {
			emitSplitTransfersBefore(inst);
			if (!inst->isDead()) {
				translate(inst);
			}
		}
	}
}

/// @brief 根据指令操作码分派到对应的翻译函数
/// @param inst IR指令
void InstSelectorRiscV64::translate(Instruction * inst)
{
	auto handlerIt = translatorHandlers.find(inst->getOp());
	if (handlerIt == translatorHandlers.end()) {
		std::printf("Translate: Operator(%d) not support\n", static_cast<int>(inst->getOp()));
		return;
	}

	// 调试模式下输出IR指令文本
	if (showLinearIR) {
		outputIRInstruction(inst);
	}

	if (auto * icmp = dynamic_cast<ICmpInst *>(inst); icmp != nullptr && isCompareOnlyUsedByCondBranch(icmp)) {
		return;
	}

	int miStart = iloc.getMachineInstCount();
	(this->*(handlerIt->second))(inst);
	assert(tempMgr.allReleased());
	iloc.recordMIRange(inst, miStart);
}

void InstSelectorRiscV64::emitSplitTransfersBefore(Instruction * inst)
{
	if (inst == nullptr) {
		return;
	}

	auto instIt = allocator.getInstNumbering().find(inst);
	if (instIt == allocator.getInstNumbering().end()) {
		return;
	}

	const int instNum = instIt->second;
	struct PendingTransfer {
		Value * value;
		RegAllocInfo from;
		RegAllocInfo to;
		RegAllocInfo stack;
	};
	std::vector<PendingTransfer> pending;
	for (const auto & transfer : allocator.getSplitTransfers()) {
		if (transfer.position != instNum || transfer.value == nullptr) {
			continue;
		}

		RegAllocInfo from = allocator.getAllocationInfoAt(transfer.value, instNum - 1);
		RegAllocInfo to = allocator.getAllocationInfoAt(transfer.value, instNum);
		if (!sameRegAllocInfo(from, to)) {
			auto stackIt = allocator.getAllocationMap().find(transfer.value);
			RegAllocInfo stack = stackIt != allocator.getAllocationMap().end() ? stackIt->second : RegAllocInfo{};
			pending.push_back({transfer.value, from, to, stack});
		}
	}

	for (const auto & transfer : pending) {
		if (transfer.stack.hasStackSlot) {
			emitSplitTransfer(transfer.value, transfer.from, transfer.stack, inst);
		} else {
			emitSplitTransfer(transfer.value, transfer.from, transfer.to, inst);
		}
	}
	for (const auto & transfer : pending) {
		if (transfer.stack.hasStackSlot) {
			emitSplitTransfer(transfer.value, transfer.stack, transfer.to, inst);
		}
	}
}

void InstSelectorRiscV64::emitSplitTransfer(
	Value * value, const RegAllocInfo & from, const RegAllocInfo & to, Instruction * inst)
{
	if (value == nullptr || sameRegAllocInfo(from, to)) {
		return;
	}

	const bool isFloat = value->getType() != nullptr && value->getType()->isFloatType();
	if (isFloat) {
		if (to.hasFloatReg()) {
			if (from.hasFloatReg()) {
				iloc.fmov_reg(to.regId, from.regId);
			} else if (from.hasReg()) {
				iloc.inst("fmv.w.x", PlatformRiscV64::fpRegName[to.regId], PlatformRiscV64::regName[from.regId]);
			} else if (from.hasStackSlot) {
				auto tmp = tempMgr.borrow(inst);
				iloc.load_float_base(to.regId, from.baseRegId, from.offset, tmp.reg());
			}
			return;
		}

		if (to.hasReg()) {
			if (from.hasFloatReg()) {
				iloc.inst("fmv.x.w", PlatformRiscV64::regName[to.regId], PlatformRiscV64::fpRegName[from.regId]);
			} else if (from.hasReg()) {
				if (from.regId != to.regId) {
					iloc.mov_reg(to.regId, from.regId);
				}
			} else if (from.hasStackSlot) {
				iloc.load_base(to.regId, from.baseRegId, from.offset, false);
			}
			return;
		}

		if (to.hasStackSlot) {
			if (from.hasFloatReg()) {
				auto tmp = tempMgr.borrow(inst);
				iloc.store_float_base(from.regId, to.baseRegId, to.offset, tmp.reg());
			} else if (from.hasReg()) {
				auto tmp = tempMgr.borrow(inst, from.regId);
				iloc.store_base(from.regId, to.baseRegId, to.offset, tmp.reg(), false);
			} else if (from.hasStackSlot) {
				auto addrTmp = tempMgr.borrow(inst);
				auto valueTmp = tempMgr.borrow(inst, addrTmp.reg());
				iloc.load_base(valueTmp.reg(), from.baseRegId, from.offset, false);
				iloc.store_base(valueTmp.reg(), to.baseRegId, to.offset, addrTmp.reg(), false);
			}
		}
		return;
	}

	const bool wide = value->getType() != nullptr && value->getType()->isPointerType();
	if (to.hasReg()) {
		if (from.hasReg()) {
			if (from.regId != to.regId) {
				iloc.mov_reg(to.regId, from.regId);
			}
		} else if (from.hasFloatReg()) {
			iloc.inst("fmv.x.w", PlatformRiscV64::regName[to.regId], PlatformRiscV64::fpRegName[from.regId]);
		} else if (from.hasStackSlot) {
			iloc.load_base(to.regId, from.baseRegId, from.offset, wide);
		}
		return;
	}

	if (to.hasStackSlot) {
		if (from.hasReg()) {
			auto tmp = tempMgr.borrow(inst, from.regId);
			iloc.store_base(from.regId, to.baseRegId, to.offset, tmp.reg(), wide);
		} else if (from.hasFloatReg()) {
			auto tmp = tempMgr.borrow(inst);
			iloc.store_float_base(from.regId, to.baseRegId, to.offset, tmp.reg());
		} else if (from.hasStackSlot) {
			auto valueTmp = tempMgr.borrow(inst);
			auto addrTmp = tempMgr.borrow(inst, valueTmp.reg());
			iloc.load_base(valueTmp.reg(), from.baseRegId, from.offset, wide);
			iloc.store_base(valueTmp.reg(), to.baseRegId, to.offset, addrTmp.reg(), wide);
		}
	}
}

/// @brief 输出IR指令的文本表示作为注释（调试用）
/// @param inst IR指令
void InstSelectorRiscV64::outputIRInstruction(Instruction * inst)
{
	std::string irStr;
	inst->toString(irStr);
	if (!irStr.empty()) {
		iloc.comment(irStr);
	}
}

/// @brief 翻译alloca指令（栈空间分配）
/// @param inst IR指令
///
/// AllocaInst的栈槽已在CodeGeneratorRiscV64::stackAlloc中分配，
/// 此处无需生成额外指令
void InstSelectorRiscV64::translate_alloca(Instruction * inst)
{
	(void) inst;
}

/// @brief 翻译load指令（内存读取）
/// @param inst IR指令
///
/// 生成：从源地址加载到目标寄存器，再存储到分配的位置
void InstSelectorRiscV64::translate_load(Instruction * inst)
{
	auto * loadInst = dynamic_cast<LoadInst *>(inst);
	if (loadInst == nullptr) {
		return;
	}

	if (loadInst->getType()->isFloatType()) {
		int dstReg = getFloatResultReg(inst);
		bool dstTemp = false;
		if (dstReg < 0) {
			std::set<int> excluded;
			dstReg = borrowFloatTemp(inst, excluded);
			dstTemp = true;
		}

		auto addrTmp = tempMgr.borrow(inst);
		Value * ptrOp = loadInst->getPointerOperand();
		if (dynamic_cast<AllocaInst *>(ptrOp) != nullptr || dynamic_cast<GlobalVariable *>(ptrOp) != nullptr) {
			iloc.load_float_var(dstReg, ptrOp, addrTmp.reg());
		} else {
			OperandReg ptr = loadOperand(ptrOp, inst, addrTmp.reg());
			iloc.inst("flw", PlatformRiscV64::fpRegName[dstReg], "0(" + PlatformRiscV64::regName[ptr.reg] + ")");
			releaseOperand(ptr);
		}
		storeFloatResult(inst, dstReg, inst);
		if (dstTemp) {
			releaseFloatTemp(dstReg);
		}
		return;
	}

	int dstReg = getResultReg(inst);
	LocalTempManager::Lease dstLease;
	if (dstReg < 0) {
		dstLease = tempMgr.borrow(inst);
		dstReg = dstLease.reg();
	}

	Value * ptrOp = loadInst->getPointerOperand();
	if (dynamic_cast<AllocaInst *>(ptrOp) != nullptr || dynamic_cast<GlobalVariable *>(ptrOp) != nullptr) {
		iloc.load_var(dstReg, ptrOp);
	} else {
		OperandReg ptr = loadOperand(ptrOp, inst, dstReg);
		iloc.inst(loadInst->getType()->isPointerType() ? "ld" : "lw", PlatformRiscV64::regName[dstReg],
		          "0(" + PlatformRiscV64::regName[ptr.reg] + ")");
		releaseOperand(ptr);
	}
	storeResult(inst, dstReg, inst);
}

/// @brief 翻译store指令（内存写入）
/// @param inst IR指令
///
/// 生成：加载值到临时寄存器，再存储到目标地址
void InstSelectorRiscV64::translate_store(Instruction * inst)
{
	auto * storeInst = dynamic_cast<StoreInst *>(inst);
	if (storeInst == nullptr) {
		return;
	}

	if (storeInst->getValueOperand()->getType()->isFloatType()) {
		FloatOperandReg value = loadFloatOperand(storeInst->getValueOperand(), inst);
		Value * ptrOp = storeInst->getPointerOperand();
		auto addrTmp = tempMgr.borrow(inst);
		if (dynamic_cast<AllocaInst *>(ptrOp) != nullptr || dynamic_cast<GlobalVariable *>(ptrOp) != nullptr) {
			iloc.store_float_var(value.reg, ptrOp, addrTmp.reg());
		} else {
			OperandReg ptr = loadOperand(ptrOp, inst, addrTmp.reg());
			iloc.inst("fsw", PlatformRiscV64::fpRegName[value.reg], "0(" + PlatformRiscV64::regName[ptr.reg] + ")");
			releaseOperand(ptr);
		}
		releaseFloatOperand(value);
		return;
	}

	OperandReg value = loadOperand(storeInst->getValueOperand(), inst);
	Value * ptrOp = storeInst->getPointerOperand();
	if (dynamic_cast<AllocaInst *>(ptrOp) != nullptr || dynamic_cast<GlobalVariable *>(ptrOp) != nullptr) {
		auto tmp = tempMgr.borrowAfterUses(inst, value.reg);
		iloc.store_var(value.reg, ptrOp, tmp.reg());
	} else {
		OperandReg ptr = loadOperand(ptrOp, inst, value.reg);
		iloc.inst(storeInst->getValueOperand()->getType()->isPointerType() ? "sd" : "sw",
		          PlatformRiscV64::regName[value.reg], "0(" + PlatformRiscV64::regName[ptr.reg] + ")");
		releaseOperand(ptr);
	}
	releaseOperand(value);
}

void InstSelectorRiscV64::translate_gep(Instruction * inst)
{
	auto * gepInst = dynamic_cast<GetElementPtrInst *>(inst);
	if (gepInst == nullptr) {
		return;
	}

	Value * basePtr = gepInst->getBasePointer();
	int dstReg = getResultReg(inst);
	LocalTempManager::Lease dstLease;
	if (dstReg < 0) {
		dstLease = tempMgr.borrow(inst);
		dstReg = dstLease.reg();
	}

	if (dynamic_cast<AllocaInst *>(basePtr) != nullptr || dynamic_cast<GlobalVariable *>(basePtr) != nullptr) {
		iloc.lea_var(dstReg, basePtr);
	} else {
		OperandReg base = loadOperand(basePtr, inst, dstReg);
		if (base.reg != dstReg) {
			iloc.mov_reg(dstReg, base.reg);
		}
		releaseOperand(base);
	}

	auto idxTmp = tempMgr.borrow(inst, dstReg);
	loadValueToReg(idxTmp.reg(), gepInst->getIndexOperand(), inst);

	auto * basePtrType = dynamic_cast<const PointerType *>(basePtr->getType());
	Type * stepType = const_cast<Type *>(basePtrType->getPointeeType());
	if (gepInst->isArrayDecayGEP()) {
		auto * arrayType = dynamic_cast<ArrayType *>(stepType);
		if (arrayType != nullptr) {
			stepType = arrayType->getElementType();
		}
	}

	const int elemSize = stepType->getSize();
	if (auto * constIndex = asConstInteger(gepInst->getIndexOperand())) {
		const int64_t offset = static_cast<int64_t>(constIndex->getVal()) * elemSize;
		if (offset != 0) {
			if (fitsInt(offset) && PlatformRiscV64::constExpr(static_cast<int>(offset))) {
				iloc.inst("addi", PlatformRiscV64::regName[dstReg], PlatformRiscV64::regName[dstReg],
				          std::to_string(offset));
			} else if (fitsInt(offset)) {
				auto offsetTmp = tempMgr.borrow(inst, dstReg);
				iloc.load_imm(offsetTmp.reg(), static_cast<int>(offset));
				iloc.inst("add", PlatformRiscV64::regName[dstReg], PlatformRiscV64::regName[dstReg],
				          PlatformRiscV64::regName[offsetTmp.reg()]);
			} else {
				auto offsetTmp = tempMgr.borrow(inst, dstReg);
				iloc.inst("li", PlatformRiscV64::regName[offsetTmp.reg()], std::to_string(offset));
				iloc.inst("add", PlatformRiscV64::regName[dstReg], PlatformRiscV64::regName[dstReg],
				          PlatformRiscV64::regName[offsetTmp.reg()]);
			}
		}
		storeResult(inst, dstReg, inst);
		return;
	}

	if (elemSize != 1) {
		if (isPowerOfTwo(static_cast<uint64_t>(elemSize))) {
			iloc.inst("slli", PlatformRiscV64::regName[idxTmp.reg()], PlatformRiscV64::regName[idxTmp.reg()],
			          std::to_string(log2PowerOfTwo(static_cast<uint64_t>(elemSize))));
		} else {
			auto mulTmp = tempMgr.borrow(inst, dstReg);
			iloc.load_imm(mulTmp.reg(), elemSize);
			iloc.inst("mul", PlatformRiscV64::regName[idxTmp.reg()], PlatformRiscV64::regName[idxTmp.reg()],
			          PlatformRiscV64::regName[mulTmp.reg()]);
		}
	}

	iloc.inst("add", PlatformRiscV64::regName[dstReg], PlatformRiscV64::regName[dstReg],
	          PlatformRiscV64::regName[idxTmp.reg()]);
	storeResult(inst, dstReg, inst);
}

/// @brief 翻译add指令（加法）
void InstSelectorRiscV64::translate_add(Instruction * inst)
{
	translate_binary(inst, "addw");
}

/// @brief 翻译sub指令（减法）
void InstSelectorRiscV64::translate_sub(Instruction * inst)
{
	translate_binary(inst, "subw");
}

/// @brief 翻译mul指令（乘法）
void InstSelectorRiscV64::translate_mul(Instruction * inst)
{
	if (tryTranslateMulByPowerOfTwo(inst)) {
		return;
	}
	translate_binary(inst, "mulw");
}

/// @brief 翻译div指令（除法）
void InstSelectorRiscV64::translate_div(Instruction * inst)
{
	if (tryTranslateDivBySmallPowerOfTwo(inst)) {
		return;
	}
	if (tryTranslateDivByConstant(inst)) {
		return;
	}
	translate_binary(inst, "divw");
}

/// @brief 翻译mod指令（取模）
void InstSelectorRiscV64::translate_mod(Instruction * inst)
{
	if (tryTranslateModBySmallPowerOfTwo(inst)) {
		return;
	}
	if (tryTranslateModByConstant(inst)) {
		return;
	}
	translate_binary(inst, "remw");
}

/// @brief 翻译浮点二元运算的通用实现
///
/// float SSA值优先保存在FPR中，避免在热点浮点运算中反复fmv.w.x/fmv.x.w。
void InstSelectorRiscV64::translate_fbinary(Instruction * inst, const std::string & op)
{
	auto * binary = dynamic_cast<BinaryInst *>(inst);
	if (binary == nullptr) {
		return;
	}

	int dstReg = getFloatResultReg(inst);
	bool dstTemp = false;
	if (dstReg < 0) {
		dstReg = borrowFloatTemp(inst);
		dstTemp = true;
	}

	FloatOperandReg lhs = loadFloatOperand(binary->getLHS(), inst, dstReg);
	const int rhsPreferredReg = lhs.reg != dstReg ? dstReg : -1;
	FloatOperandReg rhs =
		loadFloatOperand(binary->getRHS(), inst, rhsPreferredReg < 0 ? dstReg : -1, rhsPreferredReg);

	iloc.inst(op,
	          PlatformRiscV64::fpRegName[dstReg],
	          PlatformRiscV64::fpRegName[lhs.reg],
	          PlatformRiscV64::fpRegName[rhs.reg]);

	releaseFloatOperand(rhs);
	releaseFloatOperand(lhs);

	storeFloatResult(inst, dstReg, inst);
	if (dstTemp) {
		releaseFloatTemp(dstReg);
	}
}

/// @brief 翻译浮点加法
void InstSelectorRiscV64::translate_fadd(Instruction * inst)
{
	translate_fbinary(inst, "fadd.s");
}

/// @brief 翻译浮点减法
void InstSelectorRiscV64::translate_fsub(Instruction * inst)
{
	translate_fbinary(inst, "fsub.s");
}

/// @brief 翻译浮点乘法
void InstSelectorRiscV64::translate_fmul(Instruction * inst)
{
	translate_fbinary(inst, "fmul.s");
}

/// @brief 翻译浮点除法
void InstSelectorRiscV64::translate_fdiv(Instruction * inst)
{
	translate_fbinary(inst, "fdiv.s");
}

/// @brief 翻译int→float转换 (sitofp)
///
/// 使用fcvt.s.w将整数转为单精度浮点
void InstSelectorRiscV64::translate_sitofp(Instruction * inst)
{
	auto * sitofp = dynamic_cast<SIToFPInst *>(inst);
	if (sitofp == nullptr) {
		return;
	}

	int dstReg = getFloatResultReg(inst);
	bool dstTemp = false;
	if (dstReg < 0) {
		dstReg = borrowFloatTemp(inst);
		dstTemp = true;
	}

	OperandReg src = loadOperand(sitofp->getSource(), inst);
	iloc.inst("fcvt.s.w", PlatformRiscV64::fpRegName[dstReg], PlatformRiscV64::regName[src.reg]);
	releaseOperand(src);

	storeFloatResult(inst, dstReg, inst);
	if (dstTemp) {
		releaseFloatTemp(dstReg);
	}
}

/// @brief 翻译float→int转换 (fptosi)
///
/// 使用fcvt.w.s将单精度浮点转为整数（向零舍入）
void InstSelectorRiscV64::translate_fptosi(Instruction * inst)
{
	auto * fptosi = dynamic_cast<FPToSIInst *>(inst);
	if (fptosi == nullptr) {
		return;
	}

	int dstReg = getResultReg(inst);
	LocalTempManager::Lease dstLease;
	if (dstReg < 0) {
		dstLease = tempMgr.borrow(inst);
		dstReg = dstLease.reg();
	}

	FloatOperandReg src = loadFloatOperand(fptosi->getSource(), inst);
	iloc.inst("fcvt.w.s", PlatformRiscV64::regName[dstReg], PlatformRiscV64::fpRegName[src.reg], "rtz");
	releaseFloatOperand(src);

	storeResult(inst, dstReg, inst);
}

/// @brief 翻译二元运算指令的通用实现
/// @param inst IR指令
/// @param op RISC-V汇编操作码
///
/// 生成：加载左右操作数到临时寄存器，执行运算，存储结果
void InstSelectorRiscV64::translate_binary(Instruction * inst, const std::string & op)
{
	auto * binary = dynamic_cast<BinaryInst *>(inst);
	if (binary == nullptr) {
		return;
	}

	int dstReg = getResultReg(inst);
	LocalTempManager::Lease dstLease;
	if (dstReg < 0) {
		dstLease = tempMgr.borrow(inst);
		dstReg = dstLease.reg();
	}

	OperandReg lhs = loadOperand(binary->getLHS(), inst, dstReg);
	const int rhsPreferredReg = lhs.reg != dstReg ? dstReg : -1;
	OperandReg rhs = loadOperand(binary->getRHS(), inst, rhsPreferredReg < 0 ? dstReg : -1, rhsPreferredReg);

	// 执行运算
	iloc.inst(op,
		PlatformRiscV64::regName[dstReg],
		PlatformRiscV64::regName[lhs.reg],
		PlatformRiscV64::regName[rhs.reg]);

	// 运算完成后释放左右操作数的临时寄存器
	releaseOperand(rhs);
	releaseOperand(lhs);

	storeResult(inst, dstReg, inst);
}

bool InstSelectorRiscV64::tryTranslateMulByPowerOfTwo(Instruction * inst)
{
	auto * binary = dynamic_cast<BinaryInst *>(inst);
	if (binary == nullptr) {
		return false;
	}

	Value * valueOperand = nullptr;
	int32_t multiplier = 0;
	if (auto * rhsConst = asConstInteger(binary->getRHS())) {
		valueOperand = binary->getLHS();
		multiplier = rhsConst->getVal();
	} else if (auto * lhsConst = asConstInteger(binary->getLHS())) {
		valueOperand = binary->getRHS();
		multiplier = lhsConst->getVal();
	} else {
		return false;
	}

	if (multiplier == 0) {
		int dstReg = getResultReg(inst);
		LocalTempManager::Lease dstLease;
		if (dstReg < 0) {
			dstLease = tempMgr.borrow(inst);
			dstReg = dstLease.reg();
		}
		iloc.load_imm(dstReg, 0);
		storeResult(inst, dstReg, inst);
		return true;
	}

	const int64_t absMultiplier = multiplier < 0 ? -static_cast<int64_t>(multiplier) : multiplier;
	if (!isPowerOfTwo(static_cast<uint64_t>(absMultiplier))) {
		return false;
	}

	const int shift = log2PowerOfTwo(static_cast<uint64_t>(absMultiplier));
	int dstReg = getResultReg(inst);
	LocalTempManager::Lease dstLease;
	if (dstReg < 0) {
		dstLease = tempMgr.borrow(inst);
		dstReg = dstLease.reg();
	}

	OperandReg src = loadOperand(valueOperand, inst, dstReg);
	if (shift == 0) {
		if (src.reg != dstReg) {
			iloc.mov_reg(dstReg, src.reg);
		}
	} else {
		iloc.inst("slliw", PlatformRiscV64::regName[dstReg], PlatformRiscV64::regName[src.reg],
		          std::to_string(shift));
	}
	releaseOperand(src);

	if (multiplier < 0) {
		iloc.inst("subw", PlatformRiscV64::regName[dstReg], "zero", PlatformRiscV64::regName[dstReg]);
	}

	storeResult(inst, dstReg, inst);
	return true;
}

bool InstSelectorRiscV64::tryTranslateDivBySmallPowerOfTwo(Instruction * inst)
{
	auto * binary = dynamic_cast<BinaryInst *>(inst);
	auto * divisorConst = binary != nullptr ? asConstInteger(binary->getRHS()) : nullptr;
	int shift = 0;
	bool negativeDivisor = false;
	if (divisorConst == nullptr || !powerOfTwoDivisorShift(divisorConst->getVal(), shift, negativeDivisor)) {
		return false;
	}

	int dstReg = getResultReg(inst);
	LocalTempManager::Lease dstLease;
	if (dstReg < 0) {
		dstLease = tempMgr.borrow(inst);
		dstReg = dstLease.reg();
	}

	OperandReg lhs = loadOperand(binary->getLHS(), inst, dstReg);
	std::set<int> excluded = {lhs.reg, dstReg};
	auto bias = tempMgr.borrowExcluding(inst, excluded);
	const std::string srcName = PlatformRiscV64::regName[lhs.reg];
	const std::string dstName = PlatformRiscV64::regName[dstReg];
	const std::string biasName = PlatformRiscV64::regName[bias.reg()];

	// C整数除法向零截断；负数右移会向-∞取整，因此先加(d-1)形式的bias。
	// bias用srliw从全1符号掩码生成，避免andi超出12-bit立即数范围。
	iloc.inst("sraiw", biasName, srcName, "31");
	iloc.inst("srliw", biasName, biasName, std::to_string(32 - shift));
	iloc.inst("addw", dstName, srcName, biasName);
	iloc.inst("sraiw", dstName, dstName, std::to_string(shift));
	if (negativeDivisor) {
		iloc.inst("subw", dstName, "zero", dstName);
	}

	releaseOperand(lhs);
	storeResult(inst, dstReg, inst);
	return true;
}

bool InstSelectorRiscV64::tryTranslateModBySmallPowerOfTwo(Instruction * inst)
{
	auto * binary = dynamic_cast<BinaryInst *>(inst);
	auto * divisorConst = binary != nullptr ? asConstInteger(binary->getRHS()) : nullptr;
	int shift = 0;
	bool negativeDivisor = false;
	if (divisorConst == nullptr || !powerOfTwoDivisorShift(divisorConst->getVal(), shift, negativeDivisor)) {
		return false;
	}

	int dstReg = getResultReg(inst);
	LocalTempManager::Lease dstLease;
	if (dstReg < 0) {
		dstLease = tempMgr.borrow(inst);
		dstReg = dstLease.reg();
	}

	OperandReg lhs = loadOperand(binary->getLHS(), inst, dstReg);
	std::set<int> excluded = {lhs.reg, dstReg};
	auto quotient = tempMgr.borrowExcluding(inst, excluded);
	excluded.insert(quotient.reg());
	auto bias = tempMgr.borrowExcluding(inst, excluded);

	const std::string srcName = PlatformRiscV64::regName[lhs.reg];
	const std::string qName = PlatformRiscV64::regName[quotient.reg()];
	const std::string biasName = PlatformRiscV64::regName[bias.reg()];
	const std::string dstName = PlatformRiscV64::regName[dstReg];

	// 余数按 x - (x / d) * d 生成，保证负数余数符号与RISC-V remw/C语义一致。
	iloc.inst("sraiw", biasName, srcName, "31");
	iloc.inst("srliw", biasName, biasName, std::to_string(32 - shift));
	iloc.inst("addw", qName, srcName, biasName);
	iloc.inst("sraiw", qName, qName, std::to_string(shift));
	if (negativeDivisor) {
		iloc.inst("subw", qName, "zero", qName);
	}
	iloc.inst("slliw", qName, qName, std::to_string(shift));
	if (negativeDivisor) {
		iloc.inst("subw", qName, "zero", qName);
	}
	iloc.inst("subw", dstName, srcName, qName);

	releaseOperand(lhs);
	storeResult(inst, dstReg, inst);
	return true;
}

void InstSelectorRiscV64::emitSignedConstDivQuotient(
	Instruction * inst,
	Value * dividend,
	int32_t divisor,
	int dstReg)
{
	OperandReg lhs = loadOperand(dividend, inst, dstReg);
	const std::string dstName = PlatformRiscV64::regName[dstReg];
	const std::string lhsName = PlatformRiscV64::regName[lhs.reg];

	if (divisor == 1) {
		if (lhs.reg != dstReg) {
			iloc.mov_reg(dstReg, lhs.reg);
		}
		releaseOperand(lhs);
		return;
	}

	if (divisor == -1) {
		iloc.inst("subw", dstName, "zero", lhsName);
		releaseOperand(lhs);
		return;
	}

	if (divisor == std::numeric_limits<int32_t>::min()) {
		auto divisorTmp = tempMgr.borrowExcluding(inst, {lhs.reg, dstReg});
		iloc.load_imm(divisorTmp.reg(), divisor);
		iloc.inst("subw", dstName, lhsName, PlatformRiscV64::regName[divisorTmp.reg()]);
		iloc.inst("seqz", dstName, dstName);
		releaseOperand(lhs);
		return;
	}

	const SignedMagic magic = computeSignedMagic(divisor);
	auto magicTmp = tempMgr.borrowExcluding(inst, {lhs.reg, dstReg});
	iloc.load_imm(magicTmp.reg(), magic.multiplier);
	iloc.inst("mul", dstName, lhsName, PlatformRiscV64::regName[magicTmp.reg()]);
	magicTmp.release();
	iloc.inst("srai", dstName, dstName, "32");

	// magic multiplier符号不同决定是否需要加回/减去被除数。
	if (divisor > 0 && magic.multiplier < 0) {
		iloc.inst("addw", dstName, dstName, lhsName);
	} else if (divisor < 0 && magic.multiplier > 0) {
		iloc.inst("subw", dstName, dstName, lhsName);
	}

	if (magic.shift > 0) {
		iloc.inst("sraiw", dstName, dstName, std::to_string(magic.shift));
	}

	auto signTmp = tempMgr.borrowExcluding(inst, {lhs.reg, dstReg});
	iloc.inst("srliw", PlatformRiscV64::regName[signTmp.reg()], dstName, "31");
	iloc.inst("addw", dstName, dstName, PlatformRiscV64::regName[signTmp.reg()]);

	releaseOperand(lhs);
}

bool InstSelectorRiscV64::tryTranslateDivByConstant(Instruction * inst)
{
	auto * binary = dynamic_cast<BinaryInst *>(inst);
	auto * divisorConst = binary != nullptr ? asConstInteger(binary->getRHS()) : nullptr;
	if (divisorConst == nullptr || divisorConst->getVal() == 0) {
		return false;
	}

	int dstReg = getResultReg(inst);
	LocalTempManager::Lease dstLease;
	if (dstReg < 0) {
		dstLease = tempMgr.borrow(inst);
		dstReg = dstLease.reg();
	}

	emitSignedConstDivQuotient(inst, binary->getLHS(), divisorConst->getVal(), dstReg);
	storeResult(inst, dstReg, inst);
	return true;
}

bool InstSelectorRiscV64::tryTranslateModByConstant(Instruction * inst)
{
	auto * binary = dynamic_cast<BinaryInst *>(inst);
	auto * divisorConst = binary != nullptr ? asConstInteger(binary->getRHS()) : nullptr;
	if (divisorConst == nullptr || divisorConst->getVal() == 0) {
		return false;
	}

	const int32_t divisor = divisorConst->getVal();
	if (divisor == 1 || divisor == -1) {
		int dstReg = getResultReg(inst);
		LocalTempManager::Lease dstLease;
		if (dstReg < 0) {
			dstLease = tempMgr.borrow(inst);
			dstReg = dstLease.reg();
		}
		iloc.load_imm(dstReg, 0);
		storeResult(inst, dstReg, inst);
		return true;
	}

	int dstReg = getResultReg(inst);
	LocalTempManager::Lease dstLease;
	if (dstReg < 0) {
		dstLease = tempMgr.borrow(inst);
		dstReg = dstLease.reg();
	}

	OperandReg lhs = loadOperand(binary->getLHS(), inst, dstReg);
	LocalTempManager::Lease quotientLease;
	int quotientReg = dstReg;
	if (quotientReg == lhs.reg) {
		quotientLease = tempMgr.borrowExcluding(inst, {lhs.reg, dstReg});
		quotientReg = quotientLease.reg();
	}
	emitSignedConstDivQuotient(inst, binary->getLHS(), divisor, quotientReg);

	auto product = tempMgr.borrowExcluding(inst, {lhs.reg, dstReg, quotientReg});
	iloc.load_imm(product.reg(), divisor);
	iloc.inst("mulw", PlatformRiscV64::regName[product.reg()],
	          PlatformRiscV64::regName[quotientReg],
	          PlatformRiscV64::regName[product.reg()]);
	iloc.inst("subw", PlatformRiscV64::regName[dstReg], PlatformRiscV64::regName[lhs.reg],
	          PlatformRiscV64::regName[product.reg()]);

	releaseOperand(lhs);
	storeResult(inst, dstReg, inst);
	return true;
}

/// @brief 翻译icmp指令（整数比较）
/// @param inst IR指令
///
/// 生成RISC-V整数比较指令：
/// slt/xori/sub+seqz/snez
void InstSelectorRiscV64::translate_icmp(Instruction * inst)
{
	auto * icmp = dynamic_cast<ICmpInst *>(inst);
	if (icmp == nullptr) {
		return;
	}

	int dstReg = getResultReg(inst);
	LocalTempManager::Lease dstLease;
	if (dstReg < 0) {
		dstLease = tempMgr.borrow(inst);
		dstReg = dstLease.reg();
	}
	const std::string dst = PlatformRiscV64::regName[dstReg];

	OperandReg lhsOperand = loadOperand(icmp->getLHS(), inst, dstReg);
	const int rhsPreferredReg = lhsOperand.reg != dstReg ? dstReg : -1;
	OperandReg rhsOperand = loadOperand(icmp->getRHS(), inst, rhsPreferredReg < 0 ? dstReg : -1, rhsPreferredReg);

	const std::string lhs = PlatformRiscV64::regName[lhsOperand.reg];
	const std::string rhs = PlatformRiscV64::regName[rhsOperand.reg];

	switch (inst->getOp()) {
		case IRInstOperator::IRINST_OP_LT_I:
			iloc.inst("slt", dst, lhs, rhs);
			break;
		case IRInstOperator::IRINST_OP_GT_I:
			iloc.inst("slt", dst, rhs, lhs);
			break;
		case IRInstOperator::IRINST_OP_LE_I:
			iloc.inst("slt", dst, rhs, lhs);
			iloc.inst("xori", dst, dst, "1");
			break;
		case IRInstOperator::IRINST_OP_GE_I:
			iloc.inst("slt", dst, lhs, rhs);
			iloc.inst("xori", dst, dst, "1");
			break;
		case IRInstOperator::IRINST_OP_EQ_I:
			iloc.inst("subw", dst, lhs, rhs);
			iloc.inst("seqz", dst, dst);
			break;
		case IRInstOperator::IRINST_OP_NE_I:
			iloc.inst("subw", dst, lhs, rhs);
			iloc.inst("snez", dst, dst);
			break;
		default:
			break;
	}

	releaseOperand(rhsOperand);
	releaseOperand(lhsOperand);

	storeResult(inst, dstReg, inst);
}

/// @brief 翻译fcmp指令（浮点比较）
/// @param inst IR指令
///
/// 先将操作数从整数寄存器移至FP寄存器，再使用F扩展比较指令
void InstSelectorRiscV64::translate_fcmp(Instruction * inst)
{
	auto * fcmp = dynamic_cast<FCmpInst *>(inst);
	if (fcmp == nullptr) {
		return;
	}

	int dstReg = getResultReg(inst);
	LocalTempManager::Lease dstLease;
	if (dstReg < 0) {
		dstLease = tempMgr.borrow(inst);
		dstReg = dstLease.reg();
	}
	const std::string dst = PlatformRiscV64::regName[dstReg];

	FloatOperandReg lhsOperand = loadFloatOperand(fcmp->getLHS(), inst);
	FloatOperandReg rhsOperand = loadFloatOperand(fcmp->getRHS(), inst, lhsOperand.reg);

	const std::string lhs = PlatformRiscV64::fpRegName[lhsOperand.reg];
	const std::string rhs = PlatformRiscV64::fpRegName[rhsOperand.reg];

	switch (inst->getOp()) {
		case IRInstOperator::IRINST_OP_LT_F:
			iloc.inst("flt.s", dst, lhs, rhs);
			break;
		case IRInstOperator::IRINST_OP_GT_F:
			iloc.inst("flt.s", dst, rhs, lhs);
			break;
		case IRInstOperator::IRINST_OP_LE_F:
			iloc.inst("fle.s", dst, lhs, rhs);
			break;
		case IRInstOperator::IRINST_OP_GE_F:
			iloc.inst("fle.s", dst, rhs, lhs);
			break;
		case IRInstOperator::IRINST_OP_EQ_F:
			iloc.inst("feq.s", dst, lhs, rhs);
			break;
		case IRInstOperator::IRINST_OP_NE_F:
			iloc.inst("feq.s", dst, lhs, rhs);
			iloc.inst("xori", dst, dst, "1");
			break;
		default:
			break;
	}

	releaseFloatOperand(rhsOperand);
	releaseFloatOperand(lhsOperand);

	storeResult(inst, dstReg, inst);
}

/// @brief 翻译br指令（无条件跳转）
/// @param inst IR指令
void InstSelectorRiscV64::translate_br(Instruction * inst)
{
	auto * br = dynamic_cast<BranchInst *>(inst);
	if (br != nullptr) {
		iloc.jump(blockLabel(br->getTarget()));
	}
}

bool InstSelectorRiscV64::isCompareOnlyUsedByCondBranch(ICmpInst * icmp) const
{
	if (icmp == nullptr) {
		return false;
	}

	const auto & uses = icmp->getUseList();
	if (uses.size() != 1 || uses.front() == nullptr) {
		return false;
	}

	auto * condBr = dynamic_cast<CondBranchInst *>(uses.front()->getUser());
	if (condBr == nullptr) {
		return false;
	}

	// 只有紧邻条件跳转的比较才能折叠为 RISC-V 条件分支。
	// LICM 等循环优化可能把 icmp 提前到循环外或支配块中，导致 icmp 与 cond_br
	// 不在同一基本块或不再紧邻。此时若仍折叠，条件分支处会重新读取 icmp 的
	// 操作数寄存器，但这些寄存器可能已被 icmp 与 cond_br 之间的其他指令覆盖，
	// 从而产生错误的比较结果。因此要求 icmp 和 cond_br 必须在同一基本块且
	// 指令序列上紧邻，方可安全折叠。
	BasicBlock * bb = icmp->getParentBlock();
	if (bb == nullptr || bb != condBr->getParentBlock()) {
		return false;
	}

	const auto & insts = bb->getInstructions();
	auto condIt = std::find(insts.begin(), insts.end(), static_cast<Instruction *>(condBr));
	if (condIt == insts.end() || condIt == insts.begin()) {
		return false;
	}

	auto prevIt = condIt;
	--prevIt;
	return *prevIt == static_cast<Instruction *>(icmp);
}

bool InstSelectorRiscV64::translateDirectIcmpBranch(ICmpInst * icmp, CondBranchInst * condBr)
{
	if (icmp == nullptr || condBr == nullptr || !isCompareOnlyUsedByCondBranch(icmp)) {
		return false;
	}

	OperandReg lhsOperand = loadOperand(icmp->getLHS(), condBr);
	OperandReg rhsOperand = loadOperand(icmp->getRHS(), condBr, lhsOperand.reg);

	const std::string lhs = PlatformRiscV64::regName[lhsOperand.reg];
	const std::string rhs = PlatformRiscV64::regName[rhsOperand.reg];
	const std::string trueLabel = blockLabel(condBr->getTrueDest());

	switch (icmp->getOp()) {
		case IRInstOperator::IRINST_OP_LT_I:
			iloc.inst("blt", lhs, rhs, trueLabel);
			break;
		case IRInstOperator::IRINST_OP_GT_I:
			iloc.inst("blt", rhs, lhs, trueLabel);
			break;
		case IRInstOperator::IRINST_OP_LE_I:
			iloc.inst("bge", rhs, lhs, trueLabel);
			break;
		case IRInstOperator::IRINST_OP_GE_I:
			iloc.inst("bge", lhs, rhs, trueLabel);
			break;
		case IRInstOperator::IRINST_OP_EQ_I:
			iloc.inst("beq", lhs, rhs, trueLabel);
			break;
		case IRInstOperator::IRINST_OP_NE_I:
			iloc.inst("bne", lhs, rhs, trueLabel);
			break;
		default:
			releaseOperand(rhsOperand);
			releaseOperand(lhsOperand);
			return false;
	}

	releaseOperand(rhsOperand);
	releaseOperand(lhsOperand);
	iloc.jump(blockLabel(condBr->getFalseDest()));
	return true;
}

/// @brief 翻译cond_br指令（条件跳转）
/// @param inst IR指令
///
/// 生成：bne cond, zero, trueLabel; j falseLabel
void InstSelectorRiscV64::translate_cond_br(Instruction * inst)
{
	auto * condBr = dynamic_cast<CondBranchInst *>(inst);
	if (condBr == nullptr) {
		return;
	}

	if (auto * icmp = dynamic_cast<ICmpInst *>(condBr->getCondition())) {
		if (translateDirectIcmpBranch(icmp, condBr)) {
			return;
		}
	}

	OperandReg cond = loadOperand(condBr->getCondition(), inst);
	iloc.inst("bne", PlatformRiscV64::regName[cond.reg], "zero", blockLabel(condBr->getTrueDest()));
	releaseOperand(cond);
	iloc.jump(blockLabel(condBr->getFalseDest()));
}

/// @brief 翻译ret指令（函数返回）
/// @param inst IR指令
///
/// 若有返回值，先加载到a0，然后生成epilogue
void InstSelectorRiscV64::translate_ret(Instruction * inst)
{
	auto * ret = dynamic_cast<ReturnInst *>(inst);
	if (ret != nullptr && ret->hasReturnValue()) {
		Value *retVal = ret->getReturnValue();
		Type *retType = retVal->getType();
		if (retType->isFloatType()) {
			FloatOperandReg value = loadFloatOperand(retVal, inst, -1, 10);
			if (value.reg != 10) {
				iloc.fmov_reg(10, value.reg);
			}
			releaseFloatOperand(value);
		} else {
			OperandReg value = loadOperand(retVal, inst, -1, RISCV64_A0_REG_NO);
			if (value.reg != RISCV64_A0_REG_NO) {
				iloc.mov_reg(RISCV64_A0_REG_NO, value.reg);
			}
			releaseOperand(value);
		}
	}

	// 生成函数epilogue：恢复callee-saved寄存器，恢复栈指针，返回
	emitEpilogue();
}

/// @brief 翻译call指令（函数调用）
/// @param inst IR指令
///
/// 生成：
/// 1. 超过8个的参数存储到栈上
/// 2. 前8个参数加载到a0-a7
/// 3. call指令
/// 4. 若有返回值，将a0存储到结果位置
void InstSelectorRiscV64::translate_call(Instruction * inst)
{
	auto * call = dynamic_cast<CallInst *>(inst);
	if (call == nullptr) {
		return;
	}

	if (tryTranslateRepeatedPowerOfTwoDivRemCall(call)) {
		return;
	}

	// RISC-V ABI：整数参数和浮点参数使用独立的寄存器计数器
	// 整数类型参数依次占用 a0-a7，浮点参数依次占用 fa0-fa7
	// 超出对应寄存器的参数通过栈传递（每个栈槽 8 字节对齐）
	{
		std::vector<AbiArgLoc> argLocs;
		std::vector<Type *> argTypes;
		std::vector<bool> variadicFloatArgs;
		argLocs.reserve(call->getArgCount());
		argTypes.reserve(call->getArgCount());
		variadicFloatArgs.reserve(call->getArgCount());
		int intIdx = 0, floatIdx = 0, stackIdx = 0;
		for (int i = 0; i < call->getArgCount(); ++i) {
			Value *arg = call->getArg(i);
			Type *argType = arg->getType();
			if (auto *alloca = dynamic_cast<AllocaInst *>(arg)) {
				argType = alloca->getAllocaType();
			}
			const bool variadicFloatArg = isVariadicFloatArg(call, i, argType);
			argTypes.push_back(argType);
			variadicFloatArgs.push_back(variadicFloatArg);
			argLocs.push_back(
				variadicFloatArg ? classifyVariadicFloatArg(intIdx, stackIdx)
				                 : classifyAbiArg(argType, intIdx, floatIdx, stackIdx));
		}

		auto emitVariadicFloatArg = [&](Value * arg, const AbiArgLoc & loc) {
			FloatOperandReg src = loadFloatOperand(arg, inst);
			const bool reuseSrcReg = src.temp;
			int promotedReg = src.reg;
			if (!reuseSrcReg) {
				promotedReg = borrowFloatTemp(inst, {src.reg});
			}

			// Variadic float args follow the integer calling convention after default promotion to double.
			iloc.inst("fcvt.d.s", PlatformRiscV64::fpRegName[promotedReg], PlatformRiscV64::fpRegName[src.reg]);

			if (loc.kind == AbiArgLocKind::IntReg) {
				iloc.inst("fmv.x.d", PlatformRiscV64::regName[RISCV64_A0_REG_NO + loc.index],
				          PlatformRiscV64::fpRegName[promotedReg]);
			} else {
				auto bitsLease = tempMgr.borrow(inst);
				iloc.inst("fmv.x.d", PlatformRiscV64::regName[bitsLease.reg()],
				          PlatformRiscV64::fpRegName[promotedReg]);
				const int stackOffset = loc.index * 8;
				if (PlatformRiscV64::isDisp(stackOffset)) {
					iloc.inst("sd", PlatformRiscV64::regName[bitsLease.reg()],
					          std::to_string(stackOffset) + "(" + PlatformRiscV64::regName[RISCV64_SP_REG_NO] + ")");
				} else {
					auto addrLease = tempMgr.borrow(inst, bitsLease.reg());
					iloc.store_base(bitsLease.reg(), RISCV64_SP_REG_NO, stackOffset, addrLease.reg(), true);
				}
			}

			if (!reuseSrcReg) {
				releaseFloatTemp(promotedReg);
			}
			releaseFloatOperand(src);
		};

		std::vector<FloatRegMove> floatRegMoves;
		std::vector<std::pair<Value *, int>> deferredFloatLoads;
		for (int i = 0; i < call->getArgCount(); ++i) {
			if (!argTypes[i]->isFloatType() || variadicFloatArgs[i]) {
				continue;
			}

			Value * arg = call->getArg(i);
			const AbiArgLoc & loc = argLocs[i];
			if (loc.kind == AbiArgLocKind::FloatReg) {
				const int destReg = 10 + loc.index;
				RegAllocInfo argInfo = getAllocInfo(arg, inst);
				if (argInfo.hasFloatReg()) {
					if (argInfo.regId != destReg) {
						floatRegMoves.push_back(FloatRegMove{
							FloatRegMove::SourceKind::FloatReg,
							argInfo.regId,
							destReg,
						});
					}
				} else {
					deferredFloatLoads.push_back({arg, destReg});
				}
			} else if (loc.kind == AbiArgLocKind::Stack) {
				RegAllocInfo argInfo = getAllocInfo(arg, inst);
				if (argInfo.hasFloatReg()) {
					auto tmp = tempMgr.borrow(inst);
					iloc.store_float_base(argInfo.regId, RISCV64_SP_REG_NO, loc.index * 8, tmp.reg());
				} else {
					const int tmpFpr = borrowFloatTemp(inst);
					auto tmp = tempMgr.borrow(inst);
					iloc.load_float_var(tmpFpr, arg, tmp.reg(), argInfo);
					iloc.store_float_base(tmpFpr, RISCV64_SP_REG_NO, loc.index * 8, tmp.reg());
					releaseFloatTemp(tmpFpr);
				}
			}
		}

		{
			std::set<int> blockedGprs;
			for (int reg = RISCV64_A0_REG_NO; reg < RISCV64_A0_REG_NO + 8; ++reg) {
				blockedGprs.insert(reg);
			}
			auto scratch = tempMgr.borrowExcluding(inst, blockedGprs);
			emitFloatRegMoves(floatRegMoves, scratch.reg());
		}

		for (const auto & [arg, destReg] : deferredFloatLoads) {
			auto tmp = tempMgr.borrow(inst);
			loadFloatValueToReg(destReg, arg, tmp.reg(), inst);
		}

		for (int i = 0; i < call->getArgCount(); ++i) {
			if (argTypes[i]->isFloatType() && !variadicFloatArgs[i]) {
				continue;
			}

			Value * arg = call->getArg(i);
			const AbiArgLoc & loc = argLocs[i];
			if (variadicFloatArgs[i]) {
				emitVariadicFloatArg(arg, loc);
				continue;
			}
			if (loc.kind == AbiArgLocKind::IntReg) {
				loadValueToReg(RISCV64_A0_REG_NO + loc.index, arg, inst);
			} else if (loc.kind == AbiArgLocKind::Stack) {
				OperandReg value = loadOperand(arg, inst);
				auto tmp = tempMgr.borrow(inst, value.reg);
				iloc.store_base(value.reg, RISCV64_SP_REG_NO, loc.index * 8, tmp.reg(),
				                arg->getType()->isPointerType());
				releaseOperand(value);
			}
		}
	}

	// 生成call指令
	iloc.call_fun(call->getCallee()->getName());

	// 若有返回值，将a0（或fa0→a0）存储到结果位置
	if (call->hasResultValue()) {
		if (call->getType()->isFloatType()) {
			storeFloatResult(call, 10, inst);
			return;
		}
		storeResult(call, RISCV64_A0_REG_NO, inst);
	}
}

bool InstSelectorRiscV64::tryTranslateRepeatedPowerOfTwoDivRemCall(CallInst * call)
{
	// 尝试匹配被调函数为"重复除以2的幂再取模"惯用法
	RepeatedPowerOfTwoDivRemIdiom idiom;
	if (call == nullptr || call->getCallee() == nullptr || !call->hasResultValue() || call->getType() == nullptr ||
	    !call->getType()->isInt32Type() || !matchRepeatedPowerOfTwoDivRemIdiom(call->getCallee(), idiom)) {
		return false;
	}

	// 将惯用法中的形参来源映射到调用点的实参
	Value * dividendArg = resolveCallSource(call->getCallee(), call, idiom.dividendSource);
	Value * countArg = resolveCallSource(call->getCallee(), call, idiom.countSource);
	if (!isInt32Value(dividendArg) || !isInt32Value(countArg)) {
		return false;
	}

	// 获取或分配结果寄存器
	int dstReg = getResultReg(call);
	LocalTempManager::Lease dstLease;
	if (dstReg < 0) {
		dstLease = tempMgr.borrow(call);
		dstReg = dstLease.reg();
	}

	// 加载循环次数到寄存器，若与结果寄存器冲突则复制一份
	OperandReg count = loadOperand(countArg, call);
	if (count.reg == dstReg) {
		auto countCopy = tempMgr.borrowExcluding(call, {dstReg});
		iloc.mov_reg(countCopy.reg(), count.reg);
		releaseOperand(count);
		count = OperandReg(std::move(countCopy));
	}

	// 加载被除数到结果寄存器
	OperandReg dividend = loadOperand(dividendArg, call, dstReg);
	if (dividend.reg != dstReg) {
		iloc.mov_reg(dstReg, dividend.reg);
	}
	releaseOperand(dividend);

	// 分配临时寄存器：shift（移位量）、mask（掩码）、sign（符号位）
	std::set<int> excluded = {dstReg, count.reg};
	auto shift = tempMgr.borrowExcluding(call, excluded);
	excluded.insert(shift.reg());
	auto mask = tempMgr.borrowExcluding(call, excluded);
	excluded.insert(mask.reg());
	auto sign = tempMgr.borrowExcluding(call, excluded);

	// 构造各标签和寄存器名称
	const std::string dstName = PlatformRiscV64::regName[dstReg];
	const std::string countName = PlatformRiscV64::regName[count.reg];
	const std::string shiftName = PlatformRiscV64::regName[shift.reg()];
	const std::string maskName = PlatformRiscV64::regName[mask.reg()];
	const std::string signName = PlatformRiscV64::regName[sign.reg()];
	const std::string labelBase = ".L_" + func->getName() + "_pow2_divrem_" +
	                              std::to_string(iloc.getMachineInstCount());
	const std::string divLabel = labelBase + "_div";
	const std::string modLabel = labelBase + "_mod";
	const std::string modDoneLabel = labelBase + "_mod_done";
	const std::string zeroLabel = labelBase + "_zero";
	const std::string doneLabel = labelBase + "_done";
	// 当count超过此阈值时，连续右移必然归零
	const int zeroThreshold = 31 / idiom.divisorShift + 1;

	// 若count <= 0，跳过除法直接进入取模
	iloc.inst("bge", "zero", countName, modLabel);
	// 若count超过阈值，结果必为0
	iloc.load_imm(shift.reg(), zeroThreshold);
	iloc.inst("bge", countName, shiftName, zeroLabel);
	// 计算实际移位量 = count * divisorShift
	if (idiom.divisorShift == 1) {
		iloc.mov_reg(shift.reg(), count.reg);
	} else if (isPowerOfTwo(static_cast<uint64_t>(idiom.divisorShift))) {
		iloc.inst("slliw", shiftName, countName,
		          std::to_string(log2PowerOfTwo(static_cast<uint64_t>(idiom.divisorShift))));
	} else {
		iloc.load_imm(sign.reg(), idiom.divisorShift);
		iloc.inst("mulw", shiftName, countName, signName);
	}
	// 提取符号位，若被除数非负则直接做算术右移
	iloc.inst("sraiw", signName, dstName, "31");
	iloc.inst("beq", signName, "zero", divLabel);
	// 被除数为负时，先加上 (1<<shift)-1 再右移，实现向零截断语义
	iloc.load_imm(mask.reg(), 1);
	iloc.inst("sllw", maskName, maskName, shiftName);
	iloc.inst("addiw", maskName, maskName, "-1");
	iloc.inst("addw", dstName, dstName, maskName);
	iloc.label(divLabel);
	// 算术右移完成除法
	iloc.inst("sraw", dstName, dstName, shiftName);
	iloc.label(modLabel);
	// 取模：用低位掩码获取余数
	const int32_t remMask = idiom.divisor - 1;
	if (PlatformRiscV64::constExpr(remMask)) {
		iloc.inst("andi", shiftName, dstName, std::to_string(remMask));
	} else {
		iloc.load_imm(mask.reg(), remMask);
		iloc.inst("and", shiftName, dstName, maskName);
	}
	// 修正负数的余数：若被除数非负或余数已为0则无需修正
	iloc.inst("bge", dstName, "zero", modDoneLabel);
	iloc.inst("beq", shiftName, "zero", modDoneLabel);
	// 余数为负时加上除数使其为正
	if (PlatformRiscV64::constExpr(-idiom.divisor)) {
		iloc.inst("addiw", shiftName, shiftName, std::to_string(-idiom.divisor));
	} else {
		iloc.load_imm(mask.reg(), idiom.divisor);
		iloc.inst("subw", shiftName, shiftName, maskName);
	}
	iloc.label(modDoneLabel);
	iloc.mov_reg(dstReg, shift.reg());
	iloc.jump(doneLabel);
	// count超过阈值时结果为0
	iloc.label(zeroLabel);
	iloc.load_imm(dstReg, 0);
	iloc.label(doneLabel);

	sign.release();
	mask.release();
	shift.release();
	releaseOperand(count);

	storeResult(call, dstReg, call);
	return true;
}

/// @brief 翻译phi指令（φ节点）
/// @param inst IR指令
///
/// Phi节点已在PhiLowering pass中消除，此处为空实现
void InstSelectorRiscV64::translate_phi(Instruction * inst)
{
	(void) inst;
}

/// @brief 翻译zext指令（零扩展）
/// @param inst IR指令
///
/// 生成：andi dst, src, 1（将1位值零扩展到目标位宽）
void InstSelectorRiscV64::translate_zext(Instruction * inst)
{
	auto * zext = dynamic_cast<ZExtInst *>(inst);
	if (zext == nullptr) {
		return;
	}

	int dstReg = getResultReg(inst);
	LocalTempManager::Lease dstLease;
	if (dstReg < 0) {
		dstLease = tempMgr.borrow(inst);
		dstReg = dstLease.reg();
	}

	OperandReg src = loadOperand(zext->getSource(), inst, -1, dstReg);
	iloc.inst("andi", PlatformRiscV64::regName[dstReg], PlatformRiscV64::regName[src.reg], "1");
	releaseOperand(src);
	storeResult(inst, dstReg, inst);
}

/// @brief 翻译copy指令（寄存器复制）
/// @param inst IR指令
///
/// 生成：加载源值到临时寄存器，存储到目标位置
void InstSelectorRiscV64::translate_copy(Instruction * inst)
{
	auto * copy = dynamic_cast<CopyInst *>(inst);
	if (copy == nullptr) {
		return;
	}

	// 跳过已被寄存器合并消除的 copy 指令
	if (eliminatedCopies_.find(inst) != eliminatedCopies_.end()) {
		return;
	}

	Value * dst = copy->getDst() != nullptr ? copy->getDst() : static_cast<Value *>(copy);
	if (isFloatValue(dst) || isFloatValue(copy->getSource())) {
		FloatOperandReg src = loadFloatOperand(copy->getSource(), inst);
		storeFloatResult(dst, src.reg, inst);
		releaseFloatOperand(src);
		return;
	}

	OperandReg src = loadOperand(copy->getSource(), inst);
	storeResult(dst, src.reg, inst);
	releaseOperand(src);
}

void InstSelectorRiscV64::emitFloatRegMoves(std::vector<FloatRegMove> & regMoves, int scratchGpr)
{
	while (!regMoves.empty()) {
		bool progressed = false;
		for (auto it = regMoves.begin(); it != regMoves.end(); ++it) {
			bool dstIsStillFloatSource = false;
			for (const auto & move : regMoves) {
				if (move.sourceKind == FloatRegMove::SourceKind::FloatReg && move.src == it->dst) {
					dstIsStillFloatSource = true;
					break;
				}
			}

			if (dstIsStillFloatSource) {
				continue;
			}

			if (it->sourceKind == FloatRegMove::SourceKind::FloatReg) {
				if (it->src != it->dst) {
					iloc.fmov_reg(it->dst, it->src);
				}
			} else {
				iloc.inst("fmv.w.x", PlatformRiscV64::fpRegName[it->dst], PlatformRiscV64::regName[it->src]);
			}
			regMoves.erase(it);
			progressed = true;
			break;
		}

		if (progressed) {
			continue;
		}

		const int cycleSrc = regMoves.front().src;
		iloc.inst("fmv.x.w", PlatformRiscV64::regName[scratchGpr], PlatformRiscV64::fpRegName[cycleSrc]);
		for (auto & move : regMoves) {
			if (move.sourceKind == FloatRegMove::SourceKind::FloatReg && move.src == cycleSrc) {
				move.sourceKind = FloatRegMove::SourceKind::Gpr;
				move.src = scratchGpr;
			}
		}
	}
}

/// @brief 生成形参从ABI寄存器到分配位置的移动指令
///
/// RISC-V调用约定：整数和浮点参数使用独立的寄存器计数器。
/// 整数参数依次占用 a0-a7，浮点参数依次占用 fa0-fa7。
/// 超出对应寄存器数量的参数通过栈传递。
void InstSelectorRiscV64::emitFormalParamMoves()
{
	auto & params = func->getParams();
	std::vector<AbiArgLoc> paramLocs;
	paramLocs.reserve(params.size());

	int intRegCount = 0;
	int floatRegCount = 0;
	int stackCount = 0;
	for (auto * param: params) {
		paramLocs.push_back(classifyAbiArg(param->getType(), intRegCount, floatRegCount, stackCount));
	}

	std::set<int> blockedRegs;
	std::vector<RegMove> regMoves;

	// 收集实际使用的整数入参寄存器
	{
		int intIdx = 0;
		for (auto * param : params) {
			if (!param->getType()->isFloatType() && intIdx < 8) {
				blockedRegs.insert(RISCV64_A0_REG_NO + intIdx);
				intIdx++;
			}
		}
	}
	// 收集寄存器分配器为目标分配的所有寄存器
	for (auto * param : params) {
		RegAllocInfo info = getAllocInfoAt(param, 0);
		if (info.hasReg()) {
			blockedRegs.insert(info.regId);
		}
	}

	// 通过tempMgr借用scratch寄存器（自动走scratch虚拟寄存器流程）
	auto scratchLease = tempMgr.borrowExcluding(nullptr, blockedRegs);
	int scratchReg = scratchLease.reg();

	// 先处理整数入参，避免后续float形参落到a0-a7时覆盖尚未搬走的整数实参。
	{
		for (std::size_t pi = 0; pi < params.size(); ++pi) {
			auto * param = params[pi];
			if (param->getType()->isFloatType()) {
				continue;
			}

			const AbiArgLoc & loc = paramLocs[pi];
			RegAllocInfo info = getAllocInfoAt(param, 0);
			if (loc.kind == AbiArgLocKind::IntReg) {
				const int incomingReg = RISCV64_A0_REG_NO + loc.index;
				if (info.hasReg()) {
					if (info.regId != incomingReg) {
						regMoves.push_back(RegMove{incomingReg, info.regId});
					}
				} else if (info.hasStackSlot) {
					iloc.store_base(incomingReg, info.baseRegId, info.offset,
					                scratchReg, param->getType()->isPointerType());
				}
			}
		}
	}

	// 解析寄存器移动依赖（含循环依赖的打破）
	while (!regMoves.empty()) {
		bool progressed = false;
		for (auto it = regMoves.begin(); it != regMoves.end(); ++it) {
			bool dstIsStillSource = false;
			for (const auto & move : regMoves) {
				if (move.src == it->dst) {
					dstIsStillSource = true;
					break;
				}
			}

			if (dstIsStillSource) {
				continue;
			}

			iloc.mov_reg(it->dst, it->src);
			regMoves.erase(it);
			progressed = true;
			break;
		}

		if (progressed) {
			continue;
		}

		const int cycleSrc = regMoves.front().src;
		iloc.mov_reg(scratchReg, cycleSrc);
		for (auto & move : regMoves) {
			if (move.src == cycleSrc) {
				move.src = scratchReg;
			}
		}
	}

	// 栈传整数参数在 a0-a7 全部落位后再搬运，避免覆盖尚未处理的入参寄存器。
	for (std::size_t pi = 0; pi < params.size(); ++pi) {
		auto * param = params[pi];
		if (param->getType()->isFloatType() || paramLocs[pi].kind != AbiArgLocKind::Stack) {
			continue;
		}
		RegAllocInfo info = getAllocInfoAt(param, 0);
		const int stackOffset = paramLocs[pi].index * 8;
		if (info.hasReg()) {
			iloc.load_base(info.regId, RISCV64_FP_REG_NO, stackOffset, param->getType()->isPointerType());
		} else if (info.hasStackSlot &&
		           (info.baseRegId != RISCV64_FP_REG_NO || info.offset != stackOffset)) {
			auto tmp = tempMgr.borrow(nullptr, info.regId);
			iloc.load_base(tmp.reg(), RISCV64_FP_REG_NO, stackOffset, param->getType()->isPointerType());
			iloc.store_base(tmp.reg(), info.baseRegId, info.offset, scratchReg,
			                param->getType()->isPointerType());
		}
	}

	// 整数入参已经安全落位后，再处理浮点入参。
	// 浮点入参通过fa0-fa7传递，使用fmv.x.w将浮点寄存器的位模式移动到整数寄存器
	{
		std::vector<FloatRegMove> floatRegMoves;
		for (std::size_t pi = 0; pi < params.size(); ++pi) {
			auto * param = params[pi];
			if (!param->getType()->isFloatType()) {
				continue;
			}

			const AbiArgLoc & loc = paramLocs[pi];
			RegAllocInfo info = getAllocInfoAt(param, 0);
			if (loc.kind == AbiArgLocKind::FloatReg) {
				// fa0-fa7: 浮点参数寄存器
				const std::string fpReg = "fa" + std::to_string(loc.index);
				if (info.hasFloatReg()) {
					if (info.regId != 10 + loc.index) {
						floatRegMoves.push_back(FloatRegMove{
							FloatRegMove::SourceKind::FloatReg,
							10 + loc.index,
							info.regId,
						});
					}
				} else if (info.hasReg()) {
					// 目标分配了整数寄存器，用fmv.x.w将浮点寄存器位模式移入整数寄存器
					iloc.inst("fmv.x.w", PlatformRiscV64::regName[info.regId], fpReg);
				} else if (info.hasStackSlot) {
					iloc.store_float_base(10 + loc.index, info.baseRegId, info.offset, scratchReg);
				}
			}
		}
		emitFloatRegMoves(floatRegMoves, scratchReg);
	}

	for (std::size_t pi = 0; pi < params.size(); ++pi) {
		auto * param = params[pi];
		if (!param->getType()->isFloatType() || paramLocs[pi].kind != AbiArgLocKind::Stack) {
			continue;
		}
		RegAllocInfo info = getAllocInfoAt(param, 0);
		const int stackOffset = paramLocs[pi].index * 8;
		if (info.hasFloatReg()) {
			iloc.load_float_base(info.regId, RISCV64_FP_REG_NO, stackOffset, scratchReg);
		} else if (info.hasReg()) {
			iloc.load_base(info.regId, RISCV64_FP_REG_NO, stackOffset, false);
		} else if (info.hasStackSlot &&
		           (info.baseRegId != RISCV64_FP_REG_NO || info.offset != stackOffset)) {
			auto tmp = tempMgr.borrow(nullptr);
			iloc.load_base(tmp.reg(), RISCV64_FP_REG_NO, stackOffset, false);
			iloc.store_base(tmp.reg(), info.baseRegId, info.offset, scratchReg, false);
		}
	}
}

/// @brief 生成函数epilogue
///
/// 恢复callee-saved寄存器（逆序），恢复栈指针，执行ret指令
void InstSelectorRiscV64::emitEpilogue()
{
	const int frameSize = allocator.getFrameSize();
	// 获取当前函数实际需要保存的callee-saved寄存器列表
	const auto & savedRegs = iloc.getSavedRegs();
	if (frameSize == 0 && savedRegs.empty()) {
		iloc.inst("ret", "");
		return;
	}

	auto tmp = tempMgr.borrow(nullptr);

	// 逆序恢复callee-saved寄存器（与prologue中保存顺序相反）
	for (int i = static_cast<int>(savedRegs.size()) - 1; i >= 0; --i) {
		const int offset = frameSize - (i + 1) * 8;
		// 通过寄存器编号查找对应的寄存器名称
		emitLoad64(PlatformRiscV64::regName[savedRegs[i]], offset, tmp.reg());
	}

	// 逆序恢复callee-saved FPR（与prologue中保存顺序相反）
	// 使用fld指令恢复双精度浮点值
	const auto & savedFPRs = iloc.getSavedFPRs();
	const int gprSavedCount = static_cast<int>(savedRegs.size());
	for (int i = static_cast<int>(savedFPRs.size()) - 1; i >= 0; --i) {
		const int offset = frameSize - (gprSavedCount + i + 1) * 8;
		const std::string & fpReg = PlatformRiscV64::fpRegName[savedFPRs[i]];
		if (PlatformRiscV64::isDisp(offset)) {
			iloc.inst("fld", fpReg, std::to_string(offset) + "(sp)");
		} else {
			iloc.load_imm(tmp.reg(), offset);
			iloc.inst("add", PlatformRiscV64::regName[tmp.reg()], "sp", PlatformRiscV64::regName[tmp.reg()]);
			iloc.inst("fld", fpReg, "0(" + PlatformRiscV64::regName[tmp.reg()] + ")");
		}
	}

	// 恢复栈指针
	emitStackAdjust(frameSize, tmp.reg());
	// 返回指令
	iloc.inst("ret", "");
}

/// @brief 生成64位加载指令（ld），处理大偏移情况
/// @param reg 寄存器名
/// @param offset 栈偏移
void InstSelectorRiscV64::emitLoad64(const std::string & reg, int offset, int tmpReg)
{
	if (PlatformRiscV64::isDisp(offset)) {
		iloc.inst("ld", reg, std::to_string(offset) + "(sp)");
		return;
	}

	// 偏移超出12位范围，通过临时寄存器计算地址
	iloc.load_imm(tmpReg, offset);
	iloc.inst("add", PlatformRiscV64::regName[tmpReg], "sp", PlatformRiscV64::regName[tmpReg]);
	iloc.inst("ld", reg, "0(" + PlatformRiscV64::regName[tmpReg] + ")");
}

/// @brief 生成栈指针调整指令，处理大偏移情况
/// @param amount 调整量
/// @param tmpReg 临时寄存器编号（用于大偏移地址计算）
void InstSelectorRiscV64::emitStackAdjust(int amount, int tmpReg)
{
	if (PlatformRiscV64::constExpr(amount)) {
		iloc.inst("addi", "sp", "sp", std::to_string(amount));
		return;
	}

	// 偏移超出12位范围，通过临时寄存器加载
	iloc.load_imm(tmpReg, amount);
	iloc.inst("add", "sp", "sp", PlatformRiscV64::regName[tmpReg]);
}

/// @brief 获取Value分配的结果寄存器编号
/// @param val IR值
/// @return 物理寄存器编号，若未分配则返回-1
int InstSelectorRiscV64::getResultReg(Value * val) const
{
	auto * inst = dynamic_cast<Instruction *>(val);
	RegAllocInfo info = getAllocInfo(val, inst);
	if (info.hasReg()) {
		return info.regId;
	}
	return -1;
}

int InstSelectorRiscV64::getFloatResultReg(Value * val) const
{
	auto * inst = dynamic_cast<Instruction *>(val);
	RegAllocInfo info = getAllocInfo(val, inst);
	if (info.hasFloatReg()) {
		return info.regId;
	}
	return -1;
}

RegAllocInfo InstSelectorRiscV64::getAllocInfo(Value * val, Instruction * inst) const
{
	return allocator.getAllocationInfo(val, inst);
}

RegAllocInfo InstSelectorRiscV64::getAllocInfoAt(Value * val, int instNum) const
{
	return allocator.getAllocationInfoAt(val, instNum);
}

void InstSelectorRiscV64::loadValueToReg(int reg, Value * val, Instruction * inst)
{
	iloc.load_var(reg, val, getAllocInfo(val, inst));
}

void InstSelectorRiscV64::loadFloatValueToReg(int reg, Value * val, int tmpReg, Instruction * inst)
{
	iloc.load_float_var(reg, val, tmpReg, getAllocInfo(val, inst));
}

void InstSelectorRiscV64::storeValueFromReg(Value * val, int srcReg, int tmpReg, Instruction * inst)
{
	iloc.store_var(srcReg, val, tmpReg, getAllocInfo(val, inst));
}

void InstSelectorRiscV64::storeFloatValueFromReg(Value * val, int srcReg, int tmpReg, Instruction * inst)
{
	iloc.store_float_var(srcReg, val, tmpReg, getAllocInfo(val, inst));
}

/// @brief 获取只读操作数所在寄存器，必要时借用临时寄存器加载
/// @param val 操作数
/// @param inst 当前IR指令
/// @param excludeReg 借用临时寄存器时需要排除的寄存器
/// @param preferredReg 可直接承载该操作数的首选寄存器
/// @return 操作数寄存器及是否需要释放
InstSelectorRiscV64::OperandReg
InstSelectorRiscV64::loadOperand(Value * val, Instruction * inst, int excludeReg, int preferredReg)
{
	RegAllocInfo info = getAllocInfo(val, inst);
	if (info.hasReg()) {
		return OperandReg(info.regId);
	}

	if (preferredReg >= 0 && preferredReg != excludeReg) {
		iloc.load_var(preferredReg, val, info);
		return OperandReg(preferredReg);
	}

	auto reg = tempMgr.borrow(inst, excludeReg);
	iloc.load_var(reg.reg(), val, info);
	return OperandReg(std::move(reg));
}

InstSelectorRiscV64::FloatOperandReg
InstSelectorRiscV64::loadFloatOperand(Value * val, Instruction * inst, int excludeReg, int preferredReg)
{
	RegAllocInfo info = getAllocInfo(val, inst);
	if (info.hasFloatReg()) {
		return FloatOperandReg(info.regId, false);
	}

	int reg = -1;
	bool temp = false;
	if (preferredReg >= 0 && preferredReg != excludeReg && !isFloatRegLiveAt(preferredReg, inst) &&
	    borrowedFloatTemps.find(preferredReg) == borrowedFloatTemps.end()) {
		reg = preferredReg;
	} else {
		reg = borrowFloatTemp(inst, {excludeReg});
		temp = true;
	}

	auto tmp = tempMgr.borrow(inst);
	iloc.load_float_var(reg, val, tmp.reg(), info);
	return FloatOperandReg(reg, temp, std::move(tmp));
}

/// @brief 释放通过loadOperand借用的临时寄存器
void InstSelectorRiscV64::releaseOperand(OperandReg & operand)
{
	operand.lease.release();
}

void InstSelectorRiscV64::releaseFloatOperand(FloatOperandReg & operand)
{
	operand.gprLease.release();
	if (operand.temp && operand.reg >= 0) {
		releaseFloatTemp(operand.reg);
		operand.temp = false;
		operand.reg = -1;
	}
}

/// @brief 将寄存器值存储到Value的目标位置
/// @param val 目标Value
/// @param srcReg 源寄存器编号
void InstSelectorRiscV64::storeResult(Value * val, int srcReg, Instruction * inst)
{
	if (val == nullptr) {
		return;
	}
	RegAllocInfo info = getAllocInfo(val, inst);
	if (info.hasReg()) {
		if (srcReg != info.regId) {
			iloc.mov_reg(info.regId, srcReg);
		}
		return;
	}

	auto tmp = tempMgr.borrowAfterUses(inst, srcReg);
	iloc.store_var(srcReg, val, tmp.reg(), info);
}

void InstSelectorRiscV64::storeFloatResult(Value * val, int srcReg, Instruction * inst)
{
	if (val == nullptr) {
		return;
	}

	RegAllocInfo info = getAllocInfo(val, inst);
	if (info.hasFloatReg()) {
		if (srcReg != info.regId) {
			iloc.fmov_reg(info.regId, srcReg);
		}
		return;
	}

	auto tmp = tempMgr.borrowAfterUses(inst);
	iloc.store_float_var(srcReg, val, tmp.reg(), info);
}

int InstSelectorRiscV64::borrowFloatTemp(Instruction * inst, const std::set<int> & excludeRegs)
{
	std::vector<int> candidates = {
		30, // ft10: reserved scratch FPR
		31, // ft11: reserved scratch FPR
	};
	for (int reg : allocator.getAvailableFloatRegs()) {
		if (std::find(candidates.begin(), candidates.end(), reg) == candidates.end()) {
			candidates.push_back(reg);
		}
	}

	for (int reg : candidates) {
		if (excludeRegs.find(reg) != excludeRegs.end()) {
			continue;
		}
		if (borrowedFloatTemps.find(reg) != borrowedFloatTemps.end()) {
			continue;
		}
		if (isFloatRegLiveAt(reg, inst)) {
			continue;
		}
		borrowedFloatTemps.insert(reg);
		return reg;
	}

	std::fprintf(stderr, "InstSelectorRiscV64: 无可用的临时浮点寄存器！\n");
	std::abort();
}

void InstSelectorRiscV64::releaseFloatTemp(int reg)
{
	borrowedFloatTemps.erase(reg);
}

bool InstSelectorRiscV64::isFloatRegLiveAt(int reg, Instruction * inst) const
{
	if (inst == nullptr) {
		return false;
	}

	auto instIt = allocator.getInstNumbering().find(inst);
	if (instIt == allocator.getInstNumbering().end()) {
		return false;
	}
	const int instNum = instIt->second;

	auto rangesIt = allocator.getAllocatedFprLiveRanges().find(reg);
	if (rangesIt == allocator.getAllocatedFprLiveRanges().end()) {
		return false;
	}

	for (const auto & [start, end] : rangesIt->second) {
		if (start <= instNum && instNum < end) {
			return true;
		}
	}

	return false;
}

/// @brief 生成基本块对应的标签名
/// @param bb 基本块
/// @return 格式为 ".L_函数名_基本块名" 的标签
std::string InstSelectorRiscV64::blockLabel(BasicBlock * bb) const
{
	return ".L_" + sanitizeLabelPart(func->getName()) + "_" + sanitizeLabelPart(bb->getIRName());
}

/// @brief 清理标签名中的非法字符，只保留字母数字和下划线
/// @param text 原始文本
/// @return 清理后的标签名，若为空则返回"bb"
std::string InstSelectorRiscV64::sanitizeLabelPart(const std::string & text) const
{
	std::string result;
	for (unsigned char ch: text) {
		if (std::isalnum(ch) || ch == '_') {
			result.push_back(static_cast<char>(ch));
		}
	}
	if (result.empty()) {
		return "bb";
	}
	return result;
}
