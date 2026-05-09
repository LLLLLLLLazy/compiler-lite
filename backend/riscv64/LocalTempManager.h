///
/// @file LocalTempManager.h
/// @brief 动态临时寄存器管理器，用于指令选择阶段的scratch寄存器分配
///
/// 指令选择在翻译单条IR指令时需要临时寄存器（如加载左右操作数、计算地址等）。
/// 当t0-t6/a0-a7也参与全局寄存器分配时，临时寄存器必须避开当前指令
/// 仍然活跃的全局分配值。
///
#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ILocRiscV64.h"

class Function;
class ILocRiscV64;
class Instruction;
class Value;

/// @brief Scratch虚拟寄存器，用于指令选择阶段的临时寄存器需求
///
/// 在指令选择过程中创建，后续由ScratchAllocator分配物理寄存器或溢出到栈。
struct ScratchValue {
	void * identity = nullptr;      ///< 唯一标识，用作allocationMap的key
	int originalPhysReg = -1;       ///< borrow时分配的原始物理寄存器编号
	int physicalReg = -1;           ///< ScratchAllocator分配的物理寄存器, -1=未分配
	bool spilled = false;           ///< 是否溢出到栈
	int64_t spillSlot = 0;          ///< 溢出栈偏移
	int borrowPos = -1;             ///< 借用时的机器指令位置
	int releasePos = -1;            ///< 归还时的机器指令位置
	bool released = false;          ///< 是否已归还
};

/// @brief 动态临时寄存器管理器
///
/// 在指令选择过程中管理临时寄存器的借用与归还：
/// - borrow(): 从可分配寄存器池中查找当前尚未借出且不承载live值的物理寄存器
/// - release(): 归还借用的寄存器
///
/// 所有借用必须在同一条IR指令的翻译结束前归还（即不能跨translate_*调用）。
///
class LocalTempManager {

public:
	class Lease {
	public:
		Lease() = default;
		Lease(const Lease &) = delete;
		Lease & operator=(const Lease &) = delete;
		Lease(Lease && other) noexcept;
		Lease & operator=(Lease && other) noexcept;
		~Lease();

		[[nodiscard]] int reg() const { return regId; }
		[[nodiscard]] bool valid() const { return owner != nullptr && regId >= 0; }
		void release();

	private:
		friend class LocalTempManager;

		Lease(LocalTempManager * _owner, int _regId) : owner(_owner), regId(_regId)
		{}

		LocalTempManager * owner = nullptr;
		int regId = -1;
	};

	/// @brief 构造函数
	/// @param pool 全局可用物理寄存器池
	/// @param allocMap 全局寄存器分配映射（Value* -> RegAllocInfo）
	/// @param instNumbering 指令编号映射（Instruction* -> 编号）
	/// @param valueLiveRanges 虚拟寄存器活跃范围（Value* -> [start, end)）
	LocalTempManager(
		const std::vector<int> & pool,
		const std::unordered_map<Value *, RegAllocInfo> & allocMap,
		const std::map<Instruction *, int> & instNumbering,
		const std::unordered_map<Value *, std::pair<int, int>> & valueLiveRanges,
		const std::unordered_map<int, std::vector<std::pair<int, int>>> & allocatedRegLiveRanges);

	/// @brief 为当前指令借用一个空闲物理寄存器
	/// @param inst 当前正在翻译的IR指令
	/// @param excludeReg 排除的物理寄存器编号（如dstReg），-1表示不排除
	/// @return 寄存器租约
	Lease borrow(Instruction * inst, int excludeReg = -1);

	/// @brief 在当前IR指令的源操作数已经消费后借用临时寄存器
	/// @param inst 当前正在翻译的IR指令
	/// @param excludeReg 排除的物理寄存器编号
	/// @return 寄存器租约
	Lease borrowAfterUses(Instruction * inst, int excludeReg = -1);

	/// @brief 借用临时寄存器，排除一组指定的寄存器
	/// @param inst 当前正在翻译的IR指令（可为nullptr）
	/// @param excludeRegs 需要排除的寄存器集合
	/// @return 寄存器租约
	Lease borrowExcluding(Instruction * inst, const std::set<int> & excludeRegs);

	/// @brief 检查所有借出的寄存器是否已全部归还
	/// @return 是否全部归还
	bool allReleased() const { return borrowed.empty(); }

	/// @brief 设置ILoc引用（用于获取机器指令计数）
	void setILoc(ILocRiscV64 * _iloc) { iloc = _iloc; }

	/// @brief 获取所有创建的scratch值
	std::vector<ScratchValue> & getScratchValues() { return scratchValues; }
	const std::vector<ScratchValue> & getScratchValues() const { return scratchValues; }

private:
	/// @brief 查询当前指令编号
	/// @param inst 当前IR指令
	/// @return 指令编号，找不到则返回-1
	int currentInstNum(Instruction * inst) const;

	/// @brief 判断物理寄存器在某条IR指令处是否承载live值
	/// @param reg 物理寄存器编号
	/// @param instNum 指令编号
	/// @param afterUses 是否允许复用最后一次使用在当前指令的寄存器
	/// @return 是否不可作为临时寄存器借用
	bool isLiveAllocatedReg(int reg, int instNum, bool afterUses) const;

	/// @brief 临时寄存器借用的通用实现
	int borrowImpl(Instruction * inst, int excludeReg, bool afterUses);

	/// @brief 归还借用的寄存器
	/// @param reg 物理寄存器编号
	void release(int reg);

	/// @brief scratch物理寄存器池
	std::vector<int> pool;

	/// @brief 当前被借出的寄存器集合
	std::unordered_set<int> borrowed;

	/// @brief 全局寄存器分配映射
	const std::unordered_map<Value *, RegAllocInfo> & allocMap;

	/// @brief 指令编号映射
	const std::map<Instruction *, int> & instNumbering;

	/// @brief 虚拟寄存器活跃范围
	const std::unordered_map<Value *, std::pair<int, int>> & valueLiveRanges;

	/// @brief 每个物理寄存器的活跃区间反向索引：reg → [{start, end}, ...] 按start排序
	std::unordered_map<int, std::vector<std::pair<int, int>>> regIntervals;

	/// @brief ILoc引用（用于获取机器指令计数）
	ILocRiscV64 * iloc = nullptr;

	/// @brief 所有创建的scratch值
	std::vector<ScratchValue> scratchValues;

	/// @brief scratch唯一标识的所有权容器
	std::vector<std::unique_ptr<char>> scratchIdentities;

	/// @brief 物理寄存器→ScratchValue索引映射（当前借出的）
	std::unordered_map<int, std::size_t> regToScratch;
};
