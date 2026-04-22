///
/// @file CopyInst.h
/// @brief copy 指令 – 值复制，主要供 PhiLoweringPass 使用
///
/// CopyInst 有两种语义：
///
/// 1. 新值复制（explicitDst == nullptr）
///    %result = copy %src
///    此时 CopyInst 本身是一个带结果值的指令，renameIR 会为其分配 IRName。
///
/// 2. phi 降级复制（explicitDst != nullptr）
///    %explicitDst.irname = copy %src.irname
///    此时 CopyInst 的结果类型为 VoidType（hasResultValue() == false），
///    不占用新的 IRName；它的"逻辑目标"是 explicitDst 所指向的已有 Value*。
///    后端通过 getDst() 取得目标值，以建立寄存器合并约束。
///
/// PhiLoweringPass 在每个前驱块末尾（terminator 之前）插入形式 2 的 CopyInst，
/// 并将 explicitDst 设为对应 phi 节点的结果 Value*，随后删除 phi 节点本身。
///

#pragma once

#include "Instruction.h"

class Value;

class CopyInst final : public Instruction {

public:
    /// @brief 新值复制：%this_irname = copy %src
    /// @param func  所在函数
    /// @param src   被复制的源值
    CopyInst(Function * func, Value * src);

    /// @brief phi 降级复制：%explicitDst.irname = copy %src
    /// @param func        所在函数
    /// @param src         被复制的源值
    /// @param explicitDst 逻辑目标（通常是 phi 节点的结果 Value*）
    CopyInst(Function * func, Value * src, Value * explicitDst);

    /// 返回源操作数
    Value * getSource();

    /// 返回逻辑目标（phi 降级复制时非空，新值复制时为 nullptr）
    Value * getDst() const
    {
        return explicitDst;
    }

    void toString(std::string & str) override;

private:
    Value * explicitDst = nullptr; ///< phi 降级目标；nullptr 表示这是新值复制
};
