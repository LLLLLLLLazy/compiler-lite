///
/// @file LiveIntervalAnalysis.h
/// @brief 活跃区间分析器，用于Greedy寄存器分配
///
/// 对函数内的结构化SSA IR指令进行遍历，计算每个虚拟寄存器的活跃区间，
/// 并构建干涉图。遍历按基本块顺序进行，为每条指令编号。
///

#pragma once

#include <map>
#include <unordered_map>
#include <vector>

#include "InterferenceGraph.h"
#include "LiveInterval.h"

class Function;
class Instruction;
class LoopInfo;
class Value;

/// @brief 活跃区间分析器
///
/// 遍历函数的所有基本块和指令，为每个虚拟寄存器构建活跃区间，
/// 然后根据区间重叠关系构建干涉图。
///
class LiveIntervalAnalysis {

public:
	/// @brief 构造函数
	/// @param func 待分析的函数
	/// @param loopInfo 循环分析结果（可选），用于计算循环深度加权的溢出权重
	explicit LiveIntervalAnalysis(Function * func, LoopInfo * loopInfo = nullptr);

	/// @brief 析构函数，释放活跃区间和干涉图
	~LiveIntervalAnalysis();

	/// @brief 执行完整的活跃区间分析流程
	/// 依次调用 computeLiveIntervals() 和 buildInterferenceGraph()
	void run();

	/// @brief 获取所有活跃区间
	/// @return 活跃区间列表引用
	std::vector<LiveInterval *> & getIntervals();

	/// @brief 获取干涉图
	/// @return 干涉图指针
	InterferenceGraph * getInterferenceGraph();

	/// @brief 获取Value到LiveInterval索引的映射
	/// @return Value* -> interval索引的映射
	const std::unordered_map<Value *, int> & getValueToIntervalMap() const;

	/// @brief 获取指令编号映射
	/// @return Instruction* -> 指令编号的映射
	const std::map<Instruction *, int> & getInstNumbering() const;

private:
	/// @brief 计算所有虚拟寄存器的活跃区间
	/// 按基本块顺序遍历指令，为每条指令编号，
	/// 为每个定义/使用虚拟寄存器的指令更新活跃区间
	void computeLiveIntervals();

	/// @brief 根据活跃区间重叠关系构建干涉图
	/// 对每对活跃区间检查是否重叠，若重叠则添加干涉边
	void buildInterferenceGraph();

	/// @brief 获取或创建Value对应的LiveInterval
	/// @param val 虚拟寄存器对应的Value
	/// @return LiveInterval指针
	LiveInterval * getOrCreateInterval(Value * val);

	/// @brief 判断Value是否需要活跃区间分析
	/// 常量(ConstInt)、全局变量(GlobalVariable)、物理寄存器(RegVariable)、
	/// 函数(Function)等不需要分配寄存器，因此不需要活跃区间
	/// @param val 待判断的Value
	/// @return 是否需要活跃区间
	static bool needsInterval(Value * val);

	/// @brief 待分析的函数
	Function * func;

	/// @brief 所有活跃区间（每个虚拟寄存器一个）
	std::vector<LiveInterval *> intervals;

	/// @brief Value* -> LiveInterval索引的映射
	std::unordered_map<Value *, int> valueToInterval;

	/// @brief 干涉图
	InterferenceGraph * interferenceGraph = nullptr;

	/// @brief 指令编号映射：Instruction* -> 指令编号
	std::map<Instruction *, int> instNumbering;

	/// @brief 下一条指令的编号
	int nextInstNum = 0;

	/// @brief 循环分析结果（用于计算溢出权重）
	LoopInfo * loopInfo = nullptr;
};
