///
/// @file PureCallCSE.cpp
/// @brief 轻量纯函数调用公共子表达式消除 pass 实现。
///
/// 只在单个基本块内复用“同 callee + 同实参值”的纯调用结果。
/// 对 load 实参额外带上当前块内的内存版本号，使无 store 间隔的重复 load
/// 可以匹配，同时避免跨 store 误把旧内存值和新内存值当作同一个实参。
///

#include "PureCallCSE.h"

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "CallInst.h"
#include "ConstFloat.h"
#include "ConstInteger.h"
#include "DominatorTree.h"
#include "FCmpInst.h"
#include "FormalParam.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "GlobalVariable.h"
#include "ICmpInst.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "Module.h"
#include "FPToSIInst.h"
#include "SIToFPInst.h"
#include "StoreInst.h"
#include "Type.h"
#include "Value.h"
#include "ZExtInst.h"

namespace {

/// @brief 函数纯度分析状态：未知/正在访问/纯/非纯
enum class PurityState {
    Unknown,
    Visiting,
    Pure,
    Impure,
};

/// @brief 将指针地址转为字符串键，用于构建调用签名
std::string ptrKey(const void * ptr)
{
    std::ostringstream os;
    os << reinterpret_cast<std::uintptr_t>(ptr);
    return os.str();
}

/// @brief 判断全局变量是否在整个模块中未被 store（即只读）
/// @param mod 所属模块
/// @param global 待检查的全局变量
/// @return true 表示该全局变量是只读的
bool isReadOnlyGlobal(Module * mod, GlobalVariable * global)
{
    if (!mod || !global) {
        return false;
    }

    for (auto * function : mod->getFunctionList()) {
        if (!function || function->isBuiltin()) {
            continue;
        }

        for (auto * bb : function->getBlocks()) {
            for (auto * inst : bb->getInstructions()) {
                auto * store = dynamic_cast<StoreInst *>(inst);
                if (store && store->getPointerOperand() == global) {
                    return false;
                }
            }
        }
    }

    return true;
}

/// @brief 沿 GEP 链回溯找到指针的根对象（alloca/全局变量/形参等）
Value * getPointerRoot(Value * value)
{
    Value * current = value;
    std::unordered_set<Value *> visited;
    while (current && visited.insert(current).second) {
        auto * gep = dynamic_cast<GetElementPtrInst *>(current);
        if (!gep) {
            break;
        }
        current = gep->getBasePointer();
    }
    return current;
}

/// @brief 判断指针是否指向局部内存（alloca 分配的栈对象）
bool isLocalMemory(Value * ptr)
{
    return dynamic_cast<AllocaInst *>(getPointerRoot(ptr)) != nullptr;
}

/// @brief 判断指针是否为允许纯函数读取的地址（局部内存、形参、只读全局变量）
bool isAllowedReadPointer(Module * mod, Value * ptr)
{
    Value * root = getPointerRoot(ptr);
    if (dynamic_cast<AllocaInst *>(root) != nullptr || dynamic_cast<FormalParam *>(root) != nullptr) {
        return true;
    }

    auto * global = dynamic_cast<GlobalVariable *>(root);
    return global != nullptr && isReadOnlyGlobal(mod, global);
}

/// @brief 函数纯度分析器：递归判断函数是否为纯函数（无副作用）
///
/// 纯函数的条件：
///   - 只包含允许的指令（无副作用指令、只读 load、局部 store、调用纯函数）
///   - 不存在递归调用环（Visiting 状态检测）
class PurityAnalyzer {
public:
    explicit PurityAnalyzer(Module * module) : mod(module)
    {}

    /// @brief 判断函数是否为纯函数
    /// @param function 待分析的函数
    /// @return true 表示该函数是纯函数
    bool isPure(Function * function)
    {
        if (!function || function->isBuiltin() || function->getBlocks().empty()) {
            return false;
        }

        auto it = states.find(function);
        if (it != states.end()) {
            return it->second == PurityState::Pure;
        }

        states[function] = PurityState::Visiting;
        bool pure = true;

        for (auto * bb : function->getBlocks()) {
            for (auto * inst : bb->getInstructions()) {
                if (!isInstructionAllowed(inst)) {
                    pure = false;
                    break;
                }
            }
            if (!pure) {
                break;
            }
        }

        states[function] = pure ? PurityState::Pure : PurityState::Impure;
        return pure;
    }

