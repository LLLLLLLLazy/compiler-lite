///
/// @file ConditionalLeafAnalysis.cpp
/// @brief 条件性叶子函数分析实现
///

#include "ConditionalLeafAnalysis.h"

#include <unordered_set>
#include <vector>

#include "BasicBlock.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "CondBranchInst.h"
#include "Function.h"
#include "Instruction.h"
#include "ReturnInst.h"

/// @brief 检查基本块是否包含调用指令
static bool blockHasCall(BasicBlock* block)
{
	if (block == nullptr) {
		return false;
	}
	for (auto* inst : block->getInstructions()) {
		if (dynamic_cast<CallInst*>(inst) != nullptr) {
			return true;
		}
	}
	return false;
}

/// @brief 统计基本块中的调用指令数量
/// @param block 待统计的基本块
/// @return 调用指令条数
static int countBlockCalls(BasicBlock* block)
{
	int count = 0;
	if (block == nullptr) {
		return count;
	}
	for (auto* inst : block->getInstructions()) {
		if (dynamic_cast<CallInst*>(inst) != nullptr) {
			++count;
		}
	}
	return count;
}

/// @brief 检查基本块是否是出口块
static bool isExitBlock(BasicBlock* block)
{
	if (block == nullptr || block->getInstructions().empty()) {
		return false;
	}
	auto* lastInst = block->getInstructions().back();
	return dynamic_cast<ReturnInst*>(lastInst) != nullptr;
}

/// @brief 获取基本块的后继
static std::vector<BasicBlock*> getSuccessors(BasicBlock* block)
{
	std::vector<BasicBlock*> succs;
	if (block == nullptr || block->getInstructions().empty()) {
		return succs;
	}

	auto* terminator = block->getInstructions().back();
	if (auto* branch = dynamic_cast<BranchInst*>(terminator)) {
		if (branch->getTarget() != nullptr) {
			succs.push_back(branch->getTarget());
		}
	} else if (auto* condBranch = dynamic_cast<CondBranchInst*>(terminator)) {
		if (condBranch->getTrueDest() != nullptr) {
			succs.push_back(condBranch->getTrueDest());
		}
		if (condBranch->getFalseDest() != nullptr &&
		    condBranch->getFalseDest() != condBranch->getTrueDest()) {
			succs.push_back(condBranch->getFalseDest());
		}
	}

	return succs;
}

/// @brief 判断从入口块到出口块是否存在不经过任何调用指令的路径（DFS）
bool hasLeafPath(Function* func)
{
	if (func == nullptr) {
		return false;
	}

	auto blocks = func->getBlocks();
	if (blocks.empty()) {
		return false;
	}

	BasicBlock* entry = blocks.front();
	std::unordered_set<BasicBlock*> visited;
	std::vector<BasicBlock*> stack;

	stack.push_back(entry);

	while (!stack.empty()) {
		BasicBlock* current = stack.back();
		stack.pop_back();

		if (visited.count(current) > 0) {
			continue;
		}
		visited.insert(current);

		// 如果当前块包含调用，这条路径不是叶子路径
		if (blockHasCall(current)) {
			continue; // 不继续探索这条路径
		}

		// 如果到达出口块且没有遇到调用，找到叶子路径
		if (isExitBlock(current)) {
			return true;
		}

		// 继续探索后继
		auto succs = getSuccessors(current);
		for (auto* succ : succs) {
			if (visited.count(succ) == 0) {
				stack.push_back(succ);
			}
		}
	}

	return false;
}

/// @brief 判断某个调用块沿 CFG 前向能否再次遇到调用块（含经环路回到自身）
/// @param callBlock 起始调用块
/// @param callBlocks 函数内全部调用块集合
/// @return true 表示一次执行可能经过多于一个调用点
static bool callBlockReachesAnotherCall(BasicBlock* callBlock,
                                        const std::unordered_set<BasicBlock*>& callBlocks)
{
	std::unordered_set<BasicBlock*> visited;
	std::vector<BasicBlock*> stack = getSuccessors(callBlock);

	while (!stack.empty()) {
		BasicBlock* current = stack.back();
		stack.pop_back();
		if (visited.count(current) > 0) {
			continue;
		}
		visited.insert(current);

		if (callBlocks.count(current) > 0) {
			return true;
		}

		for (auto* succ : getSuccessors(current)) {
			if (visited.count(succ) == 0) {
				stack.push_back(succ);
			}
		}
	}

	return false;
}

/// @brief 分析函数的调用路径模式
///
/// 调用点级 ra 保存对比传统序言/尾声保存的代价：
/// 一次执行经过 0 个调用点省一对 sd/ld，1 个调用点打平，
/// k>=2 个调用点反而多出 2(k-1) 次访存（递归/循环内调用尤甚）。
/// 因此仅当满足以下充分条件时才启用，保证严格不劣于传统方式：
///   1. 存在不经过任何调用的叶子路径（否则无收益）
///   2. 每个调用块只含 1 条调用指令
///   3. 任何调用块沿 CFG 前向（含环路）到不了另一个调用块或自身，
///      即任何一次执行至多遇到 1 次调用
CallPathAnalysis analyzeCallPaths(Function* func)
{
	CallPathAnalysis result;

	if (func == nullptr) {
		return result;
	}

	auto blocks = func->getBlocks();

	// 收集所有包含调用的基本块
	for (auto* bb : blocks) {
		if (blockHasCall(bb)) {
			result.callBlocks.push_back(bb);
		}
	}

	// 如果没有调用，整个函数是叶子函数
	if (result.callBlocks.empty()) {
		result.allPathsHaveCall = false;
		result.canOptimize = false;
		return result;
	}

	// 检查是否存在不包含调用的路径
	bool hasLeaf = hasLeafPath(func);
	result.allPathsHaveCall = !hasLeaf;

	if (!hasLeaf) {
		result.canOptimize = false;
		return result;
	}

	// 检查"任何一次执行至多遇到 1 次调用"
	std::unordered_set<BasicBlock*> callBlockSet(result.callBlocks.begin(), result.callBlocks.end());
	for (auto* cb : result.callBlocks) {
		if (countBlockCalls(cb) > 1 || callBlockReachesAnotherCall(cb, callBlockSet)) {
			result.canOptimize = false;
			return result;
		}
	}

	result.canOptimize = true;
	return result;
}
