///
/// @file ConstProp.cpp
/// @brief Sparse Conditional Constant Propagation pass
///

#include "ConstProp.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <unordered_set>

#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CondBranchInst.h"
#include "ConstInt.h"
#include "Function.h"
#include "ICmpInst.h"
#include "Instruction.h"
#include "symboltable/Module.h"
#include "PhiInst.h"
#include "Value.h"
#include "ZExtInst.h"

namespace {

/// @brief SCCP 使用的 lattice 状态
enum class LatticeState {
    Unknown,
    Constant,
    Overdefined,
};

/// @brief 表示 SCCP 中的 lattice 值
struct LatticeValue {
    LatticeState state = LatticeState::Unknown;
    int32_t constant = 0;

    /// @brief 获取 unknown 状态的 lattice 值
    static LatticeValue getUnknown()
    {
        return {};
    }

    /// @brief 获取 constant 状态的 lattice 值
    static LatticeValue getConstant(int32_t value)
    {
        return {LatticeState::Constant, value};
    }

    /// @brief 获取 overdefined 状态的 lattice 值
    static LatticeValue getOverdefined()
    {
        return {LatticeState::Overdefined, 0};
    }

    /// @brief 判断 lattice 值是否为 unknown/constant/overdefined 状态
    bool isUnknown() const{ return state == LatticeState::Unknown; }
    bool isConstant() const{ return state == LatticeState::Constant; }
    bool isOverdefined() const{ return state == LatticeState::Overdefined; }
};

/// @brief 表示 CFG 边的结构体
struct CFGEdge {
    BasicBlock * from = nullptr;
    BasicBlock * to = nullptr;

    bool operator==(const CFGEdge & other) const
    {
        return from == other.from && to == other.to;
    }
};

/// @brief 用于在 unordered_set 中存储 CFGEdge 的哈希函数
struct CFGEdgeHash {
    std::size_t operator()(const CFGEdge & edge) const
    {
        std::size_t fromHash = std::hash<BasicBlock *>{}(edge.from);
        std::size_t toHash = std::hash<BasicBlock *>{}(edge.to);
        return fromHash ^ (toHash << 1U);
    }
};

/// @brief 标记指令为死代码并清除其操作数
void markInstDead(Instruction * inst)
{
    if (!inst) {
        return;
    }

    inst->clearOperands();
    inst->setDead(true);
}

/// @brief 从基本块中删除指定前驱块的 incoming 对
void removePhiIncomingFromPred(BasicBlock * bb, BasicBlock * pred)
{
    if (!bb || !pred) {
        return;
    }

    for (auto * inst : bb->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }

        phi->removeIncomingBlock(pred);
    }
}

/// @brief 将常量条件分支改写为无条件跳转
/// @param condBr 待改写的条件跳转指令
/// @param takeTrue 是否选择 true 分支
/// @return 改写成功时返回 true
bool rewriteCondBranch(CondBranchInst * condBr, bool takeTrue)
{
    if (!condBr) {
        return false;
    }

    auto * parent = condBr->getParentBlock();
    if (!parent) {
        return false;
    }

    BasicBlock * taken = takeTrue ? condBr->getTrueDest() : condBr->getFalseDest();
    BasicBlock * nonTaken = takeTrue ? condBr->getFalseDest() : condBr->getTrueDest();
    if (!taken) {
        return false;
    }

    if (taken != nonTaken && nonTaken) {
        parent->removeSuccessor(nonTaken);
        nonTaken->removePredecessor(parent);
        removePhiIncomingFromPred(nonTaken, parent);
    }

    parent->addSuccessor(taken);
    taken->addPredecessor(parent);

    auto * replacement = new BranchInst(condBr->getFunction(), taken);
    replacement->setParentBlock(parent);

    auto & insts = parent->getInstructions();
    auto pos = std::find(insts.begin(), insts.end(), static_cast<Instruction *>(condBr));
    if (pos == insts.end()) {
        delete replacement;
        return false;
    }

    condBr->clearOperands();
    delete condBr;
    *pos = replacement;
    return true;
}

