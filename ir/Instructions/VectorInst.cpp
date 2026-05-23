///
/// @file VectorInst.cpp
/// @brief RVV 向量 IR 指令实现
///

#include "VectorInst.h"

#include "Function.h"
#include "Types/VectorType.h"
#include "Types/VoidType.h"
#include "Value.h"

namespace {

const char * scalarOpName(IRInstOperator op)
{
    // IR 文本复用标量操作名，方便从向量指令回看原始标量语义。
    switch (op) {
    case IRInstOperator::IRINST_OP_ADD_I:
    case IRInstOperator::IRINST_OP_ADD_F:
        return "add";
    case IRInstOperator::IRINST_OP_SUB_I:
    case IRInstOperator::IRINST_OP_SUB_F:
        return "sub";
    case IRInstOperator::IRINST_OP_MUL_I:
    case IRInstOperator::IRINST_OP_MUL_F:
        return "mul";
    case IRInstOperator::IRINST_OP_DIV_I:
    case IRInstOperator::IRINST_OP_DIV_F:
        return "div";
    default:
        return "op";
    }
}

} // namespace

VSetVLInst::VSetVLInst(Function * func, Value * avl)
    : Instruction(func, IRInstOperator::IRINST_OP_VSETVL, avl != nullptr ? avl->getType() : nullptr)
{
    addOperand(avl);
}

Value * VSetVLInst::getAVL()
{
    return getOperand(0);
}

void VSetVLInst::toString(std::string & str)
{
    str = getIRName() + " = rvv.vsetvl " + getAVL()->getType()->toString() + " " + getAVL()->getIRName();
}

VectorLoadInst::VectorLoadInst(Function * func, Value * ptr, Value * vl, Type * elemType, int32_t stride)
    : Instruction(func, IRInstOperator::IRINST_OP_VLOAD, VectorType::get(elemType)), elemType(elemType), stride(stride)
{
    // 操作数顺序固定为 pointer, vl，后端按该约定读取。
    addOperand(ptr);
    addOperand(vl);
}

Value * VectorLoadInst::getPointerOperand()
{
    return getOperand(0);
}

Value * VectorLoadInst::getVL()
{
    return getOperand(1);
}

Type * VectorLoadInst::getElementType() const
{
    return elemType;
}

void VectorLoadInst::toString(std::string & str)
{
    str = getIRName() + " = rvv.vload " + getType()->toString() + ", " +
          getPointerOperand()->getIRName() + ", vl " + getVL()->getIRName();
}

VectorStoreInst::VectorStoreInst(Function * func, Value * value, Value * ptr, Value * vl, int32_t stride)
    : Instruction(func, IRInstOperator::IRINST_OP_VSTORE, VoidType::getType()), stride(stride)
{
    addOperand(value);
    addOperand(ptr);
    addOperand(vl);
}

Value * VectorStoreInst::getValueOperand()
{
    return getOperand(0);
}

Value * VectorStoreInst::getPointerOperand()
{
    return getOperand(1);
}

Value * VectorStoreInst::getVL()
{
    return getOperand(2);
}

void VectorStoreInst::toString(std::string & str)
{
    str = "rvv.vstore " + getValueOperand()->getType()->toString() + " " + getValueOperand()->getIRName() +
          ", " + getPointerOperand()->getIRName() + ", vl " + getVL()->getIRName();
}

VectorSplatInst::VectorSplatInst(Function * func, Value * scalar, Value * vl, Type * elemType)
    : Instruction(func, IRInstOperator::IRINST_OP_VSPLAT, VectorType::get(elemType)), elemType(elemType)
{
    addOperand(scalar);
    addOperand(vl);
}

Value * VectorSplatInst::getScalarOperand()
{
    return getOperand(0);
}

Value * VectorSplatInst::getVL()
{
    return getOperand(1);
}

Type * VectorSplatInst::getElementType() const
{
    return elemType;
}

void VectorSplatInst::toString(std::string & str)
{
    str = getIRName() + " = rvv.vsplat " + getType()->toString() + " " + getScalarOperand()->getIRName() +
          ", vl " + getVL()->getIRName();
}

VectorBinaryInst::VectorBinaryInst(Function * func,
                                   IRInstOperator scalarOp,
                                   Value * lhs,
                                   Value * rhs,
                                   Type * vectorType,
                                   Value * vl,
                                   bool preserveLhsTail)
    : Instruction(func, IRInstOperator::IRINST_OP_VBINARY, vectorType),
      scalarOp(scalarOp),
      preserveLhsTail(preserveLhsTail)
{
    // 操作数顺序固定为 lhs, rhs, vl，preserveLhsTail 只作为指令属性保存。
    addOperand(lhs);
    addOperand(rhs);
    addOperand(vl);
}

Value * VectorBinaryInst::getLHS()
{
    return getOperand(0);
}

Value * VectorBinaryInst::getRHS()
{
    return getOperand(1);
}

Value * VectorBinaryInst::getVL()
{
    return getOperand(2);
}

void VectorBinaryInst::toString(std::string & str)
{
    str = getIRName() + " = rvv.v" + scalarOpName(scalarOp) + " " + getType()->toString() + " " +
          getLHS()->getIRName() + ", " + getRHS()->getIRName() + ", vl " + getVL()->getIRName();
    if (preserveLhsTail) {
        str += ", preserve_lhs_tail";
    }
}

VectorReduceInst::VectorReduceInst(Function * func,
                                   IRInstOperator scalarOp,
                                   Value * value,
                                   Value * init,
                                   Type * vectorType,
                                   Value * vl)
    : Instruction(func, IRInstOperator::IRINST_OP_VREDUCE, vectorType), scalarOp(scalarOp)
{
    // 操作数顺序与 RVV reduce 指令一致：被归约向量、初始向量、VL。
    addOperand(value);
    addOperand(init);
    addOperand(vl);
}

Value * VectorReduceInst::getValueOperand()
{
    return getOperand(0);
}

Value * VectorReduceInst::getInitOperand()
{
    return getOperand(1);
}

Value * VectorReduceInst::getVL()
{
    return getOperand(2);
}

void VectorReduceInst::toString(std::string & str)
{
    str = getIRName() + " = rvv.vreduce." + scalarOpName(scalarOp) + " " + getType()->toString() + " " +
          getValueOperand()->getIRName() + ", " + getInitOperand()->getIRName() + ", vl " + getVL()->getIRName();
}

VectorExtractInst::VectorExtractInst(Function * func, Value * vector, Type * scalarType)
    : Instruction(func, IRInstOperator::IRINST_OP_VEXTRACT, scalarType)
{
    addOperand(vector);
}

Value * VectorExtractInst::getVectorOperand()
{
    return getOperand(0);
}

void VectorExtractInst::toString(std::string & str)
{
    str = getIRName() + " = rvv.vextract " + getVectorOperand()->getType()->toString() + " " +
          getVectorOperand()->getIRName();
}
