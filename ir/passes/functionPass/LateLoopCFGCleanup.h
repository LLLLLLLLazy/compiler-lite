///
/// @file LateLoopCFGCleanup.h
/// @brief 固定点循环优化结束后的晚期 CFG 收尾 pass。
///
/// 该 pass 主要撤销为循环优化而引入、但对最终代码生成不友好的
/// synthetic single-latch 纯 phi 中转块，尽量在 phi 降级前把 CFG 恢复成更直接的形状。
///

#pragma once

class Function;

class LateLoopCFGCleanup {

public:
    explicit LateLoopCFGCleanup(Function * func);

    /// @brief 清理晚期不再需要的 synthetic loop CFG
    /// @return true 表示函数被修改
    bool run();

private:
    Function * func = nullptr;
};