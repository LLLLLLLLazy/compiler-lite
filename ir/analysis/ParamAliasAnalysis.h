///
/// @file ParamAliasAnalysis.h
/// @brief 基于全调用点根对象收集的形参别名分析
///
/// SysY 没有函数指针，模块内调用图完全可见。对每个指针形参收集所有调用点
/// 实参的指针根对象（全局变量 / 调用方 alloca / 递归解析调用方形参），
/// 得到该形参可能指向的对象集合，从而回答两类保守别名查询：
///   1. 形参是否可能与某个全局变量指向同一对象
///   2. 同一函数的两个形参是否可能指向同一对象
/// 任何无法归类的实参根（如指针 phi）都会把对应形参标记为 unknown，
/// 查询时一律按可能别名处理。
///

#pragma once

#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class CallInst;
class FormalParam;
class Function;
class GlobalVariable;
class Module;
class Value;

class ParamAliasAnalysis {

public:
    /// @brief 构造形参别名分析器
    /// @param module 当前模块，允许为空（此时所有查询返回可能别名）
    explicit ParamAliasAnalysis(Module * module);

    /// @brief 判断形参是否可能与全局变量指向同一对象
    /// @param param 待查询形参
    /// @param global 目标全局变量
    /// @return true 表示可能别名
    bool mayAliasGlobal(FormalParam * param, GlobalVariable * global);

    /// @brief 判断同一函数的两个形参是否可能指向同一对象
    /// @param lhs 左侧形参
    /// @param rhs 右侧形参
    /// @return true 表示可能别名
    bool mayAliasParam(FormalParam * lhs, FormalParam * rhs);

private:
    /// @brief 单个形参的指向集合
    struct PointsTo {
        /// 可能的根对象：GlobalVariable* 或调用方 AllocaInst*
        std::unordered_set<Value *> roots;
        /// 存在无法归类的实参根，查询时按可能指向任何对象处理
        bool unknown = false;
    };

    /// @brief 解析形参的指向集合（跨调用方形参递归，带环检测）
    const PointsTo & resolvePointsTo(FormalParam * param);

    /// @brief 首次使用时扫描模块，建立形参位置与调用点索引
    void buildIndexIfNeeded();

    Module * mod = nullptr;
    bool indexBuilt = false;
    /// 形参 -> (所属函数, 形参下标)
    std::unordered_map<FormalParam *, std::pair<Function *, std::size_t>> paramSlot;
    /// 函数 -> 模块内全部调用点
    std::unordered_map<Function *, std::vector<CallInst *>> callSites;
    std::unordered_map<FormalParam *, PointsTo> pointsToCache;
    std::unordered_set<FormalParam *> resolving;
};
