///
/// @file LiveIntervalAnalysis.cpp
/// @brief 活跃区间分析器的实现
///
/// 遍历函数的所有基本块和指令，为每个虚拟寄存器构建活跃区间，
/// 然后根据区间重叠关系构建干涉图。
///

#include "LiveIntervalAnalysis.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "AllocaInst.h"
#include "LoopInfo.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "CallInst.h"
#include "CondBranchInst.h"
#include "ConstInt.h"
#include "ConstFloat.h"
#include "CopyInst.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "GlobalVariable.h"
#include "ICmpInst.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "PhiInst.h"
#include "RegVariable.h"
#include "ReturnInst.h"
#include "StoreInst.h"
#include "User.h"
#include "Value.h"
#include "ZExtInst.h"

namespace {

using ValueSet = std::unordered_set<Value *>;

std::vector<Value *> instructionUses(Instruction * inst)
{
	std::vector<Value *> uses;
	if (inst == nullptr) {
		return uses;
	}

	uses.reserve(inst->getOperandsNum());
	for (Value * operand : inst->getOperandsValue()) {
		uses.push_back(operand);
	}
	return uses;
}

Value * instructionDef(Instruction * inst)
{
	if (inst == nullptr) {
		return nullptr;
	}

	if (auto * copy = dynamic_cast<CopyInst *>(inst)) {
		if (copy->getDst() != nullptr) {
			return copy->getDst();
		}
	}

	return inst->hasResultValue() ? static_cast<Value *>(inst) : nullptr;
}

} // namespace

/// @brief 构造函数
/// @param func 待分析的函数
/// @param loopInfo 循环分析结果（可选），用于计算循环深度加权的溢出权重
LiveIntervalAnalysis::LiveIntervalAnalysis(Function * func, LoopInfo * loopInfo)
	: func(func), interferenceGraph(nullptr), nextInstNum(0), loopInfo(loopInfo)
{}

/// @brief 执行完整的活跃区间分析流程
void LiveIntervalAnalysis::run()
{
	computeLiveIntervals();
	buildInterferenceGraph();
}

/// @brief 获取所有活跃区间
std::vector<LiveInterval *> & LiveIntervalAnalysis::getIntervals()
{
	return intervals;
}

/// @brief 获取干涉图
InterferenceGraph * LiveIntervalAnalysis::getInterferenceGraph()
{
	return interferenceGraph;
}

/// @brief 获取Value到LiveInterval索引的映射
const std::unordered_map<Value *, int> & LiveIntervalAnalysis::getValueToIntervalMap() const
{
	return valueToInterval;
}

/// @brief 获取指令编号映射
const std::map<Instruction *, int> & LiveIntervalAnalysis::getInstNumbering() const
{
	return instNumbering;
}

/// @brief 判断Value是否需要活跃区间分析
///
/// 以下Value不需要分配虚拟寄存器，因此不需要活跃区间：
/// - ConstInt：整数常量，直接使用立即数编码
/// - GlobalVariable：全局变量，通过la伪指令加载地址
/// - RegVariable：物理寄存器，已分配
/// - Function：函数对象，不是变量
/// - BasicBlock：基本块标签，不是变量
///
bool LiveIntervalAnalysis::needsInterval(Value * val)
{
	if (val == nullptr) {
		return false;
	}
	// 常量不需要寄存器分配
	if (dynamic_cast<ConstInt *>(val)) {
		return false;
	}
	if (dynamic_cast<ConstFloat *>(val)) {
		return false;
	}
	// 全局变量通过地址访问，不需要分配寄存器
	if (dynamic_cast<GlobalVariable *>(val)) {
		return false;
	}
	// 物理寄存器已分配，不需要活跃区间
	if (dynamic_cast<RegVariable *>(val)) {
		return false;
	}
	// 函数对象不是变量
	if (dynamic_cast<Function *>(val)) {
		return false;
	}
	// 基本块不是变量
	if (dynamic_cast<BasicBlock *>(val)) {
		return false;
	}
	return true;
}

/// @brief 获取或创建Value对应的LiveInterval
LiveInterval * LiveIntervalAnalysis::getOrCreateInterval(Value * val)
{
	auto it = valueToInterval.find(val);
	if (it != valueToInterval.end()) {
		return intervals[it->second];
	}

	// 创建新的LiveInterval
	auto * interval = new LiveInterval(val);
	int idx = static_cast<int>(intervals.size());
	intervals.push_back(interval);
	valueToInterval[val] = idx;
	return interval;
}

