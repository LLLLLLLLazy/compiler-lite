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

} // namespace RiscV64Rematerialization
