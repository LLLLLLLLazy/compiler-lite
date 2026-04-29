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
#include <cstdio>
#include <set>
#include <vector>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "CondBranchInst.h"
#include "CopyInst.h"
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
#include "Value.h"
#include "ZExtInst.h"

namespace {


/// @brief 获取callee-saved寄存器名称列表
/// @return ra, s0-s11的寄存器名称列表
const std::vector<std::string> & savedRegs()
{
	static const std::vector<std::string> regs = {
		"ra", "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11",
	};
	return regs;
}

struct RegMove {
	int src = -1;
	int dst = -1;
};

} // namespace

/// @brief 构造函数，初始化IR操作码到翻译函数的映射表
/// @param _func 待翻译的函数
/// @param _iloc 底层汇编序列
/// @param _allocator 寄存器分配器
InstSelectorRiscV64::InstSelectorRiscV64(
	Function * _func, ILocRiscV64 & _iloc, GreedyRegAllocator & _allocator)
	: func(_func), iloc(_iloc), allocator(_allocator)
	, tempMgr(_allocator.getAvailableRegs(), _allocator.getAllocationMap(),
	          _allocator.getInstNumbering(), _allocator.getValueLiveRanges())
{
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
	// 浮点比较 (复用 ICmpInst，通过translate_icmp分发)
	translatorHandlers[IRInstOperator::IRINST_OP_LT_F] = &InstSelectorRiscV64::translate_icmp;
	translatorHandlers[IRInstOperator::IRINST_OP_GT_F] = &InstSelectorRiscV64::translate_icmp;
	translatorHandlers[IRInstOperator::IRINST_OP_LE_F] = &InstSelectorRiscV64::translate_icmp;
	translatorHandlers[IRInstOperator::IRINST_OP_GE_F] = &InstSelectorRiscV64::translate_icmp;
	translatorHandlers[IRInstOperator::IRINST_OP_EQ_F] = &InstSelectorRiscV64::translate_icmp;
	translatorHandlers[IRInstOperator::IRINST_OP_NE_F] = &InstSelectorRiscV64::translate_icmp;
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

	(this->*(handlerIt->second))(inst);

	assert(tempMgr.allReleased());
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
	iloc.load_var(idxTmp.reg(), gepInst->getIndexOperand());

	auto * basePtrType = dynamic_cast<const PointerType *>(basePtr->getType());
	Type * stepType = const_cast<Type *>(basePtrType->getPointeeType());
	if (gepInst->isArrayDecayGEP()) {
		auto * arrayType = dynamic_cast<ArrayType *>(stepType);
		if (arrayType != nullptr) {
			stepType = arrayType->getElementType();
		}
	}

	const int elemSize = stepType->getSize();
	if (elemSize != 1) {
		auto mulTmp = tempMgr.borrow(inst, dstReg);
		iloc.load_imm(mulTmp.reg(), elemSize);
		iloc.inst("mul", PlatformRiscV64::regName[idxTmp.reg()], PlatformRiscV64::regName[idxTmp.reg()],
		          PlatformRiscV64::regName[mulTmp.reg()]);
	}

	iloc.inst("add", PlatformRiscV64::regName[dstReg], PlatformRiscV64::regName[dstReg],
	          PlatformRiscV64::regName[idxTmp.reg()]);
	storeResult(inst, dstReg, inst);
}

/// @brief 翻译add指令（加法）
void InstSelectorRiscV64::translate_add(Instruction * inst)
{
	translate_binary(inst, "add");
}

/// @brief 翻译sub指令（减法）
void InstSelectorRiscV64::translate_sub(Instruction * inst)
{
	translate_binary(inst, "sub");
}

/// @brief 翻译mul指令（乘法）
void InstSelectorRiscV64::translate_mul(Instruction * inst)
{
	translate_binary(inst, "mul");
}

