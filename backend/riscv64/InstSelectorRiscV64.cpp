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

#include <cctype>
#include <cstdio>
#include <vector>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "CondBranchInst.h"
#include "CopyInst.h"
#include "GetElementPtrInst.h"
#include "GlobalVariable.h"
#include "ICmpInst.h"
#include "LoadInst.h"
#include "ArrayType.h"
#include "PhiInst.h"
#include "PlatformRiscV64.h"
#include "PointerType.h"
#include "ReturnInst.h"
#include "StoreInst.h"
#include "Value.h"
#include "ZExtInst.h"

namespace {

/// @brief 临时寄存器t0，用于大偏移地址计算等
constexpr int kTmp0 = RISCV64_TMP_REG_NO; // t0
/// @brief 临时寄存器t1，用于二元运算左操作数等
constexpr int kTmp1 = 6;                  // t1
/// @brief 临时寄存器t2，用于二元运算右操作数等
constexpr int kTmp2 = 7;                  // t2

/// @brief 获取callee-saved寄存器名称列表
/// @return ra, s0-s11的寄存器名称列表
const std::vector<std::string> & savedRegs()
{
	static const std::vector<std::string> regs = {
		"ra", "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11",
	};
	return regs;
}

} // namespace

/// @brief 构造函数，初始化IR操作码到翻译函数的映射表
/// @param _func 待翻译的函数
/// @param _iloc 底层汇编序列
/// @param _allocator 寄存器分配器
InstSelectorRiscV64::InstSelectorRiscV64(
	Function * _func, ILocRiscV64 & _iloc, GreedyRegAllocator & _allocator)
	: func(_func), iloc(_iloc), allocator(_allocator)
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
	iloc.allocStack(func, kTmp0);
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

	const int dstReg = getResultReg(inst);
	Value * ptrOp = loadInst->getPointerOperand();
	if (dynamic_cast<AllocaInst *>(ptrOp) != nullptr || dynamic_cast<GlobalVariable *>(ptrOp) != nullptr) {
		iloc.load_var(dstReg, ptrOp);
	} else {
		iloc.load_var(kTmp1, ptrOp);
		iloc.inst(loadInst->getType()->isPointerType() ? "ld" : "lw", PlatformRiscV64::regName[dstReg],
		          "0(" + PlatformRiscV64::regName[kTmp1] + ")");
	}
	storeResult(inst, dstReg);
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

	iloc.load_var(kTmp1, storeInst->getValueOperand());
	Value * ptrOp = storeInst->getPointerOperand();
	if (dynamic_cast<AllocaInst *>(ptrOp) != nullptr || dynamic_cast<GlobalVariable *>(ptrOp) != nullptr) {
		iloc.store_var(kTmp1, ptrOp, kTmp0);
	} else {
		iloc.load_var(kTmp0, ptrOp);
		iloc.inst(storeInst->getValueOperand()->getType()->isPointerType() ? "sd" : "sw",
		          PlatformRiscV64::regName[kTmp1], "0(" + PlatformRiscV64::regName[kTmp0] + ")");
	}
}

void InstSelectorRiscV64::translate_gep(Instruction * inst)
{
	auto * gepInst = dynamic_cast<GetElementPtrInst *>(inst);
	if (gepInst == nullptr) {
		return;
	}

	Value * basePtr = gepInst->getBasePointer();
	if (dynamic_cast<AllocaInst *>(basePtr) != nullptr || dynamic_cast<GlobalVariable *>(basePtr) != nullptr) {
		iloc.lea_var(kTmp1, basePtr);
	} else {
		iloc.load_var(kTmp1, basePtr);
	}

	iloc.load_var(kTmp2, gepInst->getIndexOperand());

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
		iloc.load_imm(kTmp0, elemSize);
		iloc.inst("mul", PlatformRiscV64::regName[kTmp2], PlatformRiscV64::regName[kTmp2],
		          PlatformRiscV64::regName[kTmp0]);
	}

	iloc.inst("add", PlatformRiscV64::regName[kTmp1], PlatformRiscV64::regName[kTmp1],
	          PlatformRiscV64::regName[kTmp2]);
	storeResult(inst, kTmp1);
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

	// 加载左右操作数到临时寄存器
	iloc.load_var(kTmp1, binary->getLHS());
	iloc.load_var(kTmp2, binary->getRHS());

	// 执行运算并存储结果
	const int dstReg = getResultReg(inst);
	iloc.inst(op,
		PlatformRiscV64::regName[dstReg],
		PlatformRiscV64::regName[kTmp1],
		PlatformRiscV64::regName[kTmp2]);
	storeResult(inst, dstReg);
}

