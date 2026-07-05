///
/// @file Rematerialization.cpp
/// @brief 后端可重物化值的静态判定实现
///
#include "Rematerialization.h"

#include "AllocaInst.h"
#include "BinaryInst.h"
#include "ConstFloat.h"
#include "ConstInteger.h"
#include "GetElementPtrInst.h"
#include "GlobalVariable.h"
#include "Instruction.h"
#include "PointerType.h"
#include "Value.h"

namespace {

bool hasMaterializablePointerRoot(Value * val)
{
	if (val == nullptr || val->getType() == nullptr || !val->getType()->isPointerType()) {
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

} // namespace

namespace RiscV64Rematerialization {

bool isCheapRematerializable(Value * val, int depth)
{
	if (val == nullptr || depth > 2) {
		return false;
	}
	if (dynamic_cast<ConstInteger *>(val) != nullptr || dynamic_cast<ConstFloat *>(val) != nullptr) {
		return true;
	}
	if (dynamic_cast<AllocaInst *>(val) != nullptr || dynamic_cast<GlobalVariable *>(val) != nullptr) {
		return val->getType() != nullptr && val->getType()->isPointerType();
	}

	auto operandRematerializable = [&](Value * operand) {
		return isCheapRematerializable(operand, depth + 1);
	};

	auto * gep = dynamic_cast<GetElementPtrInst *>(val);
	if (gep != nullptr) {
		if (depth >= 2 || !hasMaterializablePointerRoot(gep)) {
			return false;
		}
		Value * index = gep->getIndexOperand();
		return dynamic_cast<ConstInteger *>(index) != nullptr || operandRematerializable(index);
	}

	auto * binary = dynamic_cast<BinaryInst *>(val);
	if (binary == nullptr || depth >= 2) {
		return false;
	}
	if (binary->getOp() == IRInstOperator::IRINST_OP_ADD_I) {
		return operandRematerializable(binary->getLHS()) && operandRematerializable(binary->getRHS());
	}
	if (binary->getOp() == IRInstOperator::IRINST_OP_SHL_I) {
		return dynamic_cast<ConstInteger *>(binary->getRHS()) != nullptr && operandRematerializable(binary->getLHS());
	}
	return false;
}

} // namespace RiscV64Rematerialization
