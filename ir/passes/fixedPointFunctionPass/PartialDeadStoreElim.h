///
/// @file PartialDeadStoreElim.h
/// @brief 部分死 store 消除（Partial Dead Store Elimination）
///
/// 识别「入口先做栈数组初始化（或其它仅写非逃逸 alloca 的纯代码），
/// 之后才判断提前返回条件」的通用模式：
///
/// ```text
/// entry: [alloca / store 序列] ... -> B: br cond -> retBB / contBB
/// ```
///
/// 提前返回路径上那些对非逃逸 alloca 的写入是不可观察的死 store。
/// 本 pass 把条件的纯计算（def-chain 闭包，含 || 短路产生的 phi 形态）
/// 复制到函数入口，在初始化之前直接决策提前返回；原检查在续行路径上
/// 恒不成立，被线程化为无条件跳转。变换后：
///
/// ```text
/// entry': [克隆的纯条件] -> 克隆决策 -> earlyRet / initBB
/// initBB: [原 entry 内容] ...（原检查已被线程化删除）
/// ```
///
/// 合法性条件（全部静态可验证）：
/// 1. 提前返回目标块只含 `ret void`；
/// 2. 条件 def-chain 纯化：无 load/call/phi 跨出闭包，叶子仅为参数/常量/全局地址；
/// 3. 入口到检查块之间所有可达块可整体跳过：仅写/读非逃逸 alloca + 纯计算 + 控制流，
///    无 call、无其它 side effect；
/// 4. 区域内至少有一条可跳过的 store（收益门槛，避免无谓克隆）。
///

#pragma once

class Function;

class PartialDeadStoreElim {
public:
    explicit PartialDeadStoreElim(Function * func);

    /// @brief 对函数执行一次变换（每次运行至多变换一个候选块）
    /// @return 若本轮修改了 IR 则返回 true
    bool run();

private:
    Function * func;
};