/// @brief SCCP 求解器
///
/// 维护两类信息：
///   1. 哪些基本块和 CFG 边已经被证明可执行
///   2. 每个 SSA 值在 lattice 上的状态
///
/// 求解完成后，再统一把常量值和常量条件分支回写到 IR 中
class SCCPSolver {

public:
    /// @brief 构造 SCCP 求解器
    /// @param function 待优化的函数
    /// @param module 常量创建所需的模块对象
    SCCPSolver(Function * function, Module * module) : func(function), mod(module)
    {}

    /// @brief 执行 SCCP 求解并将结果回写到 IR
    /// @return 若 IR 被修改则返回 true
    bool run()
    {
        if (!func || !mod) {
            return false;
        }

        solve();
        return rewrite();
    }

private:
    /// @brief 读取某个值当前的 lattice 状态
    /// @param value 待查询的值
    /// @return 该值在当前求解过程中的 lattice 状态
    LatticeValue getValueState(Value * value) const
    {
        if (!value) {
            return LatticeValue::getUnknown();
        }

        if (auto * constInt = dynamic_cast<ConstInt *>(value)) {
            return LatticeValue::getConstant(constInt->getVal());
        }

        if (!dynamic_cast<Instruction *>(value)) {
            // Function parameters and other non-instruction runtime values do
            // not have an SSA definition inside the current function, so SCCP
            // must conservatively treat them as varying.
            return LatticeValue::getOverdefined();
        }

        auto it = valueState.find(value);
        if (it != valueState.end()) {
            return it->second;
        }

        return LatticeValue::getUnknown();
    }

    /// @brief 将新求得的 lattice 信息并入指定值
    /// @param value 目标值
    /// @param incoming 新的 lattice 状态
    /// @return 若该值状态发生变化则返回 true
    bool mergeValueState(Value * value, const LatticeValue & incoming)
    {
        if (!value || incoming.isUnknown()) {
            return false;
        }

        LatticeValue current = getValueState(value);
        LatticeValue next = current;

        if (current.isUnknown()) {
            next = incoming;
        } else if (current.isConstant()) {
            if (incoming.isOverdefined()) {
                next = incoming;
            } else if (incoming.isConstant() && incoming.constant != current.constant) {
                next = LatticeValue::getOverdefined();
            }
        }

        if (next.state == current.state && next.constant == current.constant) {
            return false;
        }

        // 一旦 lattice 值变得更精确或退化为 overdefined，依赖它的用户都需要重新求值
        valueState[value] = next;
        enqueueUsers(value);
        return true;
    }

    /// @brief 将指令加入值工作队列
    /// @param inst 待入队的指令
    void enqueueInstruction(Instruction * inst)
    {
        if (!inst || queuedInsts.count(inst)) {
            return;
        }

        queuedInsts.insert(inst);
        instWorklist.push_back(inst);
    }

    /// @brief 将某个值的所有用户重新加入求值队列
    /// @param value 发生状态变化的值
    void enqueueUsers(Value * value)
    {
        if (!value) {
            return;
        }

        for (auto * use : value->getUseList()) {
            auto * user = dynamic_cast<Instruction *>(use->getUser());
            enqueueInstruction(user);
        }
    }

    /// @brief 将基本块开头的 phi 指令重新加入求值队列
    /// @param bb 目标基本块
    void enqueuePhiInstructions(BasicBlock * bb)
    {
        if (!bb) {
            return;
        }

        for (auto * inst : bb->getInstructions()) {
            if (inst->getOp() != IRInstOperator::IRINST_OP_PHI) {
                break;
            }

            enqueueInstruction(inst);
        }
    }

    /// @brief 判断基本块当前是否已被证明可执行
    /// @param bb 待查询的基本块
    /// @return 若基本块可执行则返回 true
    bool isBlockExecutable(BasicBlock * bb) const
    {
        return bb && executableBlocks.count(bb);
    }

    /// @brief 判断 CFG 边当前是否已被证明可执行
    /// @param from 边的起点
    /// @param to 边的终点
    /// @return 若边可执行则返回 true
    bool isEdgeExecutable(BasicBlock * from, BasicBlock * to) const
    {
        return executableEdges.count(CFGEdge{from, to}) > 0;
    }

    /// @brief 将基本块标记为可执行并触发块内指令求值
    /// @param bb 新发现可执行的基本块
    void markBlockExecutable(BasicBlock * bb)
    {
        if (!bb || !executableBlocks.insert(bb).second) {
            return;
        }

        // 一个块首次变成可执行时，块内所有指令都需要进入求值队列
        for (auto * inst : bb->getInstructions()) {
            enqueueInstruction(inst);
        }
    }

