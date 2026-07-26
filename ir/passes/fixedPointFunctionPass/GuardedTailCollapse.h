///
/// @file GuardedTailCollapse.h
/// @brief 单调守卫循环的空转尾部折叠 pass
///
/// 针对如下惯用法（源码形如 `if (i < j) { j = j + 1; continue; }`）：
/// 计数循环体内唯一的实际工作被一个关于归纳变量的单调谓词守卫，
/// 谓词一旦成立便永远成立（守卫不变量 inv 循环不变、归纳变量严格递增），
/// 其后所有迭代都只执行"递增 + 回边"的纯空转。
///
/// 本 pass 把循环上界钳制为 min(原上界, 工作区间上限)，使空转尾部迭代物理上
/// 不再执行；随后守卫在环内恒为工作方向，条件跳转被改写为无条件跳转，空转
/// 路径块被整体删除，循环因此收敛为单 latch 形态，可继续被 LoopStrengthReduce
/// 等下游 pass 处理。等价于 GCC -fsplit-loops 在"后半为空循环"情形下的特例。
///
/// 整个变换在二进制补码回绕语义下逐点等价，不依赖"有符号运算不溢出"这一
/// 未定义行为假设，两处可能回绕的算术都被显式排除：
///   1. 上界 min(Bound, inv+1) 用 select 表达，选中 inv+1 的臂以 inv < Bound
///      为条件，此时 inv+1 ≤ INT32_MAX；另一臂取 Bound 本身即精确结果。
///   2. "进入空转区间后不再回到工作区间"要求空转步进 IV+s 不越过 INT32_MAX。
///      空转迭代满足 IV < Bound ≤ INT32_MAX，故 s == 1 时恒成立；s ≥ 2 时仅在
///      Bound 为常量且 Bound ≤ INT32_MAX-s+1 时才折叠，否则放弃。
///

#pragma once

class BasicBlock;
class Function;
class LoopInfo;
class Module;

class GuardedTailCollapse {

public:
    /// @brief 构造守卫尾部折叠 pass
    /// @param func 待优化函数
    /// @param mod 所属模块
    GuardedTailCollapse(Function * func, Module * mod);

    /// @brief 对函数内所有匹配循环执行守卫尾部折叠
    /// @return true 表示 IR 发生变化
    bool run();

private:
    /// @brief 尝试折叠以 header 为头的循环的空转尾部
    /// @param header 循环头基本块
    /// @param loopInfo 当前函数的循环信息（由调用方构建并复用）
    /// @return true 表示该循环被改写
    bool tryCollapseLoop(BasicBlock * header, const LoopInfo & loopInfo);

    Function * func = nullptr;
    Module * mod = nullptr;
};
