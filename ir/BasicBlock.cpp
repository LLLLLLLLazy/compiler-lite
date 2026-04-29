///
/// @file BasicBlock.cpp
/// @brief 基本块实现
///

#include "BasicBlock.h"

#include <algorithm>

#include "Function.h"
#include "Instruction.h"
#include "Types/VoidType.h"

/// @brief 构造一个基本块对象
/// @param _parent 所属函数
BasicBlock::BasicBlock(Function * _parent) : Value(VoidType::getType()), parent(_parent)
{}

/// @brief 析构基本块并释放其中的指令
BasicBlock::~BasicBlock()
{
	for (auto * inst : insts) {
		inst->clearOperands();
	}

    for (auto * inst : insts) {
        delete inst;
    }
    insts.clear();
}

/// @brief 向基本块末尾追加一条指令
/// @param inst 待追加的指令
void BasicBlock::addInstruction(Instruction * inst)
{
    inst->setParentBlock(this);
    insts.push_back(inst);
}

/// @brief 获取基本块末尾的终结指令
/// @return 若末尾为终结指令则返回该指令，否则返回空指针
Instruction * BasicBlock::getTerminator()
{
    if (insts.empty()) {
        return nullptr;
    }
    Instruction * last = insts.back();
    return last->isTerminator() ? last : nullptr;
}

/// @brief 判断基本块是否已经终结
/// @return true 表示末尾是终结指令，false 表示不是
bool BasicBlock::isTerminated() const
{
    if (insts.empty()) {
        return false;
    }
    return insts.back()->isTerminator();
}

/// @brief 添加一个前驱基本块
/// @param bb 前驱基本块
void BasicBlock::addPredecessor(BasicBlock * bb)
{
    if (std::find(preds.begin(), preds.end(), bb) == preds.end()) {
        preds.push_back(bb);
    }
}

/// @brief 添加一个后继基本块
/// @param bb 后继基本块
void BasicBlock::addSuccessor(BasicBlock * bb)
{
    if (std::find(succs.begin(), succs.end(), bb) == succs.end()) {
        succs.push_back(bb);
    }
}

/// @brief 删除一个前驱基本块
/// @param bb 待删除的前驱基本块
void BasicBlock::removePredecessor(BasicBlock * bb)
{
    preds.erase(std::remove(preds.begin(), preds.end(), bb), preds.end());
}

/// @brief 删除一个后继基本块
/// @param bb 待删除的后继基本块
void BasicBlock::removeSuccessor(BasicBlock * bb)
{
    succs.erase(std::remove(succs.begin(), succs.end(), bb), succs.end());
}

/// @brief 建立当前基本块到后继块的双向 CFG 关系
/// @param succ 后继基本块
void BasicBlock::linkSuccessor(BasicBlock * succ)
{
    addSuccessor(succ);
    succ->addPredecessor(this);
}

/// @brief 将基本块转换为文本形式
/// @param str 输出的文本字符串
void BasicBlock::toString(std::string & str)
{
    // 去掉 IR 名称前导的 '%'，得到纯标签文本，例如 "%entry" -> "entry"。
    std::string label = getIRName();
    if (!label.empty() && label.front() == '%') {
        label = label.substr(1);
    }
    if (!label.empty()) {
        str += label + ":\n";
    }
    for (auto * inst : insts) {
        std::string instStr;
        inst->toString(instStr);
        if (!instStr.empty()) {
            str += "  " + instStr + "\n";
        }
    }
}
