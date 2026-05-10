///
/// @file GVN.cpp
/// @brief 基于支配树的全局值编号 pass 实现
///

#include "GVN.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
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
#include "MemoryLocation.h"
#include "Module.h"
#include "SIToFPInst.h"
#include "FPToSIInst.h"
#include "StoreInst.h"
#include "Type.h"
#include "Value.h"
#include "ZExtInst.h"

namespace {

std::string ptrKey(const void * ptr)
{
    std::ostringstream os;
    os << reinterpret_cast<std::uintptr_t>(ptr);
    return os.str();
}

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
                if (store && getPointerRoot(store->getPointerOperand()) == global) {
                    return false;
                }
            }
        }
    }

    return true;
}

bool isLocalMemory(Value * ptr)
{
    return dynamic_cast<AllocaInst *>(getPointerRoot(ptr)) != nullptr;
}

bool isAllowedReadPointer(Module * mod, Value * ptr)
{
    Value * root = getPointerRoot(ptr);
    if (dynamic_cast<AllocaInst *>(root) != nullptr || dynamic_cast<FormalParam *>(root) != nullptr) {
        return true;
    }

    auto * global = dynamic_cast<GlobalVariable *>(root);
    return global != nullptr && isReadOnlyGlobal(mod, global);
}

enum class PurityState {
    Unknown,
    Visiting,
    Pure,
    Impure,
};

class PurityAnalyzer {
public:
    explicit PurityAnalyzer(Module * module) : mod(module)
    {}

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
                if (auto * load = dynamic_cast<LoadInst *>(inst)) {
                    if (dynamic_cast<AllocaInst *>(getPointerRoot(load->getPointerOperand())) == nullptr) {
                        independent = false;
                        break;
                    }
                    continue;
                }

                if (auto * call = dynamic_cast<CallInst *>(inst)) {
                    if (!isMemoryIndependent(call->getCallee())) {
                        independent = false;
                        break;
                    }
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
    bool isInstructionAllowed(Instruction * inst)
    {
        if (!inst || inst->isDead() || inst->isTerminator()) {
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
            auto it = states.find(call->getCallee());
            if (it != states.end() && it->second == PurityState::Visiting) {
                return false;
            }
            return isPure(call->getCallee());
        }

        return !inst->mayHaveSideEffects();
    }

    Module * mod = nullptr;
    std::unordered_map<Function *, PurityState> states;
    std::unordered_map<Function *, bool> memoryIndependent;
};

struct MemoryState {
    int32_t globalVersion = 0;
    std::unordered_map<std::string, int32_t> versions;
};

bool sameMemoryState(const MemoryState & lhs, const MemoryState & rhs)
{
    if (lhs.globalVersion != rhs.globalVersion || lhs.versions.size() != rhs.versions.size()) {
        return false;
    }

    for (const auto & [key, version] : lhs.versions) {
        auto it = rhs.versions.find(key);
        if (it == rhs.versions.end() || it->second != version) {
            return false;
        }
    }

    return true;
}

int32_t getVersion(const MemoryState & state, const std::string & key)
{
    auto it = state.versions.find(key);
    return it == state.versions.end() ? 0 : it->second;
}

void setVersion(MemoryState & state, const std::string & key, int32_t version)
{
    if (version == 0) {
        state.versions.erase(key);
        return;
    }

    state.versions[key] = version;
}

std::string localObjectKey(AllocaInst * object)
{
    return "obj:" + ptrKey(object);
}

std::string localPreciseKey(const MemoryLocation & location)
{
    std::string key = "loc:" + ptrKey(location.object);
    for (int32_t index : location.indices) {
        key += ":" + std::to_string(index);
    }
    return key;
}

bool isTrackableLocation(const MemoryLocation & location, const std::unordered_set<AllocaInst *> & trackableAllocas)
{
    return location.object != nullptr && trackableAllocas.find(location.object) != trackableAllocas.end();
}

class MemoryVersionAnalysis {
public:
    MemoryVersionAnalysis(Function * function,
                          PurityAnalyzer & analyzer,
                          std::unordered_set<AllocaInst *> trackable)
        : func(function), purity(analyzer), trackableAllocas(std::move(trackable))
    {
        assignClobberIds();
        solve();
    }

    const MemoryState & getInState(BasicBlock * bb) const
    {
        auto it = inStates.find(bb);
        return it == inStates.end() ? emptyState : it->second;
    }

