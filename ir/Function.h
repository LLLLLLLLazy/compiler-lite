///
/// @file Function.h
/// @brief 函数头文件
///

#pragma once

#include <string>
#include <vector>

#include "BasicBlock.h"
#include "FormalParam.h"
#include "FunctionType.h"
#include "GlobalValue.h"
#include "LocalVariable.h"

class Function : public GlobalValue {

public:
    explicit Function(std::string _name, FunctionType * _type, bool _builtin = false);

    ~Function() override;

    Type * getReturnType();

    std::vector<FormalParam *> & getParams();

    bool isBuiltin();

    void toString(std::string & str);

    std::vector<LocalVariable *> & getVarValues()
    {
        return varsVector;
    }

    [[nodiscard]] bool isFunction() const override
    {
        return true;
    }

    LocalVariable * newLocalVarValue(Type * type, std::string name = "", int32_t scope_level = 1);

    /// 创建并返回一个新的 BasicBlock，追加到 blocks 列表中
    BasicBlock * newBasicBlock();

    /// 返回入口基本块（第一个块）
    BasicBlock * getEntryBlock() const
    {
        return entryBlock;
    }

    /// 返回所有基本块
    std::vector<BasicBlock *> & getBlocks()
    {
        return blocks;
    }

    const std::vector<BasicBlock *> & getBlocks() const
    {
        return blocks;
    }

    void Delete();

    void renameIR();

private:
    Type * returnType = nullptr;
    std::vector<FormalParam *> params;
    bool builtIn = false;
    std::vector<LocalVariable *> varsVector;
    BasicBlock * entryBlock = nullptr;
    std::vector<BasicBlock *> blocks;
};
