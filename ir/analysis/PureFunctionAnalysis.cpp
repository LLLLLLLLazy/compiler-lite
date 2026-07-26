///
/// @file PureFunctionAnalysis.cpp
/// @brief 基于调用图强连通分量的纯函数与内存独立性分析
///

#include "PureFunctionAnalysis.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AllocaInst.h"
#include "CallInst.h"
#include "FormalParam.h"
#include "Function.h"
#include "GlobalVariable.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "MemoryAccess.h"
#include "Module.h"
#include "StoreInst.h"
#include "Type.h"
#include "Value.h"
#include "VectorInst.h"

namespace {

using CallGraph = std::unordered_map<Function *, std::vector<Function *>>;

/// @brief 使用 Tarjan 算法构造调用图强连通分量
class CallGraphSCCBuilder {
public:
    /// @brief 构造 SCC 构建器
    /// @param functions 模块内全部有定义函数
    /// @param graph 仅包含有定义函数之间调用边的调用图
    CallGraphSCCBuilder(const std::vector<Function *> & functions, const CallGraph & graph)
        : nodes(functions), edges(graph)
    {}

    /// @brief 计算调用图的全部强连通分量
    /// @return 每个元素是一组相互递归函数
    std::vector<std::vector<Function *>> run()
    {
        for (auto * function : nodes) {
            if (indices.find(function) == indices.end()) {
                strongConnect(function);
            }
        }
        return components;
    }

private:
    /// @brief 从一个未访问函数继续 Tarjan 深度优先搜索
    /// @param function 当前函数
    void strongConnect(Function * function)
    {
        const std::size_t index = nextIndex++;
        indices[function] = index;
        lowLinks[function] = index;
        stack.push_back(function);
        onStack.insert(function);

        auto edgeIt = edges.find(function);
        if (edgeIt != edges.end()) {
            for (auto * callee : edgeIt->second) {
                if (indices.find(callee) == indices.end()) {
                    strongConnect(callee);
                    lowLinks[function] = std::min(lowLinks[function], lowLinks[callee]);
                } else if (onStack.count(callee) != 0U) {
                    lowLinks[function] = std::min(lowLinks[function], indices[callee]);
                }
            }
        }

        if (lowLinks[function] != indices[function]) {
            return;
        }

        std::vector<Function *> component;
        while (!stack.empty()) {
            Function * member = stack.back();
            stack.pop_back();
            onStack.erase(member);
            component.push_back(member);
            if (member == function) {
                break;
            }
        }
        components.push_back(std::move(component));
    }

    const std::vector<Function *> & nodes;
    const CallGraph & edges;
    std::size_t nextIndex = 0;
    std::unordered_map<Function *, std::size_t> indices;
    std::unordered_map<Function *, std::size_t> lowLinks;
    std::vector<Function *> stack;
    std::unordered_set<Function *> onStack;
    std::vector<std::vector<Function *>> components;
};

/// @brief 一个调用图 SCC 的局部事实与求解结果
struct ComponentFacts {
    bool locallyPure = true;
    bool locallyMemoryIndependent = true;
    bool solving = false;
    bool solved = false;
    bool pure = false;
    bool memoryIndependent = false;
    std::vector<std::size_t> callees;
};

} // namespace

/// @brief 构造模块级纯函数分析器
/// @param module 当前模块
PureFunctionAnalysis::PureFunctionAnalysis(Module * module) : mod(module)
{}

bool PureFunctionAnalysis::isPure(Function * function)
{
    analyzeModule();
    auto it = pure.find(function);
    return it != pure.end() && it->second;
}

bool PureFunctionAnalysis::isMemoryIndependent(Function * function)
{
    analyzeModule();
    auto it = memoryIndependent.find(function);
    return it != memoryIndependent.end() && it->second;
}