/// @brief 翻译div指令（除法）
void InstSelectorRiscV64::translate_div(Instruction * inst)
{
	translate_binary(inst, "div");
}

/// @brief 翻译mod指令（取模）
void InstSelectorRiscV64::translate_mod(Instruction * inst)
{
	translate_binary(inst, "rem");
}

/// @brief 翻译浮点二元运算的通用实现
///
/// 整数寄存器中存储的是float的IEEE 754位模式，
/// 需要通过fmv.w.x移到FP寄存器，执行FP运算，再fmv.x.w移回整数寄存器
void InstSelectorRiscV64::translate_fbinary(Instruction * inst, const std::string & op)
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

	// 将操作数从整数寄存器移到FP寄存器
	iloc.inst("fmv.w.x", "ft0", PlatformRiscV64::regName[lhs.reg]);
	iloc.inst("fmv.w.x", "ft1", PlatformRiscV64::regName[rhs.reg]);
	// 执行FP运算
	iloc.inst(op, "ft0", "ft0", "ft1");
	// 将结果从FP寄存器移回整数寄存器
	iloc.inst("fmv.x.w", PlatformRiscV64::regName[dstReg], "ft0");

	releaseOperand(rhs);
	releaseOperand(lhs);

	storeResult(inst, dstReg, inst);
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

	int dstReg = getResultReg(inst);
	LocalTempManager::Lease dstLease;
	if (dstReg < 0) {
		dstLease = tempMgr.borrow(inst);
		dstReg = dstLease.reg();
	}

	OperandReg src = loadOperand(sitofp->getSource(), inst, dstReg);
	iloc.inst("fcvt.s.w", "ft0", PlatformRiscV64::regName[src.reg]);
	iloc.inst("fmv.x.w", PlatformRiscV64::regName[dstReg], "ft0");
	releaseOperand(src);

	storeResult(inst, dstReg, inst);
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

	OperandReg src = loadOperand(fptosi->getSource(), inst, dstReg);
	iloc.inst("fmv.w.x", "ft0", PlatformRiscV64::regName[src.reg]);
	iloc.inst("fcvt.w.s", PlatformRiscV64::regName[dstReg], "ft0", "rtz");
	releaseOperand(src);

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