    /// @brief 判断纯函数是否不读取调用者可见内存。
    ///
    /// 允许函数内部的 alloca/load/store 作为局部临时变量；若 load 的根对象
    /// 是形参或全局变量，则调用结果可能随内存状态变化，不能跨基本块复用。
    bool isMemoryIndependent(Function * function)
    {
        if (!isPure(function)) {
            return false;
        }

        auto cached = memoryIndependent.find(function);
        if (cached != memoryIndependent.end()) {
            return cached->second;
        }

        bool independent = true;
        for (auto * bb : function->getBlocks()) {
            for (auto * inst : bb->getInstructions()) {
                auto * load = dynamic_cast<LoadInst *>(inst);
                if (!load) {
                    continue;
                }

                if (dynamic_cast<AllocaInst *>(getPointerRoot(load->getPointerOperand())) == nullptr) {
                    independent = false;
                    break;
                }
            }

            for (auto * inst : bb->getInstructions()) {
                auto * call = dynamic_cast<CallInst *>(inst);
                if (!call) {
                    continue;
                }

                if (!isMemoryIndependent(call->getCallee())) {
                    independent = false;
                    break;
                }
            }
            if (!independent) {
                break;
            }
        }

        memoryIndependent[function] = independent;
        return independent;
    }

private:
    /// @brief 判断指令是否为纯函数内允许的指令
    bool isInstructionAllowed(Instruction * inst)
    {
        if (!inst || inst->isDead()) {
            return true;
        }

        if (inst->isTerminator()) {
            return true;
        }

        if (dynamic_cast<AllocaInst *>(inst) != nullptr) {
            return true;
        }

        if (auto * load = dynamic_cast<LoadInst *>(inst)) {
            return isAllowedReadPointer(mod, load->getPointerOperand());
        }

        if (auto * store = dynamic_cast<StoreInst *>(inst)) {
            return isLocalMemory(store->getPointerOperand());
        }

        if (auto * call = dynamic_cast<CallInst *>(inst)) {
            Function * callee = call->getCallee();
            auto it = states.find(callee);
            if (it != states.end() && it->second == PurityState::Visiting) {
                return false;
            }
            return isPure(callee);
        }

        return !inst->mayHaveSideEffects();
    }

    Module * mod = nullptr;
    std::unordered_map<Function *, PurityState> states;
    std::unordered_map<Function *, bool> memoryIndependent;
};

/// @brief 纯调用的签名键：callee + 各实参的规范化字符串表示
struct CallKey {
    Function * callee = nullptr;
    std::vector<std::string> args;

    bool operator==(const CallKey & other) const
    {
        return callee == other.callee && args == other.args;
    }
};

/// @brief CallKey 的哈希函数，组合 callee 指针和各实参字符串的哈希
struct CallKeyHash {
    std::size_t operator()(const CallKey & key) const
    {
        std::size_t result = std::hash<Function *>{}(key.callee);
        for (const auto & arg : key.args) {
            result ^= std::hash<std::string>{}(arg) + 0x9e3779b9U + (result << 6U) + (result >> 2U);
        }
        return result;
    }
};

/// @brief 基本块内纯调用 CSE 执行器
///
/// 在单个基本块内扫描纯函数调用，若发现"同 callee + 同实参值"的重复调用，
/// 则将后续调用替换为首次调用的结果。对 load 实参附带内存版本号，
/// 确保跨 store 时不会误匹配。
class BlockCSE {
public:
    explicit BlockCSE(PurityAnalyzer & analyzer) : purity(analyzer)
    {}

    /// @brief 对单个基本块执行纯调用 CSE
    /// @param bb 目标基本块
    /// @return true 表示至少消除了一个重复调用
    bool run(BasicBlock * bb)
    {
        if (!bb) {
            return false;
        }

        bool changed = false;
        availableCalls.clear();
        availableExprs.clear();
        availableLoads.clear();
        loadKeys.clear();
        memoryVersion = 0;

        for (auto * inst : bb->getInstructions()) {
            if (!inst || inst->isDead()) {
                continue;
            }

            if (auto * load = dynamic_cast<LoadInst *>(inst)) {
                std::string key = "load@" + std::to_string(memoryVersion) + "(" + memoryKey(load->getPointerOperand()) + ")";
                loadKeys[load] = key;
                auto it = availableLoads.find(key);
                if (it != availableLoads.end() && it->second && !it->second->isDead()) {
                    load->replaceAllUseWith(it->second);
                    load->clearOperands();
                    load->setDead(true);
                    changed = true;
                    continue;
                }
                availableLoads.emplace(std::move(key), load);
                continue;
            }

            if (auto exprKey = makeExpressionKey(inst); !exprKey.empty()) {
                auto it = availableExprs.find(exprKey);
                if (it != availableExprs.end() && it->second && !it->second->isDead()) {
                    inst->replaceAllUseWith(it->second);
                    inst->clearOperands();
                    inst->setDead(true);
                    changed = true;
                    continue;
                }
                availableExprs.emplace(std::move(exprKey), inst);
                continue;
            }

            if (dynamic_cast<StoreInst *>(inst) != nullptr) {
                invalidateMemory();
                continue;
            }

            auto * call = dynamic_cast<CallInst *>(inst);
            if (!call) {
                continue;
            }

            if (!purity.isPure(call->getCallee()) || !call->hasResultValue()) {
                invalidateMemory();
                continue;
            }

            CallKey key = makeCallKey(call);
            auto it = availableCalls.find(key);
            if (it != availableCalls.end() && it->second && !it->second->isDead()) {
                call->replaceAllUseWith(it->second);
                call->clearOperands();
                call->setDead(true);
                changed = true;
                continue;
            }

            availableCalls.emplace(std::move(key), call);
        }

        return changed;
    }

private:
    /// @brief 遇到 store 或非纯调用时使内存版本号递增，并清空可用调用缓存
    void invalidateMemory()
    {
        ++memoryVersion;
        availableCalls.clear();
        availableExprs.clear();
        availableLoads.clear();
        loadKeys.clear();
    }

