///
/// @file RegCoalescer.h
/// @brief 寄存器合并器，在 Greedy 分配前消除冗余 copy 指令
///
/// 当 copy 指令的源和目标虚拟寄存器不干涉时，将两者合并为同一
/// 虚拟寄存器，从而消除该 copy 指令，减少寄存器压力和动态指令数。
///

#pragma once

#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class Function;
class Instruction;
class InterferenceGraph;
class LiveInterval;
class Value;

/// @brief 寄存器合并器
class RegCoalescer {

public:
	/// @brief 构造函数
	/// @param enabled 是否启用寄存器合并
	explicit RegCoalescer(bool enabled = false);

	/// @brief 执行寄存器合并
	/// @param intervals 活跃区间列表（输入输出，合并后修改）
	/// @param graph 干涉图（输入输出，合并后更新）
	/// @param func 当前函数（用于遍历 copy 指令）
	/// @param valueToInterval Value* -> interval 索引映射（输入输出）
	/// @param instNumbering 指令编号映射，用于判断copy位置的伪干涉是否可忽略
	void run(std::vector<LiveInterval *> & intervals,
	         InterferenceGraph *& graph,
	         Function * func,
	         std::unordered_map<Value *, int> & valueToInterval,
	         const std::map<Instruction *, int> & instNumbering);

	/// @brief 获取被消除的 copy 指令集合
	const std::unordered_set<Instruction *> & getEliminatedCopies() const
	{
		return eliminatedCopies_;
	}

	/// @brief 获取合并后的虚拟寄存器代表映射
	const std::unordered_map<Value *, Value *> & getRepresentativeMap() const
	{
		return representative_;
	}

private:
	/// @brief 收集 IR 中所有 copy 指令的 (src, dst, copyInst) 三元组
	std::vector<std::tuple<Value *, Value *, Instruction *>> collectCopyPairs(Function * func);

	/// @brief 判断两个虚拟寄存器是否可合并
	/// 条件：类型兼容 + 不干涉（或干涉仅限于copy位置这一拍的伪干涉）
	/// @param copyInst 当前copy指令，用于判断伪干涉
	/// @param instNumbering 指令编号映射，用于定位copy位置
	bool canCoalesce(Value * src, Value * dst,
	                 const std::vector<LiveInterval *> & intervals,
	                 const InterferenceGraph * graph,
	                 const std::unordered_map<Value *, int> & valueToInterval,
	                 Instruction * copyInst,
	                 const std::map<Instruction *, int> & instNumbering);

	/// @brief 执行一次合并：将 src 和 dst 的区间合并，消除 copy
	void mergeIntervals(Value * src, Value * dst,
	                    std::vector<LiveInterval *> & intervals,
	                    std::unordered_map<Value *, int> & valueToInterval);

	/// @brief 合并后重建干涉图
	InterferenceGraph * rebuildInterferenceGraph(
		const std::vector<LiveInterval *> & intervals);

	bool enabled_;                                              ///< 是否启用寄存器合并
	std::unordered_set<Instruction *> eliminatedCopies_;       ///< 被消除的copy指令集合
	std::unordered_map<Value *, Value *> representative_;      ///< 合并后的代表映射：alias -> representative
};
