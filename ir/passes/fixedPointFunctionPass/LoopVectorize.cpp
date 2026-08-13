///
/// @file LoopVectorize.cpp
/// @brief 保守的 RVV 循环向量化 pass 实现
///

#include "LoopVectorize.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "ConstFloat.h"
#include "ConstInteger.h"
#include "DominatorTree.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "GlobalVariable.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "LoopInfo.h"
#include "Module.h"
#include "PhiInst.h"
#include "ScalarEvolution.h"
#include "StoreInst.h"
#include "Type.h"
#include "Use.h"
#include "User.h"
#include "Value.h"
#include "VectorInst.h"
#include "AnalysisCache.h"
#include "CostModel.h"
#include "Types/VectorType.h"

namespace {

struct PointerPhiInfo {
    // 记录形如 p.next = gep p, step 的指针递推，向量化后用 VL 缩放更新。
    PhiInst * phi = nullptr;
    Value * root = nullptr;
    int32_t step = 1;
};

struct AccessInfo {
    // 将 load/store 地址规约为 root + offset + phi-step，便于做保守别名判断。
    Value * pointer = nullptr;
    Value * root = nullptr;
    PhiInst * basePhi = nullptr;
    int32_t offset = 0;
    int32_t stride = 1;
    Type * elemType = nullptr;
};

struct ReductionInfo {
    // 仅匹配 acc = phi(init, acc + term) 这类单归约变量。
    PhiInst * phi = nullptr;
    BinaryInst * update = nullptr;
    Value * term = nullptr;
    IRInstOperator op = IRInstOperator::IRINST_OP_MAX;
};

// 归约采用 LLVM 常见的 vector accumulator 形态：循环内只逐 lane 累加，
// 退出后再做一次横向 reduce，避免每个 strip 都执行昂贵的 reduction。
constexpr bool kEnableReductionVectorization = true;

/// @brief RVV 1.0 在 e32,m1 下允许的架构级最大 VLMAX
constexpr int32_t kMaxE32M1VL = 2048;

bool isSupportedElementType(Type * type)
{
    return type != nullptr && (type->isInt32Type() || type->isFloatType());
}

bool isSupportedBinaryOp(IRInstOperator op)
{
    switch (op) {
    case IRInstOperator::IRINST_OP_ADD_I:
    case IRInstOperator::IRINST_OP_SUB_I:
    case IRInstOperator::IRINST_OP_MUL_I:
    case IRInstOperator::IRINST_OP_ADD_F:
    case IRInstOperator::IRINST_OP_SUB_F:
    case IRInstOperator::IRINST_OP_MUL_F:
        return true;
    default:
        return false;
    }
}

bool isSupportedReductionOp(IRInstOperator op)
{
    return op == IRInstOperator::IRINST_OP_ADD_I || op == IRInstOperator::IRINST_OP_ADD_F;
}

bool isDefinedInLoop(Value * value, const std::unordered_set<BasicBlock *> & loopBody)
{
    auto * inst = dynamic_cast<Instruction *>(value);
    return inst != nullptr && inst->getParentBlock() != nullptr &&
           loopBody.find(inst->getParentBlock()) != loopBody.end();
}

bool hasSingleBranchTo(BasicBlock * bb, BasicBlock * target)
{
    auto * branch = bb != nullptr ? dynamic_cast<BranchInst *>(bb->getTerminator()) : nullptr;
    return branch != nullptr && branch->getTarget() == target;
}

void insertBeforeTerminator(BasicBlock * bb, Instruction * inst)
{
    if (!bb || !inst) {
        return;
    }

    auto & insts = bb->getInstructions();
    auto pos = insts.end();
    if (!insts.empty() && insts.back()->isTerminator()) {
        pos = std::prev(insts.end());
    }
    inst->setParentBlock(bb);
    insts.insert(pos, inst);
}

void insertAfterPhis(BasicBlock * bb, Instruction * inst)
{
    if (!bb || !inst) {
        return;
    }

    auto & insts = bb->getInstructions();
    auto pos = insts.begin();
    while (pos != insts.end() && dynamic_cast<PhiInst *>(*pos) != nullptr) {
        ++pos;
    }

    inst->setParentBlock(bb);
    insts.insert(pos, inst);
}

void insertAfterPhis(BasicBlock * bb, const std::vector<Instruction *> & newInsts)
{
    if (!bb) {
        return;
    }

    auto & insts = bb->getInstructions();
    auto pos = insts.begin();
    while (pos != insts.end() && dynamic_cast<PhiInst *>(*pos) != nullptr) {
        ++pos;
    }

    for (auto * inst : newInsts) {
        if (!inst) {
            continue;
        }
        inst->setParentBlock(bb);
        pos = insts.insert(pos, inst);
        ++pos;
    }
}

void replaceIncomingFrom(PhiInst * phi, BasicBlock * block, Value * value)
{
    if (!phi || !block || !value) {
        return;
    }
    phi->removeIncomingBlock(block);
    phi->addIncoming(value, block);
}

bool tryConstInt(Value * value, int32_t & result)
{
    auto * constant = dynamic_cast<ConstInteger *>(value);
    if (!constant) {
        return false;
    }
    result = constant->getVal();
    return true;
}

Value * stripConstantGEP(Value * value, int32_t & offset)
{
    offset = 0;
    while (auto * gep = dynamic_cast<GetElementPtrInst *>(value)) {
        // 只剥离常量 GEP，动态下标留给上层判定为不支持，避免误算访存关系。
        int32_t delta = 0;
        if (!tryConstInt(gep->getIndexOperand(), delta)) {
            break;
        }
        const int64_t combined = static_cast<int64_t>(offset) + delta;
        if (combined < std::numeric_limits<int32_t>::min() ||
            combined > std::numeric_limits<int32_t>::max()) {
            break;
        }
        offset = static_cast<int32_t>(combined);
        value = gep->getBasePointer();
    }
    return value;
}

Value * stripToRoot(Value * value)
{
    int32_t ignored = 0;
    return stripConstantGEP(value, ignored);
}

/// @brief 不关心偏移、只为定位根对象时可剥离任意下标的 GEP
Value * stripAllGEPs(Value * value)
{
    while (auto * gep = dynamic_cast<GetElementPtrInst *>(value)) {
        value = gep->getBasePointer();
    }
    return value;
}

/// @brief 穿透 phi 解析指针的底层对象：所有非自递推入边必须收敛到同一对象。
/// 游标链通常只有几个节点，超出 kMaxUnderlyingObjectWalk 视为解析失败，
/// 避免在大函数的深层 phi 网络上做高代价遍历。
/// 解析失败返回 nullptr，调用方应回退到旧的保守 root。
constexpr std::size_t kMaxUnderlyingObjectWalk = 32;

Value * resolveUnderlyingObject(Value * value, std::unordered_set<Value *> & visiting)
{
    value = stripAllGEPs(value);
    if (!value || visiting.size() >= kMaxUnderlyingObjectWalk || !visiting.insert(value).second) {
        return nullptr;
    }

    if (auto * phi = dynamic_cast<PhiInst *>(value)) {
        Value * merged = nullptr;
        for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
            Value * incoming = stripAllGEPs(phi->getIncomingValue(i));
            if (incoming == phi) {
                // 指针递推的自身入边
                continue;
            }
            Value * resolved = resolveUnderlyingObject(incoming, visiting);
            if (!resolved) {
                return nullptr;
            }
            if (!merged) {
                merged = resolved;
            } else if (merged != resolved) {
                return nullptr;
            }
        }
        return merged;
    }