    /// @brief 将值规范化为字符串键，用于比较两个实参是否相同
    std::string valueKey(Value * value)
    {
        if (!value) {
            return "null";
        }

        if (auto * constant = dynamic_cast<ConstInteger *>(value)) {
            return "ci:" + ptrKey(constant->getType()) + ":" + std::to_string(constant->getVal());
        }

        if (auto * constant = dynamic_cast<ConstFloat *>(value)) {
            return "cf:" + std::to_string(constant->getBitPattern());
        }

        if (auto * load = dynamic_cast<LoadInst *>(value)) {
            auto it = loadKeys.find(load);
            if (it != loadKeys.end()) {
                return it->second;
            }
        }

        if (auto * gep = dynamic_cast<GetElementPtrInst *>(value)) {
            return memoryKey(gep);
        }

        return "v:" + ptrKey(value);
    }

    /// @brief 将内存指针规范化为字符串键，递归展开 GEP
    std::string memoryKey(Value * ptr)
    {
        if (!ptr) {
            return "mem:null";
        }

        if (auto * gep = dynamic_cast<GetElementPtrInst *>(ptr)) {
            return "gep(" + memoryKey(gep->getBasePointer()) + "," + valueKey(gep->getIndexOperand()) + "," +
                   (gep->isArrayDecayGEP() ? "decay" : "elem") + "," + ptrKey(gep->getType()) + ")";
        }

        return "mem:" + ptrKey(ptr);
    }

    /// @brief 构造调用的签名键：callee + 各实参的规范化字符串
    CallKey makeCallKey(CallInst * call)
    {
        CallKey key;
        key.callee = call->getCallee();
        key.args.reserve(static_cast<std::size_t>(call->getArgCount()));
        for (int32_t i = 0; i < call->getArgCount(); ++i) {
            key.args.push_back(valueKey(call->getArg(i)));
        }
        return key;
    }

    std::string makeExpressionKey(Instruction * inst)
    {
        if (!inst || !inst->hasResultValue() || dynamic_cast<CallInst *>(inst) || dynamic_cast<LoadInst *>(inst)) {
            return "";
        }

        if (auto * gep = dynamic_cast<GetElementPtrInst *>(inst)) {
            return "expr:" + memoryKey(gep);
        }

        if (dynamic_cast<BinaryInst *>(inst) || dynamic_cast<ICmpInst *>(inst) || dynamic_cast<FCmpInst *>(inst) ||
            dynamic_cast<ZExtInst *>(inst) || dynamic_cast<SIToFPInst *>(inst) || dynamic_cast<FPToSIInst *>(inst)) {
            std::vector<std::string> operands;
            operands.reserve(static_cast<std::size_t>(inst->getOperandsNum()));
            for (Value * operand : inst->getOperandsValue()) {
                operands.push_back(valueKey(operand));
            }

            if ((inst->getOp() == IRInstOperator::IRINST_OP_ADD_I ||
                 inst->getOp() == IRInstOperator::IRINST_OP_MUL_I ||
                 inst->getOp() == IRInstOperator::IRINST_OP_EQ_I ||
                 inst->getOp() == IRInstOperator::IRINST_OP_NE_I) &&
                operands.size() == 2 && operands[1] < operands[0]) {
                std::swap(operands[0], operands[1]);
            }

            std::string key = "expr:op:" + std::to_string(static_cast<int>(inst->getOp())) + ":" +
                              ptrKey(inst->getType());
            for (const auto & operand : operands) {
                key += ":" + operand;
            }
            return key;
        }

        return "";
    }