/// @brief 翻译icmp/fcmp指令（整数/浮点比较）
/// @param inst IR指令
///
/// 整数比较生成RISC-V整数比较指令，浮点比较生成F扩展比较指令：
/// 整数: slt/xori/sub+seqz/snez
/// 浮点: 先将操作数从整数寄存器移至FP寄存器(fmv.w.x)，再用flt.s/fle.s/feq.s
void InstSelectorRiscV64::translate_icmp(Instruction * inst)
{
	auto * icmp = dynamic_cast<ICmpInst *>(inst);
	if (icmp == nullptr) {
		return;
	}

	bool isFloat =
		icmp->getLHS()->getType()->isFloatType() || icmp->getRHS()->getType()->isFloatType();

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

	if (isFloat) {
		// 将操作数从整数寄存器移到FP寄存器
		iloc.inst("fmv.w.x", "ft0", lhs);
		iloc.inst("fmv.w.x", "ft1", rhs);

		switch (inst->getOp()) {
			case IRInstOperator::IRINST_OP_LT_F:
				iloc.inst("flt.s", dst, "ft0", "ft1");
				break;
			case IRInstOperator::IRINST_OP_GT_F:
				iloc.inst("flt.s", dst, "ft1", "ft0");
				break;
			case IRInstOperator::IRINST_OP_LE_F:
				iloc.inst("fle.s", dst, "ft0", "ft1");
				break;
			case IRInstOperator::IRINST_OP_GE_F:
				iloc.inst("fle.s", dst, "ft1", "ft0");
				break;
			case IRInstOperator::IRINST_OP_EQ_F:
				iloc.inst("feq.s", dst, "ft0", "ft1");
				break;
			case IRInstOperator::IRINST_OP_NE_F:
				iloc.inst("feq.s", dst, "ft0", "ft1");
				iloc.inst("xori", dst, dst, "1");
				break;
			default:
				break;
		}
	} else {
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
				iloc.inst("sub", dst, lhs, rhs);
				iloc.inst("seqz", dst, dst);
				break;
			case IRInstOperator::IRINST_OP_NE_I:
				iloc.inst("sub", dst, lhs, rhs);
				iloc.inst("snez", dst, dst);
				break;
			default:
				break;
		}
	}

	releaseOperand(rhsOperand);
	releaseOperand(lhsOperand);

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
			// float返回值：加载到a0，再移至fa0
			iloc.load_var(RISCV64_A0_REG_NO, retVal);
			iloc.inst("fmv.w.x", "fa0", PlatformRiscV64::regName[RISCV64_A0_REG_NO]);
		} else {
			iloc.load_var(RISCV64_A0_REG_NO, retVal);
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

	// 超过8个寄存器参数的实参通过栈传递
	for (int i = 8; i < call->getArgCount(); ++i) {
		OperandReg value = loadOperand(call->getArg(i), inst);
		auto tmp = tempMgr.borrow(inst, value.reg);
		iloc.store_base(value.reg,
		                RISCV64_SP_REG_NO,
		                (i - 8) * 8,
		                tmp.reg(),
		                call->getArg(i)->getType()->isPointerType());
		releaseOperand(value);
	}

	// 前8个参数通过a0-a7传递（float类型通过fa0-fa7传递）
	for (int i = 0; i < call->getArgCount() && i < 8; ++i) {
		Value *arg = call->getArg(i);
		Type *argType = arg->getType();
		if (auto *alloca = dynamic_cast<AllocaInst *>(arg)) {
			argType = alloca->getAllocaType();
		}
		if (argType->isFloatType()) {
			// float参数：加载到临时整数寄存器，再移至FP参数寄存器
			OperandReg val = loadOperand(arg, inst);
			iloc.inst("fmv.w.x", "fa" + std::to_string(i), PlatformRiscV64::regName[val.reg]);
			releaseOperand(val);
		} else {
			iloc.load_var(RISCV64_A0_REG_NO + i, arg);
		}
	}

	// 生成call指令
	iloc.call_fun(call->getCallee()->getName());

	// 若有返回值，将a0（或fa0→a0）存储到结果位置
	if (call->hasResultValue()) {
		if (call->getType()->isFloatType()) {
			// float返回值从fa0移至a0整数寄存器
			iloc.inst("fmv.x.w", PlatformRiscV64::regName[RISCV64_A0_REG_NO], "fa0");
		}
		storeResult(call, RISCV64_A0_REG_NO, inst);
	}
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

	Value * dst = copy->getDst() != nullptr ? copy->getDst() : static_cast<Value *>(copy);
	OperandReg src = loadOperand(copy->getSource(), inst);
	storeResult(dst, src.reg, inst);
	releaseOperand(src);
}

