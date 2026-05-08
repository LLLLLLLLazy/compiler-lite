///
/// @file InstCombine.cpp
/// @brief 本地模式化简 pass 实现
///

#include "InstCombine.h"

#include <cstdint>
#include <cstring>
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
#include "GetElementPtrInst.h"
#include "Instruction.h"
#include "IntegerType.h"
#include "Module.h"
#include "PhiInst.h"
#include "SIToFPInst.h"
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
            break;

        case IRInstOperator::IRINST_OP_SUB_I:
            if (isIntegerZero(rhs)) {
                return replaceInstWithValue(inst, lhs);
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
            break;

        default:
            break;
    }

    return false;
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