/// @brief 翻译icmp指令（整数比较）
/// @param inst IR指令
///
/// 根据比较操作码生成对应的RISC-V比较指令：
/// - lt: slt dst, lhs, rhs
/// - gt: slt dst, rhs, lhs
/// - le: slt dst, rhs, lhs; xori dst, dst, 1
/// - ge: slt dst, lhs, rhs; xori dst, dst, 1
/// - eq: sub dst, lhs, rhs; seqz dst, dst
/// - ne: sub dst, lhs, rhs; snez dst, dst
void InstSelectorRiscV64::translate_icmp(Instruction * inst)
{
	auto * icmp = dynamic_cast<ICmpInst *>(inst);
	if (icmp == nullptr) {
		return;
	}

	iloc.load_var(kTmp1, icmp->getLHS());
	iloc.load_var(kTmp2, icmp->getRHS());

	const int dstReg = getResultReg(inst);
	const std::string dst = PlatformRiscV64::regName[dstReg];
	const std::string lhs = PlatformRiscV64::regName[kTmp1];
	const std::string rhs = PlatformRiscV64::regName[kTmp2];

	switch (inst->getOp()) {
		case IRInstOperator::IRINST_OP_LT_I:
			iloc.inst("slt", dst, lhs, rhs);
			break;
		case IRInstOperator::IRINST_OP_GT_I:
			// a > b 等价于 b < a
			iloc.inst("slt", dst, rhs, lhs);
			break;
		case IRInstOperator::IRINST_OP_LE_I:
			// a <= b 等价于 !(b < a)
			iloc.inst("slt", dst, rhs, lhs);
			iloc.inst("xori", dst, dst, "1");
			break;
		case IRInstOperator::IRINST_OP_GE_I:
			// a >= b 等价于 !(a < b)
			iloc.inst("slt", dst, lhs, rhs);
			iloc.inst("xori", dst, dst, "1");
			break;
		case IRInstOperator::IRINST_OP_EQ_I:
			// a == b 等价于 (a - b) == 0
			iloc.inst("sub", dst, lhs, rhs);
			iloc.inst("seqz", dst, dst);
			break;
		case IRInstOperator::IRINST_OP_NE_I:
			// a != b 等价于 (a - b) != 0
			iloc.inst("sub", dst, lhs, rhs);
			iloc.inst("snez", dst, dst);
			break;
		default:
			break;
	}

	storeResult(inst, dstReg);
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

	iloc.load_var(kTmp1, condBr->getCondition());
	iloc.inst("bne", PlatformRiscV64::regName[kTmp1], "zero", blockLabel(condBr->getTrueDest()));
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
		// 将返回值加载到a0
		iloc.load_var(RISCV64_A0_REG_NO, ret->getReturnValue());
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
		iloc.load_var(kTmp1, call->getArg(i));
		iloc.store_base(kTmp1, RISCV64_SP_REG_NO, (i - 8) * 8, kTmp0, call->getArg(i)->getType()->isPointerType());
	}

	// 前8个参数通过a0-a7传递
	for (int i = 0; i < call->getArgCount() && i < 8; ++i) {
		iloc.load_var(RISCV64_A0_REG_NO + i, call->getArg(i));
	}

	// 生成call指令
	iloc.call_fun(call->getCallee()->getName());

	// 若有返回值，将a0存储到结果位置
	if (call->hasResultValue()) {
		storeResult(call, RISCV64_A0_REG_NO);
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

	iloc.load_var(kTmp1, zext->getSource());
	const int dstReg = getResultReg(inst);
	iloc.inst("andi", PlatformRiscV64::regName[dstReg], PlatformRiscV64::regName[kTmp1], "1");
	storeResult(inst, dstReg);
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
	iloc.load_var(kTmp1, copy->getSource());
	storeResult(dst, kTmp1);
}

