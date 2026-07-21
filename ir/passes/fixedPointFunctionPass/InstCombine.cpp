///
/// @file InstCombine.cpp
/// @brief 本地模式化简 pass 实现
///

#include "InstCombine.h"

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <functional>
#include <unordered_set>
#include <unordered_map>

#include "BasicBlock.h"
#include "BinaryInst.h"
#include "ConstFloat.h"
#include "ConstInteger.h"
#include "CopyInst.h"
#include "FPToSIInst.h"
#include "Function.h"
#include "AnalysisCache.h"
#include "GetElementPtrInst.h"
#include "Instruction.h"
#include "IntegerType.h"
#include "Module.h"
#include "PhiInst.h"
#include "SIToFPInst.h"
#include "SelectInst.h"
#include "Value.h"
#include "ZExtInst.h"

namespace {

/// @brief 判断是否为整数零常量
/// @param value 待判断的值
/// @return true 表示该值是整数零常量
bool isIntegerZero(Value * value)
{
    auto * constant = dynamic_cast<ConstInteger *>(value);
    return constant && constant->getVal() == 0;
}

/// @brief 判断是否为整数一常量
/// @param value 待判断的值
/// @return true 表示该值是整数一常量
bool isIntegerOne(Value * value)
{
    auto * constant = dynamic_cast<ConstInteger *>(value);
    return constant && constant->getVal() == 1;
}

/// @brief 获取 float 字面量的位模式
/// @param value 待编码的浮点数
/// @return 对应的 IEEE-754 位模式
std::uint32_t getFloatBitPattern(float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/// @brief 判断是否为正零浮点常量
/// @param value 待判断的值
/// @return true 表示该值是 +0.0f
bool isPositiveFloatZero(Value * value)
{
    auto * constant = dynamic_cast<ConstFloat *>(value);
    return constant && constant->getBitPattern() == getFloatBitPattern(0.0f);
}

/// @brief 判断是否为浮点一常量
/// @param value 待判断的值
/// @return true 表示该值是 1.0f
bool isFloatOne(Value * value)
{
    auto * constant = dynamic_cast<ConstFloat *>(value);
    return constant && constant->getBitPattern() == getFloatBitPattern(1.0f);
}

/// @brief 判断是否为“2 的幂”浮点常量(规格化、尾数为 0)，并且其倒数也是规格化数
/// @param value 待判断的值（除数）
/// @param recipOut 输出倒数 1/value（仅在返回 true 时有效）
/// @return true 表示可安全地把 x / value 改写为 x * (1/value)
///
/// 对 2 的幂 C 而言，1/C 在 IEEE-754 下可精确表示，x / C 与 x * (1/C) 逐位相等
/// （二者都是对同一实数的一次舍入），因此该改写不改变任何浮点结果。
/// 为稳妥起见，仅当倒数本身也是规格化数时才改写（排除 C=2^127 这类倒数落入非规格化的极端值）。
bool isFloatPowerOfTwoReciprocal(Value * value, float & recipOut)
{
    auto * constant = dynamic_cast<ConstFloat *>(value);
    if (!constant) {
        return false;
    }
    std::uint32_t bits = constant->getBitPattern();
    std::uint32_t exp = (bits >> 23U) & 0xFFU;
    std::uint32_t mant = bits & 0x7FFFFFU;
    // 规格化 2 的幂：尾数全 0，指数既非 0(零/非规格化)也非 255(inf/nan)
    if (mant != 0 || exp == 0 || exp == 0xFFU) {
        return false;
    }
    // 倒数 ±2^(127-exp) 的偏置指数为 254-exp；要求其仍为规格化数(指数 in [1,254])
    if (254U - exp == 0U) {
        return false;
    }
    recipOut = 1.0f / constant->getVal();
    return true;
}

/// @brief 将指令 inst 插入到 before 之前（同一基本块内）
void insertInstBefore(Instruction * before, Instruction * inst)
{
    if (!before || !inst || !before->getParentBlock()) {
        return;
    }
    auto * bb = before->getParentBlock();
    auto & insts = bb->getInstructions();
    auto pos = std::find(insts.begin(), insts.end(), before);
    if (pos == insts.end()) {
        return;
    }
    inst->setParentBlock(bb);
    insts.insert(pos, inst);
}

/// @brief 顺着新值 copy 链向前转发源值
/// @param value 起始值
/// @return 最终可直接使用的源值
Value * getForwardedCopySource(Value * value)
{
    Value * current = value;
    std::unordered_set<Value *> visited;
    while (auto * copy = dynamic_cast<CopyInst *>(current)) {
        if (copy->getDst()) {
            break;
        }

        if (!visited.insert(current).second) {
            break;
        }

        Value * next = copy->getSource();
        if (!next || next == current) {
            break;
        }

        current = next;
    }

    return current;
}

struct GEPKey {
    Value * base = nullptr;
    Value * index = nullptr;
    Type * type = nullptr;
    bool decayArray = false;

    bool operator==(const GEPKey & other) const
    {
        return base == other.base && index == other.index && type == other.type && decayArray == other.decayArray;
    }
};

struct GEPKeyHash {
    std::size_t operator()(const GEPKey & key) const
    {
        std::size_t result = std::hash<Value *>{}(key.base);
        result ^= std::hash<Value *>{}(key.index) << 1U;
        result ^= std::hash<Type *>{}(key.type) << 2U;
        result ^= std::hash<bool>{}(key.decayArray) << 3U;
        return result;
    }
};

} // namespace

/// @brief 构造 InstCombine
/// @param _func 待优化的函数
/// @param _mod 所属模块
InstCombine::InstCombine(Function * _func, Module * _mod) : func(_func), mod(_mod)
{}

/// @brief 执行本地模式化简
/// @return 若 IR 被修改则返回 true
bool InstCombine::run()
{
    if (!func || !mod || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    bool changed = false;
    bool localChanged = false;
    do {
        localChanged = false;

        if (eliminateRedundantGEPs()) {
            localChanged = true;
            changed = true;
        }

        for (auto * bb : func->getBlocks()) {
            for (auto * inst : bb->getInstructions()) {
                if (inst->isDead()) {
                    continue;
                }

                if (trySimplifyInstruction(inst)) {
                    localChanged = true;
                    changed = true;
                }
            }
        }

        if (sweepDeadInstructions() > 0) {
            changed = true;
        }
    } while (localChanged);

    if (changed) {
        // 指令化简改写值但不改 CFG，仅使值相关分析失效
        func->getAnalysisCache().invalidateValueAnalyses();
    }
    return changed;
}

/// @brief 尝试化简单条指令
/// @param inst 待化简的指令
/// @return 若成功化简则返回 true
bool InstCombine::trySimplifyInstruction(Instruction * inst)
{
    if (!inst) {
        return false;
    }

    if (auto * binary = dynamic_cast<BinaryInst *>(inst)) {
        return simplifyBinary(binary);
    }

    if (auto * phi = dynamic_cast<PhiInst *>(inst)) {
        return simplifyPhi(phi);
    }

    if (auto * copy = dynamic_cast<CopyInst *>(inst)) {
        return simplifyCopy(copy);
    }

    if (auto * zext = dynamic_cast<ZExtInst *>(inst)) {
        return simplifyZExt(zext);
    }

    if (auto * sitofp = dynamic_cast<SIToFPInst *>(inst)) {
        return simplifySIToFP(sitofp);
    }

    if (auto * select = dynamic_cast<SelectInst *>(inst)) {
        return simplifySelect(select);
    }

    if (auto * fptosi = dynamic_cast<FPToSIInst *>(inst)) {
        return simplifyFPToSI(fptosi);
    }

    return false;
}

/// @brief 消除同一基本块内重复的 GEP 地址计算
/// @return 若至少删除一条 GEP 则返回 true
bool InstCombine::eliminateRedundantGEPs()
{
    bool changed = false;
    for (auto * bb : func->getBlocks()) {
        std::unordered_map<GEPKey, GetElementPtrInst *, GEPKeyHash> availableGEPs;
        for (auto * inst : bb->getInstructions()) {
            if (!inst || inst->isDead()) {
                continue;
            }

            auto * gep = dynamic_cast<GetElementPtrInst *>(inst);
            if (!gep) {
                continue;
            }

            GEPKey key{gep->getBasePointer(), gep->getIndexOperand(), gep->getType(), gep->isArrayDecayGEP()};
            auto it = availableGEPs.find(key);
            if (it != availableGEPs.end() && !it->second->isDead()) {
                changed = replaceInstWithValue(gep, it->second) || changed;
                continue;
            }

            availableGEPs.emplace(key, gep);
        }
    }

    return changed;
}

/// @brief 用现有值替换指令结果并删除旧指令
/// @param inst 待替换的指令
/// @param replacement 新值
/// @return 替换成功时返回 true
bool InstCombine::replaceInstWithValue(Instruction * inst, Value * replacement)
{
    if (!inst || !replacement || replacement == inst) {
        return false;
    }

    inst->replaceAllUseWith(replacement);
    inst->clearOperands();
    inst->setDead(true);
    return true;
}

/// @brief 化简整数/浮点二元指令
/// @param inst 待化简的二元指令
/// @return 若成功化简则返回 true
bool InstCombine::simplifyBinary(BinaryInst * inst)
{
    if (!inst) {
        return false;
    }

    Value * lhs = inst->getLHS();
    Value * rhs = inst->getRHS();
    switch (inst->getOp()) {
        case IRInstOperator::IRINST_OP_ADD_I:
            if (isIntegerZero(lhs)) {
                return replaceInstWithValue(inst, rhs);
            }
            if (isIntegerZero(rhs)) {
                return replaceInstWithValue(inst, lhs);
            }
            if (tryLinearReassociate(inst)) {
                return true;
            }
            break;

        case IRInstOperator::IRINST_OP_SUB_I:
            if (isIntegerZero(rhs)) {
                return replaceInstWithValue(inst, lhs);
            }
            if (tryLinearReassociate(inst)) {
                return true;
            }
            break;

        case IRInstOperator::IRINST_OP_MUL_I:
            if (isIntegerZero(lhs)) {
                return replaceInstWithValue(inst, lhs);
            }
            if (isIntegerZero(rhs)) {
                return replaceInstWithValue(inst, rhs);
            }
            if (isIntegerOne(lhs)) {
                return replaceInstWithValue(inst, rhs);
            }
            if (isIntegerOne(rhs)) {
                return replaceInstWithValue(inst, lhs);
            }
            break;

        case IRInstOperator::IRINST_OP_DIV_I:
            if (isIntegerOne(rhs)) {
                return replaceInstWithValue(inst, lhs);
            }
            break;

        case IRInstOperator::IRINST_OP_MOD_I:
            if (isIntegerOne(rhs)) {
                return replaceInstWithValue(inst, mod->newConstInteger(inst->getType(), 0));
            }
            break;

        case IRInstOperator::IRINST_OP_ADD_F:
            if (isPositiveFloatZero(lhs)) {
                return replaceInstWithValue(inst, rhs);
            }
            if (isPositiveFloatZero(rhs)) {
                return replaceInstWithValue(inst, lhs);
            }
            break;

        case IRInstOperator::IRINST_OP_SUB_F:
            if (isPositiveFloatZero(rhs)) {
                return replaceInstWithValue(inst, lhs);
            }
            break;

        case IRInstOperator::IRINST_OP_MUL_F:
            if (isFloatOne(lhs)) {
                return replaceInstWithValue(inst, rhs);
            }
            if (isFloatOne(rhs)) {
                return replaceInstWithValue(inst, lhs);
            }
            break;

        case IRInstOperator::IRINST_OP_DIV_F:
            if (isFloatOne(rhs)) {
                return replaceInstWithValue(inst, lhs);
            }
            {
                // x / 2^k  ->  x * 2^-k （精确等价，fmul 远快于 fdiv）
                float recip = 0.0f;
                if (isFloatPowerOfTwoReciprocal(rhs, recip)) {
                    auto * mul = new BinaryInst(func,
                                                IRInstOperator::IRINST_OP_MUL_F,
                                                lhs,
                                                mod->newConstFloat(recip),
                                                inst->getType());
                    insertInstBefore(inst, mul);
                    return replaceInstWithValue(inst, mul);
                }
            }
            break;

        default:
            break;
    }

    return false;
}

/// @brief 尝试把整数 add/sub 链折叠为最简线性组合
///
/// 以 inst 为根收集 add/sub 构成的 DAG（允许链内共享节点），在 i32
/// 回绕环上把整条链规范化为 Σ coeff·term + K（精确等价，无溢出语义
/// 问题）。当全部系数绝对值不超过 1 且重建指令数严格少于被吸收的
/// 链节点数时，按「正项相加、负项相减、常量最后」重建。
/// 典型收益：源码用加减法模拟位运算的链，如
/// a-(a+b)+b-(a+b) → -(a+b)，甚至整链坍缩为常量。
bool InstCombine::tryLinearReassociate(BinaryInst * inst)
{
    constexpr std::size_t kMaxChainNodes = 32;

    if (!inst || inst->isDead() || !inst->getType()->isIntegerType() ||
        inst->getType() == IntegerType::getTypeInt1()) {
        return false;
    }

    auto asChainOp = [](Value * value) -> BinaryInst * {
        auto * binary = dynamic_cast<BinaryInst *>(value);
        if (!binary || binary->isDead()) {
            return nullptr;
        }
        IRInstOperator op = binary->getOp();
        if (op != IRInstOperator::IRINST_OP_ADD_I && op != IRInstOperator::IRINST_OP_SUB_I) {
            return nullptr;
        }
        return binary;
    };

    // 仅在链根触发：唯一使用者仍是 add/sub 时交给根节点统一处理，
    // 避免对同一条链的每个内部节点重复扫描
    if (inst->getUseList().size() == 1) {
        auto * user = dynamic_cast<Instruction *>(inst->getUseList().front()->getUser());
        if (user != nullptr && !user->isDead() && asChainOp(user) != nullptr) {
            return false;
        }
    }

    // 第一阶段：从根出发收集候选链节点
    std::vector<BinaryInst *> discovered = {inst};
    std::unordered_set<BinaryInst *> chain = {inst};
    for (std::size_t i = 0; i < discovered.size(); ++i) {
        if (chain.size() > kMaxChainNodes) {
            return false;
        }
        for (Value * operand : {discovered[i]->getLHS(), discovered[i]->getRHS()}) {
            auto * child = asChainOp(operand);
            if (child != nullptr && child != inst && chain.insert(child).second) {
                discovered.push_back(child);
            }
        }
    }
    if (discovered.size() < 2) {
        return false;
    }

    // 第二阶段：非根节点若存在链外使用者则降级为原子项，迭代至稳定
    bool demotedAny = true;
    while (demotedAny) {
        demotedAny = false;
        for (auto * node : discovered) {
            if (node == inst || chain.find(node) == chain.end()) {
                continue;
            }
            for (auto * use : node->getUseList()) {
                auto * userBinary = dynamic_cast<BinaryInst *>(use->getUser());
                if (userBinary == nullptr || userBinary->isDead() ||
                    chain.find(userBinary) == chain.end()) {
                    chain.erase(node);
                    demotedAny = true;
                    break;
                }
            }
        }
    }

    // 重新求根可达的链节点集合，并生成逆后序（DAG 拓扑序，父先于子）
    std::vector<BinaryInst *> postOrder;
    std::unordered_set<BinaryInst *> visited;
    std::function<void(BinaryInst *)> dfs = [&](BinaryInst * node) {
        if (!visited.insert(node).second) {
            return;
        }
        for (Value * operand : {node->getLHS(), node->getRHS()}) {
            auto * child = asChainOp(operand);
            if (child != nullptr && chain.find(child) != chain.end()) {
                dfs(child);
            }
        }
        postOrder.push_back(node);
    };
    dfs(inst);
    if (postOrder.size() < 2) {
        return false;
    }
    std::vector<BinaryInst *> topoOrder(postOrder.rbegin(), postOrder.rend());

    // 第三阶段：沿拓扑序传播乘数，累积原子项系数与常量
    std::unordered_map<BinaryInst *, std::int64_t> multiplier;
    std::unordered_map<Value *, std::int64_t> coeffs;
    std::vector<Value *> termOrder;
    std::int64_t constant = 0;
    multiplier[inst] = 1;
    for (auto * node : topoOrder) {
        std::int64_t factor = multiplier[node];
        if (factor == 0) {
            continue;
        }
        const bool isSub = node->getOp() == IRInstOperator::IRINST_OP_SUB_I;
        Value * operands[2] = {node->getLHS(), node->getRHS()};
        for (int index = 0; index < 2; ++index) {
            std::int64_t sign = (index == 1 && isSub) ? -factor : factor;
            Value * operand = operands[index];
            auto * child = asChainOp(operand);
            if (child != nullptr && visited.find(child) != visited.end() &&
                chain.find(child) != chain.end()) {
                multiplier[child] += sign;
                continue;
            }
            if (auto * constInt = dynamic_cast<ConstInteger *>(operand)) {
                constant += sign * static_cast<std::int64_t>(constInt->getVal());
                continue;
            }
            if (coeffs.find(operand) == coeffs.end()) {
                termOrder.push_back(operand);
            }
            coeffs[operand] += sign;
        }
    }

    // 常量按 i32 回绕语义归一
    const auto wrappedConstant =
        static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(constant)));

