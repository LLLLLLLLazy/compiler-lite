///
/// @file IRCode.cpp
/// @brief IR指令序列类实现
///

#include "IRCode.h"

InterCode::~InterCode()
{
    Delete();
}

void InterCode::addInst(InterCode & block)
{
    std::vector<Instruction *> & insert = block.getInsts();
    code.insert(code.end(), insert.begin(), insert.end());
    insert.clear();
}

void InterCode::addInst(Instruction * inst)
{
    code.push_back(inst);
}

std::vector<Instruction *> & InterCode::getInsts()
{
    return code;
}

void InterCode::Delete()
{
    for (auto inst: code) {
        inst->clearOperands();
    }

    for (auto inst: code) {
        delete inst;
    }

    code.clear();
}
