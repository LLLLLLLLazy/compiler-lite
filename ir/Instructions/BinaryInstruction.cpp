///
/// @file BinaryInstruction.cpp
/// @brief 二元操作指令
///

#include "BinaryInstruction.h"

BinaryInstruction::BinaryInstruction(
    Function * _func, IRInstOperator _op, Value * _srcVal1, Value * _srcVal2, Type * _type)
    : Instruction(_func, _op, _type)
{
    addOperand(_srcVal1);
    addOperand(_srcVal2);
}

void BinaryInstruction::toString(std::string & str)
{
    Value * src1 = getOperand(0);
    Value * src2 = getOperand(1);

    switch (op) {
        case IRInstOperator::IRINST_OP_ADD_I:
            str = getIRName() + " = add " + src1->getIRName() + "," + src2->getIRName();
            break;
        case IRInstOperator::IRINST_OP_SUB_I:
            str = getIRName() + " = sub " + src1->getIRName() + "," + src2->getIRName();
            break;
        case IRInstOperator::IRINST_OP_MUL_I:
            str = getIRName() + " = mul " + src1->getIRName() + "," + src2->getIRName();
            break;
        case IRInstOperator::IRINST_OP_DIV_I:
            str = getIRName() + " = div " + src1->getIRName() + "," + src2->getIRName();
            break;
        case IRInstOperator::IRINST_OP_MOD_I:
            str = getIRName() + " = mod " + src1->getIRName() + "," + src2->getIRName();
            break;
        case IRInstOperator::IRINST_OP_LT_I:
            str = getIRName() + " = lt " + src1->getIRName() + "," + src2->getIRName();
            break;
        case IRInstOperator::IRINST_OP_GT_I:
            str = getIRName() + " = gt " + src1->getIRName() + "," + src2->getIRName();
            break;
        case IRInstOperator::IRINST_OP_LE_I:
            str = getIRName() + " = le " + src1->getIRName() + "," + src2->getIRName();
            break;
        case IRInstOperator::IRINST_OP_GE_I:
            str = getIRName() + " = ge " + src1->getIRName() + "," + src2->getIRName();
            break;
        case IRInstOperator::IRINST_OP_EQ_I:
            str = getIRName() + " = eq " + src1->getIRName() + "," + src2->getIRName();
            break;
        case IRInstOperator::IRINST_OP_NE_I:
            str = getIRName() + " = ne " + src1->getIRName() + "," + src2->getIRName();
            break;
        default:
            Instruction::toString(str);
            break;
    }
}
