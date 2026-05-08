///
/// @file GlobalToLocal.cpp
/// @brief 将 main 专属全局标量下沉为局部槽位的 pass 实现
///

#include "GlobalToLocal.h"

#include <list>
#include <vector>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "ConstFloat.h"
#include "Function.h"
#include "GlobalVariable.h"
#include "Instruction.h"
#include "Module.h"
#include "StoreInst.h"
#include "Type.h"
#include "Use.h"
#include "Value.h"

namespace {

/// @brief 找到入口块里第一个非 alloca 指令的位置
/// @param entry 目标入口块
/// @return 可用于插入初始化指令的位置
std::list<Instruction *>::iterator findEntryInitInsertPoint(BasicBlock * entry)
{
    auto & insts = entry->getInstructions();
    auto insertIt = insts.begin();
    while (insertIt != insts.end() && dynamic_cast<AllocaInst *>(*insertIt) != nullptr) {
        ++insertIt;
    }
    return insertIt;
}

/// @brief 为受支持的全局标量构造等价初值
/// @param module 所属模块
/// @param global 待下沉的全局变量
/// @return 初始化值，若类型不支持则返回空指针
Value * buildScalarInitializer(Module * module, GlobalVariable * global)
{
    if (module == nullptr || global == nullptr) {
        return nullptr;
    }

    Type * valueType = global->getValueType();
    if (valueType == nullptr) {
        return nullptr;
    }

    if (valueType->isFloatType()) {
        if (global->getInitKind() == GlobalVariable::InitKind::Zero) {
            return module->newConstFloat(0.0f);
        }
        if (global->getInitKind() == GlobalVariable::InitKind::Float) {
            return module->newConstFloat(global->getInitFloatValue());
        }
        return nullptr;
    }

    if (!valueType->isInt32Type()) {
        return nullptr;
    }

    if (global->getInitKind() == GlobalVariable::InitKind::Zero) {
        return module->newConstInt32(0);
    }
    if (global->getInitKind() == GlobalVariable::InitKind::Int) {
        return module->newConstInt32(global->getInitIntValue());
    }

    return nullptr;
}

} // namespace

/// @brief 构造全局转局部 pass
/// @param _module 待处理模块
GlobalToLocal::GlobalToLocal(Module * _module) : module(_module)
{}

/// @brief 获取可作为下沉目标的 main 函数
/// @return 满足条件时返回 main 函数，否则返回空指针
Function * GlobalToLocal::getMainFunction() const
{
    if (module == nullptr) {
        return nullptr;
    }

    Function * mainFunc = module->findFunction("main");
    if (mainFunc == nullptr || mainFunc->isBuiltin() || mainFunc->getBlocks().empty()) {
        return nullptr;
    }

    return mainFunc;
}

/// @brief 判断一个全局标量是否可安全下沉到 main
/// @param global 待检查的全局变量
/// @param mainFunc 目标 main 函数
/// @return true 表示该全局变量可被下沉
bool GlobalToLocal::canInternalizeToMain(GlobalVariable * global, Function * mainFunc) const
{
    if (global == nullptr || mainFunc == nullptr || global->getUseList().empty()) {
        return false;
    }

    Type * valueType = global->getValueType();
    if (valueType == nullptr || (!valueType->isInt32Type() && !valueType->isFloatType())) {
        return false;
    }

    if (buildScalarInitializer(module, global) == nullptr) {
        return false;
    }

    for (auto * use : global->getUseList()) {
        auto * userInst = dynamic_cast<Instruction *>(use->getUser());
        if (userInst == nullptr || userInst->getFunction() != mainFunc) {
            return false;
        }
    }

    return true;
}

/// @brief 在入口块中创建局部槽位
/// @param func 目标函数
/// @param global 待下沉的全局变量
/// @return 新建的入口块 alloca
AllocaInst * GlobalToLocal::createEntrySlot(Function * func, GlobalVariable * global) const
{
    if (func == nullptr || global == nullptr) {
        return nullptr;
    }

    BasicBlock * entry = func->getEntryBlock();
    if (entry == nullptr) {
        return nullptr;
    }

    auto & insts = entry->getInstructions();
    auto insertIt = findEntryInitInsertPoint(entry);
    auto * slot = new AllocaInst(func, global->getValueType());
    slot->setParentBlock(entry);
    insts.insert(insertIt, slot);
    return slot;
}

/// @brief 在入口块中补上等价的初始化 store
/// @param func 目标函数
/// @param global 原全局变量
/// @param slot 新建局部槽位
/// @return true 表示初始化已成功补齐
bool GlobalToLocal::materializeScalarInitializer(Function * func, GlobalVariable * global, AllocaInst * slot) const
{
    if (func == nullptr || global == nullptr || slot == nullptr) {
        return false;
    }

    BasicBlock * entry = func->getEntryBlock();
    if (entry == nullptr) {
        return false;
    }

    Value * initValue = buildScalarInitializer(module, global);
    if (initValue == nullptr) {
        return false;
    }

    auto & insts = entry->getInstructions();
    auto insertIt = findEntryInitInsertPoint(entry);
    auto * initStore = new StoreInst(func, initValue, slot);
    initStore->setParentBlock(entry);
    insts.insert(insertIt, initStore);
    return true;
}

/// @brief 执行全局转局部优化
/// @return 若本轮修改了 IR 则返回 true
bool GlobalToLocal::run()
{
    Function * mainFunc = getMainFunction();
    if (mainFunc == nullptr) {
        return false;
    }

    std::vector<GlobalVariable *> globals(module->getGlobalVariables().begin(), module->getGlobalVariables().end());
    bool changed = false;

    for (auto * global : globals) {
        if (!canInternalizeToMain(global, mainFunc)) {
            continue;
        }

        AllocaInst * slot = createEntrySlot(mainFunc, global);
        if (slot == nullptr || !materializeScalarInitializer(mainFunc, global, slot)) {
            continue;
        }

        global->replaceAllUseWith(slot);
        changed = module->removeGlobalVariable(global) || changed;
    }

    return changed;
}