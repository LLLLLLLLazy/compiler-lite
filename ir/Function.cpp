///
/// @file Function.cpp
/// @brief 函数实现
///

#include "Function.h"

#include <string>

#include "IRConstant.h"
#include "Instruction.h"

/// @brief 构造函数对象
/// @param _name 函数名
/// @param _type 函数类型
/// @param _builtin 是否为内建函数
Function::Function(std::string _name, FunctionType * _type, bool _builtin)
    : GlobalValue(_type, std::move(_name)), builtIn(_builtin)
{
    returnType = _type->getReturnType();
    setAlignment(1);
}

/// @brief 析构函数
Function::~Function()
{
    Delete();
    delete static_cast<FunctionType *>(getType());
}

/// @brief 获取函数返回值类型
/// @return 返回值类型对象
Type * Function::getReturnType()
{
    return returnType;
}

/// @brief 获取函数形参列表
/// @return 形参列表引用
std::vector<FormalParam *> & Function::getParams()
{
    return params;
}

/// @brief 判断函数是否为内建函数
/// @return true 表示为内建函数，false 表示为用户函数
bool Function::isBuiltin()
{
    return builtIn;
}

/// @brief 将函数转换为 IR 文本
/// @param str 输出的 IR 文本
void Function::toString(std::string & str)
{
    if (builtIn) {
        return;
    }

    str = "define " + getReturnType()->toString() + " " + getIRName() + "(";

    bool firstParam = true;
    for (auto & param: params) {
        if (!firstParam) {
            str += ", ";
        }
        str += param->getType()->toString() + " " + param->getIRName();
        firstParam = false;
    }

    str += ")\n{\n";

    for (auto * bb : blocks) {
        std::string bbStr;
        bb->toString(bbStr);
        str += bbStr;
    }

    str += "}\n";
}

/// @brief 创建一个新的局部变量值对象
/// @param type 变量类型
/// @param name 变量名
/// @param scope_level 作用域层级
/// @return 新创建的局部变量对象
LocalVariable * Function::newLocalVarValue(Type * type, std::string name, int32_t scope_level)
{
    auto * varValue = new LocalVariable(type, std::move(name), scope_level);
    varsVector.push_back(varValue);
    return varValue;
}

/// @brief 创建并追加一个新的基本块
/// @return 新创建的基本块对象
BasicBlock * Function::newBasicBlock()
{
    auto * bb = new BasicBlock(this);
    if (blocks.empty()) {
        entryBlock = bb;
    }
    blocks.push_back(bb);
    return bb;
}

void Function::adoptDetachedValue(Value * value)
{
    if (value) {
        detachedValues.push_back(value);
    }
}

/// @brief 释放函数拥有的形参、局部变量和基本块资源
void Function::Delete()
{
    // 先在函数级统一拆除所有指令的操作数边，避免跨基本块 def-use 在销毁时访问已释放值
    for (auto * bb: blocks) {
        for (auto * inst: bb->getInstructions()) {
            inst->clearOperands();
        }
    }

    // 再删除基本块与指令，使其在析构时不会再触碰其他 Value 的 use 链
    for (auto * bb: blocks) {
        delete bb;
    }
    blocks.clear();
    entryBlock = nullptr;

    for (auto & param: params) {
        delete param;
    }
    params.clear();

    for (auto & var: varsVector) {
        delete var;
    }
    varsVector.clear();

    for (auto * value: detachedValues) {
        delete value;
    }
    detachedValues.clear();
}

/// @brief 重新为函数中的参数、基本块和指令命名 IR 名称
void Function::renameIR()
{
    if (isBuiltin()) {
        return;
    }

    int32_t nameIndex = 0;

    for (auto & param: this->params) {
        param->setIRName(IR_TEMP_VARNAME_PREFIX + std::to_string(nameIndex++));
    }

    bool firstBlock = true;
    for (auto * bb : blocks) {
        if (firstBlock) {
            bb->setIRName("%entry");
            firstBlock = false;
        } else {
            bb->setIRName("%bb" + std::to_string(nameIndex++));
        }

        for (auto * inst : bb->getInstructions()) {
            if (inst->hasResultValue()) {
                inst->setIRName(IR_TEMP_VARNAME_PREFIX + std::to_string(nameIndex++));
            }
        }
    }
}
