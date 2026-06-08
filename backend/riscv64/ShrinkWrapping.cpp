///
/// @file ShrinkWrapping.cpp
/// @brief Shrink-Wrapping 优化实现
///

#include "ShrinkWrapping.h"

#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "BasicBlock.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "CondBranchInst.h"
#include "Function.h"
#include "Instruction.h"
#include "ReturnInst.h"

namespace ShrinkWrapping {

/// @brief 判断基本块是否包含函数调用指令
bool blockHasCall(BasicBlock* block)
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

/// @brief 判断基本块是否是出口块（包含 return 指令）
bool isExitBlock(BasicBlock* block)
{
	if (block == nullptr || block->getInstructions().empty()) {
		return false;
	}
	auto* lastInst = block->getInstructions().back();
	return dynamic_cast<ReturnInst*>(lastInst) != nullptr;
}

/// @brief 获取基本块的所有前驱
std::vector<BasicBlock*> getPredecessors(BasicBlock* block)
{
	std::vector<BasicBlock*> preds;
	if (block == nullptr) {
		return preds;
	}

	// 遍历函数中所有基本块，查找跳转到当前块的块
	Function* func = block->getParent();
	if (func == nullptr) {
		return preds;
	}

	for (auto* bb : func->getBlocks()) {
		if (bb->getInstructions().empty()) {
			continue;
		}

		auto* terminator = bb->getInstructions().back();
		if (auto* branch = dynamic_cast<BranchInst*>(terminator)) {
			if (branch->getTarget() == block) {
				preds.push_back(bb);
			}
		} else if (auto* condBranch = dynamic_cast<CondBranchInst*>(terminator)) {
			if (condBranch->getTrueDest() == block || condBranch->getFalseDest() == block) {
				preds.push_back(bb);
			}
		}
	}

	return preds;
}

/// @brief 获取基本块的所有后继
std::vector<BasicBlock*> getSuccessors(BasicBlock* block)
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

/// @brief 计算基本块的支配关系（使用简单的迭代算法）
std::unordered_map<BasicBlock*, BasicBlock*> computeDominators(Function* func)
{
	std::unordered_map<BasicBlock*, BasicBlock*> idom; // immediate dominator
	std::unordered_map<BasicBlock*, std::unordered_set<BasicBlock*>> dom; // dominator sets

	auto blocks = func->getBlocks();
	if (blocks.empty()) {
		return idom;
	}

	BasicBlock* entry = blocks.front();

	// 初始化：入口块支配自己，其他块被所有块支配（全集）
	dom[entry].insert(entry);
	for (auto* bb : blocks) {
		if (bb != entry) {
			for (auto* b : blocks) {
				dom[bb].insert(b);
			}
		}
	}

	// 迭代计算支配关系：Dom(n) = {n} ∪ (∩ Dom(p) for all predecessors p)
	bool changed = true;
	while (changed) {
		changed = false;
		for (auto* bb : blocks) {
			if (bb == entry) {
				continue;
			}

			std::unordered_set<BasicBlock*> newDom;
			newDom.insert(bb); // 块支配自己

			auto preds = getPredecessors(bb);
			if (!preds.empty()) {
				// 计算所有前驱的支配集交集
				newDom = dom[preds[0]];
				for (size_t i = 1; i < preds.size(); ++i) {
					std::unordered_set<BasicBlock*> intersection;
					for (auto* d : newDom) {
						if (dom[preds[i]].count(d) > 0) {
							intersection.insert(d);
						}
					}
					newDom = intersection;
				}
				newDom.insert(bb); // 加回自己
			}

			if (newDom != dom[bb]) {
				dom[bb] = newDom;
				changed = true;
			}
		}
	}

	// 计算直接支配者（immediate dominator）
	for (auto* bb : blocks) {
		if (bb == entry) {
			continue;
		}

		// bb 的直接支配者是支配 bb 的块中，除了 bb 自己外，不被其他支配者支配的那个
		for (auto* d : dom[bb]) {
			if (d == bb) {
				continue;
			}

			bool isImmediate = true;
			for (auto* other : dom[bb]) {
				if (other == bb || other == d) {
					continue;
				}
				// 如果存在另一个支配者 other，且 d 支配 other，则 d 不是直接支配者
				if (dom[other].count(d) > 0) {
					isImmediate = false;
					break;
				}
			}

			if (isImmediate) {
				idom[bb] = d;
				break;
			}
		}
	}

	return idom;
}

/// @brief 计算基本块的后支配关系
std::unordered_map<BasicBlock*, BasicBlock*> computePostDominators(Function* func)
{
	std::unordered_map<BasicBlock*, BasicBlock*> ipostdom; // immediate post-dominator
	std::unordered_map<BasicBlock*, std::unordered_set<BasicBlock*>> postdom;

	auto blocks = func->getBlocks();
	if (blocks.empty()) {
		return ipostdom;
	}

	// 收集所有出口块
	std::vector<BasicBlock*> exitBlocks;
	for (auto* bb : blocks) {
		if (isExitBlock(bb)) {
			exitBlocks.push_back(bb);
		}
	}

	if (exitBlocks.empty()) {
		return ipostdom;
	}

	// 初始化：出口块后支配自己，其他块被所有块后支配
	for (auto* exit : exitBlocks) {
		postdom[exit].insert(exit);
	}
	for (auto* bb : blocks) {
		bool isExit = false;
		for (auto* exit : exitBlocks) {
			if (bb == exit) {
				isExit = true;
				break;
			}
		}
		if (!isExit) {
			for (auto* b : blocks) {
				postdom[bb].insert(b);
			}
		}
	}

	// 迭代计算后支配关系：PostDom(n) = {n} ∪ (∩ PostDom(s) for all successors s)
	bool changed = true;
	while (changed) {
		changed = false;
		for (auto* bb : blocks) {
			bool isExit = false;
			for (auto* exit : exitBlocks) {
				if (bb == exit) {
					isExit = true;
					break;
				}
			}
			if (isExit) {
				continue;
			}

			std::unordered_set<BasicBlock*> newPostDom;
			newPostDom.insert(bb);

			auto succs = getSuccessors(bb);
			if (!succs.empty()) {
				// 计算所有后继的后支配集交集
				newPostDom = postdom[succs[0]];
				for (size_t i = 1; i < succs.size(); ++i) {
					std::unordered_set<BasicBlock*> intersection;
					for (auto* d : newPostDom) {
						if (postdom[succs[i]].count(d) > 0) {
							intersection.insert(d);
						}
					}
					newPostDom = intersection;
				}
				newPostDom.insert(bb);
			}

			if (newPostDom != postdom[bb]) {
				postdom[bb] = newPostDom;
				changed = true;
			}
		}
	}

	// 计算直接后支配者
	for (auto* bb : blocks) {
		bool isExit = false;
		for (auto* exit : exitBlocks) {
			if (bb == exit) {
				isExit = true;
				break;
			}
		}
		if (isExit) {
			continue;
		}

		for (auto* d : postdom[bb]) {
			if (d == bb) {
				continue;
			}

			bool isImmediate = true;
			for (auto* other : postdom[bb]) {
				if (other == bb || other == d) {
					continue;
				}
				if (postdom[other].count(d) > 0) {
					isImmediate = false;
					break;
				}
			}

			if (isImmediate) {
				ipostdom[bb] = d;
				break;
			}
		}
	}

	return ipostdom;
}

/// @brief 判断块 A 是否支配块 B
bool dominates(
	const std::unordered_map<BasicBlock*, BasicBlock*>& domMap,
	BasicBlock* dominator,
	BasicBlock* block
)
{
	if (dominator == block) {
		return true;
	}

	// 沿着支配树向上查找
	BasicBlock* current = block;
	while (current != nullptr) {
		auto it = domMap.find(current);
		if (it == domMap.end()) {
			break;
		}
		current = it->second;
		if (current == dominator) {
			return true;
		}
	}

	return false;
}

/// @brief 查找一组基本块的最近公共支配者
BasicBlock* findCommonDominator(
	const std::unordered_set<BasicBlock*>& blocks,
	const std::unordered_map<BasicBlock*, BasicBlock*>& domMap,
	BasicBlock* entryBlock
)
{
	if (blocks.empty()) {
		return nullptr;
	}

	if (blocks.size() == 1) {
		return *blocks.begin();
	}

	// 从第一个块开始，向上遍历其所有支配者
	std::vector<BasicBlock*> blockVec(blocks.begin(), blocks.end());
	BasicBlock* candidate = blockVec[0];

	// 向上查找，直到找到支配所有块的节点
	while (candidate != nullptr) {
		bool dominatesAll = true;
		for (size_t i = 1; i < blockVec.size(); ++i) {
			if (!dominates(domMap, candidate, blockVec[i])) {
				dominatesAll = false;
				break;
			}
		}

		if (dominatesAll) {
			return candidate;
		}

		// 向上移动到直接支配者
		auto it = domMap.find(candidate);
		if (it != domMap.end()) {
			candidate = it->second;
		} else {
			break;
		}
	}

	return entryBlock; // 最坏情况下，入口块支配所有块
}

/// @brief 查找从某个块到出口的所有路径上必须经过的块（后支配点）
std::unordered_set<BasicBlock*> findPostDominatedBlocks(
	BasicBlock* block,
	const std::unordered_map<BasicBlock*, BasicBlock*>& postDomMap
)
{
	std::unordered_set<BasicBlock*> result;
	BasicBlock* current = block;

	while (current != nullptr) {
		result.insert(current);
		auto it = postDomMap.find(current);
		if (it == postDomMap.end()) {
			break;
		}
		current = it->second;
	}

	return result;
}

/// @brief 分析函数中哪些基本块需要保存 ra 寄存器
PathAnalysis analyzeCallPaths(Function* func)
{
	PathAnalysis result;

	if (func == nullptr) {
		return result;
	}

	auto blocks = func->getBlocks();
	if (blocks.empty()) {
		return result;
	}

	// 第一步：标记所有包含 call 指令的基本块
	std::unordered_set<BasicBlock*> callBlocks;
	for (auto* bb : blocks) {
		if (blockHasCall(bb)) {
			callBlocks.insert(bb);
		}
	}

	// 如果没有 call 指令，不需要保存 ra
	if (callBlocks.empty()) {
		result.allPathsNeedRA = false;
		return result;
	}

	// 第二步：从 call 块反向遍历，标记所有可能到达 call 的块
	std::unordered_set<BasicBlock*> needRA;
	std::queue<BasicBlock*> worklist;

	for (auto* cb : callBlocks) {
		worklist.push(cb);
		needRA.insert(cb);
	}

	while (!worklist.empty()) {
		auto* bb = worklist.front();
		worklist.pop();

		auto preds = getPredecessors(bb);
		for (auto* pred : preds) {
			if (needRA.count(pred) == 0) {
				needRA.insert(pred);
				worklist.push(pred);
			}
		}
	}

	result.blocksNeedRA = needRA;

	// 第三步：检查入口块是否需要保存 ra
	BasicBlock* entry = blocks.front();
	if (needRA.count(entry) > 0) {
		// 所有路径都需要保存，不适合 shrink-wrapping
		result.allPathsNeedRA = true;
		return result;
	}

	// 第四步：计算支配关系，找到保存点
	auto domMap = computeDominators(func);

	// 找到所有需要保存 ra 的块的最近公共支配者
	result.raSavePoint = findCommonDominator(needRA, domMap, entry);

	// 第五步：计算后支配关系，找到恢复点
	auto postDomMap = computePostDominators(func);

	// 收集所有出口块
	for (auto* bb : blocks) {
		if (isExitBlock(bb)) {
			// 如果出口块在需要 ra 的路径上，则需要在该出口恢复
			if (needRA.count(bb) > 0) {
				result.raRestorePoints.insert(bb);
			}
		}
	}

	result.allPathsNeedRA = false;
	return result;
}

} // namespace ShrinkWrapping
