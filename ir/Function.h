///
/// @file Function.h
/// @brief 函数头文件
///

#pragma once

#include <string>
#include <vector>

#include "FormalParam.h"
#include "FunctionType.h"
#include "GlobalValue.h"
#include "IRCode.h"
#include "LocalVariable.h"
#include "MemVariable.h"

class Function : public GlobalValue {

public:
    explicit Function(std::string _name, FunctionType * _type, bool _builtin = false);

    ~Function() override;

    Type * getReturnType();

    std::vector<FormalParam *> & getParams();

    InterCode & getInterCode();

    bool isBuiltin();

    void toString(std::string & str);

    void setExitLabel(Instruction * inst);

    Instruction * getExitLabel();

    void setReturnValue(LocalVariable * val);

    LocalVariable * getReturnValue();

    std::vector<LocalVariable *> & getVarValues()
    {
        return varsVector;
    }

    [[nodiscard]] bool isFunction() const override
    {
        return true;
    }

    int getMaxDep();

    void setMaxDep(int dep);

    int getExtraStackSize();

    void setExtraStackSize(int size);

    int getMaxFuncCallArgCnt();

    void setMaxFuncCallArgCnt(int count);

    bool getExistFuncCall();

    void setExistFuncCall(bool exist);

    std::vector<int32_t> & getProtectedReg();

    std::string & getProtectedRegStr();

    LocalVariable * newLocalVarValue(Type * type, std::string name = "", int32_t scope_level = 1);

    MemVariable * newMemVariable(Type * type);

    void Delete();

    void renameIR();

    int32_t getRealArgcount();

    void realArgCountInc();

    void realArgCountReset();

private:
    Type * returnType = nullptr;
    std::vector<FormalParam *> params;
    bool builtIn = false;
    InterCode code;
    std::vector<LocalVariable *> varsVector;
    std::vector<MemVariable *> memVector;
    Instruction * exitLabel = nullptr;
    LocalVariable * returnValue = nullptr;
    int maxDepth = 0;
    int maxExtraStackSize = 0;
    bool funcCallExist = false;
    int maxFuncCallArgCnt = 0;
    bool relocated = false;
    std::vector<int32_t> protectedRegs;
    std::string protectedRegStr;
    int32_t realArgCount = 0;
};