    return value;
}

Value * resolveUnderlyingObject(Value * value)
{
    std::unordered_set<Value *> visiting;
    return resolveUnderlyingObject(value, visiting);
}

Value * getPhiIncomingFrom(PhiInst * phi, BasicBlock * block)
{
    if (!phi || !block) {
        return nullptr;
    }
    for (int32_t i = 0; i < phi->getIncomingCount(); ++i) {
        if (phi->getIncomingBlock(i) == block) {
            return phi->getIncomingValue(i);
        }
    }
    return nullptr;
}

Value * makeZeroValue(Module * mod, Type * type)
{
    if (!mod || !type) {
        return nullptr;
    }
    if (type->isFloatType()) {
        return mod->newConstFloat(0.0f);
    }
    return mod->newConstInteger(type, 0);
}

bool matchPointerPhi(PhiInst * phi, BasicBlock * preheader, BasicBlock * latch, PointerPhiInfo & info)
{
    if (!phi || !phi->getType() || !phi->getType()->isPointerType()) {
        return false;
    }

    Value * init = getPhiIncomingFrom(phi, preheader);
    Value * next = getPhiIncomingFrom(phi, latch);
    auto * nextGEP = dynamic_cast<GetElementPtrInst *>(next);
    int32_t step = 0;
    // 目前只接受正向线性指针递推，负步长和数组 decay GEP 暂不转换
    // 步长上界保证 step * 架构最大 VL 仍能由 i32 GEP 索引精确表示
    if (!init || !nextGEP || nextGEP->isArrayDecayGEP() || nextGEP->getBasePointer() != phi ||
        !tryConstInt(nextGEP->getIndexOperand(), step) || step <= 0 ||
        step > std::numeric_limits<int32_t>::max() / kMaxE32M1VL) {
        return false;
    }

    info.phi = phi;
    info.root = resolveUnderlyingObject(init);
    if (!info.root) {
        info.root = stripToRoot(init);
    }
    info.step = step;
    return info.root != nullptr;
}

