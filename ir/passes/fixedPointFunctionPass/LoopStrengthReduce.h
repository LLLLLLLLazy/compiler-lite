///
/// @file LoopStrengthReduce.h
/// @brief 循环地址强度削减 pass
///

#pragma once

#include <cstdint>
#include <unordered_set>

class BasicBlock;
class Function;
class Instruction;
class Module;

class LoopStrengthReduce {

public:
    LoopStrengthReduce(Function * func, Module * mod);

    /// @brief 将循环内 gep(base, affine(i_phi)) 改写为 pointer phi + ptr += stride
    /// @return 若修改了 IR 则返回 true
    bool run();

private:
    bool tryReduceHeader(BasicBlock * header);
    /// @brief 选择一组共享同一 affine index 值的 GEP 并改写为 pointer recurrence
    bool reduceFirstCandidate(BasicBlock * header,
                              BasicBlock * preheader,
                              BasicBlock * latch,
                              class ScalarEvolution & scev,
                              const std::unordered_set<BasicBlock *> & loopBody);
    bool sweepDeadInstructions() const;

    Function * func = nullptr;
    Module * mod = nullptr;
};
