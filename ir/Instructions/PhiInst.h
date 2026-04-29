///
/// @file PhiInst.h
/// @brief φ 节点
///
/// PhiInst 表示 SSA φ 函数：在控制流汇合处，根据前驱基本块选择不同的值。
/// 每个 incoming 条目是一个 (Value*, BasicBlock*) 对。
///

#pragma once

#include <vector>

#include "BasicBlock.h"
#include "Instruction.h"

class Value;

class PhiInst final : public Instruction {

public:
    struct Incoming {
        Value * value;
        BasicBlock * block;
    };

private:
    struct IncomingBlock {
        BasicBlock * block;
    };

public:

    /// @param func     所在函数
    /// @param type     φ 值类型
    explicit PhiInst(Function * func, Type * type);

    /// 添加一个 (value, block) incoming 对
    void addIncoming(Value * value, BasicBlock * block);

    /// 删除所有来自指定前驱块的 incoming 对
    void removeIncomingBlock(BasicBlock * block);

    int32_t getIncomingCount() const
    {
        return static_cast<int32_t>(incoming.size());
    }

    [[nodiscard]] Value * getIncomingValue(int32_t i) const;

    [[nodiscard]] BasicBlock * getIncomingBlock(int32_t i) const
    {
        return incoming[i].block;
    }

    [[nodiscard]] Incoming getIncoming(int32_t i) const;

    void toString(std::string & str) override;

private:
    std::vector<IncomingBlock> incoming;
};
