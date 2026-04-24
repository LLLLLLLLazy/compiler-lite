///
/// @file SpillManager.h
/// @brief 溢出管理器，负责在活跃区间被溢出时插入spill/reload代码
///
/// - 通过SpillStrategy策略接口选择溢出候选
/// - 为溢出的活跃区间分配栈槽（AllocaInst）
/// - 在定义点后插入StoreInst（spill），在使用点前插入LoadInst（reload）
/// - 管理溢出栈槽的分配与计数
///

#pragma once

#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

class Function;
class Instruction;
class LiveInterval;
class SpillStrategy;
class Value;

/// @brief 溢出管理器
///
/// 当Greedy寄存器分配器决定溢出某个活跃区间时，SpillManager负责：
/// 1. 为该区间分配溢出栈槽（通过AllocaInst在函数入口创建）
/// 2. 在定义点后插入store指令，将值保存到栈槽
/// 3. 在使用点前插入load指令，从栈槽恢复值
/// 4. 通过SpillStrategy接口委托溢出候选的选择
///
class SpillManager {

public:
	/// @brief 构造函数
	/// @param strategy 溢出决策策略指针（不由SpillManager负责释放）
	explicit SpillManager(SpillStrategy * strategy);

	/// @brief 通过策略接口选择溢出候选
	/// @param candidates 候选溢出区间列表
	/// @return 被选中的溢出候选区间指针
	LiveInterval * selectSpillCandidate(std::vector<LiveInterval *> & candidates);

	/// @brief 为溢出的活跃区间插入spill代码
	/// 在区间的定义点后插入StoreInst，将值保存到溢出栈槽。
	/// 若该区间尚未分配溢出栈槽，则先分配。
	/// @param interval 被溢出的活跃区间
	/// @param func 所属函数
	/// @param instNumbering 指令编号映射（Instruction* -> 编号）
	void insertSpillCode(LiveInterval * interval, Function * func,
		const std::map<Instruction *, int> & instNumbering);

	/// @brief 为溢出的活跃区间在指定使用点前插入reload代码
	/// 在使用点前插入LoadInst，从溢出栈槽恢复值。
	/// @param interval 被溢出的活跃区间
	/// @param usePos 使用点的指令编号
	/// @param func 所属函数
	/// @param instNumbering 指令编号映射（Instruction* -> 编号）
	void insertReloadCode(LiveInterval * interval, int usePos, Function * func,
		const std::map<Instruction *, int> & instNumbering);

	/// @brief 获取总溢出槽位数（用于栈帧大小计算）
	/// @return 溢出槽位数
	int getSpillSlots() const;

private:
	/// @brief 为溢出的活跃区间分配溢出栈槽
	/// 在函数入口块创建AllocaInst，并设置区间的spillSlot
	/// @param interval 需要分配栈槽的活跃区间
	/// @param func 所属函数
	void allocateSpillSlot(LiveInterval * interval, Function * func);

	/// @brief 根据指令编号查找指令及其所在的基本块
	/// @param instNum 指令编号
	/// @param func 所属函数
	/// @param instNumbering 指令编号映射
	/// @return 指令指针（未找到则返回nullptr）
	Instruction * findInstructionByNumber(int instNum, Function * func,
		const std::map<Instruction *, int> & instNumbering);

	/// 溢出决策策略
	SpillStrategy * strategy;

	/// 总溢出槽位数
	int spillSlotCount;

	/// Value* -> 对应的溢出栈槽AllocaInst*
	/// 用于在spill/reload时引用正确的栈槽地址
	std::unordered_map<Value *, Instruction *> spillSlots;
};
