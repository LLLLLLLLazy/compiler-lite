///
/// @file PhiLowering.h
/// @brief phi 降级 pass
///
/// 将 SSA 中的 phi 节点转换为各前驱块末尾的 copy 指令序列，从而产生
/// 无 phi 的 IR，供后端（指令选择 + 寄存器分配）直接消费。
///
/// ## 算法概述
///
/// 对函数中的每一个基本块 BB，若 BB 顶部存在 phi 节点：
///
/// 1. 收集前驱 pred 的"并行复制集合"：
///      { (phi_result_value, incoming_value) | 来自 pred }
///
/// 2. 串行化并行复制（处理"交换"等循环依赖）：
///      - 无环复制按拓扑序直接发射；
///      - 有环（swap / cycle）时引入临时新值复制打破循环。
///
/// 3. 在 pred 的 terminator 之前依次插入 CopyInst：
///      - 循环打破临时：  CopyInst(func, src)           → %tmp = copy src
///      - 并行复制正文：  CopyInst(func, src, phi_dst)  → phi_dst_irname = copy src
///
/// 4. 从 BB 的指令列表中移除 phi，并清除其操作数（维护 use-def 链）。
///
#pragma once

#include <unordered_map>
#include <utility>
#include <vector>

class BasicBlock;
class Function;
class Module;
class Value;

class PhiLowering {

public:
    PhiLowering(Function * func, Module * mod);

    /// 对函数原地执行 phi 降级。
    /// 调用后，函数中所有 phi 节点已被移除，前驱块末尾已插入对应的
    /// copy 指令序列。调用方应在此之后调用 module->renameIR() 以更新名称。
    void run();

private:
    Function * func;
    // mod is reserved for future passes that may need to create new constants
    [[maybe_unused]] Module * mod;

    /// 将一个前驱块 pred 中收集到的并行复制集合
    ///   copies: vector of (dst_phi_value*, src_value*)
    /// 串行化并插入 pred 的 terminator 之前。
    ///
    /// 串行化规则（Briggs-Cooper 算法）：
    ///   - dst 不是任何其他待发射复制的 src → 立即可发射（ready）
    ///   - 出现循环时，引入 CopyInst(src, nullptr) 临时值打破循环，
    ///     然后将后续复制中的 src 替换为该临时值。
    void insertSequentialCopies(BasicBlock * pred,
                                std::vector<std::pair<Value *, Value *>> & copies);
};
