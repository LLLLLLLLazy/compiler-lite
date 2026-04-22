///
/// @file IRCode.h
/// @brief IR指令序列类头文件
///

#pragma once

#include <vector>

#include "Instruction.h"

class InterCode {

protected:
    std::vector<Instruction *> code;

public:
    InterCode() = default;
    ~InterCode();

    void addInst(InterCode & block);

    void addInst(Instruction * inst);

    std::vector<Instruction *> & getInsts();

    void Delete();
};
