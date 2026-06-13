///
/// @file MulModInst.cpp
/// @brief 宽乘取模指令实现
///

#include "MulModInst.h"

#include <string>

#include "Function.h"
#include "IntegerType.h"
#include "Value.h"

MulModInst::MulModInst(Function * func, Value * a, Value * b, int32_t m)
    : Instruction(func, IRInstOperator::IRINST_OP_MULMOD_I, IntegerType::getTypeInt32()), modulus(m)
{
    addOperand(a);
    addOperand(b);
}

Value * MulModInst::getA()
{
    return getOperand(0);
}

Value * MulModInst::getB()
{
    return getOperand(1);
}

void MulModInst::toString(std::string & str)
{
    // 展开为标准 LLVM IR：把两操作数符号扩展到 i64 后宽乘，再对模数取有符号余数并截回 i32
    // 临时名以本指令结果名派生，保证函数内唯一；多行之间补两空格缩进与 .ll 对齐
    const std::string dst = getIRName();
    const std::string sa = dst + ".sea";
    const std::string sb = dst + ".seb";
    const std::string prod = dst + ".w64";
    const std::string rem = dst + ".r64";
    const std::string m = std::to_string(modulus);

    str = sa + " = sext i32 " + getA()->getIRName() + " to i64\n";
    str += "  " + sb + " = sext i32 " + getB()->getIRName() + " to i64\n";
    str += "  " + prod + " = mul i64 " + sa + ", " + sb + "\n";
    str += "  " + rem + " = srem i64 " + prod + ", " + m + "\n";
    str += "  " + dst + " = trunc i64 " + rem + " to i32";
}
