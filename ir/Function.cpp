///
/// @file Function.cpp
/// @brief 函数实现
///

#include "Function.h"

#include <cstdlib>
#include <string>

#include "IRConstant.h"

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

InterCode & Function::getInterCode()
{
    return code;
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

    if (!blocks.empty()) {
        // Block-structured IR (Phase 3+)
        for (auto * bb : blocks) {
            std::string bbStr;
            bb->toString(bbStr);
            str += bbStr;
        }
    } else {
        // Legacy linear IR
        for (auto & var: this->varsVector) {
            str += "\tdeclare " + var->getType()->toString() + " " + var->getIRName();
            std::string realName = var->getName();
            if (!realName.empty()) {
                str += " ; " + std::to_string(var->getScopeLevel()) + ":" + realName;
            }
            str += "\n";
        }

        for (auto & inst: code.getInsts()) {
            if (inst->hasResultValue()) {
                str += "\tdeclare " + inst->getType()->toString() + " " + inst->getIRName() + "\n";
            }
        }

        for (auto & inst: code.getInsts()) {
            std::string instStr;
            inst->toString(instStr);
            if (instStr.empty()) {
                continue;
            }
            if (inst->getOp() == IRInstOperator::IRINST_OP_LABEL) {
                str += instStr + "\n";
            } else {
                str += "\t" + instStr + "\n";
            }
        }
    }

    str += "}\n";
}

void Function::setExitLabel(Instruction * inst)
{
    exitLabel = inst;
}

Instruction * Function::getExitLabel()
{
    return exitLabel;
}

void Function::setReturnValue(LocalVariable * val)
{
    returnValue = val;
}

LocalVariable * Function::getReturnValue()
{
    return returnValue;
}

int Function::getMaxDep()
{
    return maxDepth;
}

void Function::setMaxDep(int dep)
{
    maxDepth = dep;
    relocated = true;
}

int Function::getExtraStackSize()
{
    return maxExtraStackSize;
}

void Function::setExtraStackSize(int size)
{
    maxExtraStackSize = size;
}

std::vector<int32_t> & Function::getProtectedReg()
{
    return protectedRegs;
}

std::string & Function::getProtectedRegStr()
{
    return protectedRegStr;
}

int Function::getMaxFuncCallArgCnt()
{
    return maxFuncCallArgCnt;
}

void Function::setMaxFuncCallArgCnt(int count)
{
    maxFuncCallArgCnt = count;
}

bool Function::getExistFuncCall()
{
    return funcCallExist;
}

void Function::setExistFuncCall(bool exist)
{
    funcCallExist = exist;
}

LocalVariable * Function::newLocalVarValue(Type * type, std::string name, int32_t scope_level)
{
    auto * varValue = new LocalVariable(type, std::move(name), scope_level);
    varsVector.push_back(varValue);
    return varValue;
}

MemVariable * Function::newMemVariable(Type * type)
{
    auto * memValue = new MemVariable(type);
    memVector.push_back(memValue);
    return memValue;
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
    code.Delete();

    for (auto & param: params) {
        delete param;
    }
    params.clear();

    for (auto & var: varsVector) {
        delete var;
    }
    varsVector.clear();

    for (auto & mem: memVector) {
        delete mem;
    }
    memVector.clear();

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

    if (!blocks.empty()) {
        // Block-structured IR (Phase 3+): rename blocks and their instructions.
        // LocalVariable objects in varsVector are lookup keys only – skip them.
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
        return;
    }

    // Legacy linear IR renaming
    for (auto & var: this->varsVector) {
        var->setIRName(IR_LOCAL_VARNAME_PREFIX + std::to_string(nameIndex++));
    }

    for (auto inst: this->getInterCode().getInsts()) {
        if (inst->getOp() == IRInstOperator::IRINST_OP_LABEL) {
            inst->setIRName(IR_LABEL_PREFIX + std::to_string(nameIndex++));
        } else if (inst->hasResultValue()) {
            inst->setIRName(IR_TEMP_VARNAME_PREFIX + std::to_string(nameIndex++));
        }
    }
}

int32_t Function::getRealArgcount()
{
    return this->realArgCount;
}

void Function::realArgCountInc()
{
    this->realArgCount++;
}

void Function::realArgCountReset()
{
    this->realArgCount = 0;
}
