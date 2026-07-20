///
/// @file Rematerialization.h
/// @brief 后端可重物化值的静态判定
///
#pragma once

class Value;

namespace RiscV64Rematerialization {

/// @brief 不依赖具体寄存器分配结果的廉价重物化判定。
///
/// 仅接受常量、全局/栈对象地址、以这些地址为根的简单 GEP，以及由廉价值组成的
/// add/shl 链。该谓词用于寄存器分配阶段的权重折扣和 remat-only 溢出识别，必须
/// 比指令选择阶段的动态重物化判定更保守。
bool isCheapRematerializable(Value * val, int depth = 0);

/// @brief 判断值是否为“纯常量下标 GEP 链且根为全局/栈对象地址”。
///
/// 指针步进改写（如 reducePointerIVOffsetGEP）会产出任意深度的 GEP(prev, 常量)
/// 链。整链可折叠为 根地址+总偏移，重物化只需 lea 加一条 addi，代价与链长
/// 无关，因此该形态不受一般重物化的 depth 限制，也无需操作数寄存器可用性检查。
bool isConstOffsetChainFromMaterializableRoot(Value * val);

} // namespace RiscV64Rematerialization
