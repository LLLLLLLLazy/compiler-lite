///
/// @file LoopTiling.h
/// @brief 保守的二维循环分块 pass
///

#pragma once

#include <cstdint>

class BasicBlock;
class Function;
class Module;

class LoopTiling {

public:
    LoopTiling(Function * func, Module * mod, int32_t tileSize = 32);

    /// @brief 将安全的二维 unit-stride 循环改写为 32x32 分块循环。
    /// @return 若修改了 IR 则返回 true
    bool run();

private:
    bool tryTileHeader(BasicBlock * header, class LoopInfo & loopInfo, class ScalarEvolution & scev);

    Function * func = nullptr;
    Module * mod = nullptr;
    int32_t tileSize = 32;
};
