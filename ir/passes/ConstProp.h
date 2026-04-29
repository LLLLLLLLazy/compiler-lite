///
/// @file ConstProp.h
/// @brief Sparse Conditional Constant Propagation pass
///
/// 当前实现采用可执行边与值格双工作队列，覆盖：
///   1. 可执行基本块/CFG 边发现
///   2. SSA 值格求解（unknown/constant/overdefined）
///   3. 常量条件分支裁剪
///   4. 常量 SSA 值回写
///

#pragma once

class Function;
class Module;

class ConstProp {

public:
    ConstProp(Function * func, Module * mod);

    /// @brief 对函数原地执行 SCCP
    /// @return 若本轮对 IR 做了修改则返回 true
    bool run();

private:
    Function * func = nullptr;
    Module * mod = nullptr;
};