bool matchReductionPhi(PhiInst * phi,
                       BasicBlock * preheader,
                       BasicBlock * latch,
                       const std::unordered_set<BasicBlock *> & loopBody,
                       ReductionInfo & info)
{
    if (!phi || !isSupportedElementType(phi->getType())) {
        return false;
    }

    Value * init = getPhiIncomingFrom(phi, preheader);
    Value * updateValue = getPhiIncomingFrom(phi, latch);
    auto * update = dynamic_cast<BinaryInst *>(updateValue);
    // 归约更新必须在循环体内定义，且类型和 phi 完全一致。
    if (!init || !update || !update->getParentBlock() ||
        loopBody.find(update->getParentBlock()) == loopBody.end() ||
        !isSupportedReductionOp(update->getOp()) || update->getType() != phi->getType()) {
        return false;
    }

    Value * term = nullptr;
    if (update->getLHS() == phi) {
        term = update->getRHS();
    } else if (update->getRHS() == phi) {
        term = update->getLHS();
    } else {
        return false;
    }

    info.phi = phi;
    info.update = update;
    info.term = term;
    info.op = update->getOp();
    return true;
}

bool hasPhiUseOutsideLoop(Value * value, const std::unordered_set<BasicBlock *> & loopBody)
{
    if (!value) {
        return false;
    }

    for (auto * use : value->getUseList()) {
        auto * user = use != nullptr ? use->getUser() : nullptr;
        auto * inst = dynamic_cast<Instruction *>(user);
        if (!inst || !inst->getParentBlock()) {
            continue;
        }
        if (loopBody.find(inst->getParentBlock()) != loopBody.end()) {
            continue;
        }
        if (dynamic_cast<PhiInst *>(inst) != nullptr) {
            return true;
        }
    }
    return false;
}

void replaceUsesOutsideLoop(Value * oldValue,
                            Value * newValue,
                            const std::unordered_set<BasicBlock *> & loopBody)
{
    if (!oldValue || !newValue || oldValue == newValue) {
        return;
    }

    std::vector<Use *> uses(oldValue->getUseList().begin(), oldValue->getUseList().end());
    for (auto * use : uses) {
        auto * user = use != nullptr ? use->getUser() : nullptr;
        auto * inst = dynamic_cast<Instruction *>(user);
        if (!inst || !inst->getParentBlock() ||
            loopBody.find(inst->getParentBlock()) != loopBody.end()) {
            continue;
        }
        inst->replaceOperand(oldValue, newValue);
    }
}

