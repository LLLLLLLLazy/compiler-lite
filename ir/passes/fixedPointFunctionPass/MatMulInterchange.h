///
/// @file MatMulInterchange.h
/// @brief 矩阵乘法 j-k 循环交换 pass
///
/// 识别形如
///     for j in [0, Nj):            // j 维
///         sum = init
///         for k in [lbk, Nk):      // k 维
///             sum = sum + X[k] * Y[k][j]
///         Z[j] = sum
/// 的列访问归约嵌套（X 沿 k 单位步长、Y 沿 k 整行步长、Z 沿 j 单位步长，
/// 即 matmul 的 (j,k) 子嵌套，也覆盖矩阵-向量乘），将其改写为
///     for j: acc[j] = init
///     for k: { x = X[k]; for j: acc[j] = acc[j] + x * Y[k][j] }
///     (原地情形) for j: Z[j] = acc[j]
/// 使最内层访存变为单位步长，消除真机上每次迭代的 cache 同组冲突与 TLB miss。
///
/// 合法性要点：
/// 1. Z 与 X 根对象不同、Z 与 Y 根对象不同时，acc 直接取 Z 行，逐元素
///    加法顺序与原序完全一致（含浮点，IEEE 加法交换律成立、结合序未变）。
/// 2. Z 与 Y 同根（原地 A=C*A 型）时引入临时行缓冲，行尾统一回写；
///    原序中 Y 的第 z 行仅在列号恰为 j 时、且先读后写，与缓冲版等价。
///    此路径要求行对齐可证且 Nj<=行宽 W（运行期上界时生成 guard 双版本）。
///

#pragma once

class Function;
class Module;

class MatMulInterchange {

public:
    MatMulInterchange(Function * func, Module * mod);

    /// @brief 在函数内查找并改写一个匹配的 (j,k) 列访问归约嵌套
    /// @return 若修改了 IR 则返回 true
    bool run();

private:
    Function * func = nullptr;
    Module * mod = nullptr;
};
