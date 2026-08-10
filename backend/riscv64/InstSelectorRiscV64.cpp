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
#include <fstream>
#include <functional>
#include <limits>
#include <set>
#include <unordered_set>
#include <vector>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "ConstFloat.h"
#include "Values/FormalParam.h"
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
#include "Rematerialization.h"
#include "ReturnInst.h"
#include "SelectInst.h"
#include "SIToFPInst.h"
#include "StoreInst.h"
#include "Use.h"
#include "Value.h"
#include "VectorInst.h"
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

/// @brief 计算形参在 ABI 中的整数寄存器编号（a0-a7）
/// @param func 所在函数
/// @param param 待查询形参
/// @return 整数参数寄存器编号；浮点或第 9 个及以后的整数参数返回 -1
int abiIntParamReg(Function * func, FormalParam * param)
{
	if (func == nullptr || param == nullptr || param->getType()->isFloatType()) {
		return -1;
	}
	int intRegCount = 0;
	int floatRegCount = 0;
	int stackCount = 0;
	for (auto * p : func->getParams()) {
		AbiArgLoc loc = classifyAbiArg(p->getType(), intRegCount, floatRegCount, stackCount);
		if (p == param) {
			return loc.kind == AbiArgLocKind::IntReg ? RISCV64_A0_REG_NO + loc.index : -1;
		}
	}
	return -1;
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


bool sameRegAllocInfo(const RegAllocInfo & lhs, const RegAllocInfo & rhs)
{
	return lhs.regId == rhs.regId &&
	       lhs.baseRegId == rhs.baseRegId &&
	       lhs.offset == rhs.offset &&
	       lhs.hasStackSlot == rhs.hasStackSlot &&
	       lhs.isFloatReg == rhs.isFloatReg &&
	       lhs.isVectorReg == rhs.isVectorReg;
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
	// 支持所有2的幂除法，包括2^31 (最大正整数+1的绝对值)
	// shift范围: 1到31 (2^1=2 到 2^31=2147483648)
	return shift > 0 && shift <= 31;
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
	translatorHandlers[IRInstOperator::IRINST_OP_SHL_I] = &InstSelectorRiscV64::translate_shl;
	translatorHandlers[IRInstOperator::IRINST_OP_ASHR_I] = &InstSelectorRiscV64::translate_ashr;
	translatorHandlers[IRInstOperator::IRINST_OP_LSHR_I] = &InstSelectorRiscV64::translate_lshr;
	translatorHandlers[IRInstOperator::IRINST_OP_AND_I] = &InstSelectorRiscV64::translate_and;
	translatorHandlers[IRInstOperator::IRINST_OP_OR_I] = &InstSelectorRiscV64::translate_or;
	translatorHandlers[IRInstOperator::IRINST_OP_XOR_I] = &InstSelectorRiscV64::translate_xor;
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
	translatorHandlers[IRInstOperator::IRINST_OP_SELECT] = &InstSelectorRiscV64::translate_select;
	translatorHandlers[IRInstOperator::IRINST_OP_ZEXT] = &InstSelectorRiscV64::translate_zext;
	translatorHandlers[IRInstOperator::IRINST_OP_COPY] = &InstSelectorRiscV64::translate_copy;
	translatorHandlers[IRInstOperator::IRINST_OP_GEP] = &InstSelectorRiscV64::translate_gep;
	translatorHandlers[IRInstOperator::IRINST_OP_VSETVL] = &InstSelectorRiscV64::translate_vsetvl;
	translatorHandlers[IRInstOperator::IRINST_OP_VLOAD] = &InstSelectorRiscV64::translate_vload;
	translatorHandlers[IRInstOperator::IRINST_OP_VSTORE] = &InstSelectorRiscV64::translate_vstore;
	translatorHandlers[IRInstOperator::IRINST_OP_VSPLAT] = &InstSelectorRiscV64::translate_vsplat;
	translatorHandlers[IRInstOperator::IRINST_OP_VBINARY] = &InstSelectorRiscV64::translate_vbinary;
	translatorHandlers[IRInstOperator::IRINST_OP_VREDUCE] = &InstSelectorRiscV64::translate_vreduce;
	translatorHandlers[IRInstOperator::IRINST_OP_VEXTRACT] = &InstSelectorRiscV64::translate_vextract;
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

/// @brief 计算优化的基本块布局顺序，最小化无条件跳转
///
/// 策略：
/// 1. 从入口块开始，优先选择后继中的循环回边（热路径）
/// 2. 对于条件分支，优先选择更可能执行的分支（启发式）
/// 3. 将冷块（错误处理、异常退出）推迟到最后
///
/// 启发式规则：
/// - 循环回边 > 循环体内跳转 > 顺序后继 > 循环退出
/// - 非零比较更可能为真
/// - 循环继续比循环退出更热
std::vector<BasicBlock *> computeOptimalBlockOrder(Function * func)
{
	std::vector<BasicBlock *> ordered;
	std::unordered_set<BasicBlock *> placed;

	auto blocks = func->getBlocks();
	if (blocks.empty()) {
		return ordered;
	}

	// 构建前驱-后继关系
	std::unordered_map<BasicBlock *, std::vector<BasicBlock *>> successors;
	std::unordered_map<BasicBlock *, std::vector<BasicBlock *>> predecessors;
	std::unordered_set<BasicBlock *> loopHeaders;

	for (auto * bb : blocks) {
		auto term = bb->getTerminator();
		if (auto * br = dynamic_cast<BranchInst *>(term)) {
			// 无条件跳转
			successors[bb].push_back(br->getTarget());
			predecessors[br->getTarget()].push_back(bb);
		} else if (auto * cbr = dynamic_cast<CondBranchInst *>(term)) {
			// 条件跳转
			successors[bb].push_back(cbr->getTrueDest());
			successors[bb].push_back(cbr->getFalseDest());
			predecessors[cbr->getTrueDest()].push_back(bb);
			predecessors[cbr->getFalseDest()].push_back(bb);
		}
	}

	// 检测循环头：具有前驱在其后的基本块（简单的回边检测）
	for (auto * bb : blocks) {
		for (auto * pred : predecessors[bb]) {
			// 如果前驱在CFG中"晚于"当前块，可能是回边
			auto it1 = std::find(blocks.begin(), blocks.end(), bb);
			auto it2 = std::find(blocks.begin(), blocks.end(), pred);
			if (it1 != blocks.end() && it2 != blocks.end() && it2 > it1) {
				loopHeaders.insert(bb);
			}
		}
	}

	// 贪心链构建
	std::function<void(BasicBlock *)> placeBlock = [&](BasicBlock * bb) {
		if (placed.count(bb)) {
			return;
		}

		ordered.push_back(bb);
		placed.insert(bb);

		// 选择最佳后继
		auto & succs = successors[bb];
		if (succs.empty()) {
			return;
		}

		BasicBlock * bestSucc = nullptr;
		int bestScore = -1000;

		for (auto * succ : succs) {
			if (placed.count(succ)) {
				continue;
			}

			int score = 0;

			// 回边到循环头：最高优先级（热路径）
			if (loopHeaders.count(succ)) {
				score += 100;
			}

			// 后继只有一个前驱（链式）：高优先级
			if (predecessors[succ].size() == 1) {
				score += 50;
			}

			// 后继有多个前驱（汇合点）：中优先级
			if (predecessors[succ].size() > 1) {
				score += 20;
			}

			if (score > bestScore) {
				bestScore = score;
				bestSucc = succ;
			}
		}

		// 递归放置最佳后继
		if (bestSucc) {
			placeBlock(bestSucc);
		}
	};

	// 从入口块开始
	placeBlock(blocks.front());

	// 放置剩余未访问的块
	for (auto * bb : blocks) {
		if (!placed.count(bb)) {
			ordered.push_back(bb);
			placed.insert(bb);
		}
	}

	return ordered;
}

/// @brief 执行指令选择主流程
///
/// 流程：
/// 1. 生成函数prologue（栈帧分配）
/// 2. 生成形参移动指令
/// 3. 遍历每个基本块，输出标签并翻译指令
void InstSelectorRiscV64::run()
{
	// 入口 shrink-wrapping 模式下，prologue（帧分配+保存）与形参搬运
	// 延迟到 prologue 插入块（第一个非提前路径块）生成；提前返回路径
	// 不建立栈帧、不保存 callee-saved，直接 ret
	if (!shrinkWrapEntry_) {
		// 生成函数prologue：分配栈帧，保存callee-saved寄存器
		{
			auto tmp = tempMgr.borrow(nullptr);
			iloc.allocStack(func, tmp.reg());
		}
		// 将形参从a0-a7移动到分配的寄存器/栈槽
		emitFormalParamMoves();
	}

	// 计算优化的基本块布局顺序，最小化无条件跳转
	orderedBlocks_ = computeOptimalBlockOrder(func);

	// 遍历所有基本块（按优化顺序），输出标签并翻译指令
	for (size_t i = 0; i < orderedBlocks_.size(); ++i) {
		auto * bb = orderedBlocks_[i];
		currentBlockIndex_ = i;
		currentBlock_ = bb;

		// 入口 shrink-wrapping：提前路径上的临时借用排除 a0-a7，
		// 保护尚未被形参搬运消费的原始入参寄存器
		tempMgr.setExcludeArgRegs(shrinkWrapEntry_ && shrinkWrapBlocks_.count(bb) > 0);

		// 重要：即使基本块为空，也必须输出标签，因为可能有其他块跳转到这里
		// 只有入口块可以省略标签（不会被跳转）
		if (i > 0 || !bb->getInstructions().empty()) {
			iloc.label(blockLabel(bb));
		}

		// 完整版 shrink-wrapping：在每个 prologue 边界块处生成帧分配、保存与形参搬运
		if (shrinkWrapEntry_ && shrinkWrapPrologueBlocks_.count(bb) > 0) {
			auto tmp = tempMgr.borrow(nullptr);
			iloc.allocStack(func, tmp.reg());
			emitFormalParamMoves();
		}

		// 每个基本块重新开始注释上下文，块首指令即使与上块同行也会注释
		lastCommentedLine_ = -1;

		if (bb->getInstructions().empty()) {
			continue;
		}

		for (auto * inst: bb->getInstructions()) {
			emitSplitTransfersBefore(inst);
			if (!inst->isDead()) {
				emitSourceLineComment(inst);
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

	if (auto * icmp = dynamic_cast<ICmpInst *>(inst); icmp != nullptr) {
		if (isCompareOnlyUsedByCondBranch(icmp)) {
			return;
		}
		// 比较将折叠进紧邻select的条件分支，无需物化0/1结果
		if (auto * select = getFusableSelectUser(icmp);
		    select != nullptr && shouldFuseIcmpIntoSelect(icmp, select)) {
			return;
		}
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

		const int fromPos = transfer.fromPosition >= 0 ? transfer.fromPosition : instNum - 1;
		const int toPos = transfer.toPosition >= 0 ? transfer.toPosition : instNum;
		RegAllocInfo from = allocator.getAllocationInfoAt(transfer.value, fromPos);
		RegAllocInfo to = allocator.getAllocationInfoAt(transfer.value, toPos);
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

	const bool isVector = value->getType() != nullptr && value->getType()->isVectorType();
	if (isVector) {
		// 向量分裂搬运只在 VR 和栈槽之间发生；v31 作为保留 scratch 处理栈到栈复制。
		if (to.hasVectorReg()) {
			if (from.hasVectorReg()) {
				if (from.regId != to.regId) {
					iloc.inst("vmv.v.v", PlatformRiscV64::vectorRegName[to.regId],
					          PlatformRiscV64::vectorRegName[from.regId]);
				}
			} else if (from.hasStackSlot) {
				auto addr = tempMgr.borrow(inst);
				iloc.leaStack(addr.reg(), from.baseRegId, static_cast<int>(from.offset));
				iloc.inst("vle32.v", PlatformRiscV64::vectorRegName[to.regId],
				          "(" + PlatformRiscV64::regName[addr.reg()] + ")");
			}
			return;
		}

		if (to.hasStackSlot) {
			if (from.hasVectorReg()) {
				auto addr = tempMgr.borrow(inst);
				iloc.leaStack(addr.reg(), to.baseRegId, static_cast<int>(to.offset));
				iloc.inst("vse32.v", PlatformRiscV64::vectorRegName[from.regId],
				          "(" + PlatformRiscV64::regName[addr.reg()] + ")");
			} else if (from.hasStackSlot) {
				auto addr = tempMgr.borrow(inst);
				iloc.leaStack(addr.reg(), from.baseRegId, static_cast<int>(from.offset));
				iloc.inst("vle32.v", PlatformRiscV64::vectorRegName[31],
				          "(" + PlatformRiscV64::regName[addr.reg()] + ")");
				iloc.leaStack(addr.reg(), to.baseRegId, static_cast<int>(to.offset));
				iloc.inst("vse32.v", PlatformRiscV64::vectorRegName[31],
				          "(" + PlatformRiscV64::regName[addr.reg()] + ")");
			}
		}
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
		if (dynamic_cast<AllocaInst *>(ptrOp) != nullptr || dynamic_cast<GlobalVariable *>(ptrOp) != nullptr) {
			RegAllocInfo ptrInfo = getAllocInfo(ptrOp, inst);
			if (dynamic_cast<GlobalVariable *>(ptrOp) == nullptr && ptrInfo.hasStackSlot &&
			    PlatformRiscV64::isDisp(ptrInfo.offset)) {
				iloc.store_float_base(value.reg, ptrInfo.baseRegId, ptrInfo.offset, value.reg);
			} else {
				auto addrTmp = tempMgr.borrow(inst);
				iloc.store_float_var(value.reg, ptrOp, addrTmp.reg());
			}
		} else {
			auto addrTmp = tempMgr.borrow(inst);
			OperandReg ptr = loadOperand(ptrOp, inst, addrTmp.reg());
			iloc.inst("fsw", PlatformRiscV64::fpRegName[value.reg], "0(" + PlatformRiscV64::regName[ptr.reg] + ")");
			releaseOperand(ptr);
		}
		releaseFloatOperand(value);
		return;
	}

	// 检测存储常量0：直接使用zero寄存器，避免li x,0的冗余物化
	auto * constZero = asConstInteger(storeInst->getValueOperand());
	if (constZero != nullptr && constZero->getVal() == 0) {
		Value * ptrOp = storeInst->getPointerOperand();
		const bool wide = storeInst->getValueOperand()->getType()->isPointerType();
		const char * storeOp = wide ? "sd" : "sw";

		if (dynamic_cast<AllocaInst *>(ptrOp) != nullptr || dynamic_cast<GlobalVariable *>(ptrOp) != nullptr) {
			RegAllocInfo ptrInfo = getAllocInfo(ptrOp, inst);
			if (dynamic_cast<GlobalVariable *>(ptrOp) == nullptr && ptrInfo.hasStackSlot) {
				if (PlatformRiscV64::isDisp(ptrInfo.offset)) {
					// 栈槽 + 12位偏移：sw zero, offset(fp)
					std::string mem = std::to_string(ptrInfo.offset) + "(" +
					                  PlatformRiscV64::regName[ptrInfo.baseRegId] + ")";
					iloc.inst(storeOp, "zero", mem);
				} else {
					// 栈槽 + 大偏移：先计算地址再存
					auto tmp = tempMgr.borrowAfterUses(inst);
					iloc.load_imm(tmp.reg(), ptrInfo.offset);
					iloc.inst("add", PlatformRiscV64::regName[tmp.reg()],
					          PlatformRiscV64::regName[ptrInfo.baseRegId],
					          PlatformRiscV64::regName[tmp.reg()]);
					iloc.inst(storeOp, "zero", "0(" + PlatformRiscV64::regName[tmp.reg()] + ")");
				}
			} else {
				// 全局变量或无栈槽：通过load_symbol计算地址
				auto tmp = tempMgr.borrowAfterUses(inst);
				iloc.load_symbol(tmp.reg(), ptrOp->getName());
				iloc.inst(storeOp, "zero", "0(" + PlatformRiscV64::regName[tmp.reg()] + ")");
			}
		} else {
			// 计算地址：sw zero, 0(ptr)
			OperandReg ptr = loadOperand(ptrOp, inst);
			iloc.inst(storeOp, "zero", "0(" + PlatformRiscV64::regName[ptr.reg] + ")");
			releaseOperand(ptr);
		}
		return;
	}

	OperandReg value = loadOperand(storeInst->getValueOperand(), inst);
	Value * ptrOp = storeInst->getPointerOperand();
	if (dynamic_cast<AllocaInst *>(ptrOp) != nullptr || dynamic_cast<GlobalVariable *>(ptrOp) != nullptr) {
		RegAllocInfo ptrInfo = getAllocInfo(ptrOp, inst);
		if (dynamic_cast<GlobalVariable *>(ptrOp) == nullptr && ptrInfo.hasStackSlot &&
		    PlatformRiscV64::isDisp(ptrInfo.offset)) {
			iloc.store_base(value.reg, ptrInfo.baseRegId, ptrInfo.offset, value.reg,
			                storeInst->getValueOperand()->getType()->isPointerType());
		} else {
			auto tmp = tempMgr.borrowAfterUses(inst, value.reg);
			iloc.store_var(value.reg, ptrOp, tmp.reg());
		}
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

	auto idxTmp = tempMgr.borrow(inst, dstReg);
	loadValueToReg(idxTmp.reg(), gepInst->getIndexOperand(), inst);

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
	idxTmp.release();
	storeResult(inst, dstReg, inst);
}

void InstSelectorRiscV64::translate_vsetvl(Instruction * inst)
{
	auto * vsetvl = dynamic_cast<VSetVLInst *>(inst);
	if (vsetvl == nullptr) {
		return;
	}

	int dstReg = getResultReg(inst);
	LocalTempManager::Lease dstLease;
	if (dstReg < 0) {
		dstLease = tempMgr.borrow(inst);
		dstReg = dstLease.reg();
	}

	// AVL 禁止折叠常量0到x0：vsetvli rs1=x0且rd≠x0时语义为vl=VLMAX而非0
	OperandReg avl = loadOperand(vsetvl->getAVL(), inst, dstReg, -1, false);
	// 当前向量化只生成 i32/float 元素，统一使用 e32,m1；tu 保留旧 tail 便于归约累加。
	iloc.inst("vsetvli",
	          PlatformRiscV64::regName[dstReg],
	          PlatformRiscV64::regName[avl.reg],
	          "e32, m1, tu, ma");
	releaseOperand(avl);
	storeResult(inst, dstReg, inst);
}

void InstSelectorRiscV64::translate_vload(Instruction * inst)
{
	auto * load = dynamic_cast<VectorLoadInst *>(inst);
	if (load == nullptr) {
		return;
	}

	int dstReg = getVectorResultReg(inst, inst);
	if (dstReg < 0) {
		dstReg = 31;
	}

	OperandReg ptr = loadOperand(load->getPointerOperand(), inst);
	if (load->getStride() == 1) {
		iloc.inst("vle32.v", PlatformRiscV64::vectorRegName[dstReg],
		          "(" + PlatformRiscV64::regName[ptr.reg] + ")");
	} else {
		// RVV indexed/strided 指令的 stride 单位是字节，IR 中 stride 按元素计数保存。
		auto strideReg = tempMgr.borrow(inst, ptr.reg);
		iloc.load_imm(strideReg.reg(), load->getStride() * 4);
		iloc.inst("vlse32.v",
		          PlatformRiscV64::vectorRegName[dstReg],
		          "(" + PlatformRiscV64::regName[ptr.reg] + ")",
		          PlatformRiscV64::regName[strideReg.reg()]);
	}
	releaseOperand(ptr);
	storeVectorResult(inst, dstReg, inst);
}

void InstSelectorRiscV64::translate_vstore(Instruction * inst)
{
	auto * store = dynamic_cast<VectorStoreInst *>(inst);
	if (store == nullptr) {
		return;
	}

	int valueReg = loadVectorOperand(store->getValueOperand(), inst, 31);
	OperandReg ptr = loadOperand(store->getPointerOperand(), inst);
	if (store->getStride() == 1) {
		iloc.inst("vse32.v", PlatformRiscV64::vectorRegName[valueReg],
		          "(" + PlatformRiscV64::regName[ptr.reg] + ")");
	} else {
		// RVV strided store 同样需要字节步长，当前只向量化 32 位元素。
		auto strideReg = tempMgr.borrow(inst, ptr.reg);
		iloc.load_imm(strideReg.reg(), store->getStride() * 4);
		iloc.inst("vsse32.v",
		          PlatformRiscV64::vectorRegName[valueReg],
		          "(" + PlatformRiscV64::regName[ptr.reg] + ")",
		          PlatformRiscV64::regName[strideReg.reg()]);
	}
	releaseOperand(ptr);
}

void InstSelectorRiscV64::translate_vsplat(Instruction * inst)
{
	auto * splat = dynamic_cast<VectorSplatInst *>(inst);
	if (splat == nullptr) {
		return;
	}

	int dstReg = getVectorResultReg(inst, inst);
	if (dstReg < 0) {
		dstReg = 31;
	}

	if (splat->getElementType() != nullptr && splat->getElementType()->isFloatType()) {
		FloatOperandReg scalar = loadFloatOperand(splat->getScalarOperand(), inst);
		iloc.inst("vfmv.v.f", PlatformRiscV64::vectorRegName[dstReg], PlatformRiscV64::fpRegName[scalar.reg]);
		releaseFloatOperand(scalar);
		storeVectorResult(inst, dstReg, inst);
		return;
	}

	OperandReg scalar = loadOperand(splat->getScalarOperand(), inst);
	iloc.inst("vmv.v.x", PlatformRiscV64::vectorRegName[dstReg], PlatformRiscV64::regName[scalar.reg]);
	releaseOperand(scalar);
	storeVectorResult(inst, dstReg, inst);
}

void InstSelectorRiscV64::translate_vbinary(Instruction * inst)
{
	auto * binary = dynamic_cast<VectorBinaryInst *>(inst);
	if (binary == nullptr) {
		return;
	}

	int dstReg = getVectorResultReg(inst, inst);
	if (dstReg < 0) {
		dstReg = 31;
	}
	int lhsReg = loadVectorOperand(binary->getLHS(), inst, 31);
	int rhsReg = loadVectorOperand(binary->getRHS(), inst, 30);

	std::string op;
	switch (binary->getScalarOp()) {
	case IRInstOperator::IRINST_OP_ADD_I:
		op = "vadd.vv";
		break;
	case IRInstOperator::IRINST_OP_SUB_I:
		op = "vsub.vv";
		break;
	case IRInstOperator::IRINST_OP_MUL_I:
		op = "vmul.vv";
		break;
	case IRInstOperator::IRINST_OP_ADD_F:
		op = "vfadd.vv";
		break;
	case IRInstOperator::IRINST_OP_SUB_F:
		op = "vfsub.vv";
		break;
	case IRInstOperator::IRINST_OP_MUL_F:
		op = "vfmul.vv";
		break;
	default:
		return;
	}

	if (binary->shouldPreserveLhsTail() && dstReg != lhsReg && rhsReg == dstReg) {
		// 归约累加器需要先复制 lhs 的旧 tail；若 rhs 占了目标寄存器，先挪到 scratch。
		const int rhsScratch = lhsReg != 30 && dstReg != 30 ? 30 : 31;
		iloc.inst("vmv.v.v",
		          PlatformRiscV64::vectorRegName[rhsScratch],
		          PlatformRiscV64::vectorRegName[rhsReg]);
		rhsReg = rhsScratch;
	}

	if (binary->shouldPreserveLhsTail() && dstReg != lhsReg) {
		// vsetvli 使用 tu 策略，先把旧累加器拷到目标寄存器即可保留未激活 lane。
		iloc.inst("vmv.v.v", PlatformRiscV64::vectorRegName[dstReg], PlatformRiscV64::vectorRegName[lhsReg]);
	}

	iloc.inst(op,
	          PlatformRiscV64::vectorRegName[dstReg],
	          PlatformRiscV64::vectorRegName[lhsReg],
	          PlatformRiscV64::vectorRegName[rhsReg]);
	storeVectorResult(inst, dstReg, inst);
}

void InstSelectorRiscV64::translate_vreduce(Instruction * inst)
{
	auto * reduce = dynamic_cast<VectorReduceInst *>(inst);
	if (reduce == nullptr) {
		return;
	}

	int dstReg = getVectorResultReg(inst, inst);
	if (dstReg < 0) {
		dstReg = 31;
	}
	int valueReg = loadVectorOperand(reduce->getValueOperand(), inst, 31);
	int initReg = loadVectorOperand(reduce->getInitOperand(), inst, 30);
	const bool isFloatReduce = reduce->getScalarOp() == IRInstOperator::IRINST_OP_ADD_F;
	// reduce 的结果落在目标向量寄存器 lane0，随后由 vextract 转成标量。
	iloc.inst(isFloatReduce ? "vfredusum.vs" : "vredsum.vs",
	          PlatformRiscV64::vectorRegName[dstReg],
	          PlatformRiscV64::vectorRegName[valueReg],
	          PlatformRiscV64::vectorRegName[initReg]);
	storeVectorResult(inst, dstReg, inst);
}

void InstSelectorRiscV64::translate_vextract(Instruction * inst)
{
	auto * extract = dynamic_cast<VectorExtractInst *>(inst);
	if (extract == nullptr) {
		return;
	}

	int vectorReg = loadVectorOperand(extract->getVectorOperand(), inst, 31);
	if (inst->getType()->isFloatType()) {
		int dstReg = getFloatResultReg(inst);
		bool dstTemp = false;
		if (dstReg < 0) {
			dstReg = borrowFloatTemp(inst);
			dstTemp = true;
		}
		iloc.inst("vfmv.f.s", PlatformRiscV64::fpRegName[dstReg], PlatformRiscV64::vectorRegName[vectorReg]);
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
	iloc.inst("vmv.x.s", PlatformRiscV64::regName[dstReg], PlatformRiscV64::vectorRegName[vectorReg]);
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

/// @brief 翻译逻辑左移指令（shl）
void InstSelectorRiscV64::translate_shl(Instruction * inst)
{
	translate_shift(inst, "sllw", "slliw");
}

/// @brief 翻译算术右移指令（ashr，保留符号位）
void InstSelectorRiscV64::translate_ashr(Instruction * inst)
{
	translate_shift(inst, "sraw", "sraiw");
}

/// @brief 翻译逻辑右移指令（lshr，高位补 0）
void InstSelectorRiscV64::translate_lshr(Instruction * inst)
{
	translate_shift(inst, "srlw", "srliw");
}

/// @brief 翻译移位指令的通用实现，根据移位量是否为常量选择立即数/寄存器形式
///
/// RISC-V 的 W 后缀移位指令对 32 位结果进行符号扩展，移位量取低 5 位
void InstSelectorRiscV64::translate_shift(Instruction * inst,
                                          const std::string & regOp,
                                          const std::string & immOp)
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

	// 移位量为常量时使用立即数形式，仅保留低 5 位以匹配硬件语义
	if (auto * shiftConst = asConstInteger(binary->getRHS())) {
		int shiftAmount = shiftConst->getVal() & 31;
		iloc.inst(immOp,
			PlatformRiscV64::regName[dstReg],
			PlatformRiscV64::regName[lhs.reg],
			std::to_string(shiftAmount));
		releaseOperand(lhs);
		storeResult(inst, dstReg, inst);
		return;
	}

	const int rhsPreferredReg = lhs.reg != dstReg ? dstReg : -1;
	OperandReg rhs = loadOperand(binary->getRHS(), inst, rhsPreferredReg < 0 ? dstReg : -1, rhsPreferredReg);
	iloc.inst(regOp,
		PlatformRiscV64::regName[dstReg],
		PlatformRiscV64::regName[lhs.reg],
		PlatformRiscV64::regName[rhs.reg]);
	releaseOperand(rhs);
	releaseOperand(lhs);
	storeResult(inst, dstReg, inst);
}

/// @brief 翻译按位与指令（and）
void InstSelectorRiscV64::translate_and(Instruction * inst)
{
	translate_bitwise(inst, "and", "andi");
}

/// @brief 翻译按位或指令（or）
void InstSelectorRiscV64::translate_or(Instruction * inst)
{
	translate_bitwise(inst, "or", "ori");
}

/// @brief 翻译按位异或指令（xor）
void InstSelectorRiscV64::translate_xor(Instruction * inst)
{
	translate_bitwise(inst, "xor", "xori");
}

/// @brief 翻译按位运算指令的通用实现
///
/// RV64 的 and/or/xor 无 W 变体，对两个已按 i32 符号扩展的寄存器做按位
/// 运算结果仍保持符号扩展性质，因此无需额外的 sext.w。
/// 右操作数为 12 位有符号范围内的常量时使用立即数形式
void InstSelectorRiscV64::translate_bitwise(Instruction * inst,
                                            const std::string & regOp,
                                            const std::string & immOp)
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

	if (auto * rhsConst = asConstInteger(binary->getRHS())) {
		const int32_t imm = rhsConst->getVal();
		if (imm >= -2048 && imm <= 2047) {
			iloc.inst(immOp,
				PlatformRiscV64::regName[dstReg],
				PlatformRiscV64::regName[lhs.reg],
				std::to_string(imm));
			releaseOperand(lhs);
			storeResult(inst, dstReg, inst);
			return;
		}
	}

	const int rhsPreferredReg = lhs.reg != dstReg ? dstReg : -1;
	OperandReg rhs = loadOperand(binary->getRHS(), inst, rhsPreferredReg < 0 ? dstReg : -1, rhsPreferredReg);
	iloc.inst(regOp,
		PlatformRiscV64::regName[dstReg],
		PlatformRiscV64::regName[lhs.reg],
		PlatformRiscV64::regName[rhs.reg]);
	releaseOperand(rhs);
	releaseOperand(lhs);
	storeResult(inst, dstReg, inst);
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
	// 一元取负 -x 在前端被降为 SUB_F(-0.0, x)，以保留零值的符号
	// RISC-V 的 fsgnjn.s rd,x,x 可直接完成浮点取负，无需物化 -0.0
	// 仅匹配 -0.0 的位模式，避免影响普通浮点减法
	if (auto * binary = dynamic_cast<BinaryInst *>(inst)) {
		auto * lhsZero = dynamic_cast<ConstFloat *>(binary->getLHS());
		if (lhsZero != nullptr && lhsZero->getBitPattern() == 0x80000000U) {
			int dstReg = getFloatResultReg(inst);
			bool dstTemp = false;
			if (dstReg < 0) {
				dstReg = borrowFloatTemp(inst);
				dstTemp = true;
			}
			FloatOperandReg src = loadFloatOperand(binary->getRHS(), inst, -1, dstReg, true);
			iloc.inst("fsgnjn.s",
			          PlatformRiscV64::fpRegName[dstReg],
			          PlatformRiscV64::fpRegName[src.reg],
			          PlatformRiscV64::fpRegName[src.reg]);
			releaseFloatOperand(src);
			storeFloatResult(inst, dstReg, inst);
			if (dstTemp) {
				releaseFloatTemp(dstReg);
			}
			return;
		}
	}
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
	// 对于除以2（shift==1），bias就是符号位，单条srliw即可取得
	if (shift == 1) {
		iloc.inst("srliw", biasName, srcName, "31");
		iloc.inst("addw", dstName, srcName, biasName);
		iloc.inst("sraiw", dstName, dstName, "1");
		if (negativeDivisor) {
			iloc.inst("subw", dstName, "zero", dstName);
		}
	} else {
		// 通用情况：bias用srliw从全1符号掩码生成，避免andi超出12-bit立即数范围。
		iloc.inst("sraiw", biasName, srcName, "31");
		iloc.inst("srliw", biasName, biasName, std::to_string(32 - shift));
		iloc.inst("addw", dstName, srcName, biasName);
		iloc.inst("sraiw", dstName, dstName, std::to_string(shift));
		if (negativeDivisor) {
			iloc.inst("subw", dstName, "zero", dstName);
		}
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

	// 余数按 x - floor_biased(x) 生成，保证负数余数符号与RISC-V remw/C语义一致。
	// x % (±2^k) 的余数与除数符号无关，无需按负除数取反。
	// bias：shift==1 时即符号位；否则由全1符号掩码逻辑右移得到低 shift 位掩码
	if (shift == 1) {
		iloc.inst("srliw", biasName, srcName, "31");
	} else {
		iloc.inst("sraiw", biasName, srcName, "31");
		iloc.inst("srliw", biasName, biasName, std::to_string(32 - shift));
	}
	iloc.inst("addw", qName, srcName, biasName);
	if (shift <= 11) {
		// -(2^shift) 在 imm12 范围内（shift<=11），andi 直接清低位，省去两次移位
		iloc.inst("andi", qName, qName, std::to_string(-(1 << shift)));
	} else {
		iloc.inst("sraiw", qName, qName, std::to_string(shift));
		iloc.inst("slliw", qName, qName, std::to_string(shift));
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

	// 常量操作数优先走 slti 立即数形式：LT/GE 直接比较，GT/LE 借 c+1 变换归一。
	// 常量在左侧时交换操作数并镜像比较方向。EQ/NE 无立即数比较指令，仍走寄存器路径。
	{
		auto * lhsConst = asConstInteger(icmp->getLHS());
		auto * rhsConst = asConstInteger(icmp->getRHS());
		Value * regSide = nullptr;
		int64_t imm = 0;
		IRInstOperator op = inst->getOp();
		if (rhsConst != nullptr && lhsConst == nullptr) {
			regSide = icmp->getLHS();
			imm = rhsConst->getVal();
		} else if (lhsConst != nullptr && rhsConst == nullptr) {
			regSide = icmp->getRHS();
			imm = lhsConst->getVal();
			switch (op) {
				case IRInstOperator::IRINST_OP_LT_I:
					op = IRInstOperator::IRINST_OP_GT_I;
					break;
				case IRInstOperator::IRINST_OP_GT_I:
					op = IRInstOperator::IRINST_OP_LT_I;
					break;
				case IRInstOperator::IRINST_OP_LE_I:
					op = IRInstOperator::IRINST_OP_GE_I;
					break;
				case IRInstOperator::IRINST_OP_GE_I:
					op = IRInstOperator::IRINST_OP_LE_I;
					break;
				default:
					break;
			}
		}
		if (regSide != nullptr) {
			// 与0比较相等性可单条seqz/snez完成（寄存器值恒为规范符号扩展的i32）
			if (imm == 0 &&
			    (op == IRInstOperator::IRINST_OP_EQ_I || op == IRInstOperator::IRINST_OP_NE_I)) {
				OperandReg src = loadOperand(regSide, inst, dstReg);
				iloc.inst(op == IRInstOperator::IRINST_OP_EQ_I ? "seqz" : "snez", dst,
				          PlatformRiscV64::regName[src.reg]);
				releaseOperand(src);
				storeResult(inst, dstReg, inst);
				return;
			}
			bool applicable = true;
			bool invert = false;
			int64_t sltImm = imm;
			switch (op) {
				case IRInstOperator::IRINST_OP_LT_I:
					break;
				case IRInstOperator::IRINST_OP_GE_I:
					invert = true;
					break;
				case IRInstOperator::IRINST_OP_LE_I:
					sltImm = imm + 1;
					break;
				case IRInstOperator::IRINST_OP_GT_I:
					sltImm = imm + 1;
					invert = true;
					break;
				default:
					applicable = false;
					break;
			}
			if (applicable && sltImm >= -2048 && sltImm <= 2047) {
				OperandReg src = loadOperand(regSide, inst, dstReg);
				iloc.inst("slti", dst, PlatformRiscV64::regName[src.reg], std::to_string(sltImm));
				if (invert) {
					iloc.inst("xori", dst, dst, "1");
				}
				releaseOperand(src);
				storeResult(inst, dstReg, inst);
				return;
			}
		}
	}

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
///
/// 优化：如果目标块是下一个基本块，省略跳转指令
void InstSelectorRiscV64::translate_br(Instruction * inst)
{
	auto * br = dynamic_cast<BranchInst *>(inst);
	if (br != nullptr) {
		BasicBlock * target = br->getTarget();
		// 优化：如果目标是下一个基本块，则不需要生成跳转
		bool targetIsNext = (currentBlockIndex_ + 1 < orderedBlocks_.size() &&
		                     orderedBlocks_[currentBlockIndex_ + 1] == target);
		if (!targetIsNext) {
			iloc.jump(blockLabel(target));
		}
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

/// @brief 若比较指令的唯一使用者是同块内其后小窗口内的select且作为其条件，返回该select
///
/// 与cond_br折叠不同，select的true/false值（如取负）常定义在icmp与select之间，
/// 无法要求严格紧邻；改为限定同块内小窗口，操作数寄存器的有效性
/// 由shouldFuseIcmpIntoSelect按活跃区间验证
SelectInst * InstSelectorRiscV64::getFusableSelectUser(ICmpInst * icmp) const
{
	if (icmp == nullptr) {
		return nullptr;
	}

	const auto & uses = icmp->getUseList();
	if (uses.size() != 1 || uses.front() == nullptr) {
		return nullptr;
	}

	auto * select = dynamic_cast<SelectInst *>(uses.front()->getUser());
	if (select == nullptr || select->getCondition() != static_cast<Value *>(icmp)) {
		return nullptr;
	}

	BasicBlock * bb = icmp->getParentBlock();
	if (bb == nullptr || bb != select->getParentBlock()) {
		return nullptr;
	}

	const auto & insts = bb->getInstructions();
	auto selIt = std::find(insts.begin(), insts.end(), static_cast<Instruction *>(select));
	if (selIt == insts.end()) {
		return nullptr;
	}

	constexpr int kMaxFuseWindow = 8;
	auto it = selIt;
	for (int steps = 0; steps < kMaxFuseWindow && it != insts.begin(); ++steps) {
		--it;
		if (*it == static_cast<Instruction *>(icmp)) {
			return select;
		}
	}
	return nullptr;
}

/// @brief 判断比较能否安全折叠进select的条件分支
///
/// 折叠决策在跳过icmp物化（translate分派）与translate_select两处独立求值，
/// 只依赖RA静态分配信息，保证两处结论一致。
/// icmp与select间可能存在其它指令（如true值的取负），分支在select处重读比较
/// 操作数，因此要求操作数为常量0或其活跃区间确实覆盖select（getLiveRegInfo，
/// 不回退到可能已被复用的home寄存器）；正向模式还须先把false值写入dst再发
/// 分支，故操作数寄存器不得是dst，false值加载不得引入临时借用。
bool InstSelectorRiscV64::shouldFuseIcmpIntoSelect(ICmpInst * icmp, SelectInst * select) const
{
	if (icmp == nullptr || select == nullptr || select->getType()->isFloatType()) {
		return false;
	}

	switch (icmp->getOp()) {
		case IRInstOperator::IRINST_OP_LT_I:
		case IRInstOperator::IRINST_OP_GT_I:
		case IRInstOperator::IRINST_OP_LE_I:
		case IRInstOperator::IRINST_OP_GE_I:
		case IRInstOperator::IRINST_OP_EQ_I:
		case IRInstOperator::IRINST_OP_NE_I:
			break;
		default:
			return false;
	}

	const int dstReg = getResultReg(select);
	if (dstReg < 0) {
		// 结果寄存器未分配时translate_select会借用临时寄存器，
		// 该寄存器在icmp分派时不可知，无法静态验证安全性
		return false;
	}

	auto * selInst = static_cast<Instruction *>(select);
	RegAllocInfo trueInfo = getAllocInfo(select->getTrueValue(), selInst);
	const bool reversed = trueInfo.hasReg() && trueInfo.regId == dstReg;

	for (Value * op : {icmp->getLHS(), icmp->getRHS()}) {
		if (isConstIntValue(op, 0)) {
			continue;
		}
		RegAllocInfo live = getLiveRegInfo(op, selInst);
		if (!live.hasReg()) {
			return false;
		}
		// 反向模式先发分支后写dst，操作数与dst同寄存器也安全
		if (!reversed && live.regId == dstReg) {
			return false;
		}
	}
	if (reversed) {
		return true;
	}

	Value * falseVal = select->getFalseValue();
	if (asConstInteger(falseVal) == nullptr && !getAllocInfo(falseVal, selInst).hasReg()) {
		return false;
	}
	return true;
}

/// @brief 将整数比较直接翻译为条件分支
/// @param icmp 比较指令
/// @param inst 当前插入位置对应的 IR 指令
/// @param label 跳转目标标签
/// @param branchOnTrue true时比较成立跳转；false时按取反条件（比较不成立）跳转
/// @return 若成功生成直接比较分支则返回 true
bool InstSelectorRiscV64::emitDirectIcmpBranch(ICmpInst * icmp,
                                               Instruction * inst,
                                               const std::string & label,
                                               bool branchOnTrue)
{
	if (icmp == nullptr || inst == nullptr) {
		return false;
	}

	OperandReg lhsOperand;
	OperandReg rhsOperand;
	std::string lhs = "zero";
	std::string rhs = "zero";

	if (!isConstIntValue(icmp->getLHS(), 0)) {
		lhsOperand = loadOperand(icmp->getLHS(), inst);
		lhs = PlatformRiscV64::regName[lhsOperand.reg];
	}

	if (!isConstIntValue(icmp->getRHS(), 0)) {
		rhsOperand = loadOperand(icmp->getRHS(), inst, lhsOperand.reg);
		rhs = PlatformRiscV64::regName[rhsOperand.reg];
	}

	IRInstOperator op = icmp->getOp();
	if (!branchOnTrue) {
		// 取反比较方向：六种比较在取反下封闭
		switch (op) {
			case IRInstOperator::IRINST_OP_LT_I:
				op = IRInstOperator::IRINST_OP_GE_I;
				break;
			case IRInstOperator::IRINST_OP_GE_I:
				op = IRInstOperator::IRINST_OP_LT_I;
				break;
			case IRInstOperator::IRINST_OP_GT_I:
				op = IRInstOperator::IRINST_OP_LE_I;
				break;
			case IRInstOperator::IRINST_OP_LE_I:
				op = IRInstOperator::IRINST_OP_GT_I;
				break;
			case IRInstOperator::IRINST_OP_EQ_I:
				op = IRInstOperator::IRINST_OP_NE_I;
				break;
			case IRInstOperator::IRINST_OP_NE_I:
				op = IRInstOperator::IRINST_OP_EQ_I;
				break;
			default:
				break;
		}
	}

	switch (op) {
		case IRInstOperator::IRINST_OP_LT_I:
			iloc.inst("blt", lhs, rhs, label);
			break;
		case IRInstOperator::IRINST_OP_GT_I:
			iloc.inst("blt", rhs, lhs, label);
			break;
		case IRInstOperator::IRINST_OP_LE_I:
			iloc.inst("bge", rhs, lhs, label);
			break;
		case IRInstOperator::IRINST_OP_GE_I:
			iloc.inst("bge", lhs, rhs, label);
			break;
		case IRInstOperator::IRINST_OP_EQ_I:
			iloc.inst("beq", lhs, rhs, label);
			break;
		case IRInstOperator::IRINST_OP_NE_I:
			iloc.inst("bne", lhs, rhs, label);
			break;
		default:
			releaseOperand(rhsOperand);
			releaseOperand(lhsOperand);
			return false;
	}

	releaseOperand(rhsOperand);
	releaseOperand(lhsOperand);
	return true;
}

bool InstSelectorRiscV64::translateDirectIcmpBranch(ICmpInst * icmp, CondBranchInst * condBr)
{
	if (icmp == nullptr || condBr == nullptr || !isCompareOnlyUsedByCondBranch(icmp)) {
		return false;
	}
	const std::string trueLabel = blockLabel(condBr->getTrueDest());
	if (!emitDirectIcmpBranch(icmp, condBr, trueLabel, true)) {
		return false;
	}

	// 优化：如果false分支是下一个基本块，则不需要生成跳转
	// 程序会自然地顺序执行到下一个块
	BasicBlock * falseDest = condBr->getFalseDest();
	bool falseIsNext = (currentBlockIndex_ + 1 < orderedBlocks_.size() &&
	                    orderedBlocks_[currentBlockIndex_ + 1] == falseDest);

	if (!falseIsNext) {
		iloc.jump(blockLabel(falseDest));
	}
	return true;
}

/// @brief 翻译cond_br指令（条件跳转）
/// @param inst IR指令
///
/// 生成：bne cond, zero, trueLabel; [j falseLabel]
/// 优化：如果true或false分支是下一个基本块，省略对应的跳转指令
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

	BasicBlock * trueDest = condBr->getTrueDest();
	BasicBlock * falseDest = condBr->getFalseDest();

	// 检查哪个分支是下一个块
	bool trueIsNext = (currentBlockIndex_ + 1 < orderedBlocks_.size() &&
	                   orderedBlocks_[currentBlockIndex_ + 1] == trueDest);
	bool falseIsNext = (currentBlockIndex_ + 1 < orderedBlocks_.size() &&
	                    orderedBlocks_[currentBlockIndex_ + 1] == falseDest);

	OperandReg cond = loadOperand(condBr->getCondition(), inst);

	// 情况1：true 分支是下一个块 → 生成 beq cond, zero, falseLabel（条件为假时跳转）
	// 这样条件为真时会 fall-through 到下一个块（true 分支）
	if (trueIsNext && !falseIsNext) {
		iloc.inst("beq", PlatformRiscV64::regName[cond.reg], "zero", blockLabel(falseDest));
		releaseOperand(cond);
		return;
	}

	// 情况2：false 分支是下一个块（或两者都不是）→ 生成 bne cond, zero, trueLabel
	// 条件为真时跳转到 true 分支，条件为假时 fall-through 到下一个块（false 分支）
	iloc.inst("bne", PlatformRiscV64::regName[cond.reg], "zero", blockLabel(trueDest));
	releaseOperand(cond);

	// 如果 false 分支不是下一个块，需要显式跳转
	if (!falseIsNext) {
		iloc.jump(blockLabel(falseDest));
	}
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

	// 完整版 shrink-wrapping：提前返回路径无栈帧、未保存任何寄存器，直接返回
	if (shrinkWrapEntry_ && shrinkWrapRetTargets_.count(currentBlock_) > 0) {
		iloc.inst("ret", "");
		return;
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

		for (int i = 0; i < call->getArgCount(); ++i) {
			Value * arg = call->getArg(i);
			const AbiArgLoc & loc = argLocs[i];
			if (loc.kind != AbiArgLocKind::Stack) {
				continue;
			}
			if (argTypes[i]->isFloatType() && !variadicFloatArgs[i]) {
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
			} else if (variadicFloatArgs[i]) {
				emitVariadicFloatArg(arg, loc);
			} else {
				OperandReg value = loadOperand(arg, inst);
				auto tmp = tempMgr.borrow(inst, value.reg);
				iloc.store_base(value.reg, RISCV64_SP_REG_NO, loc.index * 8, tmp.reg(),
				                arg->getType()->isPointerType());
				releaseOperand(value);
			}
		}

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

		std::vector<RegMove> intRegMoves;
		std::vector<std::pair<Value *, int>> deferredIntLoads;
		for (int i = 0; i < call->getArgCount(); ++i) {
			if ((argTypes[i]->isFloatType() && !variadicFloatArgs[i]) || variadicFloatArgs[i]) {
				continue;
			}

			Value * arg = call->getArg(i);
			const AbiArgLoc & loc = argLocs[i];
			if (loc.kind != AbiArgLocKind::IntReg) {
				continue;
			}

			const int destReg = RISCV64_A0_REG_NO + loc.index;
			RegAllocInfo argInfo = getAllocInfo(arg, inst);
			if (argInfo.hasReg()) {
				if (argInfo.regId != destReg) {
					intRegMoves.push_back(RegMove{argInfo.regId, destReg});
				}
			} else {
				deferredIntLoads.push_back({arg, destReg});
			}
		}

		{
			std::set<int> blockedGprs;
			for (int reg = RISCV64_A0_REG_NO; reg < RISCV64_A0_REG_NO + 8; ++reg) {
				blockedGprs.insert(reg);
			}
			auto scratch = tempMgr.borrowExcluding(inst, blockedGprs);
			emitGprRegMoves(intRegMoves, scratch.reg());
		}

		for (const auto & [arg, destReg] : deferredIntLoads) {
			loadValueToReg(destReg, arg, inst);
		}

		for (int i = 0; i < call->getArgCount(); ++i) {
			if (!variadicFloatArgs[i]) {
				continue;
			}

			Value * arg = call->getArg(i);
			const AbiArgLoc & loc = argLocs[i];
			if (loc.kind == AbiArgLocKind::IntReg) {
				emitVariadicFloatArg(arg, loc);
			}
		}
	}

	// Shrink-wrapping: 在调用点保存ra（如果需要）
	emitCallSiteSaveRA(inst);

	// 生成call指令
	iloc.call_fun(call->getCallee()->getName());

	// Shrink-wrapping: 在调用后立即恢复 ra（如果之前保存了）
	if (raSavedAtCallSite) {
		const int offset = raSaveOffset();
		if (offset >= 0) {
			auto tmp = tempMgr.borrow(inst);
			if (PlatformRiscV64::isDisp(offset)) {
				iloc.inst("ld", PlatformRiscV64::regName[RISCV64_RA_REG_NO],
				          std::to_string(offset) + "(sp)");
			} else {
				iloc.load_imm(tmp.reg(), offset);
				iloc.inst("add", PlatformRiscV64::regName[tmp.reg()], "sp",
				          PlatformRiscV64::regName[tmp.reg()]);
				iloc.inst("ld", PlatformRiscV64::regName[RISCV64_RA_REG_NO],
				          "0(" + PlatformRiscV64::regName[tmp.reg()] + ")");
			}
		}
		// 重置标志，使得下一个call可以再次保存ra
		raSavedAtCallSite = false;
	}

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

/// @brief 翻译select指令（条件选择）
/// @param inst IR指令
///
/// RISC-V64 没有通用条件移动指令，这里采用“先写 false，再按 cond 覆写 true”的单分支形式。
/// 条件值统一使用前序指令已物化的 0/1 结果，避免跨越其它定义后再次读取原比较操作数。
void InstSelectorRiscV64::translate_select(Instruction * inst)
{
	auto * select = dynamic_cast<SelectInst *>(inst);
	if (select == nullptr) {
		return;
	}

	const std::string labelBase = ".L_" + func->getName() + "_select_" +
	                              std::to_string(iloc.getMachineInstCount());
	const std::string doneLabel = labelBase + "_done";

	if (select->getType()->isFloatType()) {
		// float 结果优先保留在 FPR 中，避免额外的整数/浮点搬运
		int dstReg = getFloatResultReg(inst);
		bool dstTemp = false;
		if (dstReg < 0) {
			dstReg = borrowFloatTemp(inst);
			dstTemp = true;
		}

		OperandReg cond = loadOperand(select->getCondition(), inst);

		// 与整数路径同理：trueValue 已合并到 dstReg 时改用反向模式，避免写 false 覆盖仍存活的 true
		RegAllocInfo trueInfoF = getAllocInfo(select->getTrueValue(), inst);
		if (trueInfoF.hasFloatReg() && trueInfoF.regId == dstReg) {
			iloc.inst("bne", PlatformRiscV64::regName[cond.reg], "zero", doneLabel);
			releaseOperand(cond);
			FloatOperandReg falseOperand = loadFloatOperand(select->getFalseValue(), inst, -1, dstReg);
			if (falseOperand.reg != dstReg) {
				iloc.fmov_reg(dstReg, falseOperand.reg);
			}
			releaseFloatOperand(falseOperand);
			iloc.label(doneLabel);
			storeFloatResult(inst, dstReg, inst);
			if (dstTemp) {
				releaseFloatTemp(dstReg);
			}
			return;
		}

		FloatOperandReg falseOperand = loadFloatOperand(select->getFalseValue(), inst, cond.reg, dstReg);
		if (falseOperand.reg != dstReg) {
			iloc.fmov_reg(dstReg, falseOperand.reg);
		}
		releaseFloatOperand(falseOperand);
		iloc.inst("beq", PlatformRiscV64::regName[cond.reg], "zero", doneLabel);
		releaseOperand(cond);

		FloatOperandReg trueOperand = loadFloatOperand(select->getTrueValue(), inst, -1, dstReg);
		if (trueOperand.reg != dstReg) {
			iloc.fmov_reg(dstReg, trueOperand.reg);
		}
		releaseFloatOperand(trueOperand);
		iloc.label(doneLabel);

		storeFloatResult(inst, dstReg, inst);
		if (dstTemp) {
			releaseFloatTemp(dstReg);
		}
		return;
	}

	// 整数和指针结果复用通用 GPR 结果槽
	int dstReg = getResultReg(inst);
	LocalTempManager::Lease dstLease;
	if (dstReg < 0) {
		dstLease = tempMgr.borrow(inst);
		dstReg = dstLease.reg();
	}

	// 条件是紧邻的单用途icmp时，比较直接折叠为分支，跳过0/1物化与cond寄存器读取。
	// 折叠判定与translate分派处跳过icmp的判定完全一致
	auto * condIcmp = dynamic_cast<ICmpInst *>(select->getCondition());
	const bool fuseCond = condIcmp != nullptr && getFusableSelectUser(condIcmp) == select &&
	                      shouldFuseIcmpIntoSelect(condIcmp, select);

	// 若 trueValue 已被寄存器合并到 dstReg（累加器场景），先把 falseValue 写进 dst
	// 会覆盖仍存活于 dst 的 trueValue，且其后"按 cond 覆写 true"因 true 已在 dst 被跳过，
	// 使 select 恒取 falseValue。此时改用反向模式：dst 已持有 true，cond 为假时再用 false 覆写
	RegAllocInfo trueInfo = getAllocInfo(select->getTrueValue(), inst);
	if (trueInfo.hasReg() && trueInfo.regId == dstReg) {
		if (fuseCond) {
			emitDirectIcmpBranch(condIcmp, inst, doneLabel, true);
		} else {
			OperandReg cond = loadOperand(select->getCondition(), inst);
			iloc.inst("bne", PlatformRiscV64::regName[cond.reg], "zero", doneLabel);
			releaseOperand(cond);
		}
		OperandReg falseOperand = loadOperand(select->getFalseValue(), inst, -1, dstReg);
		if (falseOperand.reg != dstReg) {
			iloc.mov_reg(dstReg, falseOperand.reg);
		}
		releaseOperand(falseOperand);
		iloc.label(doneLabel);
		storeResult(inst, dstReg, inst);
		return;
	}

	if (fuseCond) {
		// 折叠谓词已保证false值加载只写dst、比较操作数驻留寄存器且异于dst，
		// 因此先写false再按取反条件跳过true覆写是安全的
		OperandReg falseOperand = loadOperand(select->getFalseValue(), inst, -1, dstReg);
		if (falseOperand.reg != dstReg) {
			iloc.mov_reg(dstReg, falseOperand.reg);
		}
		releaseOperand(falseOperand);
		emitDirectIcmpBranch(condIcmp, inst, doneLabel, false);
	} else {
		OperandReg cond = loadOperand(select->getCondition(), inst);
		OperandReg falseOperand = loadOperand(select->getFalseValue(), inst, cond.reg, dstReg);
		if (falseOperand.reg != dstReg) {
			iloc.mov_reg(dstReg, falseOperand.reg);
		}
		releaseOperand(falseOperand);
		iloc.inst("beq", PlatformRiscV64::regName[cond.reg], "zero", doneLabel);
		releaseOperand(cond);
	}

	OperandReg trueOperand = loadOperand(select->getTrueValue(), inst, -1, dstReg);
	if (trueOperand.reg != dstReg) {
		iloc.mov_reg(dstReg, trueOperand.reg);
	}
	releaseOperand(trueOperand);
	iloc.label(doneLabel);

	storeResult(inst, dstReg, inst);
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
	const bool isVectorCopy =
		(dst != nullptr && dst->getType() != nullptr && dst->getType()->isVectorType()) ||
		(copy->getSource() != nullptr && copy->getSource()->getType() != nullptr &&
		 copy->getSource()->getType()->isVectorType());
	if (isVectorCopy) {
		RegAllocInfo dstInfo = getAllocInfo(dst, inst);
		const int preferredReg = dstInfo.hasVectorReg() ? dstInfo.regId : 31;
		int srcReg = loadVectorOperand(copy->getSource(), inst, preferredReg);
		storeVectorResult(dst, srcReg, inst);
		return;
	}

	if (isFloatValue(dst) || isFloatValue(copy->getSource())) {
		// 浮点copy：以目标寄存器为preferredReg，尝试直接加载到目标位置以消除冗余move
		RegAllocInfo dstInfo = getAllocInfo(dst, inst);
		const int preferredReg = dstInfo.hasFloatReg() ? dstInfo.regId : -1;
		// allowLivePreferredReg=true：copy的src与dst共享同一指令位置，
		// preferredReg是当前指令定义的目标寄存器，允许直接写入
		FloatOperandReg src = loadFloatOperand(copy->getSource(), inst, -1, preferredReg, true);
		storeFloatResult(dst, src.reg, inst);
		releaseFloatOperand(src);
		return;
	}

	// 整数copy：以目标寄存器为preferredReg，尝试直接加载到目标位置以消除冗余move
	RegAllocInfo dstInfo = getAllocInfo(dst, inst);
	const int preferredReg = dstInfo.hasReg() ? dstInfo.regId : -1;
	OperandReg src = loadOperand(copy->getSource(), inst, -1, preferredReg);
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

void InstSelectorRiscV64::emitGprRegMoves(std::vector<RegMove> & regMoves, int scratchGpr)
{
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

			if (it->src != it->dst) {
				iloc.mov_reg(it->dst, it->src);
			}
			regMoves.erase(it);
			progressed = true;
			break;
		}

		if (progressed) {
			continue;
		}

		const int cycleSrc = regMoves.front().src;
		iloc.mov_reg(scratchGpr, cycleSrc);
		for (auto & move : regMoves) {
			if (move.src == cycleSrc) {
				move.src = scratchGpr;
			}
		}
	}
}

/// @brief 读取并缓存源文件的所有行，文件不可读时返回空向量
static const std::vector<std::string> & getSourceFileLines(const std::string & path)
{
	static std::unordered_map<std::string, std::vector<std::string>> cache;
	auto it = cache.find(path);
	if (it != cache.end()) {
		return it->second;
	}
	std::vector<std::string> lines;
	std::ifstream in(path);
	std::string line;
	while (std::getline(in, line)) {
		lines.push_back(line);
	}
	return cache.emplace(path, std::move(lines)).first->second;
}

/// @brief 推断指令的源码行号：自身无行号时，沿操作数的定义指令有限深度回溯继承
/// @param inst 待推断的指令
/// @param depth 剩余回溯深度
/// @return 源码行号，找不到返回 -1
static int64_t inferSourceLine(Instruction * inst, int depth = 3)
{
	if (inst == nullptr) {
		return -1;
	}
	if (inst->getSourceLine() >= 1) {
		return inst->getSourceLine();
	}
	if (depth <= 0) {
		return -1;
	}
	for (int32_t i = 0; i < inst->getOperandsNum(); ++i) {
		auto * opInst = dynamic_cast<Instruction *>(inst->getOperand(i));
		if (opInst == nullptr) {
			continue;
		}
		int64_t line = inferSourceLine(opInst, depth - 1);
		if (line >= 1) {
			return line;
		}
	}
	return -1;
}

/// @brief 若指令的源码行号与上次注释不同，则在其前输出 "# 行号: 源码" 注释
/// @param inst 即将翻译的 IR 指令
void InstSelectorRiscV64::emitSourceLineComment(Instruction * inst)
{
	Module * module = iloc.getModule();
	if (module == nullptr) {
		return;
	}

	int64_t lineNo = inferSourceLine(inst);
	if (lineNo < 1 || lineNo == lastCommentedLine_) {
		return;
	}
	lastCommentedLine_ = lineNo;

	const auto & srcLines = getSourceFileLines(module->getName());
	std::string text;
	if (static_cast<size_t>(lineNo) <= srcLines.size()) {
		text = srcLines[static_cast<size_t>(lineNo) - 1];
		// 去掉行首缩进，注释更紧凑
		size_t firstNonSpace = text.find_first_not_of(" \t");
		text = firstNonSpace == std::string::npos ? std::string() : text.substr(firstNonSpace);
	}
	iloc.comment(std::to_string(lineNo) + ": " + text);
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

	emitGprRegMoves(regMoves, scratchReg);

	// 栈传整数参数在 a0-a7 全部落位后再搬运，避免覆盖尚未处理的入参寄存器。
	for (std::size_t pi = 0; pi < params.size(); ++pi) {
		auto * param = params[pi];
		if (param->getType()->isFloatType() || paramLocs[pi].kind != AbiArgLocKind::Stack) {
			continue;
			}
			RegAllocInfo info = getAllocInfoAt(param, 0);
			const int stackOffset = paramLocs[pi].index * 8;
			const int stackBaseReg = incomingStackBaseReg();
			const int sourceOffset = incomingStackOffset(stackOffset);
			if (info.hasReg()) {
				iloc.load_base(info.regId, stackBaseReg, sourceOffset, param->getType()->isPointerType());
			} else if (info.hasStackSlot &&
			           (info.baseRegId != stackBaseReg || info.offset != sourceOffset)) {
				auto tmp = tempMgr.borrow(nullptr, info.regId);
				iloc.load_base(tmp.reg(), stackBaseReg, sourceOffset, param->getType()->isPointerType());
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
			const int stackBaseReg = incomingStackBaseReg();
			const int sourceOffset = incomingStackOffset(stackOffset);
			if (info.hasFloatReg()) {
				iloc.load_float_base(info.regId, stackBaseReg, sourceOffset, scratchReg);
			} else if (info.hasReg()) {
				iloc.load_base(info.regId, stackBaseReg, sourceOffset, false);
			} else if (info.hasStackSlot &&
			           (info.baseRegId != stackBaseReg || info.offset != sourceOffset)) {
				auto tmp = tempMgr.borrow(nullptr);
				iloc.load_base(tmp.reg(), stackBaseReg, sourceOffset, false);
				iloc.store_base(tmp.reg(), info.baseRegId, info.offset, scratchReg, false);
			}
		}
	}

int InstSelectorRiscV64::incomingStackBaseReg() const
{
	return iloc.usesFramePointer() ? RISCV64_FP_REG_NO : RISCV64_SP_REG_NO;
}

int InstSelectorRiscV64::incomingStackOffset(int abiOffset) const
{
	return iloc.usesFramePointer() ? abiOffset : allocator.getFrameSize() + abiOffset;
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
	// 注意：如果启用 shrink-wrapping，ra 在调用点管理，这里跳过
	for (int i = static_cast<int>(savedRegs.size()) - 1; i >= 0; --i) {
		const int reg = savedRegs[i];

		// 如果是 ra 且启用了 shrink-wrapping，跳过在 epilogue 中恢复
		if (reg == RISCV64_RA_REG_NO && iloc.getShrinkWrapRA()) {
			continue;
		}

		const int offset = frameSize - (i + 1) * 8;
		// 通过寄存器编号查找对应的寄存器名称
		emitLoad64(PlatformRiscV64::regName[reg], offset, tmp.reg());
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

/// @brief 计算ra在栈帧中的保存位置偏移（相对于sp）
/// @return ra的栈偏移，若不需要保存ra则返回-1
int InstSelectorRiscV64::raSaveOffset() const
{
	const auto & savedRegs = iloc.getSavedRegs();
	// 查找ra在savedRegs中的位置
	for (int i = 0; i < static_cast<int>(savedRegs.size()); ++i) {
		if (savedRegs[i] == RISCV64_RA_REG_NO) {
			// ra在savedRegs中的索引为i，其栈偏移为：frameSize - (i+1)*8
			const int frameSize = allocator.getFrameSize();
			return frameSize - (i + 1) * 8;
		}
	}
	return -1;  // 叶子函数，不需要保存ra
}

/// @brief 在调用点保存ra到栈上（仅在第一次call时）
/// @param inst 当前call指令（用于借用临时寄存器）
void InstSelectorRiscV64::emitCallSiteSaveRA(Instruction * inst)
{
	if (!iloc.getShrinkWrapRA()) {
		return;  // 未启用 shrink-wrapping，ra 已在 prologue 保存
	}

	if (raSavedAtCallSite) {
		return;  // 已经保存过，避免重复保存
	}

	const int offset = raSaveOffset();
	if (offset < 0) {
		return;  // 叶子函数，不需要保存ra
	}

	auto tmp = tempMgr.borrow(inst);
	if (PlatformRiscV64::isDisp(offset)) {
		iloc.inst("sd", PlatformRiscV64::regName[RISCV64_RA_REG_NO],
		          std::to_string(offset) + "(sp)");
	} else {
		iloc.load_imm(tmp.reg(), offset);
		iloc.inst("add", PlatformRiscV64::regName[tmp.reg()], "sp",
		          PlatformRiscV64::regName[tmp.reg()]);
		iloc.inst("sd", PlatformRiscV64::regName[RISCV64_RA_REG_NO],
		          "0(" + PlatformRiscV64::regName[tmp.reg()] + ")");
	}

	raSavedAtCallSite = true;  // 标记已保存
}

/// @brief 在调用点恢复ra（目前不需要，因为ra在调用之间保持栈上）
/// @param inst 当前call指令
void InstSelectorRiscV64::emitCallSiteRestoreRA(Instruction * inst)
{
	// 在RISC-V中，call指令会覆盖ra，但我们已经在第一次call前保存了旧ra。
	// 连续多个call之间，ra会不断被覆盖，但我们只需要在epilogue时恢复最初保存的值。
	// 因此，调用点恢复是不必要的。
	(void)inst;
}

/// @brief 获取Value分配的结果寄存器编号
/// @param val IR值
/// @return 物理寄存器编号，若未分配则返回-1
int InstSelectorRiscV64::getResultReg(Value * val) const
{
	// 入口 shrink-wrapping：提前路径上定义的值强制使用 scratch 寄存器
	// （其 RA 分配可能是 a0-a7——会覆盖尚未搬运的原始入参，或 callee-saved——
	// 提前路径不保存恢复）
	if (shrinkWrapEntry_ && currentBlock_ != nullptr &&
	    shrinkWrapBlocks_.count(currentBlock_) > 0) {
		return -1;
	}
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

int InstSelectorRiscV64::getVectorResultReg(Value * val, Instruction * inst) const
{
	RegAllocInfo info = getAllocInfo(val, inst);
	if (info.hasVectorReg()) {
		return info.regId;
	}
	return -1;
}

RegAllocInfo InstSelectorRiscV64::getAllocInfo(Value * val, Instruction * inst) const
{
	return allocator.getAllocationInfo(val, inst);
}

RegAllocInfo InstSelectorRiscV64::getLiveRegInfo(Value * val, Instruction * inst) const
{
	return allocator.getRegisterHoldingValueAt(val, inst);
}

RegAllocInfo InstSelectorRiscV64::getAllocInfoAt(Value * val, int instNum) const
{
	return allocator.getAllocationInfoAt(val, instNum);
}

bool hasMaterializablePointerRoot(Value * val)
{
	if (!val || val->getType() == nullptr || !val->getType()->isPointerType()) {
		return false;
	}
	if (dynamic_cast<AllocaInst *>(val) != nullptr || dynamic_cast<GlobalVariable *>(val) != nullptr) {
		return true;
	}
	if (auto * gep = dynamic_cast<GetElementPtrInst *>(val)) {
		return hasMaterializablePointerRoot(gep->getBasePointer());
	}
	return false;
}

bool InstSelectorRiscV64::isCheapRematerializable(Value * val, Instruction * inst, int depth) const
{
	if (val == nullptr || depth > 2) {
		return false;
	}
	if (!RiscV64Rematerialization::isCheapRematerializable(val, depth)) {
		return false;
	}

	// 纯常量下标 GEP 链自根地址即可整链重建，无需操作数寄存器可用性检查
	if (RiscV64Rematerialization::isConstOffsetChainFromMaterializableRoot(val)) {
		return true;
	}

	auto operandAvailable = [&](Value * operand) {
		// 必须用严格查询：操作数只有在此处仍活跃于寄存器中才可直接读取，
		// 否则其物理寄存器可能已被别的值复用（重新物化会读到错误数据）。
		return getLiveRegInfo(operand, inst).hasReg() || isCheapRematerializable(operand, inst, depth + 1);
	};
	auto * gep = dynamic_cast<GetElementPtrInst *>(val);
	if (gep != nullptr) {
		if (!operandAvailable(gep->getBasePointer())) {
			return false;
		}
		Value * index = gep->getIndexOperand();
		return dynamic_cast<ConstInteger *>(index) != nullptr || operandAvailable(index);
	}
	auto * binary = dynamic_cast<BinaryInst *>(val);
	if (binary == nullptr) {
		return true;
	}
	if (binary->getOp() == IRInstOperator::IRINST_OP_ADD_I) {
		return operandAvailable(binary->getLHS()) && operandAvailable(binary->getRHS());
	}
	if (binary->getOp() == IRInstOperator::IRINST_OP_SHL_I) {
		return dynamic_cast<ConstInteger *>(binary->getRHS()) != nullptr && operandAvailable(binary->getLHS());
	}
	return false;
}

bool InstSelectorRiscV64::tryRematerializeGEP(GetElementPtrInst * gep, int dstReg, Instruction * inst, int depth,
                                              const std::set<int> & busy)
{
	if (gep == nullptr || depth > 2) {
		return false;
	}
	// dstReg 从这里开始承载基址，后续所有临时寄存器借用都必须避开它以及祖先帧的活跃寄存器。
	std::set<int> childBusy = busy;
	childBusy.insert(dstReg);

	// 纯常量下标 GEP 链：整链折叠为 根地址+总偏移 一次性发射，深度无关
	if (RiscV64Rematerialization::isConstOffsetChainFromMaterializableRoot(gep)) {
		Value * cursor = gep;
		int64_t totalOffset = 0;
		Value * root = nullptr;
		while (auto * link = dynamic_cast<GetElementPtrInst *>(cursor)) {
			auto * basePtrType = dynamic_cast<const PointerType *>(link->getBasePointer()->getType());
			auto * constIndex = asConstInteger(link->getIndexOperand());
			if (basePtrType == nullptr || constIndex == nullptr) {
				root = nullptr;
				break;
			}
			Type * stepType = const_cast<Type *>(basePtrType->getPointeeType());
			if (link->isArrayDecayGEP()) {
				if (auto * arrayType = dynamic_cast<ArrayType *>(stepType)) {
					stepType = arrayType->getElementType();
				}
			}
			totalOffset += static_cast<int64_t>(constIndex->getVal()) * stepType->getSize();
			cursor = link->getBasePointer();
			if (dynamic_cast<AllocaInst *>(cursor) != nullptr || dynamic_cast<GlobalVariable *>(cursor) != nullptr) {
				root = cursor;
				break;
			}
		}
		if (root != nullptr && fitsInt(totalOffset)) {
			iloc.lea_var(dstReg, root);
			if (totalOffset != 0) {
				if (PlatformRiscV64::constExpr(static_cast<int>(totalOffset))) {
					iloc.inst("addi", PlatformRiscV64::regName[dstReg], PlatformRiscV64::regName[dstReg],
					          std::to_string(totalOffset));
				} else {
					auto offsetTmp = tempMgr.borrowExcluding(inst, childBusy);
					iloc.load_imm(offsetTmp.reg(), static_cast<int>(totalOffset));
					iloc.inst("add", PlatformRiscV64::regName[dstReg], PlatformRiscV64::regName[dstReg],
					          PlatformRiscV64::regName[offsetTmp.reg()]);
				}
			}
			return true;
		}
	}

	Value * basePtr = gep->getBasePointer();
	if (dynamic_cast<AllocaInst *>(basePtr) != nullptr || dynamic_cast<GlobalVariable *>(basePtr) != nullptr) {
		iloc.lea_var(dstReg, basePtr);
	} else {
		RegAllocInfo baseInfo = getLiveRegInfo(basePtr, inst);
		if (baseInfo.hasReg()) {
			iloc.mov_reg(dstReg, baseInfo.regId);
		} else if (isCheapRematerializable(basePtr, inst, depth + 1)) {
			if (!tryRematerializeValue(basePtr, dstReg, inst, depth + 1, busy)) {
				return false;
			}
		} else {
			return false;
		}
	}

	auto * basePtrType = dynamic_cast<const PointerType *>(basePtr->getType());
	if (basePtrType == nullptr) {
		return false;
	}
	Type * stepType = const_cast<Type *>(basePtrType->getPointeeType());
	if (gep->isArrayDecayGEP()) {
		auto * arrayType = dynamic_cast<ArrayType *>(stepType);
		if (arrayType != nullptr) {
			stepType = arrayType->getElementType();
		}
	}
	const int elemSize = stepType->getSize();
	if (auto * constIndex = asConstInteger(gep->getIndexOperand())) {
		const int64_t offset = static_cast<int64_t>(constIndex->getVal()) * elemSize;
		if (offset == 0) {
			return true;
		}
		if (!fitsInt(offset)) {
			return false;
		}
		if (PlatformRiscV64::constExpr(static_cast<int>(offset))) {
			iloc.inst("addi", PlatformRiscV64::regName[dstReg], PlatformRiscV64::regName[dstReg],
			          std::to_string(offset));
		} else {
			auto offsetTmp = tempMgr.borrowExcluding(inst, childBusy);
			iloc.load_imm(offsetTmp.reg(), static_cast<int>(offset));
			iloc.inst("add", PlatformRiscV64::regName[dstReg], PlatformRiscV64::regName[dstReg],
			          PlatformRiscV64::regName[offsetTmp.reg()]);
		}
		return true;
	}

	Value * index = gep->getIndexOperand();
	RegAllocInfo indexInfo = getLiveRegInfo(index, inst);
	auto idxTmp = tempMgr.borrowExcluding(inst, childBusy);
	if (indexInfo.hasReg()) {
		iloc.mov_reg(idxTmp.reg(), indexInfo.regId);
	} else if (!tryRematerializeValue(index, idxTmp.reg(), inst, depth + 1, childBusy)) {
		return false;
	}
	if (elemSize != 1) {
		if (isPowerOfTwo(static_cast<uint64_t>(elemSize))) {
			iloc.inst("slli", PlatformRiscV64::regName[idxTmp.reg()], PlatformRiscV64::regName[idxTmp.reg()],
			          std::to_string(log2PowerOfTwo(static_cast<uint64_t>(elemSize))));
		} else {
			auto mulTmp = tempMgr.borrowExcluding(inst, childBusy);
			iloc.load_imm(mulTmp.reg(), elemSize);
			iloc.inst("mul", PlatformRiscV64::regName[idxTmp.reg()], PlatformRiscV64::regName[idxTmp.reg()],
			          PlatformRiscV64::regName[mulTmp.reg()]);
		}
	}
	iloc.inst("add", PlatformRiscV64::regName[dstReg], PlatformRiscV64::regName[dstReg],
	          PlatformRiscV64::regName[idxTmp.reg()]);
	return true;
}

bool InstSelectorRiscV64::tryRematerializeValue(Value * val, int dstReg, Instruction * inst, int depth,
                                                const std::set<int> & busy)
{
	if (val == nullptr || dstReg < 0 || depth > 2) {
		return false;
	}
	// dstReg 一旦写入左操作数/基址等中间结果，计算右操作数时必须保住它，
	// 同时也要保住祖先帧仍在使用的寄存器（busy）。
	std::set<int> childBusy = busy;
	childBusy.insert(dstReg);
	if (auto * intConst = dynamic_cast<ConstInteger *>(val)) {
		iloc.load_imm(dstReg, intConst->getVal());
		return true;
	}
	if (auto * floatConst = dynamic_cast<ConstFloat *>(val)) {
		iloc.load_imm(dstReg, static_cast<int32_t>(floatConst->getBitPattern()));
		return true;
	}
	if (dynamic_cast<AllocaInst *>(val) != nullptr || dynamic_cast<GlobalVariable *>(val) != nullptr) {
		if (val->getType() != nullptr && val->getType()->isPointerType()) {
			iloc.lea_var(dstReg, val);
			return true;
		}
		return false;
	}
	if (auto * gep = dynamic_cast<GetElementPtrInst *>(val)) {
		if (!hasMaterializablePointerRoot(gep)) {
			return false;
		}
		// GEP 本身仍处在当前 depth；只有它的 base/index 操作数进入下一层。
		// 若这里提前 +1，两层 GEP（如 main 中 arr[i] -> arr[i][0]）会被误判为
		// 超过重物化深度，进而回退读取一个从未写入的 remat-only 栈槽。
		return tryRematerializeGEP(gep, dstReg, inst, depth, busy);
	}
	auto * binary = dynamic_cast<BinaryInst *>(val);
	if (binary == nullptr || depth >= 2) {
		return false;
	}
	const bool isAddressValue = val->getType() != nullptr && val->getType()->isPointerType();
	if (binary->getOp() == IRInstOperator::IRINST_OP_SHL_I) {
		auto * shiftConst = asConstInteger(binary->getRHS());
		if (shiftConst == nullptr) {
			return false;
		}
		RegAllocInfo lhsInfo = getLiveRegInfo(binary->getLHS(), inst);
		if (lhsInfo.hasReg()) {
			iloc.mov_reg(dstReg, lhsInfo.regId);
		} else if (!tryRematerializeValue(binary->getLHS(), dstReg, inst, depth + 1, busy)) {
			return false;
		}
		iloc.inst(isAddressValue ? "slli" : "slliw", PlatformRiscV64::regName[dstReg], PlatformRiscV64::regName[dstReg],
		          std::to_string(shiftConst->getVal() & (isAddressValue ? 63 : 31)));
		return true;
	}
	if (binary->getOp() != IRInstOperator::IRINST_OP_ADD_I) {
		return false;
	}

	Value * lhsValue = binary->getLHS();
	Value * rhsValue = binary->getRHS();
	RegAllocInfo lhsInfo = getLiveRegInfo(lhsValue, inst);
	if (lhsInfo.hasReg()) {
		iloc.mov_reg(dstReg, lhsInfo.regId);
	} else if (!tryRematerializeValue(lhsValue, dstReg, inst, depth + 1, busy)) {
		return false;
	}
	if (auto * rhsConst = asConstInteger(rhsValue)) {
		const int32_t imm = rhsConst->getVal();
		if (isAddressValue) {
			if (PlatformRiscV64::constExpr(imm)) {
				iloc.inst("addi", PlatformRiscV64::regName[dstReg], PlatformRiscV64::regName[dstReg], std::to_string(imm));
			} else {
				auto rhsTmp = tempMgr.borrowExcluding(inst, childBusy);
				iloc.load_imm(rhsTmp.reg(), imm);
				iloc.inst("add", PlatformRiscV64::regName[dstReg], PlatformRiscV64::regName[dstReg],
				          PlatformRiscV64::regName[rhsTmp.reg()]);
			}
		} else if (PlatformRiscV64::constExpr(imm)) {
			iloc.inst("addiw", PlatformRiscV64::regName[dstReg], PlatformRiscV64::regName[dstReg], std::to_string(imm));
		} else {
			auto rhsTmp = tempMgr.borrowExcluding(inst, childBusy);
			iloc.load_imm(rhsTmp.reg(), imm);
			iloc.inst("addw", PlatformRiscV64::regName[dstReg], PlatformRiscV64::regName[dstReg],
			          PlatformRiscV64::regName[rhsTmp.reg()]);
		}
		return true;
	}
	RegAllocInfo rhsInfo = getLiveRegInfo(rhsValue, inst);
	auto rhsTmp = tempMgr.borrowExcluding(inst, childBusy);
	if (rhsInfo.hasReg()) {
		iloc.mov_reg(rhsTmp.reg(), rhsInfo.regId);
	} else if (!tryRematerializeValue(rhsValue, rhsTmp.reg(), inst, depth + 1, childBusy)) {
		return false;
	}
	iloc.inst(isAddressValue ? "add" : "addw", PlatformRiscV64::regName[dstReg], PlatformRiscV64::regName[dstReg],
	          PlatformRiscV64::regName[rhsTmp.reg()]);
	return true;
}

void InstSelectorRiscV64::loadValueToReg(int reg, Value * val, Instruction * inst)
{
	RegAllocInfo info = getAllocInfo(val, inst);
	if ((info.hasStackSlot || allocator.isRematOnlySpill(val)) && isCheapRematerializable(val, inst) &&
	    tryRematerializeValue(val, reg, inst)) {
		return;
	}
	iloc.load_var(reg, val, info);
}

void InstSelectorRiscV64::loadFloatValueToReg(int reg, Value * val, int tmpReg, Instruction * inst)
{
	RegAllocInfo info = getAllocInfo(val, inst);
	if ((info.hasStackSlot || allocator.isRematOnlySpill(val)) && isCheapRematerializable(val, inst) &&
	    tryRematerializeValue(val, tmpReg, inst)) {
		iloc.inst("fmv.w.x", PlatformRiscV64::fpRegName[reg], PlatformRiscV64::regName[tmpReg]);
		return;
	}
	iloc.load_float_var(reg, val, tmpReg, info);
}

void InstSelectorRiscV64::loadVectorValueToReg(int reg, Value * val, Instruction * inst)
{
	RegAllocInfo info = getAllocInfo(val, inst);
	if (info.hasVectorReg()) {
		if (info.regId != reg) {
			iloc.inst("vmv.v.v", PlatformRiscV64::vectorRegName[reg], PlatformRiscV64::vectorRegName[info.regId]);
		}
		return;
	}
	if (info.hasStackSlot) {
		// 向量栈槽偏移可能超出立即数字段，统一先计算地址再 vle32.v。
		auto addr = tempMgr.borrow(inst);
		iloc.leaStack(addr.reg(), info.baseRegId, static_cast<int>(info.offset));
		iloc.inst("vle32.v", PlatformRiscV64::vectorRegName[reg],
		          "(" + PlatformRiscV64::regName[addr.reg()] + ")");
	}
}

void InstSelectorRiscV64::storeValueFromReg(Value * val, int srcReg, int tmpReg, Instruction * inst)
{
	iloc.store_var(srcReg, val, tmpReg, getAllocInfo(val, inst));
}

void InstSelectorRiscV64::storeFloatValueFromReg(Value * val, int srcReg, int tmpReg, Instruction * inst)
{
	iloc.store_float_var(srcReg, val, tmpReg, getAllocInfo(val, inst));
}

void InstSelectorRiscV64::storeVectorValueFromReg(Value * val, int srcReg, Instruction * inst)
{
	RegAllocInfo info = getAllocInfo(val, inst);
	if (info.hasVectorReg()) {
		if (info.regId != srcReg) {
			iloc.inst("vmv.v.v", PlatformRiscV64::vectorRegName[info.regId], PlatformRiscV64::vectorRegName[srcReg]);
		}
		return;
	}
	if (info.hasStackSlot) {
		// 向量 store 没有 base+large-offset 封装，先 materialize 地址再写回。
		auto addr = tempMgr.borrow(inst);
		iloc.leaStack(addr.reg(), info.baseRegId, static_cast<int>(info.offset));
		iloc.inst("vse32.v", PlatformRiscV64::vectorRegName[srcReg],
		          "(" + PlatformRiscV64::regName[addr.reg()] + ")");
	}
}

/// @brief 获取只读操作数所在寄存器，必要时借用临时寄存器加载
/// @param val 操作数
/// @param inst 当前IR指令
/// @param excludeReg 借用临时寄存器时需要排除的寄存器
/// @param preferredReg 可直接承载该操作数的首选寄存器
/// @return 操作数寄存器及是否需要释放
InstSelectorRiscV64::OperandReg
InstSelectorRiscV64::loadOperand(Value * val, Instruction * inst, int excludeReg, int preferredReg, bool foldConstZero)
{
	// 整数常量0直接复用恒零寄存器x0，避免li的冗余物化。
	// 所有调用点均将操作数寄存器作为只读源使用；rs1=x0具特殊语义的指令须传foldConstZero=false豁免。
	if (foldConstZero && isConstIntValue(val, 0)) {
		return OperandReg(0);
	}

	// 入口 shrink-wrapping：提前路径上形参直接读原始 a0-a7。
	// 此时 prologue 与形参搬运尚未执行，aN 仍持有入参值，且提前路径
	// 不保存 callee-saved（形参若被分配到 s 寄存器将读到调用者旧值）
	if (shrinkWrapEntry_ && currentBlock_ != nullptr &&
	    shrinkWrapBlocks_.count(currentBlock_) > 0) {
		if (auto * fp = dynamic_cast<FormalParam *>(val)) {
			int reg = abiIntParamReg(func, fp);
			if (reg >= 0) {
				return OperandReg(reg);
			}
		}
	}

	RegAllocInfo info = getAllocInfo(val, inst);
	if (info.hasReg()) {
		return OperandReg(info.regId);
	}

	if (preferredReg >= 0 && preferredReg != excludeReg) {
		loadValueToReg(preferredReg, val, inst);
		return OperandReg(preferredReg);
	}

	auto reg = tempMgr.borrow(inst, excludeReg);
	loadValueToReg(reg.reg(), val, inst);
	return OperandReg(std::move(reg));
}

InstSelectorRiscV64::FloatOperandReg
InstSelectorRiscV64::loadFloatOperand(Value * val,
	                                  Instruction * inst,
	                                  int excludeReg,
	                                  int preferredReg,
	                                  bool allowLivePreferredReg)
{
	RegAllocInfo info = getAllocInfo(val, inst);
	if (info.hasFloatReg()) {
		return FloatOperandReg(info.regId, false);
	}

	// 若preferredReg可用（未被排除、未被借用），则优先使用preferredReg；
	// allowLivePreferredReg为true时跳过活跃性检查，允许使用当前指令定义的目标寄存器
	int reg = -1;
	bool temp = false;
	if (preferredReg >= 0 && preferredReg != excludeReg &&
	    (allowLivePreferredReg || !isFloatRegLiveAt(preferredReg, inst)) &&
	    (allowLivePreferredReg || borrowedFloatTemps.find(preferredReg) == borrowedFloatTemps.end())) {
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
	if (allocator.isRematOnlySpill(val)) {
		return;
	}
	if (info.hasReg()) {
		if (srcReg != info.regId) {
			iloc.mov_reg(info.regId, srcReg);
		}
		return;
	}
	if (info.hasStackSlot && PlatformRiscV64::isDisp(info.offset)) {
		iloc.store_base(srcReg, info.baseRegId, info.offset, srcReg, val->getType()->isPointerType());
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
	if (allocator.isRematOnlySpill(val)) {
		return;
	}
	if (info.hasFloatReg()) {
		if (srcReg != info.regId) {
			iloc.fmov_reg(info.regId, srcReg);
		}
		return;
	}
	if (info.hasStackSlot && PlatformRiscV64::isDisp(info.offset)) {
		iloc.store_float_base(srcReg, info.baseRegId, info.offset, srcReg);
		return;
	}

	auto tmp = tempMgr.borrowAfterUses(inst);
	iloc.store_float_var(srcReg, val, tmp.reg(), info);
}

void InstSelectorRiscV64::storeVectorResult(Value * val, int srcReg, Instruction * inst)
{
	if (val == nullptr) {
		return;
	}

	RegAllocInfo info = getAllocInfo(val, inst);
	if (allocator.isRematOnlySpill(val)) {
		return;
	}
	if (info.hasVectorReg()) {
		if (srcReg != info.regId) {
			iloc.inst("vmv.v.v", PlatformRiscV64::vectorRegName[info.regId], PlatformRiscV64::vectorRegName[srcReg]);
		}
		return;
	}
	if (info.hasStackSlot) {
		// 结果值可能在所有 uses 之后才需要借用地址寄存器，避免覆盖当前指令操作数。
		auto addr = tempMgr.borrowAfterUses(inst);
		iloc.leaStack(addr.reg(), info.baseRegId, static_cast<int>(info.offset));
		iloc.inst("vse32.v", PlatformRiscV64::vectorRegName[srcReg],
		          "(" + PlatformRiscV64::regName[addr.reg()] + ")");
	}
}

int InstSelectorRiscV64::loadVectorOperand(Value * val, Instruction * inst, int scratchReg)
{
	RegAllocInfo info = getAllocInfo(val, inst);
	if (info.hasVectorReg()) {
		return info.regId;
	}
	if (info.hasStackSlot) {
		// 调用者传入 v30/v31 这类保留 scratch，避免和全局分配的 VR 冲突。
		loadVectorValueToReg(scratchReg, val, inst);
	}
	return scratchReg;
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
