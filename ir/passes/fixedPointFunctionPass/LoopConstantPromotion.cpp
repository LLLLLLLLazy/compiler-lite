///
/// @file LoopConstantPromotion.cpp
/// @brief 循环常量提升 pass 实现
///
/// 对于循环体内被使用 2 次及以上的「大整数常量」（绝对值超出 12 位有符号
/// 立即数范围，即 |val| > 2047）和所有浮点常量，在 preheader 中生成一条
/// 物化指令将其固化为虚拟寄存器值，并替换循环体内的全部对应使用。
///
/// 后端在指令选择时对每个常量使用点独立调用 load_imm / 浮点常量加载，
/// 超出 12 位范围的整数常量需要 lui+addiw 两条指令，浮点常量则需要
/// lui+addiw+fmv.w.x 三条指令。将这些常量提升到 preheader 可避免在
/// 循环体内重复物化，显著减少热路径指令数。
///
/// 本 pass 当前放在 late pass 中（紧接 LoopRotate 之前）运行，
/// 因此不会被 ConstProp/InstCombine 等折叠还原。
///

#include "LoopConstantPromotion.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ArrayType.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "ConstFloat.h"
#include "ConstInteger.h"
#include "DominatorTree.h"
#include "Function.h"
#include "AnalysisCache.h"
#include "GetElementPtrInst.h"
#include "ICmpInst.h"
#include "Instruction.h"
#include "IntegerType.h"
#include "LoopInfo.h"
#include "Module.h"
#include "PhiInst.h"
#include "PointerType.h"
#include "StoreInst.h"
#include "Value.h"

namespace {

/// @brief 整数常量绝对值超过此阈值时需要 lui+addiw 物化
constexpr int32_t kLargeImmThreshold = 2047;

/// @brief 判断常量是否值得提升到循环外
///
/// 对整数常量仅提升超出 12 位有符号立即数范围的（需要 lui+addiw）；
/// 对浮点常量提升非零值（正负零均需保留原始符号，不参与提升）
/// @param value 待判断的值
/// @return true 表示该常量值得提升
bool shouldPromoteConstant(Value * value)
{
    if (auto * constInt = dynamic_cast<ConstInteger *>(value)) {
        const int32_t val = constInt->getVal();
        return val > kLargeImmThreshold || val < -kLargeImmThreshold;
    }

    if (auto * constFloat = dynamic_cast<ConstFloat *>(value)) {
        // fadd 物化会改变 -0.0f 的符号，因此正负零均不提升
        return (constFloat->getBitPattern() & 0x7FFFFFFFU) != 0U;
    }

    return false;
}

/// @brief 收集循环头的全部循环外前驱，并尝试识别已有 preheader
/// @param header 循环头基本块
/// @param loopBody 循环体块集合
/// @return preheader 块，若不存在则返回 nullptr
BasicBlock * findPreheader(BasicBlock * header, const std::unordered_set<BasicBlock *> & loopBody)
{
    if (!header) {
        return nullptr;
    }

    BasicBlock * preheader = nullptr;
    for (auto * pred : header->getPredecessors()) {
        if (loopBody.find(pred) != loopBody.end()) {
            continue;
        }

        if (preheader) {
            return nullptr; // 多个循环外前驱，无唯一 preheader
        }
        preheader = pred;
    }

    // preheader 必须只有一条出边（指向循环头）
    if (preheader && preheader->getSuccessors().size() == 1) {
        return preheader;
    }

    return nullptr;
}

/// @brief 在 preheader 终结指令之前插入一条物化指令
/// @param inst 待插入的指令
/// @param preheader 目标 preheader
void insertBeforeTerminator(Instruction * inst, BasicBlock * preheader)
{
    if (!inst || !preheader) {
        return;
    }

    auto & insts = preheader->getInstructions();
    auto insertPos = insts.end();
    if (!insts.empty()) {
        auto last = std::prev(insts.end());
        if ((*last)->isTerminator()) {
            insertPos = last;
        }
    }

    insts.insert(insertPos, inst);
    inst->setParentBlock(preheader);
}

// ---------------------------------------------------------------------------
// 分组提升（热循环内重复物化的小常量 + GEP 缩放索引共享）
//
// 后端在指令选择时对每个常量使用点独立物化（load_imm），对每个动态下标 GEP
// 独立发射 slli+add。对嵌套循环（深度 >= 2）里的重复物化，把「物化一次」
// 的虚拟寄存器放到公共支配点，交给寄存器分配器分配（跨调用的活跃区间会
// 自然落到 callee-saved 寄存器），循环内只剩对寄存器的直接使用。
// ---------------------------------------------------------------------------

/// @brief 分组键：ConstInteger 不唯一化，按值分组；其余按 Value 指针分组
struct PromotionGroupKey {
    bool isConst = false;
    int32_t constVal = 0;
    Value * value = nullptr;  ///< 非常量组的索引 Value
    int32_t scale = 0;        ///< GEP 组：log2(elemSize)；小常量组恒为 0

