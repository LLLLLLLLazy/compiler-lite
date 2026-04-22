///
/// @file Module.h
/// @brief 符号表-模块类
///

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "ConstInt.h"
#include "Function.h"
#include "GlobalVariable.h"
#include "Type.h"

class ScopeStack;

class Module {

public:
    explicit Module(std::string _name);

    virtual ~Module() = default;

    std::string toIRString();

    [[nodiscard]] std::string getName() const
    {
        return name;
    }

    void enterScope();

    void leaveScope();

    Function * getCurrentFunction();

    void setCurrentFunction(Function * current);

    Function *
    newFunction(std::string name, Type * returnType, std::vector<FormalParam *> params = {}, bool builtin = false);

    Function * findFunction(std::string name);

    std::vector<GlobalVariable *> & getGlobalVariables()
    {
        return globalVariableVector;
    }

    std::vector<Function *> & getFunctionList()
    {
        return funcVector;
    }

    ConstInt * newConstInt(int32_t intVal);

    Value * newVarValue(Type * type, std::string name = "");

    Value * findVarValue(std::string name);

    void Delete();

    void outputIR(const std::string & filePath);

    void renameIR();

protected:
    ConstInt * findConstInt(int32_t val);

    GlobalVariable * newGlobalVariable(Type * type, std::string name);

    GlobalVariable * findGlobalVariable(std::string name);

    void insertFunctionDirectly(Function * func);

    void insertGlobalValueDirectly(GlobalVariable * val);

    void insertConstIntDirectly(ConstInt * val);

private:
    std::string name;
    ScopeStack * scopeStack = nullptr;
    Function * currentFunc = nullptr;
    std::unordered_map<std::string, Function *> funcMap;
    std::vector<Function *> funcVector;
    std::unordered_map<std::string, GlobalVariable *> globalVariableMap;
    std::vector<GlobalVariable *> globalVariableVector;
    std::unordered_map<int32_t, ConstInt *> constIntMap;
};
