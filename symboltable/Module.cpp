///
/// @file Module.cpp
/// @brief 符号表-模块类
///

#include "Module.h"

#include <cstdio>

#include "Common.h"
#include "FunctionType.h"
#include "IntegerType.h"
#include "ScopeStack.h"
#include "VoidType.h"

Module::Module(std::string _name) : name(std::move(_name))
{
    scopeStack = new ScopeStack();
    scopeStack->enterScope();

    (void) newFunction("putint", VoidType::getType(), {new FormalParam{IntegerType::getTypeInt(), ""}}, true);
    (void) newFunction("getint", IntegerType::getTypeInt(), {}, true);
    (void) newFunction("putch", VoidType::getType(), {new FormalParam{IntegerType::getTypeInt(), ""}}, true);
    (void) newFunction("getch", IntegerType::getTypeInt(), {}, true);
}

void Module::enterScope()
{
    scopeStack->enterScope();
}

void Module::leaveScope()
{
    scopeStack->leaveScope();
}

Function * Module::getCurrentFunction()
{
    return currentFunc;
}

void Module::setCurrentFunction(Function * current)
{
    currentFunc = current;
}

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

Function * Module::findFunction(std::string name)
{
    auto pIter = funcMap.find(name);
    if (pIter != funcMap.end()) {
        return pIter->second;
    }

    return nullptr;
}

void Module::insertFunctionDirectly(Function * func)
{
    funcMap.insert({func->getName(), func});
    funcVector.emplace_back(func);
}

void Module::insertGlobalValueDirectly(GlobalVariable * val)
{
    globalVariableMap.emplace(val->getName(), val);
    globalVariableVector.push_back(val);
}

void Module::insertConstIntDirectly(ConstInt * val)
{
    constIntMap.emplace(val->getVal(), val);
}

ConstInt * Module::newConstInt(int32_t intVal)
{
    ConstInt * val = findConstInt(intVal);
    if (!val) {
        val = new ConstInt(intVal);
        insertConstIntDirectly(val);
    }

    return val;
}

ConstInt * Module::findConstInt(int32_t val)
{
    auto pIter = constIntMap.find(val);
    if (pIter != constIntMap.end()) {
        return pIter->second;
    }

    return nullptr;
}

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

Value * Module::findVarValue(std::string name)
{
    return scopeStack->findAllScope(name);
}

GlobalVariable * Module::newGlobalVariable(Type * type, std::string name)
{
    auto * val = new GlobalVariable(type, std::move(name));
    insertGlobalValueDirectly(val);
    return val;
}

GlobalVariable * Module::findGlobalVariable(std::string name)
{
    auto pIter = globalVariableMap.find(name);
    if (pIter != globalVariableMap.end()) {
        return pIter->second;
    }

    return nullptr;
}

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

    delete scopeStack;
    scopeStack = nullptr;
}

void Module::renameIR()
{
    for (auto func: funcVector) {
        func->renameIR();
    }
}

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