    // 收集非零系数项；v1 仅处理系数 ±1 的情形
    std::vector<Value *> positives;
    std::vector<Value *> negatives;
    for (auto * term : termOrder) {
        std::int64_t coeff = coeffs[term];
        if (coeff == 0) {
            continue;
        }
        if (coeff == 1) {
            positives.push_back(term);
        } else if (coeff == -1) {
            negatives.push_back(term);
        } else {
            return false;
        }
    }

    const std::size_t expandedCount = postOrder.size();
    const std::size_t termCount = positives.size() + negatives.size();
    std::size_t newCost = 0;
    if (termCount > 0) {
        newCost = termCount - 1 + (positives.empty() ? 1 : 0) + (wrappedConstant != 0 ? 1 : 0);
    }
    if (newCost >= expandedCount) {
        return false;
    }

    // 第四阶段：重建（正项相加 → 负项相减 → 常量最后，保持确定性顺序）
    Type * resultType = inst->getType();
    auto emitBinary = [&](IRInstOperator op, Value * lhsValue, Value * rhsValue) -> Value * {
        auto * created = new BinaryInst(func, op, lhsValue, rhsValue, resultType);
        insertInstBefore(inst, created);
        return created;
    };

    Value * accumulated = nullptr;
    for (auto * term : positives) {
        accumulated = accumulated == nullptr
                          ? term
                          : emitBinary(IRInstOperator::IRINST_OP_ADD_I, accumulated, term);
    }
    for (auto * term : negatives) {
        if (accumulated == nullptr) {
            accumulated =
                emitBinary(IRInstOperator::IRINST_OP_SUB_I, mod->newConstInteger(resultType, 0), term);
        } else {
            accumulated = emitBinary(IRInstOperator::IRINST_OP_SUB_I, accumulated, term);
        }
    }
    if (accumulated == nullptr) {
        accumulated = mod->newConstInteger(resultType, wrappedConstant);
    } else if (wrappedConstant != 0) {
        accumulated = emitBinary(IRInstOperator::IRINST_OP_ADD_I,
                                 accumulated,
                                 mod->newConstInteger(resultType, wrappedConstant));
    }

