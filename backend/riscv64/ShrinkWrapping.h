///
/// @file ShrinkWrapping.h
/// @brief Shrink-Wrapping 优化：将 callee-saved 寄存器的保存/恢复操作下推到真正需要的路径
///
/// 传统的 prologue/epilogue 在函数入口/出口统一保存/恢复所有 callee-saved 寄存器。
/// Shrink-Wrapping 优化通过控制流分析，识别哪些路径真正需要保存寄存器，将保存操作
/// 下推到必要的分支，从而减少快速路径（如叶子路径、早期返回）的开销。
///
/// 典型场景：
/// ```c
/// float my_pow(float a, int n) {
///   if (n < 0) return 1 / my_pow(a, -n);  // 需要保存 ra
///   float res = 1.0;
///   while (n) { ... }  // 不需要保存 ra
///   return res;
/// }
/// ```
///
/// 优化效果：
/// - n >= 0 路径：无需保存/恢复 ra，减少 2 次内存访问
/// - n < 0 路径：在该分支入口保存 ra，出口恢复
///

#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

class BasicBlock;
class Function;
class Instruction;

namespace ShrinkWrapping {

/// @brief 路径分析结果：记录每个基本块是否需要保存特定寄存器
struct PathAnalysis {
	/// 需要保存 ra 的基本块集合
	std::unordered_set<BasicBlock*> blocksNeedRA;

	/// 需要保存特定 callee-saved 寄存器的基本块集合
	std::unordered_map<int, std::unordered_set<BasicBlock*>> blocksNeedReg;

	/// 是否所有路径都需要保存（此时不适合 shrink-wrapping，直接在入口保存）
	bool allPathsNeedRA = false;

	/// 可以进行 shrink-wrapping 优化的基本块（保存点）
	BasicBlock* raSavePoint = nullptr;

	/// 保存点的所有后继出口（恢复点）
	std::unordered_set<BasicBlock*> raRestorePoints;
};

/// @brief 分析函数中哪些基本块需要保存 ra 寄存器
///
/// 算法：
/// 1. 标记所有包含 call 指令的基本块
/// 2. 从这些基本块反向遍历，标记所有可能到达 call 的路径
/// 3. 如果入口块被标记，说明所有路径都需要保存
/// 4. 否则，找到需要保存的最早支配点
///
/// @param func 待分析的函数
/// @return 路径分析结果
PathAnalysis analyzeCallPaths(Function* func);

/// @brief 计算基本块的支配关系
///
/// 支配关系：如果从入口到达块 B 的所有路径都必须经过块 A，则 A 支配 B
/// 用于确定 shrink-wrapping 的保存点：保存操作应该放在所有需要保存的路径的
/// 最早公共支配点，以避免重复保存。
///
/// @param func 待分析的函数
/// @return 支配关系映射：block -> 其直接支配者
std::unordered_map<BasicBlock*, BasicBlock*> computeDominators(Function* func);

/// @brief 计算基本块的后支配关系
///
/// 后支配关系：如果从块 B 到出口的所有路径都必须经过块 A，则 A 后支配 B
/// 用于确定恢复点：恢复操作应该放在所有可能返回的路径上。
///
/// @param func 待分析的函数
/// @return 后支配关系映射：block -> 其直接后支配者
std::unordered_map<BasicBlock*, BasicBlock*> computePostDominators(Function* func);

/// @brief 判断块 A 是否支配块 B
///
/// @param domMap 支配关系映射
/// @param dominator 候选支配者
/// @param block 被支配块
/// @return 是否存在支配关系
bool dominates(
	const std::unordered_map<BasicBlock*, BasicBlock*>& domMap,
	BasicBlock* dominator,
	BasicBlock* block
);

/// @brief 查找一组基本块的最近公共支配者
///
/// @param blocks 基本块集合
/// @param domMap 支配关系映射
/// @param entryBlock 函数入口块
/// @return 最近公共支配者
BasicBlock* findCommonDominator(
	const std::unordered_set<BasicBlock*>& blocks,
	const std::unordered_map<BasicBlock*, BasicBlock*>& domMap,
	BasicBlock* entryBlock
);

/// @brief 查找从某个块到出口的所有路径上必须经过的块（后支配点）
///
/// @param block 起始块
/// @param postDomMap 后支配关系映射
/// @return 后支配者集合
std::unordered_set<BasicBlock*> findPostDominatedBlocks(
	BasicBlock* block,
	const std::unordered_map<BasicBlock*, BasicBlock*>& postDomMap
);

/// @brief 判断基本块是否包含函数调用指令
///
/// @param block 待检查的基本块
/// @return 是否包含 CallInst
bool blockHasCall(BasicBlock* block);

/// @brief 判断基本块是否是出口块（包含 return 指令）
///
/// @param block 待检查的基本块
/// @return 是否是出口块
bool isExitBlock(BasicBlock* block);

/// @brief 获取基本块的所有前驱
///
/// @param block 基本块
/// @return 前驱块集合
std::vector<BasicBlock*> getPredecessors(BasicBlock* block);

/// @brief 获取基本块的所有后继
///
/// @param block 基本块
/// @return 后继块集合
std::vector<BasicBlock*> getSuccessors(BasicBlock* block);

} // namespace ShrinkWrapping
