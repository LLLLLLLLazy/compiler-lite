///
/// @file DeadFunctionElim.cpp
/// @brief 死函数消除 pass 实现
///

#include "DeadFunctionElim.h"

#include <vector>

#include "BasicBlock.h"
#include "CallInst.h"
#include "Function.h"
#include "Instruction.h"
#include "Module.h"

/// @brief 构造死函数消除 pass
/// @param _module 待处理模块
DeadFunctionElim::DeadFunctionElim(Module * _module) : module(_module)
{}

/// @brief 从入口函数出发标记所有可达的用户函数
/// @param entry 入口函数
/// @param reachable 输出可达函数集合
void DeadFunctionElim::markReachable(Function * entry, std::unordered_set<Function *> & reachable) const
{
    if (entry == nullptr) {
        return;
    }

    std::vector<Function *> worklist;
    if (reachable.insert(entry).second) {
        worklist.push_back(entry);
    }

    while (!worklist.empty()) {
        Function * func = worklist.back();
        worklist.pop_back();

        for (auto * bb : func->getBlocks()) {
            for (auto * inst : bb->getInstructions()) {
                auto * call = dynamic_cast<CallInst *>(inst);
                if (call == nullptr) {
                    continue;
                }

                Function * callee = call->getCallee();
                if (callee == nullptr || callee->isBuiltin() || callee->getBlocks().empty()) {
                    continue;
                }

                if (reachable.insert(callee).second) {
                    worklist.push_back(callee);
                }
            }
        }
    }
}

/// @brief 删除从 main 不可达的用户函数
/// @return true 表示移除了至少一个函数
bool DeadFunctionElim::run()
{
    if (module == nullptr) {
        return false;
    }

    Function * mainFunc = module->findFunction("main");
    if (mainFunc == nullptr || mainFunc->getBlocks().empty()) {
        return false;
    }

    std::unordered_set<Function *> reachable;
    markReachable(mainFunc, reachable);

    std::vector<Function *> deadFuncs;
    for (auto * func : module->getFunctionList()) {
        if (func == nullptr || func->isBuiltin() || func->getBlocks().empty()) {
            continue;
        }
        if (reachable.find(func) == reachable.end()) {
            deadFuncs.push_back(func);
        }
    }

    bool changed = false;
    for (auto * func : deadFuncs) {
        changed = module->removeFunction(func) || changed;
    }

    return changed;
}