    /// @brief 将一条 CFG 边加入流工作队列
    /// @param from 边的起点
    /// @param to 边的终点
    void enqueueEdge(BasicBlock * from, BasicBlock * to)
    {
        if (!from || !to) {
            return;
        }

        CFGEdge edge{from, to};
        if (queuedEdges.count(edge) || executableEdges.count(edge)) {
            return;
        }

        queuedEdges.insert(edge);
        edgeWorklist.push_back(edge);
    }

    /// @brief 计算二元算术指令的 lattice 值
    /// @param inst 待求值的二元算术指令
    /// @return 求值得到的 lattice 状态
    LatticeValue evaluateBinary(BinaryInst * inst) const
    {
        auto lhs = getValueState(inst->getLHS());
        auto rhs = getValueState(inst->getRHS());

        if (lhs.isConstant() && rhs.isConstant()) {
            switch (inst->getOp()) {
                case IRInstOperator::IRINST_OP_ADD_I:
                    return LatticeValue::getConstant(lhs.constant + rhs.constant);

                case IRInstOperator::IRINST_OP_SUB_I:
                    return LatticeValue::getConstant(lhs.constant - rhs.constant);

                case IRInstOperator::IRINST_OP_MUL_I:
                    return LatticeValue::getConstant(lhs.constant * rhs.constant);

                case IRInstOperator::IRINST_OP_DIV_I:
                    return rhs.constant == 0 ? LatticeValue::getOverdefined()
                                             : LatticeValue::getConstant(lhs.constant / rhs.constant);

                case IRInstOperator::IRINST_OP_MOD_I:
                    return rhs.constant == 0 ? LatticeValue::getOverdefined()
                                             : LatticeValue::getConstant(lhs.constant % rhs.constant);

                default:
                    return LatticeValue::getOverdefined();
            }
        }

        if (lhs.isOverdefined() || rhs.isOverdefined()) {
            return LatticeValue::getOverdefined();
        }

        return LatticeValue::getUnknown();
    }

    /// @brief 计算整数比较指令的 lattice 值
    /// @param inst 待求值的比较指令
    /// @return 求值得到的 lattice 状态
    LatticeValue evaluateICmp(ICmpInst * inst) const
    {
        auto lhs = getValueState(inst->getLHS());
        auto rhs = getValueState(inst->getRHS());

        if (lhs.isConstant() && rhs.isConstant()) {
            int32_t result = 0;
            switch (inst->getOp()) {
                case IRInstOperator::IRINST_OP_LT_I:
                    result = lhs.constant < rhs.constant;
                    break;

                case IRInstOperator::IRINST_OP_GT_I:
                    result = lhs.constant > rhs.constant;
                    break;

                case IRInstOperator::IRINST_OP_LE_I:
                    result = lhs.constant <= rhs.constant;
                    break;

                case IRInstOperator::IRINST_OP_GE_I:
                    result = lhs.constant >= rhs.constant;
                    break;

                case IRInstOperator::IRINST_OP_EQ_I:
                    result = lhs.constant == rhs.constant;
                    break;

                case IRInstOperator::IRINST_OP_NE_I:
                    result = lhs.constant != rhs.constant;
                    break;

                default:
                    return LatticeValue::getOverdefined();
            }

            return LatticeValue::getConstant(result);
        }

        if (lhs.isOverdefined() || rhs.isOverdefined()) {
            return LatticeValue::getOverdefined();
        }

        return LatticeValue::getUnknown();
    }

    /// @brief 计算 phi 指令的 lattice 值
    /// @param phi 待求值的 phi 指令
    /// @return 仅基于可执行 incoming 得到的 lattice 状态
    LatticeValue evaluatePhi(PhiInst * phi) const
    {
        LatticeValue result = LatticeValue::getUnknown();
        bool hasExecutableIncoming = false;
        bool hasUnknownExecutableIncoming = false;

        for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
            const auto & incoming = phi->getIncoming(i);
            // SCCP 只看当前已经可执行的 incoming 边
            if (!isEdgeExecutable(incoming.block, phi->getParentBlock())) {
                continue;
            }

            hasExecutableIncoming = true;
            LatticeValue incomingState = getValueState(incoming.value);
            if (incomingState.isOverdefined()) {
                return LatticeValue::getOverdefined();
            }

            if (incomingState.isUnknown()) {
                hasUnknownExecutableIncoming = true;
                continue;
            }

            if (result.isUnknown()) {
                result = incomingState;
                continue;
            }

            if (result.constant != incomingState.constant) {
                return LatticeValue::getOverdefined();
            }
        }