bool rootsCanAlias(Value * lhs, Value * rhs)
{
    if (lhs == rhs) {
        return true;
    }

    const bool lhsStackOrGlobal = dynamic_cast<GlobalVariable *>(lhs) != nullptr ||
                                  dynamic_cast<AllocaInst *>(lhs) != nullptr;
    const bool rhsStackOrGlobal = dynamic_cast<GlobalVariable *>(rhs) != nullptr ||
                                  dynamic_cast<AllocaInst *>(rhs) != nullptr;
    // 两个明确不同的栈对象/全局对象视为不别名，其余来源保守认为可能别名。
    return !(lhsStackOrGlobal && rhsStackOrGlobal);
}

bool storesAreAliasSafe(const std::vector<AccessInfo> & loads, const std::vector<AccessInfo> & stores)
{
    // 当前不做运行期别名检查：只允许同 root 同 offset/stride 的读写或确定不同对象。
    for (std::size_t storeIndex = 0; storeIndex < stores.size(); ++storeIndex) {
        const AccessInfo & store = stores[storeIndex];
        for (std::size_t otherIndex = storeIndex + 1; otherIndex < stores.size(); ++otherIndex) {
            const AccessInfo & other = stores[otherIndex];
            if (!rootsCanAlias(store.root, other.root)) {
                continue;
            }
            return false;
        }

        for (const auto & load : loads) {
            if (!rootsCanAlias(store.root, load.root)) {
                continue;
            }
            // 同一指针 phi 同偏移同步长 => 每次迭代读写同一地址，strip 内先读后写安全；
            // 仅 root 相同不足以保证（不同游标可指向同一对象的不同位置）。
            if (store.basePhi != nullptr && store.basePhi == load.basePhi && store.offset == load.offset &&
                store.stride == load.stride) {
                continue;
            }
            return false;
        }
    }
    return true;
}

class VectorizationBuilder {
public:
    VectorizationBuilder(Function * func,
                         Module * mod,
                         BasicBlock * body,
                         Value * vl,
                         const std::unordered_set<BasicBlock *> & loopBody,
                         const std::unordered_map<PhiInst *, PointerPhiInfo> & pointerPhis)
        : func(func), mod(mod), body(body), vl(vl), loopBody(loopBody), pointerPhis(pointerPhis)
    {}

    Value * materialize(Value * value, Type * elemType)
    {
        if (!value || !isSupportedElementType(elemType)) {
            return nullptr;
        }

        auto cached = vectorValues.find(value);
        if (cached != vectorValues.end()) {
            return cached->second;
        }

        Value * result = nullptr;
        auto * inst = dynamic_cast<Instruction *>(value);
        if (!inst || loopBody.find(inst->getParentBlock()) == loopBody.end()) {
            // 循环外定义或常量作为不变量处理，广播到当前 VL。
            result = createSplat(value, elemType);
        } else if (auto * load = dynamic_cast<LoadInst *>(inst)) {
            AccessInfo access;
            if (!resolveAccess(load->getPointerOperand(), load->getType(), access)) {
                return nullptr;
            }
            auto * vectorLoad = new VectorLoadInst(func, access.pointer, vl, access.elemType, access.stride);
            insertBeforeTerminator(body, vectorLoad);
            loads.push_back(access);
            result = vectorLoad;
        } else if (auto * binary = dynamic_cast<BinaryInst *>(inst)) {
            if (!isSupportedBinaryOp(binary->getOp()) || binary->getType() != elemType) {
                return nullptr;
            }
            Value * lhs = materialize(binary->getLHS(), elemType);
            Value * rhs = materialize(binary->getRHS(), elemType);
            if (!lhs || !rhs) {
                return nullptr;
            }
            auto * vectorBinary = new VectorBinaryInst(func, binary->getOp(), lhs, rhs, VectorType::get(elemType), vl);
            insertBeforeTerminator(body, vectorBinary);
            result = vectorBinary;
        } else {
            return nullptr;
        }

        vectorValues[value] = result;
        return result;
    }