/// @brief 通过参数写集合不动点计算不会被模块内代码改写的全局对象
///
/// 直接 store 先标记对应的全局或形参
/// 调用边再把被调函数的形参写集合反向传播到实参根对象
void PureFunctionAnalysis::analyzeReadOnlyGlobals()
{
    readOnlyGlobals.clear();
    if (!mod) {
        return;
    }

    std::unordered_set<GlobalVariable *> modifiedGlobals;
    std::unordered_map<Function *, std::vector<bool>> writtenParams;
    for (auto * function : mod->getFunctionList()) {
        if (!function) {
            continue;
        }

        auto & params = function->getParams();
        auto & writes = writtenParams[function];
        writes.assign(params.size(), false);
        if (!function->isBuiltin() && !function->getBlocks().empty()) {
            continue;
        }

        for (std::size_t index = 0; index < params.size(); ++index) {
            if (params[index] && params[index]->getType()->isPointerType()) {
                writes[index] = true;
            }
        }
    }

    auto markPointerWritten = [&](Function * function, Value * pointer) {
        bool changed = false;
        Value * root = getPointerRoot(pointer);
        if (auto * global = dynamic_cast<GlobalVariable *>(root)) {
            return modifiedGlobals.insert(global).second;
        }
        if (dynamic_cast<AllocaInst *>(root) != nullptr) {
            return false;
        }
        if (auto * param = dynamic_cast<FormalParam *>(root)) {
            auto writesIt = writtenParams.find(function);
            if (writesIt == writtenParams.end()) {
                return false;
            }
            auto & params = function->getParams();
            for (std::size_t index = 0; index < params.size(); ++index) {
                if (params[index] == param && !writesIt->second[index]) {
                    writesIt->second[index] = true;
                    return true;
                }
            }
            return false;
        }

        for (auto * global : mod->getGlobalVariables()) {
            if (global) {
                changed = modifiedGlobals.insert(global).second || changed;
            }
        }
        auto writesIt = writtenParams.find(function);
        if (writesIt != writtenParams.end()) {
            auto & params = function->getParams();
            for (std::size_t index = 0; index < params.size(); ++index) {
                if (params[index] && params[index]->getType()->isPointerType() &&
                    !writesIt->second[index]) {
                    writesIt->second[index] = true;
                    changed = true;
                }
            }
        }
        return changed;
    };

    for (auto * function : mod->getFunctionList()) {
        if (!function || function->isBuiltin() || function->getBlocks().empty()) {
            continue;
        }
        for (auto * bb : function->getBlocks()) {
            for (auto * inst : bb->getInstructions()) {
                if (!inst || inst->isDead()) {
                    continue;
                }
                if (auto * store = dynamic_cast<StoreInst *>(inst)) {
                    markPointerWritten(function, store->getPointerOperand());
                    continue;
                }
                if (auto * store = dynamic_cast<VectorStoreInst *>(inst)) {
                    markPointerWritten(function, store->getPointerOperand());
                }
            }
        }
    }

    bool changed = false;
    do {
        changed = false;
        for (auto * function : mod->getFunctionList()) {
            if (!function || function->isBuiltin() || function->getBlocks().empty()) {
                continue;
            }
            for (auto * bb : function->getBlocks()) {
                for (auto * inst : bb->getInstructions()) {
                    auto * call = dynamic_cast<CallInst *>(inst);
                    if (!call || call->isDead()) {
                        continue;
                    }

                    auto calleeIt = writtenParams.find(call->getCallee());
                    for (int32_t index = 0; index < call->getArgCount(); ++index) {
                        Value * argument = call->getArg(index);
                        bool mayWrite = argument && argument->getType()->isPointerType();
                        if (calleeIt != writtenParams.end() &&
                            static_cast<std::size_t>(index) < calleeIt->second.size()) {
                            mayWrite = calleeIt->second[static_cast<std::size_t>(index)];
                        }
                        if (mayWrite) {
                            changed = markPointerWritten(function, argument) || changed;
                        }
                    }
                }
            }
        }
    } while (changed);

    for (auto * global : mod->getGlobalVariables()) {
        if (global && modifiedGlobals.count(global) == 0U) {
            readOnlyGlobals.insert(global);
        }
    }
}

/// @brief 判断纯函数可读取的指针来源
/// @param pointer 待读地址
/// @return true 表示来自局部对象、形参或全局对象
bool PureFunctionAnalysis::isAllowedReadPointer(Value * pointer) const
{
    Value * root = getPointerRoot(pointer);
    if (dynamic_cast<AllocaInst *>(root) != nullptr ||
        dynamic_cast<FormalParam *>(root) != nullptr ||
        dynamic_cast<GlobalVariable *>(root) != nullptr) {
        return true;
    }
    return false;
}

/// @brief 判断读取是否不依赖调用者可变内存
/// @param pointer 待读地址
/// @return true 表示来自本帧对象或已证明只读的全局对象
bool PureFunctionAnalysis::isMemoryIndependentPointer(Value * pointer) const
{
    Value * root = getPointerRoot(pointer);
    if (dynamic_cast<AllocaInst *>(root) != nullptr) {
        return true;
    }
    auto * global = dynamic_cast<GlobalVariable *>(root);
    return global && readOnlyGlobals.count(global) != 0U;
}

