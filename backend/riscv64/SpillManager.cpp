///
/// @file SpillManager.cpp
/// @brief 溢出管理器的实现
///
/// 负责在活跃区间被溢出时插入spill（store到栈）和reload（load从栈）代码，
/// 并管理溢出栈槽的分配。
///

#include "SpillManager.h"

#include <algorithm>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "Function.h"
#include "Instruction.h"
#include "IntegerType.h"
#include "LiveInterval.h"
#include "LoadInst.h"
#include "SpillStrategy.h"
#include "StoreInst.h"
#include "User.h"
#include "Value.h"

SpillManager::SpillManager(SpillStrategy * strategy)
	: strategy(strategy), spillSlotCount(0)
{}

LiveInterval *
SpillManager::selectSpillCandidate(std::vector<LiveInterval *> & candidates)
{
	return strategy->selectSpillCandidate(candidates);
}

void
SpillManager::allocateSpillSlot(LiveInterval * interval, Function * func)
{
	Value * vreg = interval->getVReg();

	// 若已分配栈槽，直接返回
	auto it = spillSlots.find(vreg);
	if (it != spillSlots.end()) {
		return;
	}

	// 在函数入口块创建AllocaInst，为溢出变量分配栈空间
	// 溢出变量的类型与虚拟寄存器的类型一致（通常为i32）
	Type * valType = vreg->getType();
	auto * allocaInst = new AllocaInst(func, valType);

	// 将AllocaInst插入到入口块的第一条指令之前
	BasicBlock * entryBlock = func->getEntryBlock();
	auto & insts = entryBlock->getInstructions();
	insts.push_front(allocaInst);
	allocaInst->setParentBlock(entryBlock);

	// 记录栈槽映射
	spillSlots[vreg] = allocaInst;

	// 设置区间的溢出栈偏移（每个溢出槽4字节，按spillSlotCount递增）
	int64_t slotOffset = static_cast<int64_t>(spillSlotCount) * 4;
	interval->setSpillSlot(slotOffset);
	interval->setSpilled(true);

	++spillSlotCount;
}

Instruction *
SpillManager::findInstructionByNumber(int instNum, Function * func,
	const std::map<Instruction *, int> & instNumbering)
{
	// 反向查找：遍历instNumbering找到编号对应的指令
	for (auto & [inst, num] : instNumbering) {
		if (num == instNum) {
			return inst;
		}
	}
	return nullptr;
}

void
SpillManager::insertSpillCode(LiveInterval * interval, Function * func,
	const std::map<Instruction *, int> & instNumbering)
{
	Value * vreg = interval->getVReg();

	// 确保已分配溢出栈槽
	allocateSpillSlot(interval, func);

	// 获取溢出栈槽的AllocaInst（作为store的指针操作数）
	auto it = spillSlots.find(vreg);
	Instruction * allocaInst = it->second;

	// 在定义点后插入StoreInst：store vreg, allocaInst
	// 定义点为vreg本身（vreg是一个Instruction的结果值）
	auto * defInst = dynamic_cast<Instruction *>(vreg);
	if (defInst == nullptr) {
		// vreg不是指令（如形参），跳过spill代码插入
		// 形参的值已在函数入口处通过寄存器传递，无需额外spill
		return;
	}

	// 创建StoreInst：将vreg的值存储到溢出栈槽
	auto * storeInst = new StoreInst(func, vreg, allocaInst);

	// 在定义指令所在的基本块中，找到定义指令的位置，在其后插入store
	BasicBlock * bb = defInst->getParentBlock();
	if (bb == nullptr) {
		return;
	}

	auto & insts = bb->getInstructions();
	auto iter = std::find(insts.begin(), insts.end(), defInst);
	if (iter != insts.end()) {
		++iter; // 移到定义指令之后
		insts.insert(iter, storeInst);
		storeInst->setParentBlock(bb);
	}
}

void
SpillManager::insertReloadCode(LiveInterval * interval, int usePos, Function * func,
	const std::map<Instruction *, int> & instNumbering)
{
	Value * vreg = interval->getVReg();

	// 确保已分配溢出栈槽
	allocateSpillSlot(interval, func);

	// 获取溢出栈槽的AllocaInst
	auto it = spillSlots.find(vreg);
	Instruction * allocaInst = it->second;

	// 查找使用点对应的指令
	Instruction * useInst = findInstructionByNumber(usePos, func, instNumbering);
	if (useInst == nullptr) {
		return;
	}

	// 创建LoadInst：从溢出栈槽加载值
	Type * valType = vreg->getType();
	auto * loadInst = new LoadInst(func, allocaInst, valType);

	// 在使用指令所在的基本块中，找到使用指令的位置，在其前插入load
	BasicBlock * bb = useInst->getParentBlock();
	if (bb == nullptr) {
		return;
	}

	auto & insts = bb->getInstructions();
	auto iter = std::find(insts.begin(), insts.end(), useInst);
	if (iter != insts.end()) {
		insts.insert(iter, loadInst);
		loadInst->setParentBlock(bb);
	}

	// 将使用指令中对vreg的引用替换为loadInst的结果
	// 使用User::replaceOperand仅替换当前使用指令中的操作数，
	// 而非replaceAllUseWith（那会替换所有使用点）
	auto * user = dynamic_cast<User *>(useInst);
	if (user) {
		user->replaceOperand(vreg, loadInst);
	}
}

int
SpillManager::getSpillSlots() const
{
	return spillSlotCount;
}
