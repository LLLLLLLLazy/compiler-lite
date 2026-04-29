///
/// @file Module.h
/// @brief 符号表-模块类
///

#pragma once

#include <string>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "ConstFloat.h"
#include "ConstInt.h"
#include "Function.h"
#include "GlobalVariable.h"
#include "Type.h"

class ScopeStack;

class Module {

public:
    /// @brief 构造模块对象并初始化全局作用域
    explicit Module(std::string _name);

    /// @brief 析构函数
    virtual ~Module() = default;

    /// @brief 将模块内容转换为 IR 文本
    std::string toIRString();

    /// @brief 获取模块名称
    [[nodiscard]] std::string getName() const
    {
        return name;
    }

    /// @brief 进入一层新的作用域
    void enterScope();

    /// @brief 离开当前作用域
    void leaveScope();

    /// @brief 获取当前正在处理的函数
    Function * getCurrentFunction();

    /// @brief 设置当前正在处理的函数
    void setCurrentFunction(Function * current);

    /// @brief 创建并注册一个函数对象
    Function *
    newFunction(std::string name, Type * returnType, std::vector<FormalParam *> params = {}, bool builtin = false);

    /// @brief 按名称查找函数
    Function * findFunction(std::string name);

    /// @brief 获取全局变量列表
    std::vector<GlobalVariable *> & getGlobalVariables()
    {
        return globalVariableVector;
    }

    /// @brief 获取函数列表
    std::vector<Function *> & getFunctionList()
    {
        return funcVector;
    }

    /// @brief 获取或创建整型常量对象
    ConstInt * newConstInt(int32_t intVal);

    /// @brief 获取或创建浮点常量对象
    ConstFloat * newConstFloat(float floatVal);

    /// @brief 创建新的变量值对象并插入当前作用域
    Value * newVarValue(Type * type, std::string name = "");

    /// @brief 按名称查找变量值对象
    Value * findVarValue(std::string name);

    /// @brief 释放模块持有的全部资源
    void Delete();

    /// @brief 将 IR 文本输出到文件
    void outputIR(const std::string & filePath);

    /// @brief 重新命名模块中的 IR 值
    void renameIR();

protected:
    /// @brief 按数值查找整型常量对象
    ConstInt * findConstInt(int32_t val);

    /// @brief 按位模式查找浮点常量对象
    ConstFloat * findConstFloat(std::uint32_t bits);

    /// @brief 创建并注册全局变量对象
    GlobalVariable * newGlobalVariable(Type * type, std::string name);

    /// @brief 按名称查找全局变量对象
    GlobalVariable * findGlobalVariable(std::string name);

    /// @brief 直接把函数插入模块容器
    void insertFunctionDirectly(Function * func);

    /// @brief 直接把全局变量插入模块容器
    void insertGlobalValueDirectly(GlobalVariable * val);

    /// @brief 直接把整型常量插入模块容器
    void insertConstIntDirectly(ConstInt * val);

    /// @brief 直接把浮点常量插入模块容器
    void insertConstFloatDirectly(ConstFloat * val);

private:
    std::string name;
    ScopeStack * scopeStack = nullptr;
    Function * currentFunc = nullptr;
    std::unordered_map<std::string, Function *> funcMap;
    std::vector<Function *> funcVector;
    std::unordered_map<std::string, GlobalVariable *> globalVariableMap;
    std::vector<GlobalVariable *> globalVariableVector;
    std::unordered_map<int32_t, ConstInt *> constIntMap;
    std::unordered_map<std::uint32_t, ConstFloat *> constFloatMap;
};
