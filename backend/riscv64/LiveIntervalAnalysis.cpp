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
#include "ConstInteger.h"
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

struct ValueLocation {
	int pos;
	BasicBlock * block;
};

struct LoopRange {
	const std::unordered_set<BasicBlock *> * body;
	int start;
	int end;
};

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

/// @brief 析构函数，释放活跃区间和干涉图
LiveIntervalAnalysis::~LiveIntervalAnalysis()
{
	for (auto * interval : intervals) {
		delete interval;
	}
	intervals.clear();
	valueToInterval.clear();
	delete interferenceGraph;
	interferenceGraph = nullptr;
}

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
/// - ConstInteger：整数类型常量，直接使用立即数编码
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
	if (dynamic_cast<ConstInteger *>(val)) {
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
	// alloca 一定分配在栈上，不需要活跃区间和寄存器分配
	if (dynamic_cast<AllocaInst *>(val)) {
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
/// 5. 基于CFG live-in/live-out、SSA def-use关系和LoopInfo补充跨块、跨回边存活段
///
void LiveIntervalAnalysis::computeLiveIntervals()
{
	nextInstNum = 0;
	auto & blocks = func->getBlocks();

	std::unordered_map<BasicBlock *, int> blockFirstInst;
	std::unordered_map<BasicBlock *, int> blockLastInst;
	std::unordered_map<BasicBlock *, ValueSet> blockUse;
	std::unordered_map<BasicBlock *, ValueSet> blockDef;
	std::unordered_map<BasicBlock *, ValueSet> liveIn;
	std::unordered_map<BasicBlock *, ValueSet> liveOut;
	std::unordered_map<Value *, std::vector<ValueLocation>> valueDefs;
	std::unordered_map<Value *, std::vector<ValueLocation>> valueUses;

	auto recordDef = [&](Value * value, BasicBlock * bb, int instNum) {
		if (!needsInterval(value)) {
			return;
		}

		valueDefs[value].push_back({instNum, bb});
		getOrCreateInterval(value)->addSegment(instNum, instNum + 1);
	};

	auto recordUse = [&](Value * value, BasicBlock * bb, int instNum) {
		if (!needsInterval(value)) {
			return;
		}

		valueUses[value].push_back({instNum, bb});
		getOrCreateInterval(value)->addUsePosition(instNum);
	};

	// 处理函数形参：形参在CFG入口前隐式定义，活跃区间起点为0
	auto & params = func->getParams();
	for (auto * param : params) {
		if (needsInterval(param)) {
			recordDef(param, nullptr, 0);
		}
	}

	// 按基本块顺序遍历指令
	for (auto * bb : blocks) {
		for (auto * inst : bb->getInstructions()) {
			int instNum = nextInstNum++;
			instNumbering[inst] = instNum;

			if (blockFirstInst.find(bb) == blockFirstInst.end()) {
				blockFirstInst[bb] = instNum;
			}
			blockLastInst[bb] = instNum;

			for (Value * usedValue : instructionUses(inst)) {
				recordUse(usedValue, bb, instNum);
				if (needsInterval(usedValue) && blockDef[bb].find(usedValue) == blockDef[bb].end()) {
					blockUse[bb].insert(usedValue);
				}
			}

			if (Value * definedValue = instructionDef(inst); needsInterval(definedValue)) {
				recordDef(definedValue, bb, instNum);
				blockDef[bb].insert(definedValue);
			}
		}
	}

	// CFG级活跃性补充了单纯def-use线性区间看不到的路径关系。
	// 这对PhiLowering生成的跨前驱copy尤其重要：某个copy的目标寄存器
	// 不能覆盖同一并行copy组里后续仍要读取的旧值。
	bool changed = true;
	while (changed) {
		changed = false;
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

	for (auto * bb : blocks) {
		auto firstIt = blockFirstInst.find(bb);
		auto lastIt = blockLastInst.find(bb);
		if (firstIt == blockFirstInst.end() || lastIt == blockLastInst.end()) {
			continue;
		}

		ValueSet blockLive = liveIn[bb];
		blockLive.insert(liveOut[bb].begin(), liveOut[bb].end());
		for (Value * value : blockLive) {
			if (needsInterval(value)) {
				getOrCreateInterval(value)->addSegment(firstIt->second, lastIt->second + 1);
			}
		}
	}

	// SSA值通常只有一个定义，可直接从定义点延伸到各使用点。
	// PhiLowering会为同一个phi结果在不同前驱块插入显式copy，形成多个定义；
	// 这种值使用一个保守连续区间覆盖所有copy定义和后续使用。
	for (auto * interval : intervals) {
		Value * value = interval->getVReg();
		auto defsIt = valueDefs.find(value);
		auto usesIt = valueUses.find(value);
		const auto * uses = usesIt != valueUses.end() ? &usesIt->second : nullptr;

		if (defsIt == valueDefs.end() || defsIt->second.empty()) {
			if (uses == nullptr || uses->empty()) {
				continue;
			}

			int lastUse = 0;
			for (const auto & use : *uses) {
				lastUse = std::max(lastUse, use.pos);
			}
			interval->addSegment(0, lastUse + 1);
			continue;
		}

		const auto & defs = defsIt->second;
		if (defs.size() == 1) {
			if (uses == nullptr) {
				continue;
			}

			const int defPos = defs.front().pos;
			for (const auto & use : *uses) {
				interval->addSegment(std::min(defPos, use.pos), std::max(defPos, use.pos) + 1);
			}
			continue;
		}

		int firstPos = std::numeric_limits<int>::max();
		int lastPos = std::numeric_limits<int>::min();
		for (const auto & def : defs) {
			firstPos = std::min(firstPos, def.pos);
			lastPos = std::max(lastPos, def.pos);
		}
		if (uses != nullptr) {
			for (const auto & use : *uses) {
				firstPos = std::min(firstPos, use.pos);
				lastPos = std::max(lastPos, use.pos);
			}
		}
		if (firstPos != std::numeric_limits<int>::max() && lastPos != std::numeric_limits<int>::min()) {
			interval->addSegment(firstPos, lastPos + 1);
		}
	}

	// 对跨越自然循环的值补充整段循环体。这里覆盖两类常见形态：
	// 1. 循环前定义，循环体内或循环退出后使用，必须穿过整个循环；
	// 2. 循环内定义，循环外使用，必须活到循环出口。
	// 这直接用LoopInfo覆盖回边，不再通过基本块live-in/live-out求不动点。
	if (loopInfo != nullptr) {
		std::vector<LoopRange> loopRanges;
		for (auto * bb : blocks) {
			if (!loopInfo->isLoopHeader(bb)) {
				continue;
			}
			const auto * body = loopInfo->getLoopBody(bb);
			if (body == nullptr) {
				continue;
			}
			int minInst = std::numeric_limits<int>::max();
			int maxInst = std::numeric_limits<int>::min();
			for (auto * bodyBB : *body) {
				auto itFirst = blockFirstInst.find(bodyBB);
				auto itLast = blockLastInst.find(bodyBB);
				if (itFirst != blockFirstInst.end()) {
					minInst = std::min(minInst, itFirst->second);
				}
				if (itLast != blockLastInst.end()) {
					maxInst = std::max(maxInst, itLast->second);
				}
			}
			if (minInst == std::numeric_limits<int>::max() || maxInst == std::numeric_limits<int>::min()) {
				continue;
			}
			loopRanges.push_back({body, minInst, maxInst + 1});
		}

		for (auto * interval : intervals) {
			Value * value = interval->getVReg();
			auto defsIt = valueDefs.find(value);
			auto usesIt = valueUses.find(value);
			if (usesIt == valueUses.end() || usesIt->second.empty()) {
				continue;
			}

			for (const auto & loop : loopRanges) {
				bool usedInLoop = false;
				bool usedOutsideLoop = false;
				bool usedAtOrAfterLoop = false;
				for (const auto & use : usesIt->second) {
					if (use.block != nullptr && loop.body->find(use.block) != loop.body->end()) {
						usedInLoop = true;
					} else {
						usedOutsideLoop = true;
					}
					if (use.pos >= loop.start) {
						usedAtOrAfterLoop = true;
					}
				}

				bool hasDefBeforeLoop = defsIt == valueDefs.end();
				bool hasDefInsideLoop = false;
				if (defsIt != valueDefs.end()) {
					for (const auto & def : defsIt->second) {
						if (def.block == nullptr || def.pos < loop.start) {
							hasDefBeforeLoop = true;
						}
						if (def.block != nullptr && loop.body->find(def.block) != loop.body->end()) {
							hasDefInsideLoop = true;
						}
					}
				}

				if ((hasDefBeforeLoop && (usedInLoop || usedAtOrAfterLoop)) ||
					(hasDefInsideLoop && usedOutsideLoop)) {
					interval->addSegment(loop.start, loop.end);
				}
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

	// 按区间 start 排序的索引数组，用于提前终止内层循环
	std::vector<int> sortedIdx(n);
	for (int k = 0; k < n; ++k) {
		sortedIdx[k] = k;
	}
	std::sort(sortedIdx.begin(), sortedIdx.end(), [this](int a, int b) {
		return intervals[a]->getStart() < intervals[b]->getStart();
	});

	for (int ii = 0; ii < n; ++ii) {
		int i = sortedIdx[ii];
		for (int jj = ii + 1; jj < n; ++jj) {
			int j = sortedIdx[jj];
			// intervals 按 start 排序，一旦 j 的 start >= i 的 end，后续都不会重叠
			if (intervals[j]->getStart() >= intervals[i]->getEnd()) break;
			if (intervals[i]->overlaps(*intervals[j])) {
				interferenceGraph->addEdge(i, j);
			}
		}
	}

	interferenceGraph->finalizeEdges();
}
