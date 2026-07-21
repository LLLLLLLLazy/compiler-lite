///
/// @file ParamAliasAnalysis.cpp
/// @brief 基于全调用点根对象收集的形参别名分析实现
///

#include "ParamAliasAnalysis.h"

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "CallInst.h"
#include "FormalParam.h"
#include "Function.h"
#include "GlobalVariable.h"
#include "MemoryAccess.h"
#include "Module.h"
#include "Value.h"

ParamAliasAnalysis::ParamAliasAnalysis(Module * module) : mod(module)
{}

void ParamAliasAnalysis::buildIndexIfNeeded()
{
    if (indexBuilt || !mod) {
        return;
    }
    indexBuilt = true;

    for (auto * function : mod->getFunctionList()) {
        if (!function) {
            continue;
        }

        auto & params = function->getParams();
        for (std::size_t index = 0; index < params.size(); ++index) {
            if (params[index]) {
                paramSlot[params[index]] = {function, index};
            }
        }

        if (function->isBuiltin()) {
            continue;
        }
        for (auto * bb : function->getBlocks()) {
            for (auto * inst : bb->getInstructions()) {
                auto * call = dynamic_cast<CallInst *>(inst);
                if (call && call->getCallee()) {
                    callSites[call->getCallee()].push_back(call);
                }
            }
        }
    }
}

const ParamAliasAnalysis::PointsTo & ParamAliasAnalysis::resolvePointsTo(FormalParam * param)
{
    buildIndexIfNeeded();

    auto cached = pointsToCache.find(param);
    if (cached != pointsToCache.end()) {
        return cached->second;
    }

    PointsTo result;

    auto slot = paramSlot.find(param);
    if (!mod || slot == paramSlot.end()) {
        result.unknown = true;
        return pointsToCache.emplace(param, std::move(result)).first->second;
    }

    // 递归环（形参沿互递归调用链传回自身）：一律按 unknown 处理。
    // 环内成员的真实指向集需要不动点并集，单趟解析拿到的中间结果可能过窄，
    // 缓存后会漏报别名，因此这里直接取最保守值保证健全性
    if (!resolving.insert(param).second) {
        static const PointsTo unknownPointsTo{{}, true};
        return unknownPointsTo;
    }

    Function * function = slot->second.first;
    std::size_t index = slot->second.second;

    auto sites = callSites.find(function);
    if (sites == callSites.end()) {
        // 无调用点（如 main 或死函数）：形参不会绑定任何对象
        resolving.erase(param);
        return pointsToCache.emplace(param, std::move(result)).first->second;
    }

    for (auto * call : sites->second) {
        if (static_cast<std::size_t>(call->getArgCount()) <= index) {
            result.unknown = true;
            break;
        }

        Value * root = getPointerRoot(call->getArg(static_cast<int32_t>(index)));
        if (dynamic_cast<GlobalVariable *>(root) || dynamic_cast<AllocaInst *>(root)) {
            result.roots.insert(root);
            continue;
        }

        auto * callerParam = dynamic_cast<FormalParam *>(root);
        if (callerParam) {
            // 自递归把同一形参原样传下去（如 fft(arr,...) 的递归实参 arr）：
            // 该边对指向集是恒等约束，直接跳过，不触发环处理
            if (callerParam == param) {
                continue;
            }
            const PointsTo & inherited = resolvePointsTo(callerParam);
            result.unknown = result.unknown || inherited.unknown;
            result.roots.insert(inherited.roots.begin(), inherited.roots.end());
            continue;
        }

        // 指针 phi/select 等无法归类的根：按可能指向任何对象处理
        result.unknown = true;
        break;
    }

    resolving.erase(param);
    return pointsToCache.emplace(param, std::move(result)).first->second;
}

bool ParamAliasAnalysis::mayAliasGlobal(FormalParam * param, GlobalVariable * global)
{
    if (!mod || !param || !global) {
        return true;
    }

    const PointsTo & pointsTo = resolvePointsTo(param);
    return pointsTo.unknown || pointsTo.roots.find(global) != pointsTo.roots.end();
}

bool ParamAliasAnalysis::mayAliasParam(FormalParam * lhs, FormalParam * rhs)
{
    if (!mod || !lhs || !rhs) {
        return true;
    }
    if (lhs == rhs) {
        return true;
    }

    const PointsTo & lhsPointsTo = resolvePointsTo(lhs);
    const PointsTo & rhsPointsTo = resolvePointsTo(rhs);
    if (lhsPointsTo.unknown || rhsPointsTo.unknown) {
        return true;
    }

    const auto & smaller = lhsPointsTo.roots.size() <= rhsPointsTo.roots.size() ? lhsPointsTo.roots : rhsPointsTo.roots;
    const auto & larger = &smaller == &lhsPointsTo.roots ? rhsPointsTo.roots : lhsPointsTo.roots;
    for (auto * root : smaller) {
        if (larger.find(root) != larger.end()) {
            return true;
        }
    }
    return false;
}