    return replaceInstWithValue(inst, accumulated);
}

/// @brief 化简冗余 phi
/// @param phi 待化简的 phi 指令
/// @return 若成功化简则返回 true
bool InstCombine::simplifyPhi(PhiInst * phi)
{
    if (!phi || phi->getIncomingCount() <= 0) {
        return false;
    }

    Value * commonValue = phi->getIncomingValue(0);
    if (!commonValue || commonValue == phi) {
        return false;
    }

    if (phi->getIncomingCount() == 1) {
        return replaceInstWithValue(phi, commonValue);
    }

    for (int32_t index = 1; index < phi->getIncomingCount(); ++index) {
        if (phi->getIncomingValue(index) != commonValue) {
            return false;
        }
    }

    return replaceInstWithValue(phi, commonValue);
}

/// @brief 化简冗余 copy
/// @param copy 待化简的 copy 指令
/// @return 若成功化简则返回 true
bool InstCombine::simplifyCopy(CopyInst * copy)
{
    if (!copy) {
        return false;
    }

    bool changed = false;
    Value * forwarded = getForwardedCopySource(copy->getSource());
    if (forwarded != copy->getSource()) {
        copy->setOperand(0, forwarded);
        changed = true;
    }

    if (copy->getDst()) {
        if (copy->getDst() == copy->getSource()) {
            copy->clearOperands();
            copy->setDead(true);
            return true;
        }

        return changed;
    }

    return replaceInstWithValue(copy, copy->getSource()) || changed;
}