        if (!hasExecutableIncoming || hasUnknownExecutableIncoming) {
            return LatticeValue::getUnknown();
        }

        return result;
    }

    /// @brief 计算 zext 指令的 lattice 值
    /// @param inst 待求值的 zext 指令
    /// @return 求值得到的 lattice 状态
    LatticeValue evaluateZExt(ZExtInst * inst) const
    {
        LatticeValue source = getValueState(inst->getSource());
        if (source.isConstant()) {
            return LatticeValue::getConstant(source.constant);
        }

        if (source.isOverdefined()) {
            return LatticeValue::getOverdefined();
        }

        return LatticeValue::getUnknown();
    }

    /// @brief 根据指令种类分发具体的 lattice 求值逻辑
    /// @param inst 待求值的指令
    /// @return 该指令结果值的 lattice 状态
    LatticeValue evaluateInstruction(Instruction * inst) const
    {
        if (!inst || !inst->hasResultValue()) {
            return LatticeValue::getUnknown();
        }

        switch (inst->getOp()) {
            case IRInstOperator::IRINST_OP_ADD_I:
            case IRInstOperator::IRINST_OP_SUB_I:
            case IRInstOperator::IRINST_OP_MUL_I:
            case IRInstOperator::IRINST_OP_DIV_I:
            case IRInstOperator::IRINST_OP_MOD_I:
                return evaluateBinary(dynamic_cast<BinaryInst *>(inst));

            case IRInstOperator::IRINST_OP_LT_I:
            case IRInstOperator::IRINST_OP_GT_I:
            case IRInstOperator::IRINST_OP_LE_I:
            case IRInstOperator::IRINST_OP_GE_I:
            case IRInstOperator::IRINST_OP_EQ_I:
            case IRInstOperator::IRINST_OP_NE_I:
                return evaluateICmp(dynamic_cast<ICmpInst *>(inst));

            case IRInstOperator::IRINST_OP_PHI:
                return evaluatePhi(dynamic_cast<PhiInst *>(inst));

            case IRInstOperator::IRINST_OP_ZEXT:
                return evaluateZExt(dynamic_cast<ZExtInst *>(inst));

            case IRInstOperator::IRINST_OP_COPY:
                return getValueState(inst->getOperand(0));

            default:
                return LatticeValue::getOverdefined();
        }
    }

    /// @brief 根据终结指令传播新的可执行 CFG 边
    /// @param inst 当前处理的终结指令
    void processFlowInstruction(Instruction * inst)
    {
        auto * parent = inst->getParentBlock();
        if (!parent) {
            return;
        }

        if (auto * br = dynamic_cast<BranchInst *>(inst)) {
            enqueueEdge(parent, br->getTarget());
            return;
        }

        auto * condBr = dynamic_cast<CondBranchInst *>(inst);
        if (!condBr) {
            return;
        }

        LatticeValue cond = getValueState(condBr->getCondition());
        if (cond.isConstant()) {
            // 条件已知时，只传播可达那一条边
            enqueueEdge(parent, cond.constant != 0 ? condBr->getTrueDest() : condBr->getFalseDest());
            return;
        }

        if (cond.isOverdefined() || condBr->getTrueDest() == condBr->getFalseDest()) {
            // 条件无法再精化时，保守地认为两条边都可能执行
            enqueueEdge(parent, condBr->getTrueDest());
            if (condBr->getFalseDest() != condBr->getTrueDest()) {
                enqueueEdge(parent, condBr->getFalseDest());
            }
        }
    }

    /// @brief 执行 SCCP 的双工作队列求解过程
    void solve()
    {
        // 从入口块开始，交替消费边工作队列和值工作队列直到收敛
        markBlockExecutable(func->getEntryBlock());

        while (!edgeWorklist.empty() || !instWorklist.empty()) {
            if (!edgeWorklist.empty()) {
                CFGEdge edge = edgeWorklist.front();
                edgeWorklist.pop_front();
                queuedEdges.erase(edge);

                if (!executableEdges.insert(edge).second) {
                    continue;
                }

                // 已执行块新增了一条可执行 incoming 时，只需要重看 phi
                if (isBlockExecutable(edge.to)) {
                    enqueuePhiInstructions(edge.to);
                } else {
                    markBlockExecutable(edge.to);
                }
                continue;
            }

            Instruction * inst = instWorklist.front();
            instWorklist.pop_front();
            queuedInsts.erase(inst);

            if (!inst || inst->isDead()) {
                continue;
            }

            BasicBlock * parent = inst->getParentBlock();
            if (parent && !isBlockExecutable(parent)) {
                continue;
            }

            if (inst->isTerminator()) {
                processFlowInstruction(inst);
                continue;
            }

            if (!inst->hasResultValue()) {
                continue;
            }

            mergeValueState(inst, evaluateInstruction(inst));
        }
    }

    /// @brief 将 lattice 里的常量结果还原成 IR 常量对象
    /// @param type 目标常量类型
    /// @param value 常量的整数值
    /// @return 对应的 IR 常量对象
    ConstInt * materializeConstant(Type * type, int32_t value) const
    {
        if (!type || !type->isIntegerType()) {
            return nullptr;
        }

        if (type->isInt1Byte()) {
            return mod->newConstBool(value != 0);
        }

        if (type->isInt32Type()) {
            return mod->newConstInt(value);
        }

        return mod->newConstInteger(type, value);
    }

    /// @brief 将 SCCP 求得的常量与常量分支回写到 IR 中
    /// @return 若回写阶段修改了 IR 则返回 true
    bool rewrite()
    {
        bool changed = false;

        // 只改写可执行块里的指令
        for (auto * bb : func->getBlocks()) {
            if (!isBlockExecutable(bb)) {
                continue;
            }

            for (auto * inst : bb->getInstructions()) {
                if (!inst || inst->isDead()) {
                    continue;
                }

                if (auto * condBr = dynamic_cast<CondBranchInst *>(inst)) {
                    LatticeValue cond = getValueState(condBr->getCondition());
                    if (cond.isConstant()) {
                        // 先裁剪控制流，再交给后续 DCE 删除死块
                        changed |= rewriteCondBranch(condBr, cond.constant != 0);
                    }
                    continue;
                }

                if (!inst->hasResultValue()) {
                    continue;
                }

                LatticeValue state = getValueState(inst);
                if (!state.isConstant()) {
                    continue;
                }

                ConstInt * replacement = materializeConstant(inst->getType(), state.constant);
                if (!replacement) {
                    continue;
                }

                // 指令结果一旦是常量，就直接把 SSA 使用替换掉
                inst->replaceAllUseWith(replacement);
                markInstDead(inst);
                changed = true;
            }
        }

        return changed;
    }

    /// @brief 当前正在求解的函数
    Function * func = nullptr;

    /// @brief 当前模块，用于创建常量对象
    Module * mod = nullptr;

    /// @brief SSA 值到 lattice 状态的映射
    std::unordered_map<Value *, LatticeValue> valueState;

    /// @brief 已经被证明可执行的基本块集合
    std::unordered_set<BasicBlock *> executableBlocks;

    /// @brief 已经被证明可执行的 CFG 边集合
    std::unordered_set<CFGEdge, CFGEdgeHash> executableEdges;

    /// @brief 等待处理的 CFG 边队列
    std::deque<CFGEdge> edgeWorklist;

    /// @brief 已经进入流工作队列但尚未处理的边集合
    std::unordered_set<CFGEdge, CFGEdgeHash> queuedEdges;

    /// @brief 等待重新求值的指令队列
    std::deque<Instruction *> instWorklist;

    /// @brief 已经进入值工作队列但尚未处理的指令集合
    std::unordered_set<Instruction *> queuedInsts;
};

} // namespace

/// @brief 构造常量传播 pass
/// @param _func 待优化的函数
/// @param _mod 所属模块
ConstProp::ConstProp(Function * _func, Module * _mod) : func(_func), mod(_mod)
{}

/// @brief 执行当前函数上的 SCCP
/// @return 若本轮优化修改了 IR 则返回 true
bool ConstProp::run()
{
    if (!func || !mod || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    SCCPSolver solver(func, mod);
    return solver.run();
}