    bool operator==(const PromotionGroupKey & other) const
    {
        return isConst == other.isConst && constVal == other.constVal && value == other.value &&
               scale == other.scale;
    }
};

struct PromotionGroupKeyHash {
    size_t operator()(const PromotionGroupKey & key) const
    {
        size_t h = std::hash<bool>()(key.isConst);
        h ^= std::hash<int32_t>()(key.constVal) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<const void *>()(key.value) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int32_t>()(key.scale) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

/// @brief 判断常量在该操作数位置是否必须完整物化（后端无立即数形式可用）
///
/// 复刻后端 translate_icmp 的立即数折叠判定：EQ/NE 与 0 折叠为 seqz/snez，
/// LT/GE/LE/GT 在 slti 立即数范围内折叠。store 值操作数需要完整物化。
/// 二元运算/条件分支等具备立即数形式的位置返回 false，避免把 addi/slti
/// 退化成寄存器形式；select 值/调用实参/return 值/phi 入边虽需物化，但
/// 寄存器形式同样要显式 mv，与逐次 li 同价，不参与提升。
bool useRequiresMaterialization(Instruction * inst, int32_t operandIdx, int32_t val)
{
    if (auto * icmp = dynamic_cast<ICmpInst *>(inst)) {
        Value * lhs = icmp->getLHS();
        Value * rhs = icmp->getRHS();
        const bool constOnLeft = operandIdx == 0;
        // 两侧都是常量：应已被常量传播折叠，保守不参与
        if (constOnLeft ? dynamic_cast<ConstInteger *>(rhs) != nullptr
                        : dynamic_cast<ConstInteger *>(lhs) != nullptr) {
            return false;
        }
        IRInstOperator op = icmp->getOp();
        if (constOnLeft) {
            switch (op) {
                case IRInstOperator::IRINST_OP_LT_I:
                    op = IRInstOperator::IRINST_OP_GT_I;
                    break;
                case IRInstOperator::IRINST_OP_GT_I:
                    op = IRInstOperator::IRINST_OP_LT_I;
                    break;
                case IRInstOperator::IRINST_OP_LE_I:
                    op = IRInstOperator::IRINST_OP_GE_I;
                    break;
                case IRInstOperator::IRINST_OP_GE_I:
                    op = IRInstOperator::IRINST_OP_LE_I;
                    break;
                default:
                    break;
            }
        }
        switch (op) {
            case IRInstOperator::IRINST_OP_EQ_I:
            case IRInstOperator::IRINST_OP_NE_I:
                return val != 0;
            case IRInstOperator::IRINST_OP_LT_I:
            case IRInstOperator::IRINST_OP_GE_I:
                return val < -2048 || val > 2047;
            case IRInstOperator::IRINST_OP_LE_I:
            case IRInstOperator::IRINST_OP_GT_I:
                return val < -2049 || val > 2046;
            default:
                return false;
        }
    }

    // 其余形态（select 值、调用实参、return 值、phi 入边）虽需物化，但寄存器
    // 形式同样要显式 mv 到目标寄存器，与逐次 li 同价，提升只徒增寄存器压力，
    // 不参与。store 值、icmp 比较对象由消费指令就地读寄存器，才有净收益。
    if (dynamic_cast<StoreInst *>(inst) != nullptr) {
        return operandIdx == 0;
    }
    return false;
}

/// @brief 计算 GEP 的元素大小缩放位数（log2(elemSize)），不可缩放时返回 -1
int32_t gepScaleLog2(GetElementPtrInst * gep)
{
    if (gep == nullptr || gep->getBasePointer() == nullptr) {
        return -1;
    }
    auto * basePtrType = dynamic_cast<const PointerType *>(gep->getBasePointer()->getType());
    if (basePtrType == nullptr) {
        return -1;
    }
    Type * stepType = const_cast<Type *>(basePtrType->getPointeeType());
    if (gep->isArrayDecayGEP()) {
        if (auto * arrayType = dynamic_cast<ArrayType *>(stepType)) {
            stepType = arrayType->getElementType();
        }
    }
    const int32_t elemSize = stepType->getSize();
    if (elemSize <= 1 || (elemSize & (elemSize - 1)) != 0) {
        return -1;
    }
    return 31 - __builtin_clz(static_cast<uint32_t>(elemSize));
}

/// @brief 计算多个块的最近公共支配者
BasicBlock * commonDominator(const std::vector<BasicBlock *> & blocks, DominatorTree & domTree)
{
    if (blocks.empty()) {
        return nullptr;
    }
    BasicBlock * result = blocks.front();
    for (size_t i = 1; i < blocks.size(); ++i) {
        BasicBlock * other = blocks[i];
        while (result != other && !domTree.dominates(result, other)) {
            result = domTree.getIDom(result);
            if (result == nullptr) {
                return nullptr;
            }
        }
    }
    return result;
}

/// @brief 在基本块顶部（phi 之后）插入一条指令
void insertAtBlockTop(Instruction * inst, BasicBlock * bb)
{
    if (inst == nullptr || bb == nullptr) {
        return;
    }
    auto & insts = bb->getInstructions();
    auto insertPos = insts.begin();
    while (insertPos != insts.end() && dynamic_cast<PhiInst *>(*insertPos) != nullptr) {
        ++insertPos;
    }
    insts.insert(insertPos, inst);
    inst->setParentBlock(bb);
}

/// @brief 在放置块中、操作数定义之后插入指令（保持 SSA 支配关系）
///
/// 公共支配点可能正是操作数定义所在块：直接插到块顶会让新指令在定义之前
/// 使用操作数（如 transpose0 中公共支配块同时定义 select 与两个 GEP），
/// 寄存器分配会把两个重叠活跃区间分到同一物理寄存器，产生错误代码。
/// 操作数定义在本块时紧跟其定义插入；否则插到块顶（phi 之后）。
void insertAfterDominatingDef(Instruction * inst, Value * operand, BasicBlock * bb)
{
    if (inst == nullptr || bb == nullptr) {
        return;
    }
    auto & insts = bb->getInstructions();
    auto insertPos = insts.begin();
    while (insertPos != insts.end() && dynamic_cast<PhiInst *>(*insertPos) != nullptr) {
        ++insertPos;
    }

    if (auto * defInst = dynamic_cast<Instruction *>(operand)) {
        if (defInst->getParentBlock() == bb) {
            for (auto it = insertPos; it != insts.end(); ++it) {
                if (*it == defInst) {
                    ++it;
                    insertPos = it;
                    break;
                }
            }
        }
    }

    insts.insert(insertPos, inst);
    inst->setParentBlock(bb);
}

} // namespace

/// @brief 构造 LoopConstantPromotion
/// @param _func 待优化的函数
/// @param _mod 所属模块
LoopConstantPromotion::LoopConstantPromotion(Function * _func, Module * _mod) : func(_func), mod(_mod)
{}

/// @brief 执行循环常量提升
/// @return 若至少提升了一个常量则返回 true
bool LoopConstantPromotion::run()
{
    if (!func || !mod || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    auto & cache = func->getAnalysisCache();
    auto & domTree = cache.getOrCompute<DominatorTree>([this] { return DominatorTree(func); });
    auto & loopInfo =
        cache.getOrCompute<LoopInfo>([this, &domTree] { return LoopInfo(func, &domTree); });

    bool changed = false;

    // 分组提升阶段：热循环内重复物化的小常量 + GEP 缩放索引共享
    // （与下面的 preheader 大常量提升互补，公共支配点放置可覆盖无 preheader 的循环）
    if (std::getenv("MINIC_DISABLE_CONST_PROMOTE") == nullptr) {
        changed = promoteHotLoopMaterializedConstants(loopInfo, domTree) || changed;
    }
    if (std::getenv("MINIC_DISABLE_SCALED_SHARING") == nullptr) {
        changed = shareHotLoopScaledGEPIndices(loopInfo, domTree) || changed;
    }

    // 按深度降序排列（最内层优先），确保内层常量提升后外层也能受益
    std::vector<BasicBlock *> headers;
    for (auto * bb : func->getBlocks()) {
        if (loopInfo.isLoopHeader(bb)) {
            headers.push_back(bb);
        }
    }

    std::stable_sort(headers.begin(),
                     headers.end(),
                     [&loopInfo](BasicBlock * lhs, BasicBlock * rhs) {
                         return loopInfo.getLoopDepth(lhs) > loopInfo.getLoopDepth(rhs);
                     });

    for (auto * header : headers) {
        const auto * loopBody = loopInfo.getLoopBody(header);
        if (!loopBody || loopBody->empty()) {
            continue;
        }

        BasicBlock * preheader = findPreheader(header, *loopBody);
        if (!preheader) {
            continue;
        }

        // 第一遍扫描：统计循环体内每个可提升常量的出现次数，
        // 并记录被用作 div/mod 除数的常量——这类常量保持字面量才能让后端
        // 把除法/取模强度削减为魔数乘法（远比省一次重物化划算），不予提升
        std::unordered_map<Value *, int32_t> useCounts;
        std::unordered_set<Value *> divisorConstants;
        for (auto * bb : *loopBody) {
            for (auto * inst : bb->getInstructions()) {
                if (!inst || inst->isDead()) {
                    continue;
                }

                if (auto * binary = dynamic_cast<BinaryInst *>(inst)) {
                    const IRInstOperator op = binary->getOp();
                    if (op == IRInstOperator::IRINST_OP_DIV_I || op == IRInstOperator::IRINST_OP_MOD_I) {
                        if (dynamic_cast<ConstInteger *>(binary->getRHS()) != nullptr) {
                            divisorConstants.insert(binary->getRHS());
                        }
                    }
                }

                for (auto * operand : inst->getOperandsValue()) {
                    if (shouldPromoteConstant(operand)) {
                        ++useCounts[operand];
                    }
                }
            }
        }

        // 第二遍扫描：对出现足够次数的常量创建 preheader 物化指令并替换
        // 浮点常量即使只使用一次也值得提升——后端每次物化需要 lui+addiw+fmv.w.x 三条指令
        // 整数常量（尤其小立即数）只需一条 addi，阈值保持 >= 2
        for (auto & [constant, count] : useCounts) {
            bool isFloatConst = dynamic_cast<ConstFloat *>(constant) != nullptr;
            if (isFloatConst ? count < 1 : count < 2) {
                continue;
            }

            // 作 div/mod 除数的常量保持字面量，交给后端强度削减
            if (divisorConstants.count(constant) > 0) {
                continue;
            }

            // 创建物化指令：将常量"固化"为虚拟寄存器值
            // 创建物化指令：将常量"固化"为虚拟寄存器值，插入 preheader
            // 整数：shl const, 0 —— 左移零位等价于恒等操作
            // 浮点：fadd const, 0.0 —— 加零恒等（本 pass 在 late pass 中运行，
            //   ConstProp/InstCombine 均已收敛，不会被折叠回常量）
            Instruction * materialized = nullptr;
            if (dynamic_cast<ConstInteger *>(constant)) {
                materialized = new BinaryInst(func,
                                               IRInstOperator::IRINST_OP_SHL_I,
                                               constant,
                                               mod->newConstInteger(constant->getType(), 0),
                                               constant->getType());
            } else {
                auto * zero = mod->newConstFloat(0.0f);
                materialized = new BinaryInst(func,
                                               IRInstOperator::IRINST_OP_ADD_F,
                                               constant,
                                               zero,
                                               constant->getType());
            }

            if (!materialized) {
                continue;
            }

            insertBeforeTerminator(materialized, preheader);

            // 替换循环体内所有使用
            for (auto * bb : *loopBody) {
                for (auto * inst : bb->getInstructions()) {
                    if (!inst || inst->isDead()) {
                        continue;
                    }

                    for (int32_t idx = 0; idx < inst->getOperandsNum(); ++idx) {
                        if (inst->getOperand(idx) == constant) {
                            inst->setOperand(idx, materialized);
                        }
                    }
                }
            }

            changed = true;
        }
    }

    if (changed) {
        // 本 pass 替换了部分常量使用，但不改变 CFG，仅使值相关分析失效
        func->getAnalysisCache().invalidateValueAnalyses();
    }

    return changed;
}

/// @brief 热循环（深度>=2）内重复物化的小常量提升到公共支配点
///
/// 后端对 ConstInteger 使用点逐次 load_imm，即使只有一条 li 的小常量，
/// 在嵌套循环里也会每迭代重复物化。仅当「所有热循环使用都需要完整物化」
/// （即后端在该位置没有立即数形式）才提升，避免把 addi/slti 等立即数
/// 形式退化成寄存器操作。
bool LoopConstantPromotion::promoteHotLoopMaterializedConstants(LoopInfo & loopInfo, DominatorTree & domTree)
{
    struct ConstantUse {
        Instruction * inst = nullptr;
        int32_t operandIdx = -1;
    };

    std::unordered_map<PromotionGroupKey, std::vector<ConstantUse>, PromotionGroupKeyHash> groups;

    for (auto * bb : func->getBlocks()) {
        if (loopInfo.getLoopDepth(bb) < 2) {
            continue;
        }
        for (auto * inst : bb->getInstructions()) {
            if (inst == nullptr || inst->isDead()) {
                continue;
            }
            for (int32_t idx = 0; idx < inst->getOperandsNum(); ++idx) {
                auto * constant = dynamic_cast<ConstInteger *>(inst->getOperand(idx));
                // 常量 0 可直接读 x0 寄存器，无需物化，提升只会把零成本形式
                // 退化成寄存器形式
                if (constant == nullptr || constant->getVal() == 0 ||
                    !useRequiresMaterialization(inst, idx, constant->getVal())) {
                    continue;
                }
                PromotionGroupKey key;
                key.isConst = true;
                key.constVal = constant->getVal();
                groups[key].push_back({inst, idx});
            }
        }
    }

    bool changed = false;
    for (auto & [key, uses] : groups) {
        if (uses.size() < 2) {
            continue;
        }

        std::vector<BasicBlock *> useBlocks;
        for (const auto & use : uses) {
            if (use.inst == nullptr || use.inst->getParentBlock() == nullptr) {
                continue;
            }
            useBlocks.push_back(use.inst->getParentBlock());
        }
        if (useBlocks.size() < 2) {
            continue;
        }

        BasicBlock * placement = commonDominator(useBlocks, domTree);
        if (placement == nullptr) {
            continue;
        }

        // 物化：shl C, 0（本 pass 在 late 阶段运行，不会被常量折叠还原）
        auto * constant = dynamic_cast<ConstInteger *>(uses.front().inst->getOperand(uses.front().operandIdx));
        if (constant == nullptr) {
            continue;
        }
        auto * materialized = new BinaryInst(func,
                                             IRInstOperator::IRINST_OP_SHL_I,
                                             constant,
                                             mod->newConstInteger(constant->getType(), 0),
                                             constant->getType());
        insertAtBlockTop(materialized, placement);

        for (const auto & use : uses) {
            use.inst->setOperand(use.operandIdx, materialized);
        }
        changed = true;
    }
    return changed;
}

/// @brief 热循环内同索引 GEP 的缩放共享
///
/// 后端对动态下标 GEP 逐次发射 slli idx, k 再 add。对深度 >= 2 循环内
/// （下标, elemSize）相同的多个 GEP，把 slli 合并为公共支配点上的一条
/// shl，GEP 就地改写为「索引已缩放」形态（preScaled），后端只剩 add。
bool LoopConstantPromotion::shareHotLoopScaledGEPIndices(LoopInfo & loopInfo, DominatorTree & domTree)
{
    std::unordered_map<PromotionGroupKey, std::vector<GetElementPtrInst *>, PromotionGroupKeyHash> groups;

    for (auto * bb : func->getBlocks()) {
        if (loopInfo.getLoopDepth(bb) < 2) {
            continue;
        }
        for (auto * inst : bb->getInstructions()) {
            auto * gep = dynamic_cast<GetElementPtrInst *>(inst);
            if (gep == nullptr || gep->isDead() || gep->isIndexPreScaled()) {
                continue;
            }
            const int32_t k = gepScaleLog2(gep);
            if (k < 1) {
                continue;
            }

            Value * index = gep->getIndexOperand();
            PromotionGroupKey key;
            if (auto * constant = dynamic_cast<ConstInteger *>(index)) {
                // 常量索引缩放后若落在 addi 立即数范围，后端本就折叠为单条
                // addi，共享反而把立即数形式退化成寄存器形式；只共享需要
                // lui+addiw 物化的大偏移
                const int64_t scaledOffset = static_cast<int64_t>(constant->getVal()) << k;
                if (scaledOffset >= -2048 && scaledOffset <= 2047) {
                    continue;
                }
                key.isConst = true;
                key.constVal = constant->getVal();
            } else {
                key.value = index;
            }
            key.scale = k;
            groups[key].push_back(gep);
        }
    }

    bool changed = false;
    for (auto & [key, geps] : groups) {
        if (geps.size() < 2) {
            continue;
        }

        std::vector<BasicBlock *> gepBlocks;
        for (auto * gep : geps) {
            if (gep == nullptr || gep->getParentBlock() == nullptr) {
                continue;
            }
            gepBlocks.push_back(gep->getParentBlock());
        }
        if (gepBlocks.size() < 2) {
            continue;
        }

        BasicBlock * placement = commonDominator(gepBlocks, domTree);
        if (placement == nullptr) {
            continue;
        }

        Value * index = geps.front()->getIndexOperand();
        auto * scaled = new BinaryInst(func,
                                       IRInstOperator::IRINST_OP_SHL_I,
                                       index,
                                       mod->newConstInteger(IntegerType::getTypeInt32(), key.scale),
                                       IntegerType::getTypeInt32());
        // 放置点必须被 index 的定义支配：公共支配块可能同时定义 index
        // （如 select 与两个 GEP 同块），此时须紧跟定义插入
        insertAfterDominatingDef(scaled, index, placement);

        for (auto * gep : geps) {
            gep->setOperand(1, scaled);
            gep->setIndexPreScaled(true);
        }
        changed = true;
    }
    return changed;
}
