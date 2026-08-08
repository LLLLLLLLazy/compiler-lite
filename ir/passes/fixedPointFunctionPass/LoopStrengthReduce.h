///
/// @file LoopStrengthReduce.h
/// @brief 循环地址强度削减 pass
///

#pragma once

#include <cstdint>
#include <unordered_set>

class BasicBlock;
class Function;
class Instruction;
class Module;

class LoopStrengthReduce {

public:
    LoopStrengthReduce(Function * func, Module * mod);

    /// @brief 将循环内 gep(base, affine(i_phi)) 改写为 pointer phi + ptr += stride
    /// @return 若修改了 IR 则返回 true
    bool run();

private:
    bool tryReduceHeader(BasicBlock * header);
    /// @brief 选择一组共享同一 affine index 值的 GEP 并改写为 pointer recurrence
    bool reduceFirstCandidate(BasicBlock * header,
                              BasicBlock * preheader,
                              BasicBlock * latch,
                              class ScalarEvolution & scev,
                              const std::unordered_set<BasicBlock *> & loopBody);
    /// @brief 将 gep(指针IV基址, 0, 循环不变索引) 改写为折入不变列偏移的新指针 recurrence
    bool reducePointerIVOffsetGEP(BasicBlock * header,
                                  BasicBlock * preheader,
                                  BasicBlock * latch,
                                  class ScalarEvolution & scev,
                                  const std::unordered_set<BasicBlock *> & loopBody);
    /// @brief 将 gep(base, IV*S + C)（S/C 循环不变，步长为运行期值）的下标改写为
    ///        每步累加 step*S 的 i32 递推，消去每迭代一次的乘法
    ///        （停留在 i32 宽度，回绕语义与原式逐迭代一致）
    bool reduceInvariantStrideGEP(BasicBlock * header,
                                  BasicBlock * preheader,
                                  BasicBlock * latch,
                                  const std::unordered_set<BasicBlock *> & loopBody);
    /// @brief 为不变量步长下标递推构造两级运行期无回绕检查 + 快慢双版本循环：
    ///        preheader 先以行程门槛筛掉小循环，checkBlock 再用 i32 算术证明
    ///        整段下标序列不出 i32 值域，成立走 64 位指针递推快路径，否则走
    ///        i32 下标递推慢路径。仅版本化 ≤3 块的最内层小循环。
    ///        两条路径在二进制补码回绕语义下均逐点精确，不利用有符号溢出未定义行为。
    /// @return 版本化成功返回原循环新的环外前驱（slowPre，调用方为 header 新增
    ///         phi 时须以其为入边块），放弃版本化返回空指针
    BasicBlock * tryVersionInvariantStrideLoop(BasicBlock * header,
                                               BasicBlock * preheader,
                                               BasicBlock * latch,
                                               const std::unordered_set<BasicBlock *> & loopBody,
                                               class GetElementPtrInst * seed,
                                               class PhiInst * ivPhi,
                                               class Value * ivInit,
                                               int32_t ivStep,
                                               class Value * initIdx,
                                               class Value * stepIdx);
    /// @brief 匹配"上界为 min(外层IV[+1], B0) 钳制、initIdx 即外层 IV"的嵌套形态，
    ///        把无回绕检查按全嵌套上界 (C0-1)+(B0-1)·S 伸缩后整体提升到外层循环
    ///        之外，行内选路只剩一条对提升 i1 的分支；不匹配返回空由两级检查兜底
    class Value * tryBuildHoistedNestCheck(BasicBlock * preheader,
                                           class Value * bound,
                                           class Value * initIdx,
                                           class Value * stepIdx,
                                           class Type * i1Type,
                                           class Type * i32Type);
    bool sweepDeadInstructions() const;

    Function * func = nullptr;
    Module * mod = nullptr;
    /// @brief 本轮 LSR 中已经完成版本化的原循环头与快路径克隆头。
    ///        后续动态步长 GEP 仍做精确 i32 递推，但不再复制同一逻辑循环。
    std::unordered_set<BasicBlock *> versionedLoopHeaders;
};
