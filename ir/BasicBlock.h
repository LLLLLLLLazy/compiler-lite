///
/// @file BasicBlock.h
/// @brief 基本块 – Phase 2 块结构 IR 核心类
///
/// 每个 BasicBlock 包含一个有序指令列表，并维护 CFG 中的前驱/后继集合。
/// 基本块的最后一条指令必须是终结指令（BranchInst、CondBranchInst 或 ReturnInst）。
///

#pragma once

#include <list>
#include <string>
#include <vector>

#include "Value.h"

class Function;
class Instruction;

class BasicBlock : public Value {

public:
    explicit BasicBlock(Function * parent);
    ~BasicBlock() override;

    Function * getParent() const
    {
        return parent;
    }

    /// 返回块中的指令列表（可修改）
    std::list<Instruction *> & getInstructions()
    {
        return insts;
    }

    const std::list<Instruction *> & getInstructions() const
    {
        return insts;
    }

    /// 在块末尾追加一条指令，并设置其 parentBlock
    void addInstruction(Instruction * inst);

    /// 返回最后一条指令（若有），否则返回 nullptr
    Instruction * getTerminator();

    /// 若最后一条指令是终结指令则返回 true
    bool isTerminated() const;

    /// CFG 前驱/后继访问
    std::vector<BasicBlock *> & getPredecessors()
    {
        return preds;
    }

    const std::vector<BasicBlock *> & getPredecessors() const
    {
        return preds;
    }

    std::vector<BasicBlock *> & getSuccessors()
    {
        return succs;
    }

    const std::vector<BasicBlock *> & getSuccessors() const
    {
        return succs;
    }

    void addPredecessor(BasicBlock * bb);
    void addSuccessor(BasicBlock * bb);
    void removePredecessor(BasicBlock * bb);
    void removeSuccessor(BasicBlock * bb);

    /// 建立 this -> succ 的 CFG 边（同时更新双方的前驱/后继集合）
    void linkSuccessor(BasicBlock * succ);

    void toString(std::string & str);

private:
    Function * parent = nullptr;
    std::list<Instruction *> insts;
    std::vector<BasicBlock *> preds;
    std::vector<BasicBlock *> succs;
};
