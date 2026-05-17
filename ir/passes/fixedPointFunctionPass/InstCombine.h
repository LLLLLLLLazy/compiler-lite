///
/// @file InstCombine.h
/// @brief 本地模式化简 pass
///
/// 当前实现聚焦不依赖复杂分析的 SSA 局部化简，覆盖：
///   1. 整数/浮点恒等式模式
///   2. 常量 cast 折叠
///   3. 冗余 phi 折叠
///   4. 冗余 copy 转发与删除
///

#pragma once

#include <cstdint>

class BasicBlock;
class BinaryInst;
class CopyInst;
class FPToSIInst;
class Function;
class GetElementPtrInst;
class Instruction;
class Module;
class PhiInst;
class SIToFPInst;
class SelectInst;
class Value;
class ZExtInst;

class InstCombine {

public:
    InstCombine(Function * func, Module * mod);

    /// @brief 对函数原地执行本地模式化简
    /// @return 若本轮对 IR 做了修改则返回 true
    bool run();

private:
    /// @brief 尝试化简单条指令
    /// @param inst 待化简的指令
    /// @return 若成功化简则返回 true
    bool trySimplifyInstruction(Instruction * inst);

    /// @brief 消除同一基本块内重复的 GEP 地址计算
    /// @return 若至少删除一条 GEP 则返回 true
    bool eliminateRedundantGEPs();

    /// @brief 用现有值替换指令结果并删除旧指令
    /// @param inst 待替换的指令
    /// @param replacement 新值
    /// @return 替换成功时返回 true
    bool replaceInstWithValue(Instruction * inst, Value * replacement);

    /// @brief 化简整数/浮点二元指令
    /// @param inst 待化简的二元指令
    /// @return 若成功化简则返回 true
    bool simplifyBinary(BinaryInst * inst);

    /// @brief 化简冗余 phi
    /// @param phi 待化简的 phi 指令
    /// @return 若成功化简则返回 true
    bool simplifyPhi(PhiInst * phi);

    /// @brief 化简冗余 copy
    /// @param copy 待化简的 copy 指令
    /// @return 若成功化简则返回 true
    bool simplifyCopy(CopyInst * copy);

    /// @brief 折叠常量 zero-extend
    /// @param inst 待化简的 zext 指令
    /// @return 若成功化简则返回 true
    bool simplifyZExt(ZExtInst * inst);

    /// @brief 折叠常量 int-to-float cast
    /// @param inst 待化简的 sitofp 指令
    /// @return 若成功化简则返回 true
    bool simplifySIToFP(SIToFPInst * inst);

    /// @brief 化简 select 指令
    /// @param inst 待化简的 select 指令
    /// @return 若成功化简则返回 true
    bool simplifySelect(SelectInst * inst);

    /// @brief 折叠常量 float-to-int cast
    /// @param inst 待化简的 fptosi 指令
    /// @return 若成功化简则返回 true
    bool simplifyFPToSI(FPToSIInst * inst);

    /// @brief 清扫已标记为 dead 的指令
    /// @return 被真正移除的指令数量
    int32_t sweepDeadInstructions();

    Function * func = nullptr;
    Module * mod = nullptr;
};
