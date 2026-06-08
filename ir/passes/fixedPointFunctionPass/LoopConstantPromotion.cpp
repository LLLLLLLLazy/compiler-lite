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
#include <vector>

#include "BasicBlock.h"
#include "BinaryInst.h"
#include "ConstFloat.h"
#include "ConstInteger.h"
#include "DominatorTree.h"
#include "Function.h"
#include "AnalysisCache.h"
#include "Instruction.h"
#include "LoopInfo.h"
#include "Module.h"
#include "Value.h"

namespace {

/// @brief 整数常量绝对值超过此阈值时需要 lui+addiw 物化
constexpr int32_t kLargeImmThreshold = 2047;

/// @brief 判断常量是否值得提升到循环外
///
/// 对整数常量仅提升超出 12 位有符号立即数范围的（需要 lui+addiw）；
/// 对浮点常量提升非零值（+0.0f 可直接从 x0 加载，无需物化）。
/// @param value 待判断的值
/// @return true 表示该常量值得提升
bool shouldPromoteConstant(Value * value)
{
    if (auto * constInt = dynamic_cast<ConstInteger *>(value)) {
        const int32_t val = constInt->getVal();
        return val > kLargeImmThreshold || val < -kLargeImmThreshold;
    }

    if (auto * constFloat = dynamic_cast<ConstFloat *>(value)) {
        // +0.0f 可通过 fmv.w.x rd, x0 直接获取，不值得提升
        return constFloat->getBitPattern() != 0U;
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

    bool changed = false;

    for (auto * header : headers) {
        const auto * loopBody = loopInfo.getLoopBody(header);
        if (!loopBody || loopBody->empty()) {
            continue;
        }

        BasicBlock * preheader = findPreheader(header, *loopBody);
        if (!preheader) {
            continue;
        }

        // 第一遍扫描：统计循环体内每个可提升常量的出现次数
        std::unordered_map<Value *, int32_t> useCounts;
        for (auto * bb : *loopBody) {
            for (auto * inst : bb->getInstructions()) {
                if (!inst || inst->isDead()) {
                    continue;
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