    void applyInstructionClobber(Instruction * inst, MemoryState & state) const
    {
        if (!inst || inst->isDead()) {
            return;
        }

        auto idIt = clobberIds.find(inst);
        if (idIt == clobberIds.end()) {
            return;
        }

        if (auto * store = dynamic_cast<StoreInst *>(inst)) {
            applyStoreClobber(store, state, idIt->second);
            return;
        }

        auto * call = dynamic_cast<CallInst *>(inst);
        if (call && !purity.isPure(call->getCallee())) {
            state.globalVersion = idIt->second;
        }
    }

    std::string readVersionKey(Value * pointer, const MemoryState & state) const
    {
        MemoryLocation location = normalizeMemoryLocation(pointer);
        if (isTrackableLocation(location, trackableAllocas)) {
            const std::string objectKey = localObjectKey(location.object);
            if (!location.isPrecise()) {
                return "local:" + std::to_string(getVersion(state, objectKey));
            }

            const std::string preciseKey = localPreciseKey(location);
            return "local:" + std::to_string(getVersion(state, preciseKey)) + ":" +
                   std::to_string(getVersion(state, objectKey));
        }

        return "global:" + std::to_string(state.globalVersion);
    }

private:
    void assignClobberIds()
    {
        int32_t nextId = 1;
        for (auto * bb : func->getBlocks()) {
            for (auto * inst : bb->getInstructions()) {
                if (dynamic_cast<StoreInst *>(inst) != nullptr) {
                    clobberIds[inst] = nextId++;
                    continue;
                }

                auto * call = dynamic_cast<CallInst *>(inst);
                if (call && !purity.isPure(call->getCallee())) {
                    clobberIds[inst] = nextId++;
                }
            }
        }
    }

    void solve()
    {
        DominatorTree dt(func);
        const auto & rpo = dt.getRPO();
        for (auto * bb : rpo) {
            reachableBlocks.insert(bb);
        }

        bool changed = false;
        do {
            changed = false;
            for (auto * bb : rpo) {
                MemoryState in = meetAtBlock(bb);
                MemoryState out = transferBlock(bb, in);

                auto inIt = inStates.find(bb);
                if (inIt == inStates.end() || !sameMemoryState(inIt->second, in)) {
                    inStates[bb] = in;
                    changed = true;
                }

                auto outIt = outStates.find(bb);
                if (outIt == outStates.end() || !sameMemoryState(outIt->second, out)) {
                    outStates[bb] = out;
                    changed = true;
                }
            }
        } while (changed);
    }

    MemoryState meetAtBlock(BasicBlock * bb)
    {
        MemoryState result;
        bool hasPred = false;

        for (auto * pred : bb->getPredecessors()) {
            if (reachableBlocks.find(pred) == reachableBlocks.end()) {
                continue;
            }

            auto predIt = outStates.find(pred);
            const MemoryState & predState = predIt == outStates.end() ? emptyState : predIt->second;
            if (!hasPred) {
                result = predState;
                hasPred = true;
                continue;
            }

            result.globalVersion = meetVersion(result.globalVersion, predState.globalVersion, bb, "global");

            std::unordered_set<std::string> keys;
            keys.reserve(result.versions.size() + predState.versions.size());
            for (const auto & [key, _] : result.versions) {
                keys.insert(key);
            }
            for (const auto & [key, _] : predState.versions) {
                keys.insert(key);
            }

            for (const auto & key : keys) {
                const int32_t merged = meetVersion(getVersion(result, key), getVersion(predState, key), bb, key);
                setVersion(result, key, merged);
            }
        }

        return result;
    }

    int32_t meetVersion(int32_t lhs, int32_t rhs, BasicBlock * bb, const std::string & key)
    {
        if (lhs == rhs) {
            return lhs;
        }

        const std::string unknownKey = ptrKey(bb) + "|" + key;
        auto it = unknownVersions.find(unknownKey);
        if (it != unknownVersions.end()) {
            return it->second;
        }

        const int32_t version = nextUnknownVersion--;
        unknownVersions[unknownKey] = version;
        return version;
    }

    MemoryState transferBlock(BasicBlock * bb, MemoryState state) const
    {
        for (auto * inst : bb->getInstructions()) {
            applyInstructionClobber(inst, state);
        }
        return state;
    }