/// @brief 生成形参从a0-a7到分配寄存器/栈槽的移动指令
///
/// RISC-V调用约定：前8个参数通过a0-a7传递，
/// 需要将它们移动到寄存器分配器分配的位置
void InstSelectorRiscV64::emitFormalParamMoves()
{
	auto & params = func->getParams();
	auto & allocMap = allocator.getAllocationMap();

	for (int i = 0; i < static_cast<int>(params.size()) && i < 8; ++i) {
		auto * param = params[i];
		auto it = allocMap.find(param);
		if (it == allocMap.end()) {
			continue;
		}

		const int incomingReg = RISCV64_A0_REG_NO + i;
		if (it->second.hasReg()) {
			// 分配了寄存器：若与传入寄存器不同则生成mv指令
			if (it->second.regId != incomingReg) {
				iloc.mov_reg(it->second.regId, incomingReg);
			}
		} else if (it->second.hasStackSlot) {
			// 分配在栈上：生成store指令将传入寄存器值存到栈槽
			iloc.store_base(incomingReg, it->second.baseRegId, it->second.offset, kTmp0, param->getType()->isPointerType());
		}
	}
}

/// @brief 生成函数epilogue
///
/// 恢复callee-saved寄存器（逆序），恢复栈指针，执行ret指令
void InstSelectorRiscV64::emitEpilogue()
{
	const int frameSize = allocator.getFrameSize();

	// 逆序恢复callee-saved寄存器
	for (int i = static_cast<int>(savedRegs().size()) - 1; i >= 0; --i) {
		const int offset = frameSize - (i + 1) * 8;
		emitLoad64(savedRegs()[i], offset);
	}

	// 恢复栈指针
	emitStackAdjust(frameSize);
	// 返回指令
	iloc.inst("ret", "");
}

/// @brief 生成64位加载指令（ld），处理大偏移情况
/// @param reg 寄存器名
/// @param offset 栈偏移
void InstSelectorRiscV64::emitLoad64(const std::string & reg, int offset)
{
	if (PlatformRiscV64::isDisp(offset)) {
		iloc.inst("ld", reg, std::to_string(offset) + "(sp)");
		return;
	}

	// 偏移超出12位范围，通过临时寄存器计算地址
	iloc.load_imm(kTmp0, offset);
	iloc.inst("add", PlatformRiscV64::regName[kTmp0], "sp", PlatformRiscV64::regName[kTmp0]);
	iloc.inst("ld", reg, "0(" + PlatformRiscV64::regName[kTmp0] + ")");
}

/// @brief 生成栈指针调整指令，处理大偏移情况
/// @param amount 调整量
void InstSelectorRiscV64::emitStackAdjust(int amount)
{
	if (PlatformRiscV64::constExpr(amount)) {
		iloc.inst("addi", "sp", "sp", std::to_string(amount));
		return;
	}

	// 偏移超出12位范围，通过临时寄存器加载
	iloc.load_imm(kTmp0, amount);
	iloc.inst("add", "sp", "sp", PlatformRiscV64::regName[kTmp0]);
}

/// @brief 获取Value分配的结果寄存器编号
/// @param val IR值
/// @return 物理寄存器编号，若未分配则返回临时寄存器t0
int InstSelectorRiscV64::getResultReg(Value * val) const
{
	auto & allocMap = allocator.getAllocationMap();
	auto it = allocMap.find(val);
	if (it != allocMap.end() && it->second.hasReg()) {
		return it->second.regId;
	}
	return kTmp0;
}

/// @brief 将寄存器值存储到Value的目标位置
/// @param val 目标Value
/// @param srcReg 源寄存器编号
void InstSelectorRiscV64::storeResult(Value * val, int srcReg)
{
	if (val == nullptr) {
		return;
	}
	iloc.store_var(srcReg, val, kTmp0);
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