/// @brief 折叠常量 zero-extend
/// @param inst 待化简的 zext 指令
/// @return 若成功化简则返回 true
bool InstCombine::simplifyZExt(ZExtInst * inst)
{
    if (!inst) {
        return false;
    }

    // 指令的操作数必须是整数常量，且目标类型必须也是整数类型，此时可以化简
    auto * source = dynamic_cast<ConstInteger *>(inst->getSource());
    auto * sourceType = dynamic_cast<IntegerType *>(inst->getSource()->getType());
    auto * targetType = dynamic_cast<IntegerType *>(inst->getType());
    if (!source || !sourceType || !targetType) {
        return false;
    }

    int32_t value = source->getVal();
    int32_t bitWidth = sourceType->getBitWidth();
    if (bitWidth > 0 && bitWidth < 32) {
        value &= static_cast<int32_t>((1ULL << bitWidth) - 1ULL);
    }

    return replaceInstWithValue(inst, mod->newConstInteger(targetType, value));
}

/// @brief 折叠常量 int-to-float cast
/// @param inst 待化简的 sitofp 指令
/// @return 若成功化简则返回 true
bool InstCombine::simplifySIToFP(SIToFPInst * inst)
{
    if (!inst) {
        return false;
    }

    auto * source = dynamic_cast<ConstInteger *>(inst->getSource());
    if (!source) {
        return false;
    }

    return replaceInstWithValue(inst, mod->newConstFloat(static_cast<float>(source->getVal())));
}