    void applyStoreClobber(StoreInst * store, MemoryState & state, int32_t version) const
    {
        MemoryLocation location = normalizeMemoryLocation(store->getPointerOperand());
        if (isTrackableLocation(location, trackableAllocas)) {
            if (location.isPrecise()) {
                setVersion(state, localPreciseKey(location), version);
            } else {
                setVersion(state, localObjectKey(location.object), version);
            }
            return;
        }

        state.globalVersion = version;
    }

    Function * func = nullptr;
    PurityAnalyzer & purity;
    std::unordered_set<AllocaInst *> trackableAllocas;
    std::unordered_map<Instruction *, int32_t> clobberIds;
    std::unordered_set<BasicBlock *> reachableBlocks;
    std::unordered_map<BasicBlock *, MemoryState> inStates;
    std::unordered_map<BasicBlock *, MemoryState> outStates;
    std::unordered_map<std::string, int32_t> unknownVersions;
    int32_t nextUnknownVersion = -1;
    MemoryState emptyState;
};

class ScopedValueTable {
public:
    Value * lookup(const std::string & key) const
    {
        auto it = table.find(key);
        if (it == table.end()) {
            return nullptr;
        }

        if (auto * inst = dynamic_cast<Instruction *>(it->second)) {
            if (inst->isDead()) {
                return nullptr;
            }
        }

        return it->second;
    }

    std::size_t mark() const
    {
        return changes.size();
    }

    void insert(const std::string & key, Value * value)
    {
        auto it = table.find(key);
        if (it == table.end()) {
            changes.push_back({key, nullptr, false});
        } else {
            changes.push_back({key, it->second, true});
        }
        table[key] = value;
    }

    void restore(std::size_t mark)
    {
        while (changes.size() > mark) {
            const Change change = changes.back();
            changes.pop_back();
            if (change.hadOldValue) {
                table[change.key] = change.oldValue;
            } else {
                table.erase(change.key);
            }
        }
    }

private:
    struct Change {
        std::string key;
        Value * oldValue = nullptr;
        bool hadOldValue = false;
    };

    std::unordered_map<std::string, Value *> table;
    std::vector<Change> changes;
};

bool isCommutativeOp(IRInstOperator op)
{
    switch (op) {
        case IRInstOperator::IRINST_OP_ADD_I:
        case IRInstOperator::IRINST_OP_MUL_I:
        case IRInstOperator::IRINST_OP_EQ_I:
        case IRInstOperator::IRINST_OP_NE_I:
        case IRInstOperator::IRINST_OP_EQ_F:
        case IRInstOperator::IRINST_OP_NE_F:
            return true;

        default:
            return false;
    }
}

bool isValueInstruction(Instruction * inst)
{
    return dynamic_cast<BinaryInst *>(inst) != nullptr ||
           dynamic_cast<ICmpInst *>(inst) != nullptr ||
           dynamic_cast<FCmpInst *>(inst) != nullptr ||
           dynamic_cast<ZExtInst *>(inst) != nullptr ||
           dynamic_cast<SIToFPInst *>(inst) != nullptr ||
           dynamic_cast<FPToSIInst *>(inst) != nullptr;
}

class DominatorGVN {
public:
    DominatorGVN(Function * function, Module * module)
        : func(function), mod(module), domTree(function), purity(module),
          memory(function, purity, collectTrackableAllocas(function))
    {}

    bool run()
    {
        if (!func || !mod || domTree.getRPO().empty()) {
            return false;
        }

        visit(func->getEntryBlock());
        return sweepDeadInstructions();
    }

private:
    static std::unordered_set<AllocaInst *> collectTrackableAllocas(Function * function)
    {
        std::unordered_set<AllocaInst *> result;
        if (!function) {
            return result;
        }

        for (auto * bb : function->getBlocks()) {
            for (auto * inst : bb->getInstructions()) {
                auto * alloca = dynamic_cast<AllocaInst *>(inst);
                if (alloca && !doesPointerEscape(alloca)) {
                    result.insert(alloca);
                }
            }
        }

        return result;
    }

