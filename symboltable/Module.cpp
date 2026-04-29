///
/// @file Module.cpp
/// @brief 符号表-模块类
///

#include "Module.h"

#include <cstdio>
#include <cstring>

#include "Common.h"
#include "FunctionType.h"
#include "FloatType.h"
#include "IntegerType.h"
#include "PointerType.h"
#include "ScopeStack.h"
#include "VoidType.h"

/// @brief 构造模块对象并初始化内建函数与全局作用域
/// @param _name 模块名称
Module::Module(std::string _name) : name(std::move(_name))
{
    scopeStack = new ScopeStack();
    scopeStack->enterScope();

    (void) newFunction("putint", VoidType::getType(), {new FormalParam{IntegerType::getTypeInt(), ""}}, true);
    (void) newFunction("getint", IntegerType::getTypeInt(), {}, true);
    (void) newFunction("putch", VoidType::getType(), {new FormalParam{IntegerType::getTypeInt(), ""}}, true);
    (void) newFunction("getch", IntegerType::getTypeInt(), {}, true);
    auto * intPtrType = const_cast<PointerType *>(PointerType::get(IntegerType::getTypeInt()));
    auto * floatType = FloatType::getTypeFloat();
    auto * floatPtrType = const_cast<PointerType *>(PointerType::get(floatType));
    (void) newFunction("getarray", IntegerType::getTypeInt(), {new FormalParam{intPtrType, ""}}, true);
    (void) newFunction(
        "putarray", VoidType::getType(), {new FormalParam{IntegerType::getTypeInt(), ""}, new FormalParam{intPtrType, ""}}, true);
    (void) newFunction("getfloat", floatType, {}, true);
    (void) newFunction("putfloat", VoidType::getType(), {new FormalParam{floatType, ""}}, true);
    (void) newFunction("getfarray", IntegerType::getTypeInt(), {new FormalParam{floatPtrType, ""}}, true);
    (void) newFunction(
        "putfarray", VoidType::getType(), {new FormalParam{IntegerType::getTypeInt(), ""}, new FormalParam{floatPtrType, ""}}, true);
}

/// @brief 进入一层新的作用域
void Module::enterScope()
{
    scopeStack->enterScope();
}

/// @brief 离开当前作用域
void Module::leaveScope()
{
    scopeStack->leaveScope();
}

/// @brief 获取当前正在处理的函数
/// @return 当前函数对象
Function * Module::getCurrentFunction()
{
    return currentFunc;
}

/// @brief 设置当前正在处理的函数
/// @param current 当前函数对象
void Module::setCurrentFunction(Function * current)
{
    currentFunc = current;
}

/// @brief 创建并注册一个函数对象
/// @param name 函数名
/// @param returnType 返回值类型
/// @param params 形参列表
/// @param builtin 是否为内建函数
/// @return 创建成功时返回函数对象，失败时返回空指针
Function * Module::newFunction(std::string name, Type * returnType, std::vector<FormalParam *> params, bool builtin)
{
    Function * tempFunc = findFunction(name);
    if (tempFunc) {
        return nullptr;
    }

    std::vector<Type *> paramsType;
    paramsType.reserve(params.size());
    for (auto & param: params) {
        paramsType.push_back(param->getType());
    }

    auto * type = new FunctionType(returnType, paramsType);
    tempFunc = new Function(std::move(name), type, builtin);
    tempFunc->getParams().assign(params.begin(), params.end());

    insertFunctionDirectly(tempFunc);

    return tempFunc;
}

/// @brief 按名称查找函数
/// @param name 函数名
/// @return 查找到的函数对象，未找到时返回空指针
Function * Module::findFunction(std::string name)
{
    auto pIter = funcMap.find(name);
    if (pIter != funcMap.end()) {
        return pIter->second;
    }

    return nullptr;
}

/// @brief 直接把函数插入模块容器
/// @param func 待插入的函数对象
void Module::insertFunctionDirectly(Function * func)
{
    funcMap.insert({func->getName(), func});
    funcVector.emplace_back(func);
}

/// @brief 直接把全局变量插入模块容器
/// @param val 待插入的全局变量对象
void Module::insertGlobalValueDirectly(GlobalVariable * val)
{
    globalVariableMap.emplace(val->getName(), val);
    globalVariableVector.push_back(val);
}

/// @brief 直接把整型常量插入模块容器
/// @param val 待插入的整型常量对象
void Module::insertConstIntDirectly(ConstInt * val)
{
    constIntMap.emplace(ConstIntKey{val->getType(), val->getVal()}, val);
}

void Module::insertConstFloatDirectly(ConstFloat * val)
{
    constFloatMap.emplace(val->getBitPattern(), val);
}

/// @brief 获取或创建整型常量对象
/// @param intVal 整型常量值
/// @return 对应的常量对象
ConstInt * Module::newConstInt(int32_t intVal)
{
    return newConstInteger(IntegerType::getTypeInt(), intVal);
}

/// @brief 获取或创建布尔常量对象
/// @param boolVal 布尔常量值
/// @return 对应的常量对象
ConstInt * Module::newConstBool(bool boolVal)
{
    return newConstInteger(IntegerType::getTypeBool(), boolVal ? 1 : 0);
}