/// @brief 计算所有虚拟寄存器的活跃区间
///
/// 遍历策略：
/// 1. 按基本块顺序遍历函数的所有基本块
/// 2. 对每个基本块，遍历其指令列表，为每条指令编号（0, 1, 2, ...）
/// 3. 对于函数形参（前8个分配到a0-a7），设置其活跃区间的起点为0
/// 4. 对于每条指令：
///    - 源操作数为使用点，更新对应活跃区间的使用位置
///    - 结果值为定义点，更新对应活跃区间的定义位置
/// 5. 基于CFG求解基本块live-in/live-out，为跨回边和跨块copy补充存活段
///
void LiveIntervalAnalysis::computeLiveIntervals()
{
	nextInstNum = 0;
	std::unordered_map<BasicBlock *, int> blockStartNums;
	std::unordered_map<BasicBlock *, int> blockEndNums;
	auto & blocks = func->getBlocks();
	std::unordered_map<BasicBlock *, int> blockOrder;
	for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
		blockOrder[blocks[i]] = i;
	}

	std::unordered_map<BasicBlock *, int> blockFirstInst;
	std::unordered_map<BasicBlock *, int> blockLastInst;
	std::unordered_map<BasicBlock *, ValueSet> blockUse;
	std::unordered_map<BasicBlock *, ValueSet> blockDef;
	std::unordered_map<BasicBlock *, ValueSet> liveIn;
	std::unordered_map<BasicBlock *, ValueSet> liveOut;

	// 处理函数形参：形参在函数入口处定义，活跃区间起点为0
	auto & params = func->getParams();
	for (auto * param : params) {
		if (needsInterval(param)) {
			LiveInterval * interval = getOrCreateInterval(param);
			// 形参在指令编号0处定义
			interval->addSegment(0, 1);
			interval->addUsePosition(0);
		}
	}

	// 按基本块顺序遍历指令
	for (auto * bb : func->getBlocks()) {
		for (auto * inst : bb->getInstructions()) {
			int instNum = nextInstNum++;
			instNumbering[inst] = instNum;

			if (blockFirstInst.find(bb) == blockFirstInst.end()) {
				blockFirstInst[bb] = instNum;
			}
			blockLastInst[bb] = instNum;

			for (Value * usedValue : instructionUses(inst)) {
				if (!needsInterval(usedValue)) {
					continue;
				}
				getOrCreateInterval(usedValue)->addUsePosition(instNum);
				if (blockDef[bb].find(usedValue) == blockDef[bb].end()) {
					blockUse[bb].insert(usedValue);
				}
			}

			if (Value * definedValue = instructionDef(inst); needsInterval(definedValue)) {
				LiveInterval * interval = getOrCreateInterval(definedValue);
				// 定义点：从当前指令编号开始
				// 添加一个从定义点开始的子段，结束点暂时设为定义点+1
				// 后续通过使用点来扩展区间
				interval->addSegment(instNum, instNum + 1);
				blockDef[bb].insert(definedValue);
			}
		}
	}

	// 基于CFG求解基本块级live-in/live-out集合，覆盖循环回边与PhiLowering
	// 插入的跨前驱copy。后续区间扩展使用这些集合保守地添加整块存活段。
	bool changed = true;
	while (changed) {
		changed = false;
		auto & blocks = func->getBlocks();
		for (auto it = blocks.rbegin(); it != blocks.rend(); ++it) {
			BasicBlock * bb = *it;
			ValueSet newOut;
			for (BasicBlock * succ : bb->getSuccessors()) {
				auto succLiveIt = liveIn.find(succ);
				if (succLiveIt != liveIn.end()) {
					newOut.insert(succLiveIt->second.begin(), succLiveIt->second.end());
				}
			}

			ValueSet newIn = blockUse[bb];
			for (Value * value : newOut) {
				if (blockDef[bb].find(value) == blockDef[bb].end()) {
					newIn.insert(value);
				}
			}

			if (newOut != liveOut[bb] || newIn != liveIn[bb]) {
				liveOut[bb] = std::move(newOut);
				liveIn[bb] = std::move(newIn);
				changed = true;
			}
		}
	}

	for (auto * bb : func->getBlocks()) {
		auto firstIt = blockFirstInst.find(bb);
		auto lastIt = blockLastInst.find(bb);
		if (firstIt == blockFirstInst.end() || lastIt == blockLastInst.end()) {
			continue;
		}

		const int blockStart = firstIt->second;
		const int blockEnd = lastIt->second + 1;
		ValueSet blockLive = liveIn[bb];
		blockLive.insert(liveOut[bb].begin(), liveOut[bb].end());
		for (Value * value : blockLive) {
			if (needsInterval(value)) {
				getOrCreateInterval(value)->addSegment(blockStart, blockEnd);
			}
		}

		if (nextInstNum > blockStart) {
			blockStartNums[bb] = blockStart;
			blockEndNums[bb] = nextInstNum;
		}
	}

	// 第二遍：根据使用点扩展活跃区间
	// 对于每个活跃区间，使用点已经记录，现在需要构建完整的存活子段
	// 活跃区间从定义点延伸到最后一个使用点
	for (auto * interval : intervals) {
		const auto & usePositions = interval->getUsePositions();

		if (usePositions.empty()) {
			// 没有使用点，区间仅包含定义点（已在上面添加）
			continue;
		}

		// 找到最后一个使用点
		int lastUse = *std::max_element(usePositions.begin(), usePositions.end());

		// 找到定义点（区间的起始位置）
		int defPoint = interval->getStart();

		// 如果定义点有效，扩展区间到覆盖最后一个使用点
		if (defPoint < std::numeric_limits<int>::max() && lastUse >= defPoint) {
			// 添加从定义点到最后一个使用点+1的子段
			// addSegment会自动与已有子段合并
			interval->addSegment(defPoint, lastUse + 1);
		} else if (defPoint >= std::numeric_limits<int>::max()) {
			// 定义点未设置（如形参，其定义点为0但通过addSegment(0,1)已设置）
			// 对于形参，start已被设为0，需要从0延伸到最后使用点
			interval->addSegment(0, lastUse + 1);
		}
	}
	// 线性编号本身不表达回边迭代。对所有“在循环内使用、且定义发生在循环外”的值，
	// 保守地把活跃区间延长到循环头之后，避免循环不变量在一次迭代后被错误复用寄存器。
	for (auto * bb : blocks) {
		auto bbStartIt = blockStartNums.find(bb);
		auto bbEndIt = blockEndNums.find(bb);
		if (bbStartIt == blockStartNums.end() || bbEndIt == blockEndNums.end()) {
			continue;
		}

		for (auto * succ : bb->getSuccessors()) {
			auto succStartIt = blockStartNums.find(succ);
			auto succOrderIt = blockOrder.find(succ);
			auto bbOrderIt = blockOrder.find(bb);
			if (succStartIt == blockStartNums.end() || succOrderIt == blockOrder.end() ||
				bbOrderIt == blockOrder.end() || succOrderIt->second > bbOrderIt->second) {
				continue;
			}

			const int loopStart = succStartIt->second;
			for (int idx = succOrderIt->second; idx <= bbOrderIt->second; ++idx) {
				BasicBlock * loopBB = blocks[idx];
				for (auto * inst : loopBB->getInstructions()) {
					for (auto * operand : inst->getOperandsValue()) {
						if (!needsInterval(operand)) {
							continue;
						}

						LiveInterval * interval = getOrCreateInterval(operand);
						if (interval->getStart() < loopStart) {
							interval->addSegment(loopStart, nextInstNum);
						}
					}
				}
			}
		}
	}

	// 第三遍：循环感知的活跃区间扩展
	// Mem2Reg 会将 alloca 提升为 SSA 值直接引用。对于来自循环外部的值
	// （函数参数等），其 use 仅出现在循环头，但对应寄存器可能在循环体内
	// 被 GEP 等操作覆盖。这里将这些"循环外定义、循环头使用"的值扩展到
	// 循环体末尾，确保寄存器在循环内不会被错误复用。
	// 循环内定义的值无需扩展——它们的定义点本身就在循环内，寄存器分配
	// 已能正确处理。
	if (loopInfo != nullptr) {
		// 构建每个基本块中的指令编号范围
		std::unordered_map<BasicBlock *, int> bbFirstInst;
		std::unordered_map<BasicBlock *, int> bbLastInst;
		for (auto & [inst, num] : instNumbering) {
			BasicBlock * bb = inst->getParentBlock();
			if (bb == nullptr) {
				continue;
			}
			auto it = bbFirstInst.find(bb);
			if (it == bbFirstInst.end()) {
				bbFirstInst[bb] = num;
				bbLastInst[bb] = num;
			} else {
				bbFirstInst[bb] = std::min(bbFirstInst[bb], num);
				bbLastInst[bb] = std::max(bbLastInst[bb], num);
			}
		}

		// 对每个循环，计算循环体块的编号范围
		std::unordered_map<BasicBlock *, int> loopBodyMinInst;
		std::unordered_map<BasicBlock *, int> loopBodyMaxInst;
		for (auto * bb : func->getBlocks()) {
			if (!loopInfo->isLoopHeader(bb)) {
				continue;
			}
			const auto * body = loopInfo->getLoopBody(bb);
			if (body == nullptr) {
				continue;
			}
			int minInst = std::numeric_limits<int>::max();
			int maxInst = 0;
			for (auto * bodyBB : *body) {
				auto itFirst = bbFirstInst.find(bodyBB);
				auto itLast = bbLastInst.find(bodyBB);
				if (itFirst != bbFirstInst.end()) {
					minInst = std::min(minInst, itFirst->second);
				}
				if (itLast != bbLastInst.end()) {
					maxInst = std::max(maxInst, itLast->second);
				}
			}
			loopBodyMinInst[bb] = minInst;
			loopBodyMaxInst[bb] = maxInst;
		}

		// 扩展循环头中使用的、在循环外定义的值
		for (auto * interval : intervals) {
			int defPoint = interval->getStart();
			if (defPoint >= std::numeric_limits<int>::max()) {
				defPoint = 0; // 形参等隐式定义在0处
			}

			const auto & usePositions = interval->getUsePositions();
			for (int usePos : usePositions) {
				BasicBlock * useBB = nullptr;
				for (auto & [inst, num] : instNumbering) {
					if (num == usePos) {
						useBB = inst->getParentBlock();
						break;
					}
				}
				if (useBB == nullptr || !loopInfo->isLoopHeader(useBB)) {
					continue;
				}

				int bodyMin = loopBodyMinInst[useBB];
				// 仅当值定义在循环体外部时，才需要扩展到循环体末尾
				if (defPoint >= bodyMin) {
					continue; // 值在循环体内定义，寄存器分配可正确处理
				}

				int maxInst = loopBodyMaxInst[useBB];
				if (maxInst > interval->getEnd()) {
					interval->addSegment(defPoint, maxInst + 1);
				}
				break;
			}
		}
	}

	// 计算溢出权重
	if (loopInfo != nullptr) {
		// 建立指令编号到循环深度的映射
		std::unordered_map<int, int> instNumToLoopDepth;
		for (auto & [inst, num] : instNumbering) {
			BasicBlock * bb = inst->getParentBlock();
			if (bb != nullptr) {
				instNumToLoopDepth[num] = loopInfo->getLoopDepth(bb);
			}
		}

		for (auto * interval : intervals) {
			int maxDepth = 0;
			for (int pos : interval->getUsePositions()) {
				auto it = instNumToLoopDepth.find(pos);
				if (it != instNumToLoopDepth.end()) {
					maxDepth = std::max(maxDepth, it->second);
				}
			}
			interval->maxLoopDepth = maxDepth;
			interval->calcSpillWeight(maxDepth);
		}
	} else {
		for (auto * interval : intervals) {
			interval->calcSpillWeight(0);
		}
	}
}

/// @brief 根据活跃区间重叠关系构建干涉图
///
/// 对每对活跃区间检查是否重叠（overlaps），若重叠则添加干涉边。
/// 干涉图节点索引与intervals中的索引一致。
///
void LiveIntervalAnalysis::buildInterferenceGraph()
{
	int n = static_cast<int>(intervals.size());
	interferenceGraph = new InterferenceGraph(n);

	// 对每对区间检查是否干涉
	for (int i = 0; i < n; ++i) {
		for (int j = i + 1; j < n; ++j) {
			if (intervals[i]->overlaps(*intervals[j])) {
				interferenceGraph->addEdge(i, j);
			}
		}
	}
}