/// @brief 生成形参从a0-a7到分配寄存器/栈槽的移动指令
///
/// RISC-V调用约定：前8个参数通过a0-a7传递，
/// 需要将它们移动到寄存器分配器分配的位置
void InstSelectorRiscV64::emitFormalParamMoves()
{
	auto & params = func->getParams();
	auto & allocMap = allocator.getAllocationMap();

	std::set<int> blockedRegs;
	std::vector<RegMove> regMoves;

	for (int i = 0; i < static_cast<int>(params.size()) && i < 8; ++i) {
		blockedRegs.insert(RISCV64_A0_REG_NO + i);
	}
	for (int i = 0; i < static_cast<int>(params.size()) && i < 8; ++i) {
		auto * param = params[i];
		auto it = allocMap.find(param);
		if (it != allocMap.end() && it->second.hasReg()) {
			blockedRegs.insert(it->second.regId);
		}
	}

	int scratchReg = -1;
	for (int reg : allocator.getAvailableRegs()) {
		if (blockedRegs.find(reg) == blockedRegs.end()) {
			scratchReg = reg;
			break;
		}
	}
	if (scratchReg < 0) {
		scratchReg = RISCV64_TMP_REG_NO;
	}

	for (int i = 0; i < static_cast<int>(params.size()) && i < 8; ++i) {
		auto * param = params[i];
		auto it = allocMap.find(param);
		if (it == allocMap.end()) {
			continue;
		}

		const bool isFloat = param->getType()->isFloatType();
		const int incomingReg = RISCV64_A0_REG_NO + i;
		const std::string incomingFPReg = "fa" + std::to_string(i);

		if (isFloat) {
			// float参数从fa0-fa7传入
			if (it->second.hasReg()) {
				iloc.inst("fmv.x.w", PlatformRiscV64::regName[it->second.regId], incomingFPReg);
				blockedRegs.insert(it->second.regId);
			} else if (it->second.hasStackSlot) {
				iloc.inst("fmv.x.w", PlatformRiscV64::regName[scratchReg], incomingFPReg);
				iloc.store_base(scratchReg, it->second.baseRegId, it->second.offset, scratchReg,
					false);
			}
		} else {
			if (it->second.hasReg()) {
				if (it->second.regId != incomingReg) {
					regMoves.push_back(RegMove{incomingReg, it->second.regId});
				}
			} else if (it->second.hasStackSlot) {
				// 分配在栈上：生成store指令将传入寄存器值存到栈槽
				iloc.store_base(incomingReg,
					it->second.baseRegId,
					it->second.offset,
					scratchReg,
					param->getType()->isPointerType());
			}
		}
	}

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
}

/// @brief 生成函数epilogue
///
/// 恢复callee-saved寄存器（逆序），恢复栈指针，执行ret指令
void InstSelectorRiscV64::emitEpilogue()
{
	const int frameSize = allocator.getFrameSize();
	auto tmp = tempMgr.borrow(nullptr);

	// 逆序恢复callee-saved寄存器
	for (int i = static_cast<int>(savedRegs().size()) - 1; i >= 0; --i) {
		const int offset = frameSize - (i + 1) * 8;
		emitLoad64(savedRegs()[i], offset, tmp.reg());
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
	auto & allocMap = allocator.getAllocationMap();
	auto it = allocMap.find(val);
	if (it != allocMap.end() && it->second.hasReg()) {
		return it->second.regId;
	}
	return -1;
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
	auto & allocMap = allocator.getAllocationMap();
	auto it = allocMap.find(val);
	if (it != allocMap.end() && it->second.hasReg()) {
		return OperandReg(it->second.regId);
	}

	if (preferredReg >= 0 && preferredReg != excludeReg) {
		iloc.load_var(preferredReg, val);
		return OperandReg(preferredReg);
	}

	auto reg = tempMgr.borrow(inst, excludeReg);
	iloc.load_var(reg.reg(), val);
	return OperandReg(std::move(reg));
}

/// @brief 释放通过loadOperand借用的临时寄存器
void InstSelectorRiscV64::releaseOperand(OperandReg & operand)
{
	operand.lease.release();
}

/// @brief 将寄存器值存储到Value的目标位置
/// @param val 目标Value
/// @param srcReg 源寄存器编号
void InstSelectorRiscV64::storeResult(Value * val, int srcReg, Instruction * inst)
{
	if (val == nullptr) {
		return;
	}
	auto & allocMap = allocator.getAllocationMap();
	auto it = allocMap.find(val);
	if (it != allocMap.end() && it->second.hasReg()) {
		if (srcReg != it->second.regId) {
			iloc.mov_reg(it->second.regId, srcReg);
		}
		return;
	}

	auto tmp = tempMgr.borrowAfterUses(inst, srcReg);
	iloc.store_var(srcReg, val, tmp.reg());
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