/// @brief 获取或创建指定整数类型的常量对象
/// @param type 整数类型
/// @param intVal 常量值
/// @return 对应的常量对象
ConstInt * Module::newConstInteger(Type * type, int32_t intVal)
{
    ConstInt * val = findConstInteger(type, intVal);
    if (!val) {
        val = new ConstInt(intVal, type);
        insertConstIntDirectly(val);
    }

    return val;
}

ConstFloat * Module::newConstFloat(float floatVal)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &floatVal, sizeof(bits));

    ConstFloat * val = findConstFloat(bits);
    if (!val) {
        val = new ConstFloat(floatVal);
        insertConstFloatDirectly(val);
    }

    return val;
}

/// @brief 按数值查找整型常量对象
/// @param val 常量值
/// @return 查找到的常量对象，未找到时返回空指针
ConstInt * Module::findConstInt(int32_t val)
{
    return findConstInteger(IntegerType::getTypeInt(), val);
}

/// @brief 按类型和值查找整数常量对象
/// @param type 整数类型
/// @param val 常量值
/// @return 查找到的常量对象，未找到时返回空指针
ConstInt * Module::findConstInteger(Type * type, int32_t val)
{
    auto pIter = constIntMap.find(ConstIntKey{type, val});
    if (pIter != constIntMap.end()) {
        return pIter->second;
    }

    return nullptr;
}

ConstFloat * Module::findConstFloat(std::uint32_t bits)
{
    auto pIter = constFloatMap.find(bits);
    if (pIter != constFloatMap.end()) {
        return pIter->second;
    }

    return nullptr;
}

/// @brief 创建新的变量值对象并插入当前作用域
/// @param type 变量类型
/// @param name 变量名
/// @return 创建成功时返回变量对象，失败时返回空指针
Value * Module::newVarValue(Type * type, std::string name)
{
    Value * retVal = nullptr;

    if (!name.empty()) {
        Value * tempValue = scopeStack->findCurrentScope(name);
        if (tempValue) {
            minic_log(LOG_ERROR, "变量(%s)已经存在", name.c_str());
            return nullptr;
        }
    } else if (!currentFunc) {
        minic_log(LOG_ERROR, "变量名为空");
        return nullptr;
    }

    if (currentFunc) {
        int32_t scopeLevel = name.empty() ? 1 : scopeStack->getCurrentScopeLevel();
        retVal = currentFunc->newLocalVarValue(type, std::move(name), scopeLevel);
    } else {
        retVal = newGlobalVariable(type, std::move(name));
    }

    scopeStack->insertValue(retVal);
    return retVal;
}

/// @brief 按名称查找变量值对象
/// @param name 变量名
/// @return 查找到的变量对象，未找到时返回空指针
Value * Module::findVarValue(std::string name)
{
    return scopeStack->findAllScope(name);
}

/// @brief 创建并注册全局变量对象
/// @param type 全局变量类型
/// @param name 全局变量名
/// @return 新创建的全局变量对象
GlobalVariable * Module::newGlobalVariable(Type * type, std::string name)
{
    auto * val = new GlobalVariable(type, std::move(name));
    insertGlobalValueDirectly(val);
    return val;
}

/// @brief 按名称查找全局变量对象
/// @param name 全局变量名
/// @return 查找到的全局变量对象，未找到时返回空指针
GlobalVariable * Module::findGlobalVariable(std::string name)
{
    auto pIter = globalVariableMap.find(name);
    if (pIter != globalVariableMap.end()) {
        return pIter->second;
    }

    return nullptr;
}

/// @brief 释放模块持有的全部资源
void Module::Delete()
{
    for (auto func: funcVector) {
        delete func;
    }
    funcMap.clear();
    funcVector.clear();

    for (auto var: globalVariableVector) {
        delete var;
    }
    globalVariableMap.clear();
    globalVariableVector.clear();

    for (auto & [_, constInt]: constIntMap) {
        delete constInt;
    }
    constIntMap.clear();

    for (auto & [_, constFloat]: constFloatMap) {
        delete constFloat;
    }
    constFloatMap.clear();

    delete scopeStack;
    scopeStack = nullptr;
}

/// @brief 重新命名模块中函数里的 IR 值
void Module::renameIR()
{
    for (auto func: funcVector) {
        func->renameIR();
    }
}

/// @brief 将模块内容转换为 IR 文本
/// @return 模块对应的 IR 字符串
std::string Module::toIRString()
{
    std::string result;

    for (auto var: globalVariableVector) {
        std::string str;
        var->toDeclareString(str);
        result += str + "\n";
    }

    for (auto func: funcVector) {
        if (func->isBuiltin()) {
            std::string line = "declare " + func->getReturnType()->toString() + " " + func->getIRName() + "(";

            bool first = true;
            for (auto param: func->getParams()) {
                if (!first) {
                    line += ", ";
                }
                line += param->getType()->toString();
                first = false;
            }

            line += ")\n";
            result += line;
            continue;
        }

        std::string str;
        func->toString(str);
        result += str;
    }

    return result;
}

/// @brief 将 IR 文本输出到指定文件
/// @param filePath 输出文件路径
void Module::outputIR(const std::string & filePath)
{
    FILE * fp = fopen(filePath.c_str(), "w");
    if (nullptr == fp) {
        printf("fopen() failed\n");
        return;
    }

    std::string text = toIRString();
    fprintf(fp, "%s", text.c_str());
    fclose(fp);
}