    void visit(BasicBlock * bb)
    {
        if (!bb || !visited.insert(bb).second) {
            return;
        }

        const std::size_t tableMark = valueTable.mark();
        MemoryState state = memory.getInState(bb);

        for (auto * inst : bb->getInstructions()) {
            if (!inst || inst->isDead()) {
                continue;
            }

            const std::string key = makeInstructionKey(inst, state);
            if (!key.empty()) {
                if (Value * replacement = valueTable.lookup(key)) {
                    replaceInstruction(inst, replacement);
                    continue;
                }

                valueTable.insert(key, inst);
            }

            memory.applyInstructionClobber(inst, state);
        }

        for (auto * child : domTree.getDomChildren(bb)) {
            visit(child);
        }

        valueTable.restore(tableMark);
    }

    std::string valueKey(Value * value) const
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

        if (auto * gep = dynamic_cast<GetElementPtrInst *>(value)) {
            return pointerExpressionKey(gep);
        }

        return "v:" + ptrKey(value);
    }

    std::string pointerExpressionKey(Value * pointer) const
    {
        if (!pointer) {
            return "ptr:null";
        }

        if (auto * gep = dynamic_cast<GetElementPtrInst *>(pointer)) {
            return "gep:" + pointerExpressionKey(gep->getBasePointer()) + ":" +
                   valueKey(gep->getIndexOperand()) + ":" + ptrKey(gep->getType()) + ":" +
                   (gep->isArrayDecayGEP() ? "decay" : "elem");
        }

        return valueKey(pointer);
    }

    std::string makeInstructionKey(Instruction * inst, const MemoryState & state)
    {
        if (!inst || !inst->hasResultValue()) {
            return "";
        }

        if (auto * load = dynamic_cast<LoadInst *>(inst)) {
            return "load:" + ptrKey(load->getType()) + ":" + pointerExpressionKey(load->getPointerOperand()) +
                   ":" + memory.readVersionKey(load->getPointerOperand(), state);
        }

        if (auto * gep = dynamic_cast<GetElementPtrInst *>(inst)) {
            return pointerExpressionKey(gep);
        }

        if (auto * call = dynamic_cast<CallInst *>(inst)) {
            if (!purity.isPure(call->getCallee())) {
                return "";
            }

            std::string key = "call:" + ptrKey(call->getCallee()) + ":" + ptrKey(call->getType());
            for (int32_t i = 0; i < call->getArgCount(); ++i) {
                key += ":" + valueKey(call->getArg(i));
            }
            if (!purity.isMemoryIndependent(call->getCallee())) {
                key += ":mem:" + std::to_string(state.globalVersion);
            }
            return key;
        }

        if (!isValueInstruction(inst)) {
            return "";
        }

        std::vector<std::string> operands;
        operands.reserve(static_cast<std::size_t>(inst->getOperandsNum()));
        for (auto * operand : inst->getOperandsValue()) {
            operands.push_back(valueKey(operand));
        }

        if (isCommutativeOp(inst->getOp()) && operands.size() == 2 && operands[1] < operands[0]) {
            std::swap(operands[0], operands[1]);
        }

        std::string key = "expr:" + std::to_string(static_cast<int32_t>(inst->getOp())) + ":" +
                          ptrKey(inst->getType());
        for (const auto & operand : operands) {
            key += ":" + operand;
        }
        return key;
    }

    void replaceInstruction(Instruction * inst, Value * replacement)
    {
        if (!inst || !replacement || replacement == inst) {
            return;
        }

        inst->replaceAllUseWith(replacement);
        inst->clearOperands();
        inst->setDead(true);
        changed = true;
    }

    bool sweepDeadInstructions()
    {
        if (!changed) {
            return false;
        }

        bool removed = false;
        for (auto * bb : func->getBlocks()) {
            auto & insts = bb->getInstructions();
            for (auto it = insts.begin(); it != insts.end();) {
                Instruction * inst = *it;
                if (!inst || !inst->isDead()) {
                    ++it;
                    continue;
                }

                inst->clearOperands();
                auto next = std::next(it);
                insts.erase(it);
                delete inst;
                it = next;
                removed = true;
            }
        }

        return removed;
    }

    Function * func = nullptr;
    Module * mod = nullptr;
    DominatorTree domTree;
    PurityAnalyzer purity;
    MemoryVersionAnalysis memory;
    ScopedValueTable valueTable;
    std::unordered_set<BasicBlock *> visited;
    bool changed = false;
};

} // namespace

GVN::GVN(Function * _func, Module * _mod) : func(_func), mod(_mod)
{}

bool GVN::run()
{
    if (!func || !mod || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    DominatorGVN executor(func, mod);
    return executor.run();
}