    bool resolveAccess(Value * pointer, Type * elemType, AccessInfo & access)
    {
        if (!pointer || !isSupportedElementType(elemType)) {
            return false;
        }

        int32_t offset = 0;
        Value * base = stripConstantGEP(pointer, offset);
        auto * pointerPhi = dynamic_cast<PhiInst *>(base);
        auto it = pointerPhis.find(pointerPhi);
        if (it == pointerPhis.end()) {
            return false;
        }

        Value * materializedPointer = pointerPhi;
        if (offset != 0) {
            // 保留常量偏移，生成向量 load/store 前重新 materialize 对应地址。
            auto * offsetValue = mod->newConstInt32(offset);
            auto * gep = new GetElementPtrInst(func, pointerPhi, offsetValue, pointerPhi->getType(), false);
            insertBeforeTerminator(body, gep);
            materializedPointer = gep;
        }

        access.pointer = materializedPointer;
        access.root = it->second.root;
        access.basePhi = pointerPhi;
        access.offset = offset;
        access.stride = it->second.step;
        access.elemType = elemType;
        return true;
    }

    const std::vector<AccessInfo> & getLoads() const
    {
        return loads;
    }

private:
    Value * createSplat(Value * scalar, Type * elemType)
    {
        auto * splat = new VectorSplatInst(func, scalar, vl, elemType);
        insertBeforeTerminator(body, splat);
        return splat;
    }

    Function * func = nullptr;
    Module * mod = nullptr;
    BasicBlock * body = nullptr;
    Value * vl = nullptr;
    const std::unordered_set<BasicBlock *> & loopBody;
    const std::unordered_map<PhiInst *, PointerPhiInfo> & pointerPhis;
    std::unordered_map<Value *, Value *> vectorValues;
    std::vector<AccessInfo> loads;
};

bool loopAlreadyVectorized(const std::unordered_set<BasicBlock *> & loopBody)
{
    for (auto * bb : loopBody) {
        for (auto * inst : bb->getInstructions()) {
            if (dynamic_cast<VSetVLInst *>(inst) != nullptr) {
                return true;
            }
        }
    }
    return false;
}

void killOldBodyInstructions(const std::vector<Instruction *> & oldBodyInsts)
{
    for (auto * inst : oldBodyInsts) {
        if (!inst || inst->isTerminator()) {
            continue;
        }
        inst->clearOperands();
        inst->setDead(true);
    }
}

void eraseInstructionFromBlock(BasicBlock * bb, Instruction * inst)
{
    if (!bb || !inst) {
        return;
    }

    inst->clearOperands();
    auto & insts = bb->getInstructions();
    for (auto it = insts.begin(); it != insts.end(); ++it) {
        if (*it != inst) {
            continue;
        }
        insts.erase(it);
        delete inst;
        return;
    }
}

void removeInsertedInstructions(BasicBlock * body, const std::unordered_set<Instruction *> & originalInsts)
{
    if (!body) {
        return;
    }

    std::vector<Instruction *> inserted;
    for (auto * inst : body->getInstructions()) {
        if (originalInsts.find(inst) == originalInsts.end()) {
            inserted.push_back(inst);
        }
    }
    // 失败回滚时先断开 use-def，再从基本块删除，避免残留操作数引用已删指令。
    for (auto * inst : inserted) {
        inst->clearOperands();
    }

    auto & insts = body->getInstructions();
    for (auto it = insts.begin(); it != insts.end();) {
        Instruction * inst = *it;
        if (originalInsts.find(inst) != originalInsts.end()) {
            ++it;
            continue;
        }

        auto next = std::next(it);
        insts.erase(it);
        delete inst;
        it = next;
    }
}

} // namespace

LoopVectorize::LoopVectorize(Function * _func, Module * _mod) : func(_func), mod(_mod)
{}