/// @brief 构造调用图 SCC 并在缩点 DAG 上求解纯度与内存独立性
///
/// SCC 内的调用只形成递归假设，不直接证明或否定纯度
/// 每个分量必须先通过全部局部指令检查，再要求所有分量外被调函数满足同一性质
void PureFunctionAnalysis::analyzeModule()
{
    if (analyzed) {
        return;
    }
    analyzed = true;

    if (!mod) {
        return;
    }

    analyzeReadOnlyGlobals();

    std::vector<Function *> functions;
    std::unordered_set<Function *> definedFunctions;
    for (auto * function : mod->getFunctionList()) {
        if (!function || function->isBuiltin() || function->getBlocks().empty()) {
            continue;
        }
        functions.push_back(function);
        definedFunctions.insert(function);
    }

    CallGraph graph;
    for (auto * function : functions) {
        auto & callees = graph[function];
        for (auto * bb : function->getBlocks()) {
            for (auto * inst : bb->getInstructions()) {
                auto * call = dynamic_cast<CallInst *>(inst);
                if (!call || call->isDead()) {
                    continue;
                }
                if (definedFunctions.count(call->getCallee()) != 0U) {
                    callees.push_back(call->getCallee());
                }
            }
        }
    }

    CallGraphSCCBuilder builder(functions, graph);
    std::vector<std::vector<Function *>> components = builder.run();
    std::unordered_map<Function *, std::size_t> componentOf;
    for (std::size_t index = 0; index < components.size(); ++index) {
        for (auto * function : components[index]) {
            componentOf[function] = index;
        }
    }

    std::vector<ComponentFacts> facts(components.size());
    for (std::size_t index = 0; index < components.size(); ++index) {
        ComponentFacts & component = facts[index];
        for (auto * function : components[index]) {
            for (auto * bb : function->getBlocks()) {
                for (auto * inst : bb->getInstructions()) {
                    if (!inst || inst->isDead()) {
                        continue;
                    }

                    if (auto * call = dynamic_cast<CallInst *>(inst)) {
                        auto calleeIt = componentOf.find(call->getCallee());
                        if (calleeIt == componentOf.end()) {
                            component.locallyPure = false;
                            component.locallyMemoryIndependent = false;
                            continue;
                        }
                        if (calleeIt->second != index) {
                            component.callees.push_back(calleeIt->second);
                        }
                        continue;
                    }

                    if (!isLocallyPureInstruction(inst)) {
                        component.locallyPure = false;
                        component.locallyMemoryIndependent = false;
                        continue;
                    }

                    if (auto * load = dynamic_cast<LoadInst *>(inst)) {
                        if (!isMemoryIndependentPointer(load->getPointerOperand())) {
                            component.locallyMemoryIndependent = false;
                        }
                        continue;
                    }

                    auto * vectorLoad = dynamic_cast<VectorLoadInst *>(inst);
                    if (vectorLoad && !isMemoryIndependentPointer(vectorLoad->getPointerOperand())) {
                        component.locallyMemoryIndependent = false;
                    }
                }
            }
        }

        std::sort(component.callees.begin(), component.callees.end());
        component.callees.erase(
            std::unique(component.callees.begin(), component.callees.end()), component.callees.end());
    }

    std::function<void(std::size_t)> solve = [&](std::size_t index) {
        ComponentFacts & component = facts[index];
        if (component.solved) {
            return;
        }
        if (component.solving) {
            component.pure = false;
            component.memoryIndependent = false;
            component.solved = true;
            return;
        }

        component.solving = true;
        bool componentPure = component.locallyPure;
        bool componentMemoryIndependent = component.locallyMemoryIndependent;
        for (std::size_t callee : component.callees) {
            solve(callee);
            componentPure = componentPure && facts[callee].pure;
            componentMemoryIndependent =
                componentMemoryIndependent && facts[callee].memoryIndependent;
        }
        component.solving = false;
        component.pure = componentPure;
        component.memoryIndependent = componentPure && componentMemoryIndependent;
        component.solved = true;
    };

    for (std::size_t index = 0; index < components.size(); ++index) {
        solve(index);
        for (auto * function : components[index]) {
            pure[function] = facts[index].pure;
            memoryIndependent[function] = facts[index].memoryIndependent;
        }
    }
}

/// @brief 判断非调用指令是否满足 SCC 的局部纯度条件
/// @param inst 待检查指令
/// @return true 表示指令不产生调用者可见写入且内存读取来源可证明
bool PureFunctionAnalysis::isLocallyPureInstruction(Instruction * inst) const
{
    if (!inst || inst->isDead() || inst->isTerminator()) {
        return true;
    }

    if (dynamic_cast<AllocaInst *>(inst) != nullptr) {
        return true;
    }

    if (auto * load = dynamic_cast<LoadInst *>(inst)) {
        return isAllowedReadPointer(load->getPointerOperand());
    }

    if (auto * store = dynamic_cast<StoreInst *>(inst)) {
        return isLocalMemory(store->getPointerOperand());
    }

    if (auto * load = dynamic_cast<VectorLoadInst *>(inst)) {
        return isAllowedReadPointer(load->getPointerOperand());
    }

    if (auto * store = dynamic_cast<VectorStoreInst *>(inst)) {
        return isLocalMemory(store->getPointerOperand());
    }

    if (dynamic_cast<CallInst *>(inst) != nullptr) {
        return true;
    }

    if (inst->mayReadMemory() || inst->mayWriteMemory()) {
        return false;
    }
    return !inst->mayHaveSideEffects();
}
