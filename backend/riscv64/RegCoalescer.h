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

#include "LiveInterval.h"

class Function;
class Instruction;
class InterferenceGraph;
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
	/// @param preciseSegments 保守循环扩展前的精确活跃区间段（用于 hole-aware 干涉判断）
	void run(std::vector<LiveInterval *> & intervals,
	         InterferenceGraph *& graph,
	         Function * func,
	         std::unordered_map<Value *, int> & valueToInterval,
	         const std::map<Instruction *, int> & instNumbering,
	         const std::unordered_map<Value *, std::vector<Segment>> & preciseSegments);

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

	/// @brief 基于精确区间判断 src/dst 是否真正干涉（hole-aware）
	///
	/// 用扩展前的精确活跃段判重叠。仅当两个类构成二元破坏性更新且在定义点只
	/// 重叠一拍时，才把该定义点及当前 copy 的交接单拍视为伪干涉；其余重叠和
	/// 回边携带空洞仍按真干涉处理
	/// @param copyInst 当前尝试消除的 copy 指令
	/// @return true 表示真正干涉（不可合并）
	bool preciseInterferes(Value * src, Value * dst,
						   Instruction * copyInst,
	                       const std::map<Instruction *, int> & instNumbering);

	/// @brief 回边携带空洞守卫：判断 spanner 是否跨越 holed 类某原始成员的回边携带空洞
	///
	/// 逐原始成员判定：成员自身的精确段在循环回边处会留下空洞（被携带值实际仍
	/// 活跃，保守扩展段能覆盖）。若 spanner 以“外来”方式（只贴空洞一端，非接力
	/// 两侧）插入某成员自身的空洞，且该成员的保守扩展段覆盖被插入区段，则二者
	/// 真正干涉。逐成员（而非合并类并集）判定可避免把两个不同成员之间的良性间隙
	/// 误判为携带空洞而过度阻止合并。接力（carrier，跨越空洞两端）属累加器在空洞
	/// 上传递值的良性形态，已被主重叠循环按定义点单拍伪干涉放行
	/// @param holedMembers holed 合并类的全部原始成员
	/// @param spannerSegs spanner 合并类的精确段并集
	/// @return true 表示存在外来跨越真干涉
	bool spansCarriedHole(const std::vector<Value *> & holedMembers,
	                      const std::vector<Segment> & spannerSegs) const;

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
	Function * func_ = nullptr;                                ///< 当前函数（精确干涉需扫描 copy）
	std::unordered_map<Value *, std::vector<Segment>> preciseSegments_; ///< 精确活跃段（合并时同步并入代表）
	std::unordered_map<Value *, std::vector<Segment>> originalPrecise_; ///< 每个原始值自身的精确段（不随合并改变），用于逐来源判回边携带空洞
	std::unordered_map<Value *, std::vector<Segment>> conservativeSegments_; ///< 每个原始值的保守循环扩展段（不随合并改变），用于回边携带判定
	std::unordered_map<Value *, std::vector<Value *>> classMembers_; ///< 代表 -> 该合并类的全部原始成员
	std::unordered_map<Value *, std::unordered_set<int>> defPositions_; ///< 每个原始值的定义点编号集合，用于区分回边携带与新定义
};
