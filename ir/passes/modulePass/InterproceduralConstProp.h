///
/// @file InterproceduralConstProp.h
/// @brief 极小规模跨过程常量实参传播优化。
///
/// 若某个形参在所有调用点都接收到相同的常量值，
/// 则将该形参的所有使用替换为该常量。
///

#pragma once

class Module;

class InterproceduralConstProp {

public:
    explicit InterproceduralConstProp(Module * mod);

    /// @brief 将在所有调用点接收到相同常量的形参替换为该常量
    /// @return true 表示 IR 被修改
    bool run();

private:
    Module * mod = nullptr;
};
