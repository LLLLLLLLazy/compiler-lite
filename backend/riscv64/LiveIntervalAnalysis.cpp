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

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "CallInst.h"
#include "CondBranchInst.h"
#include "ConstInt.h"
#include "CopyInst.h"
#include "Function.h"
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

/// @brief 构造函数
/// @param func 待分析的函数
LiveIntervalAnalysis::LiveIntervalAnalysis(Function * func)
	: func(func), interferenceGraph(nullptr), nextInstNum(0)
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
/// 5. 活跃区间使用半开区间 [def, lastUse+1) 表示
///
void LiveIntervalAnalysis::computeLiveIntervals()
{
	nextInstNum = 0;

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

			// 处理源操作数（使用点）
			// 通过遍历指令的操作数来识别使用点
			auto op = inst->getOp();

			switch (op) {
			case IRInstOperator::IRINST_OP_ADD_I:
			case IRInstOperator::IRINST_OP_SUB_I:
			case IRInstOperator::IRINST_OP_MUL_I:
			case IRInstOperator::IRINST_OP_DIV_I:
			case IRInstOperator::IRINST_OP_MOD_I: {
				// BinaryInst: lhs和rhs为使用点
				auto * binary = dynamic_cast<BinaryInst *>(inst);
				if (binary) {
					Value * lhs = binary->getLHS();
					if (needsInterval(lhs)) {
						getOrCreateInterval(lhs)->addUsePosition(instNum);
					}
					Value * rhs = binary->getRHS();
					if (needsInterval(rhs)) {
						getOrCreateInterval(rhs)->addUsePosition(instNum);
					}
				}
				break;
			}

			case IRInstOperator::IRINST_OP_LT_I:
			case IRInstOperator::IRINST_OP_GT_I:
			case IRInstOperator::IRINST_OP_LE_I:
			case IRInstOperator::IRINST_OP_GE_I:
			case IRInstOperator::IRINST_OP_EQ_I:
			case IRInstOperator::IRINST_OP_NE_I: {
				// ICmpInst: lhs和rhs为使用点
				auto * icmp = dynamic_cast<ICmpInst *>(inst);
				if (icmp) {
					Value * lhs = icmp->getLHS();
					if (needsInterval(lhs)) {
						getOrCreateInterval(lhs)->addUsePosition(instNum);
					}
					Value * rhs = icmp->getRHS();
					if (needsInterval(rhs)) {
						getOrCreateInterval(rhs)->addUsePosition(instNum);
					}
				}
				break;
			}

			case IRInstOperator::IRINST_OP_ALLOCA: {
				// AllocaInst: 无源操作数，结果为定义点
				// 定义点在下方统一处理
				break;
			}

			case IRInstOperator::IRINST_OP_LOAD: {
				// LoadInst: 指针操作数为使用点
				auto * load = dynamic_cast<LoadInst *>(inst);
				if (load) {
					Value * ptr = load->getPointerOperand();
					if (needsInterval(ptr)) {
						getOrCreateInterval(ptr)->addUsePosition(instNum);
					}
				}
				break;
			}

			case IRInstOperator::IRINST_OP_STORE: {
				// StoreInst: 值操作数和指针操作数均为使用点
				auto * store = dynamic_cast<StoreInst *>(inst);
				if (store) {
					Value * val = store->getValueOperand();
					if (needsInterval(val)) {
						getOrCreateInterval(val)->addUsePosition(instNum);
					}
					Value * ptr = store->getPointerOperand();
					if (needsInterval(ptr)) {
						getOrCreateInterval(ptr)->addUsePosition(instNum);
					}
				}
				break;
			}

			case IRInstOperator::IRINST_OP_BR: {
				// BranchInst: 无条件跳转，无源操作数
				break;
			}

			case IRInstOperator::IRINST_OP_COND_BR: {
				// CondBranchInst: 条件值为使用点
				auto * condBr = dynamic_cast<CondBranchInst *>(inst);
				if (condBr) {
					Value * cond = condBr->getCondition();
					if (needsInterval(cond)) {
						getOrCreateInterval(cond)->addUsePosition(instNum);
					}
				}
				break;
			}

			case IRInstOperator::IRINST_OP_RET: {
				// ReturnInst: 返回值为使用点
				auto * ret = dynamic_cast<ReturnInst *>(inst);
				if (ret && ret->hasReturnValue()) {
					Value * retVal = ret->getReturnValue();
					if (needsInterval(retVal)) {
						getOrCreateInterval(retVal)->addUsePosition(instNum);
					}
				}
				break;
			}

			case IRInstOperator::IRINST_OP_CALL: {
				// CallInst: 所有实参为使用点
				auto * call = dynamic_cast<CallInst *>(inst);
				if (call) {
					for (int32_t i = 0; i < call->getArgCount(); ++i) {
						Value * arg = call->getArg(i);
						if (needsInterval(arg)) {
							getOrCreateInterval(arg)->addUsePosition(instNum);
						}
					}
				}
				break;
			}

			case IRInstOperator::IRINST_OP_PHI: {
				// PhiInst: 各incoming的value为使用点
				auto * phi = dynamic_cast<PhiInst *>(inst);
				if (phi) {
					for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
						Value * incomingVal = phi->getIncoming(i).value;
						if (needsInterval(incomingVal)) {
							getOrCreateInterval(incomingVal)->addUsePosition(instNum);
						}
					}
				}
				break;
			}

			case IRInstOperator::IRINST_OP_ZEXT: {
				// ZExtInst: 源操作数为使用点
				auto * zext = dynamic_cast<ZExtInst *>(inst);
				if (zext) {
					Value * src = zext->getSource();
					if (needsInterval(src)) {
						getOrCreateInterval(src)->addUsePosition(instNum);
					}
				}
				break;
			}

			case IRInstOperator::IRINST_OP_COPY: {
				// CopyInst: 源操作数为使用点
				auto * copy = dynamic_cast<CopyInst *>(inst);
				if (copy) {
					Value * src = copy->getSource();
					if (needsInterval(src)) {
						getOrCreateInterval(src)->addUsePosition(instNum);
					}
				}
				break;
			}

			default:
				break;
			}

			// 处理定义点：如果指令有结果值，则该指令定义了一个虚拟寄存器
			if (inst->hasResultValue()) {
				LiveInterval * interval = getOrCreateInterval(inst);
				// 定义点：从当前指令编号开始
				// 添加一个从定义点开始的子段，结束点暂时设为定义点+1
				// 后续通过使用点来扩展区间
				interval->addSegment(instNum, instNum + 1);
			}

			// 对于phi降级的CopyInst（有explicitDst），逻辑目标也需要处理
			auto * copyInst = dynamic_cast<CopyInst *>(inst);
			if (copyInst && copyInst->getDst()) {
				Value * dst = copyInst->getDst();
				if (needsInterval(dst)) {
					// 逻辑目标在此处被定义（写入）
					LiveInterval * interval = getOrCreateInterval(dst);
					interval->addUsePosition(instNum);
				}
			}
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

	// 计算溢出权重（当前循环深度默认为0，后续可扩展）
	for (auto * interval : intervals) {
		interval->calcSpillWeight(0);
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