    PurityAnalyzer & purity;
    std::unordered_map<CallKey, CallInst *, CallKeyHash> availableCalls;
    std::unordered_map<std::string, Instruction *> availableExprs;
    std::unordered_map<std::string, LoadInst *> availableLoads;
    std::unordered_map<LoadInst *, std::string> loadKeys;
    int32_t memoryVersion = 0;
};

/// @brief 跨基本块的纯调用 CSE。
///
/// 仅处理不读取调用者可见内存的纯函数，且要求已有调用支配当前调用点。
/// 这覆盖 min/max/getNumPos/exp 这类纯计算函数，同时避免跨 store 复用
/// 可能读取数组/全局内存的调用结果。
class DominatingCallCSE {
public:
    DominatingCallCSE(Function * function, PurityAnalyzer & analyzer)
        : func(function), purity(analyzer), domTree(function)
    {}

    bool run()
    {
        if (!func) {
            return false;
        }

        bool changed = false;
        std::vector<CallInst *> seenCalls;

        for (auto * bb : func->getBlocks()) {
            for (auto * inst : bb->getInstructions()) {
                auto * call = dynamic_cast<CallInst *>(inst);
                if (!call || call->isDead() || !call->hasResultValue()) {
                    continue;
                }
                if (!purity.isMemoryIndependent(call->getCallee())) {
                    continue;
                }

                CallKey key = makeStableCallKey(call);
                CallInst * replacement = nullptr;
                for (auto * candidate : seenCalls) {
                    if (!candidate || candidate->isDead() || !sameStableCallKey(key, candidate)) {
                        continue;
                    }
                    if (dominates(candidate, call)) {
                        replacement = candidate;
                        break;
                    }
                }

                if (replacement != nullptr) {
                    call->replaceAllUseWith(replacement);
                    call->clearOperands();
                    call->setDead(true);
                    changed = true;
                    continue;
                }

                seenCalls.push_back(call);
            }
        }

        return changed;
    }

private:
    std::string stableValueKey(Value * value) const
    {
        if (!value) {
            return "null";
        }
        if (auto * constant = dynamic_cast<ConstInteger *>(value)) {
            return "ci:" + ptrKey(constant->getType()) + ":" + std::to_string(constant->getVal());
        }
        if (auto * constant = dynamic_cast<ConstFloat *>(value)) {
            return "cf:" + std::to_string(constant->getBitPattern());
        }
        return "v:" + ptrKey(value);
    }

    CallKey makeStableCallKey(CallInst * call) const
    {
        CallKey key;
        key.callee = call->getCallee();
        key.args.reserve(static_cast<std::size_t>(call->getArgCount()));
        for (int32_t i = 0; i < call->getArgCount(); ++i) {
            key.args.push_back(stableValueKey(call->getArg(i)));
        }
        return key;
    }

    bool sameStableCallKey(const CallKey & key, CallInst * call) const
    {
        return key == makeStableCallKey(call);
    }

    bool dominates(Instruction * def, Instruction * use) const
    {
        if (!def || !use || !def->getParentBlock() || !use->getParentBlock()) {
            return false;
        }
        if (def->getParentBlock() != use->getParentBlock()) {
            return domTree.dominates(def->getParentBlock(), use->getParentBlock());
        }

        for (auto * inst : def->getParentBlock()->getInstructions()) {
            if (inst == def) {
                return true;
            }
            if (inst == use) {
                return false;
            }
        }
        return false;
    }

    Function * func = nullptr;
    PurityAnalyzer & purity;
    DominatorTree domTree;
};

/// @brief 从函数中清扫已标记为 dead 的指令
/// @param func 待清扫函数
/// @return 被移除的指令数量
int32_t sweepDeadInstructions(Function * func)
{
    int32_t removed = 0;
    if (!func) {
        return removed;
    }

    for (auto * bb : func->getBlocks()) {
        auto & insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end();) {
            Instruction * inst = *it;
            if (inst && inst->isDead()) {
                it = insts.erase(it);
                delete inst;
                ++removed;
                continue;
            }
            ++it;
        }
    }

    return removed;
}

} // namespace

/// @brief 构造纯调用 CSE pass
/// @param _func 待优化的函数
/// @param _mod 所属模块
PureCallCSE::PureCallCSE(Function * _func, Module * _mod) : func(_func), mod(_mod)
{}

/// @brief 执行纯调用公共子表达式消除
/// @return 若删除了至少一个重复调用则返回 true
bool PureCallCSE::run()
{
    if (!func || !mod || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    PurityAnalyzer purity(mod);
    BlockCSE blockCSE(purity);
    bool changed = false;

    for (auto * bb : func->getBlocks()) {
        changed = blockCSE.run(bb) || changed;
    }

    DominatingCallCSE dominatingCallCSE(func, purity);
    changed = dominatingCallCSE.run() || changed;

    if (changed) {
        sweepDeadInstructions(func);
    }

    return changed;
}