/// @brief 化简 select 指令
/// @param inst 待化简的 select 指令
/// @return 若成功化简则返回 true
bool InstCombine::simplifySelect(SelectInst * inst)
{
    if (!inst) {
        return false;
    }

    // true/false 两路相同则 select 本身冗余
    if (inst->getTrueValue() == inst->getFalseValue()) {
        return replaceInstWithValue(inst, inst->getTrueValue());
    }

    auto * condition = dynamic_cast<ConstInteger *>(inst->getCondition());
    if (!condition) {
        return false;
    }

    // 条件已知时直接挑出对应分支值
    return replaceInstWithValue(inst, condition->getVal() != 0 ? inst->getTrueValue() : inst->getFalseValue());
}

/// @brief 折叠常量 float-to-int cast
/// @param inst 待化简的 fptosi 指令
/// @return 若成功化简则返回 true
bool InstCombine::simplifyFPToSI(FPToSIInst * inst)
{
    if (!inst) {
        return false;
    }

    auto * source = dynamic_cast<ConstFloat *>(inst->getSource());
    auto * targetType = dynamic_cast<IntegerType *>(inst->getType());
    if (!source || !targetType) {
        return false;
    }

    return replaceInstWithValue(inst, mod->newConstInteger(targetType, static_cast<int32_t>(source->getVal())));
}

/// @brief 清扫已标记为 dead 的指令
/// @return 被真正移除的指令数量
int32_t InstCombine::sweepDeadInstructions()
{
    int32_t removedCount = 0;
    for (auto * bb : func->getBlocks()) {
        auto & insts = bb->getInstructions();
        auto it = insts.begin();
        while (it != insts.end()) {
            Instruction * inst = *it;
            if (!inst->isDead()) {
                ++it;
                continue;
            }

            it = insts.erase(it);
            delete inst;
            ++removedCount;
        }
    }

    return removedCount;
}
