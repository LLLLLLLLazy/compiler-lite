///
/// @file Function.cpp
/// @brief 函数实现
///

#include "Function.h"

#include <string>

#include "IRConstant.h"
#include "Instruction.h"

Function::Function(std::string _name, FunctionType * _type, bool _builtin)
    : GlobalValue(_type, std::move(_name)), builtIn(_builtin)
{
    returnType = _type->getReturnType();
    setAlignment(1);
}

Function::~Function()
{
    Delete();
}

Type * Function::getReturnType()
{
    return returnType;
}

std::vector<FormalParam *> & Function::getParams()
{
    return params;
}

bool Function::isBuiltin()
{
    return builtIn;
}

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

LocalVariable * Function::newLocalVarValue(Type * type, std::string name, int32_t scope_level)
{
    auto * varValue = new LocalVariable(type, std::move(name), scope_level);
    varsVector.push_back(varValue);
    return varValue;
}

BasicBlock * Function::newBasicBlock()
{
    auto * bb = new BasicBlock(this);
    if (blocks.empty()) {
        entryBlock = bb;
    }
    blocks.push_back(bb);
    return bb;
}

void Function::Delete()
{
    for (auto & param: params) {
        delete param;
    }
    params.clear();

    for (auto & var: varsVector) {
        delete var;
    }
    varsVector.clear();

    for (auto * bb: blocks) {
        delete bb;
    }
    blocks.clear();
    entryBlock = nullptr;
}

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
