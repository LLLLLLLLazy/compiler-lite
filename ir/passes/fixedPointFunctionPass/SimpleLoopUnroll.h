///
/// @file SimpleLoopUnroll.h
/// @brief 小常数循环完全展开 pass。
///

#pragma once

class Function;
class Instruction;
class Module;

class SimpleLoopUnroll {

public:
    SimpleLoopUnroll(Function * func, Module * mod);

    /// @brief 展开形如 for (i = const; i < const; ++i) 的小循环。
    bool run();

private:
    bool tryUnrollHeader(class BasicBlock * header, class ScalarEvolution & scev);
    Instruction * cloneInstruction(Instruction * inst);

    Function * func = nullptr;
    Module * mod = nullptr;
};
