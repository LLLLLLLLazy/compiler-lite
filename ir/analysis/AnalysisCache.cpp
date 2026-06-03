///
/// @file AnalysisCache.cpp
/// @brief 函数级 analysis 结果缓存的分组失效实现
///

#include "AnalysisCache.h"

#include "DominanceFrontier.h"
#include "DominatorTree.h"
#include "LoopInfo.h"
#include "PostDominatorTree.h"
#include "ScalarEvolution.h"

/// @brief CFG 结构改变时统一失效所有 CFG 派生分析
void AnalysisCache::invalidateCFGAnalyses()
{
    invalidate<DominatorTree>();
    invalidate<DominanceFrontier>();
    invalidate<PostDominatorTree>();
    invalidate<LoopInfo>();
    invalidate<ScalarEvolution>();
}

/// @brief 仅指令/值发生改变时失效依赖具体指令的分析
void AnalysisCache::invalidateValueAnalyses()
{
    invalidate<ScalarEvolution>();
}
