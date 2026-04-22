///
/// @file ArgInstruction.cpp
/// @brief 函数调用前的实参指令
///

#include "ArgInstruction.h"

#include "Function.h"
#include "Instruction.h"
#include "LocalVariable.h"
#include "FormalParam.h"
#include "RegVariable.h"
#include "MemVariable.h"
#include "VoidType.h"

ArgInstruction::ArgInstruction(Function * _func, Value * src)
    : Instruction(_func, IRInstOperator::IRINST_OP_ARG, VoidType::getType())
{
    this->addOperand(src);
}

void ArgInstruction::toString(std::string & str)
{
    Value * src = getOperand(0);
    str = "arg " + src->getIRName();

    // Annotate with physical location info when available (legacy linear IR debug path).
    // Use explicit downcasts since Value base no longer exposes backend virtual methods.
    int32_t regId = -1;
    int64_t offset = 0;

    auto tryRegId = [&]() -> int32_t {
        if (auto * inst = dynamic_cast<Instruction *>(src))  return inst->getRegId();
        if (auto * lv   = dynamic_cast<LocalVariable *>(src)) return lv->getRegId();
        if (auto * fp   = dynamic_cast<FormalParam *>(src))   return fp->getRegId();
        if (auto * rv   = dynamic_cast<RegVariable *>(src))   return rv->getRegId();
        return -1;
    };

    auto tryMemAddr = [&](int32_t * baseReg, int64_t * off) -> bool {
        if (auto * inst = dynamic_cast<Instruction *>(src))   return inst->getMemoryAddr(baseReg, off);
        if (auto * lv   = dynamic_cast<LocalVariable *>(src)) return lv->getMemoryAddr(baseReg, off);
        if (auto * fp   = dynamic_cast<FormalParam *>(src))   return fp->getMemoryAddr(baseReg, off);
        if (auto * mv   = dynamic_cast<MemVariable *>(src))   return mv->getMemoryAddr(baseReg, off);
        return false;
    };

    regId = tryRegId();
    if (regId != -1) {
        str += " ; " + std::to_string(regId);
    } else if (tryMemAddr(&regId, &offset)) {
        str += " ; " + std::to_string(regId) + "[" + std::to_string(offset) + "]";
    }

    func->realArgCountInc();
}
