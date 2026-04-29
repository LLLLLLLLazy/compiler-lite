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
    /// @brief 构造函数对象
    explicit Function(std::string _name, FunctionType * _type, bool _builtin = false);

    /// @brief 析构函数
    ~Function() override;

    /// @brief 获取函数返回值类型
    Type * getReturnType();

    /// @brief 获取函数形参列表
    std::vector<FormalParam *> & getParams();

    /// @brief 判断函数是否为内建函数
    bool isBuiltin();

    /// @brief 将函数转换为 IR 文本
    void toString(std::string & str);

    /// @brief 获取函数内创建的局部变量列表
    std::vector<LocalVariable *> & getVarValues()
    {
        return varsVector;
    }

    /// @brief 判断当前值是否为函数对象
    [[nodiscard]] bool isFunction() const override
    {
        return true;
    }

    /// @brief 创建一个新的局部变量值对象
    LocalVariable * newLocalVarValue(Type * type, std::string name = "", int32_t scope_level = 1);

    /// @brief 创建并返回一个新的基本块，并追加到 blocks 列表中
    BasicBlock * newBasicBlock();

    /// @brief 接管不再挂在基本块上的临时 IR 值所有权
    void adoptDetachedValue(Value * value);

    /// @brief 返回入口基本块
    BasicBlock * getEntryBlock() const
    {
        return entryBlock;
    }

    /// @brief 返回可修改的基本块列表
    std::vector<BasicBlock *> & getBlocks()
    {
        return blocks;
    }

    /// @brief 返回只读的基本块列表
    const std::vector<BasicBlock *> & getBlocks() const
    {
        return blocks;
    }

    /// @brief 释放函数拥有的各类 IR 资源
    void Delete();

    /// @brief 重新为函数中的 IR 值命名
    void renameIR();

private:
    Type * returnType = nullptr;
    std::vector<FormalParam *> params;
    bool builtIn = false;
    std::vector<LocalVariable *> varsVector;
    std::vector<Value *> detachedValues;
    BasicBlock * entryBlock = nullptr;
    std::vector<BasicBlock *> blocks;
};