bool LoopVectorize::run()
{
    if (!func || !mod || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    bool changed = false;
    auto & cache = func->getAnalysisCache();

    while (true) {
        bool transformed = false;
        {
            auto & domTree = cache.getOrCompute<DominatorTree>([this] { return DominatorTree(func); });
            auto & loopInfo =
                cache.getOrCompute<LoopInfo>([this, &domTree] { return LoopInfo(func, &domTree); });
            auto & scev = cache.getOrCompute<ScalarEvolution>(
                [this, &domTree, &loopInfo] { return ScalarEvolution(func, &domTree, &loopInfo); });

            std::vector<BasicBlock *> headers;
            for (auto * bb : func->getBlocks()) {
                if (loopInfo.isLoopHeader(bb)) {
                    headers.push_back(bb);
                }
            }
            // 从内层循环开始尝试，避免外层先改写后破坏内层 canonical 形态
            std::stable_sort(headers.begin(), headers.end(), [&loopInfo](BasicBlock * lhs, BasicBlock * rhs) {
                return loopInfo.getLoopDepth(lhs) > loopInfo.getLoopDepth(rhs);
            });

            for (auto * header : headers) {
                if (tryVectorizeHeader(header, scev)) {
                    transformed = true;
                    changed = true;
                    break;
                }
            }
        }

        if (!transformed) {
            break;
        }
        // 每次成功后立即重建分析，继续处理同一函数中的其余合法循环
        cache.invalidateCFGAnalyses();
    }

    return changed;
}

bool LoopVectorize::tryVectorizeHeader(BasicBlock * header, ScalarEvolution & scev)
{
    ScalarEvolution::CanonicalLoop loop;
    // 只处理 i++ 且 i < bound 的规范循环，strip-mining 后用 VL 直接更新归纳变量。
    if (!header || !scev.matchCanonicalLoop(header, loop) || !loop.recurrence ||
        loop.compareKind != ScalarEvolution::CompareKind::LessThan ||
        loop.recurrence->getStep() != 1 || !loop.boundValue || !loop.induction) {
        return false;
    }

    auto & cache = func->getAnalysisCache();
    auto & domTree = cache.getOrCompute<DominatorTree>([this] { return DominatorTree(func); });
    auto & loopInfo =
        cache.getOrCompute<LoopInfo>([this, &domTree] { return LoopInfo(func, &domTree); });
    const auto * loopBodyPtr = loopInfo.getLoopBody(header);
    // 当前 pass 只处理 header+单 body/latch 的最小循环，避免复杂 CFG 下错误迁移指令。
    if (!loopBodyPtr || loopBodyPtr->size() != 2 || loopAlreadyVectorized(*loopBodyPtr)) {
        return false;
    }

    // 收益性判断(合法性已过)：trip count 已知且过短时，vsetvli 设置与归约横向 reduce
    // 的固定开销摊不开；循环体过轻同样不值得向量化。此处尚未改写 IR，可安全跳过。
    if (CostModel::profitabilityEnabled()) {
        if (loop.hasConstTripCount && loop.tripCount < CostModel::kVecMinTripCount) {
            CostModel::remark("vectorize", false, "trip count below threshold");
            return false;
        }
        if (CostModel::loopBodyCost(*loopBodyPtr) < CostModel::kVecMinBodyCost) {
            CostModel::remark("vectorize", false, "loop body too light");
            return false;
        }
    }

    BasicBlock * body = loop.body;
    BasicBlock * latch = loop.latch;
    BasicBlock * preheader = loop.preheader;
    if (!body || !latch || !preheader || body != latch || body == header ||
        !hasSingleBranchTo(body, header) || header->getPredecessors().size() != 2) {
        return false;
    }

    std::vector<Instruction *> oldBodyInsts;
    std::unordered_set<Instruction *> originalInsts;
    std::vector<StoreInst *> scalarStores;
    for (auto * inst : body->getInstructions()) {
        originalInsts.insert(inst);
        if (inst->isTerminator()) {
            continue;
        }
        // 有调用、body phi 或已存在 RVV 指令时暂不介入，保持转换边界清晰。
        if (dynamic_cast<CallInst *>(inst) != nullptr || dynamic_cast<PhiInst *>(inst) != nullptr ||
            dynamic_cast<VSetVLInst *>(inst) != nullptr) {
            return false;
        }
        if (auto * store = dynamic_cast<StoreInst *>(inst)) {
            scalarStores.push_back(store);
        }
        oldBodyInsts.push_back(inst);
    }
    if (oldBodyInsts.empty()) {
        return false;
    }

    std::unordered_map<PhiInst *, PointerPhiInfo> pointerPhis;
    ReductionInfo reduction;
    bool hasReduction = false;

    for (auto * inst : header->getInstructions()) {
        auto * phi = dynamic_cast<PhiInst *>(inst);
        if (!phi) {
            break;
        }
        if (phi == loop.induction) {
            continue;
        }

        PointerPhiInfo pointerInfo;
        if (matchPointerPhi(phi, preheader, latch, pointerInfo)) {
            pointerPhis[phi] = pointerInfo;
            continue;
        }

        ReductionInfo candidateReduction;
        if (matchReductionPhi(phi, preheader, latch, *loopBodyPtr, candidateReduction)) {
            if (hasReduction) {
                return false;
            }
            reduction = candidateReduction;
            hasReduction = true;
            continue;
        }

        return false;
    }

    if (pointerPhis.empty()) {
        return false;
    }
    if (hasReduction && !scalarStores.empty()) {
        // 归约循环和普通 store 循环先分开处理，避免同时维护标量可见副作用。
        return false;
    }
    if (hasReduction && !kEnableReductionVectorization) {
        return false;
    }
    Value * reductionInitial = nullptr;
    if (hasReduction) {
        reductionInitial = getPhiIncomingFrom(reduction.phi, preheader);
        if (!reductionInitial || reductionInitial->getType() != reduction.phi->getType() ||
            hasPhiUseOutsideLoop(reduction.phi, *loopBodyPtr)) {
            return false;
        }
    }

    auto * remaining = new BinaryInst(func,
                                      IRInstOperator::IRINST_OP_SUB_I,
                                      loop.boundValue,
                                      loop.induction,
                                      loop.induction->getType());
    insertBeforeTerminator(body, remaining);
    auto * vl = new VSetVLInst(func, remaining);
    insertBeforeTerminator(body, vl);
    // 每个 strip 根据剩余迭代数设置 VL，天然覆盖尾循环。

    VectorizationBuilder builder(func, mod, body, vl, *loopBodyPtr, pointerPhis);
    std::vector<AccessInfo> storeAccesses;

    if (hasReduction) {
        Type * elemType = reduction.phi->getType();
        Value * vectorTerm = builder.materialize(reduction.term, elemType);
        if (!vectorTerm) {
            removeInsertedInstructions(body, originalInsts);
            return false;
        }

        Value * zero = makeZeroValue(mod, elemType);
        if (!zero) {
            removeInsertedInstructions(body, originalInsts);
            return false;
        }

        if (elemType->isFloatType()) {
            // vfredosum 以当前标量累加值为 init[0]，按 lane 顺序完成每个 strip
            // strip 之间继续由标量 phi 串联，保持与原循环完全相同的浮点加法顺序
            auto * initialVector = new VectorSplatInst(func, reduction.phi, vl, elemType);
            auto * reduced = new VectorReduceInst(
                func, reduction.op, vectorTerm, initialVector, VectorType::get(elemType), vl);
            auto * extracted = new VectorExtractInst(func, reduced, elemType);
            insertBeforeTerminator(body, initialVector);
            insertBeforeTerminator(body, reduced);
            insertBeforeTerminator(body, extracted);
            replaceIncomingFrom(reduction.phi, latch, extracted);
        } else {
            auto * fullAvl = mod->newConstInteger(loop.induction->getType(), -1);
            auto * initFullVl = new VSetVLInst(func, fullAvl);
            insertBeforeTerminator(preheader, initFullVl);
            auto * zeroAccumulator = new VectorSplatInst(func, zero, initFullVl, elemType);
            insertBeforeTerminator(preheader, zeroAccumulator);

            // 用全 VL 零向量初始化累加器，循环中 preserve_lhs_tail 保留尾部未激活 lane
            auto * vectorAccumulator = new PhiInst(func, VectorType::get(elemType));
            insertAfterPhis(header, vectorAccumulator);
            vectorAccumulator->addIncoming(zeroAccumulator, preheader);

            auto * nextAccumulator = new VectorBinaryInst(func,
                                                          reduction.op,
                                                          vectorAccumulator,
                                                          vectorTerm,
                                                          VectorType::get(elemType),
                                                          vl,
                                                          true);
            insertBeforeTerminator(body, nextAccumulator);
            vectorAccumulator->addIncoming(nextAccumulator, latch);

            auto * exitFullVl = new VSetVLInst(func, fullAvl);
            auto * exitZero = new VectorSplatInst(func, zero, exitFullVl, elemType);
            auto * reduced = new VectorReduceInst(
                func, reduction.op, vectorAccumulator, exitZero, VectorType::get(elemType), exitFullVl);
            auto * extracted = new VectorExtractInst(func, reduced, elemType);
            auto * finalReduction =
                new BinaryInst(func, reduction.op, reductionInitial, extracted, reduction.phi->getType());
            insertAfterPhis(loop.exit,
                            std::vector<Instruction *>{exitFullVl, exitZero, reduced, extracted, finalReduction});
            // 原标量 phi 的循环外 uses 改接最终归约值，循环内旧链随后标死
            replaceUsesOutsideLoop(reduction.phi, finalReduction, *loopBodyPtr);
        }
    } else {
        if (scalarStores.empty()) {
            removeInsertedInstructions(body, originalInsts);
            return false;
        }

        for (auto * store : scalarStores) {
            AccessInfo storeAccess;
            if (!builder.resolveAccess(store->getPointerOperand(), store->getValueOperand()->getType(), storeAccess)) {
                removeInsertedInstructions(body, originalInsts);
                return false;
            }
            Value * vectorValue = builder.materialize(store->getValueOperand(), storeAccess.elemType);
            if (!vectorValue) {
                removeInsertedInstructions(body, originalInsts);
                return false;
            }
            auto * vectorStore =
                new VectorStoreInst(func, vectorValue, storeAccess.pointer, vl, storeAccess.stride);
            insertBeforeTerminator(body, vectorStore);
            storeAccesses.push_back(storeAccess);
        }

        if (!storesAreAliasSafe(builder.getLoads(), storeAccesses)) {
            // 新插入的向量指令还未提交，别名检查失败时可直接回滚。
            removeInsertedInstructions(body, originalInsts);
            return false;
        }
    }

    auto * nextInduction =
        new BinaryInst(func, IRInstOperator::IRINST_OP_ADD_I, loop.induction, vl, loop.induction->getType());
    insertBeforeTerminator(body, nextInduction);
    replaceIncomingFrom(loop.induction, latch, nextInduction);

    for (const auto & [phi, info] : pointerPhis) {
        Value * stepValue = vl;
        if (info.step != 1) {
            // 指针递推步长按元素计数，strip-mining 后需要乘以当前 VL。
            auto * stepConst = mod->newConstInteger(loop.induction->getType(), info.step);
            auto * scaledStep = new BinaryInst(func, IRInstOperator::IRINST_OP_MUL_I, vl, stepConst, vl->getType());
            insertBeforeTerminator(body, scaledStep);
            stepValue = scaledStep;
        }
        auto * nextPtr = new GetElementPtrInst(func, phi, stepValue, phi->getType(), false);
        insertBeforeTerminator(body, nextPtr);
        replaceIncomingFrom(phi, latch, nextPtr);
    }

    killOldBodyInstructions(oldBodyInsts);
    if (hasReduction && !reduction.phi->getType()->isFloatType()) {
        eraseInstructionFromBlock(header, reduction.phi);
    }
    return true;
}
