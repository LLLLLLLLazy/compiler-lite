///
/// @file LoopVectorize.h
/// @brief 保守的 RVV 循环向量化 pass
///

#pragma once

class BasicBlock;
class Function;
class Module;
class ScalarEvolution;

class LoopVectorize {

public:
    LoopVectorize(Function * func, Module * mod);

    /// @brief 将规范单体循环改写为 RVV strip-mined 形式。
    bool run();

private:
    bool tryVectorizeHeader(BasicBlock * header, ScalarEvolution & scev);

    Function * func = nullptr;
    Module * mod = nullptr;
};
