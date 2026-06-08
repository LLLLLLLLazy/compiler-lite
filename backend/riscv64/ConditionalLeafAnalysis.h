///
/// @file ConditionalLeafAnalysis.h
/// @brief 条件性叶子函数分析：识别函数中哪些路径需要调用其他函数
///
/// 这是 shrink-wrapping 的简化版本，专门优化 ra 寄存器的保存：
/// - 如果函数的某些路径不需要调用其他函数（叶子路径），则这些路径不需要保存 ra
/// - 只在真正需要调用的路径上保存/恢复 ra
///
/// 示例：
/// ```c
/// float my_pow(float a, int n) {
///   if (n < 0) return 1 / my_pow(a, -n);  // 非叶子路径
///   // 下面是叶子路径，不需要保存 ra
///   float res = 1.0;
///   while (n) { ... }
///   return res;
/// }
/// ```
///

#pragma once

#include <vector>

class BasicBlock;
class Function;

/// @brief 分析函数中的调用模式
struct CallPathAnalysis {
	/// 是否所有路径都包含调用（true = 不适合优化，false = 有叶子路径）
	bool allPathsHaveCall = false;

	/// 包含调用指令的基本块列表
	std::vector<BasicBlock*> callBlocks;

	/// 是否可以进行条件性保存优化
	bool canOptimize = false;
};

/// @brief 分析函数的调用路径模式
///
/// 返回是否所有路径都包含函数调用。如果返回 false，说明存在叶子路径，
/// 可以进行优化（在调用点前保存 ra，而不是在函数入口）。
///
/// @param func 待分析的函数
/// @return 调用路径分析结果
CallPathAnalysis analyzeCallPaths(Function* func);

/// @brief 判断从入口块到出口块是否存在不经过任何调用指令的路径
///
/// 如果存在这样的路径，则该路径不需要保存 ra。
///
/// @param func 待分析的函数
/// @return 是否存在叶子路径
bool hasLeafPath(Function* func);
