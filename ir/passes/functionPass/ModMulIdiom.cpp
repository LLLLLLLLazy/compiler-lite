///
/// @file ModMulIdiom.cpp
/// @brief 递归倍加模乘惯用法识别 pass 实现
///
/// 匹配的 SSA 形态（Mem2Reg 后、GVN/InstCombine 前）：
///   entry: br (icmp eq b,0) retZero, bb4
///   retZero: ret 0
///   bb4:   br (icmp eq b,1) retAmod, bb8
///   retAmod: ret (srem a, m)
///   bb8:   q=sdiv b,2; rec=call self(a,q); dbl=add rec,rec; cur=srem dbl,m;
///          r=srem b,2; br (icmp eq r,1) retOdd, retEven
///   retOdd:  ret srem(add(cur,a), m)
///   retEven: ret cur
/// 该结构按归纳法等于 (a*b) % m（b>=0）。改写：入口插入守卫
///   (a|b)>=0 且 a<m  ->  fast: ret mulmod(a,b,m)；否则 -> 原递归（slow）
/// 守卫区内 0<=a<m、b>=0，原递归无 32 位溢出且严格等于 (a*b)%m，故任意输入语义不变
///

#include "ModMulIdiom.h"

#include <cstdint>
#include <list>
#include <optional>
#include <vector>

#include "AnalysisCache.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "CallInst.h"
#include "CondBranchInst.h"
#include "ConstInteger.h"
#include "Function.h"
#include "ICmpInst.h"
#include "IntegerType.h"
#include "Instruction.h"
#include "Module.h"
#include "MulModInst.h"
#include "PhiInst.h"
#include "ReturnInst.h"
#include "Value.h"

namespace {

/// @brief 取常量整数值
std::optional<int32_t> asConstInt(Value * value)
{
    if (auto * constInt = dynamic_cast<ConstInteger *>(value)) {
        return constInt->getVal();
    }
    return std::nullopt;
}

/// @brief 判断 value 是否为常量整数 k
bool isConstVal(Value * value, int32_t k)
{
    auto v = asConstInt(value);
    return v && *v == k;
}

/// @brief 把 value 视为指定操作码的二元指令，匹配返回该指令否则返回 nullptr
BinaryInst * asBinary(Value * value, IRInstOperator op)
{
    auto * binary = dynamic_cast<BinaryInst *>(value);
    if (binary && binary->getOp() == op) {
        return binary;
    }
    return nullptr;
}

/// @brief 取基本块终结返回指令返回的值，块不以 ret 结尾时返回 nullptr
Value * returnValueOf(BasicBlock * bb)
{
    if (!bb) {
        return nullptr;
    }
    auto * ret = dynamic_cast<ReturnInst *>(bb->getTerminator());
    return ret ? ret->getReturnValue() : nullptr;
}

/// @brief 判断块是否为 "ret 常量 k"
bool isReturnConst(BasicBlock * bb, int32_t k)
{
    Value * v = returnValueOf(bb);
    return v && isConstVal(v, k);
}

/// @brief 取块终结的条件跳转（条件须为 icmp eq(x, k)），输出比较的左值与两个目标
/// @return 匹配成功返回 true
bool matchEqBranch(BasicBlock * bb, int32_t k, Value *& cmpLhs, BasicBlock *& trueDest, BasicBlock *& falseDest)
{
    if (!bb) {
        return false;
    }
    auto * branch = dynamic_cast<CondBranchInst *>(bb->getTerminator());
    if (!branch) {
        return false;
    }
    auto * cmp = dynamic_cast<ICmpInst *>(branch->getCondition());
    if (!cmp || cmp->getOp() != IRInstOperator::IRINST_OP_EQ_I || !isConstVal(cmp->getRHS(), k)) {
        return false;
    }
    cmpLhs = cmp->getLHS();
    trueDest = branch->getTrueDest();
    falseDest = branch->getFalseDest();
    return true;
}

/// @brief 判断类型是否为 i32
bool isInt32(Type * type)
{
    return type && type->isInt32Type();
}

/// @brief 识别函数是否为递归倍加模乘，成功时输出两形参与模数
/// @param func 目标函数
/// @param outA 输出被乘数形参
/// @param outB 输出乘数形参
/// @param outMod 输出模数（>0）
/// @return 匹配成功返回 true
bool matchModMul(Function * func, Value *& outA, Value *& outB, int32_t & outMod)
{
    auto & params = func->getParams();
    if (params.size() != 2 || !isInt32(func->getReturnType()) || !isInt32(params[0]->getType()) ||
        !isInt32(params[1]->getType())) {
        return false;
    }
    Value * a = params[0];
    Value * b = params[1];

    BasicBlock * entry = func->getEntryBlock();

    // entry: br (icmp eq b,0) retZero, bb4
    Value * eqLhs = nullptr;
    BasicBlock * retZero = nullptr;
    BasicBlock * bb4 = nullptr;
    if (!matchEqBranch(entry, 0, eqLhs, retZero, bb4) || eqLhs != b) {
        return false;
    }
    if (!isReturnConst(retZero, 0)) {
        return false;
    }

    // bb4: br (icmp eq b,1) retAmod, bb8
    Value * eq1Lhs = nullptr;
    BasicBlock * retAmod = nullptr;
    BasicBlock * bb8 = nullptr;
    if (!matchEqBranch(bb4, 1, eq1Lhs, retAmod, bb8) || eq1Lhs != b) {
        return false;
    }

    // retAmod: ret (srem a, m)，由此捕获模数 m
    auto * amod = asBinary(returnValueOf(retAmod), IRInstOperator::IRINST_OP_MOD_I);
    if (!amod || amod->getLHS() != a) {
        return false;
    }
    auto modConst = asConstInt(amod->getRHS());
    if (!modConst || *modConst <= 0) {
        return false;
    }
    const int32_t m = *modConst;

    // bb8: br (icmp eq (srem b,2),1) retOdd, retEven
    Value * remLhs = nullptr;
    BasicBlock * retOdd = nullptr;
    BasicBlock * retEven = nullptr;
    if (!matchEqBranch(bb8, 1, remLhs, retOdd, retEven)) {
        return false;
    }
    auto * bMod2 = asBinary(remLhs, IRInstOperator::IRINST_OP_MOD_I);
    if (!bMod2 || bMod2->getLHS() != b || !isConstVal(bMod2->getRHS(), 2)) {
        return false;
    }

    // retEven: ret cur，其中 cur = srem(add(rec,rec), m)，rec = call self(a, sdiv(b,2))
    Value * cur = returnValueOf(retEven);
    auto * curMod = asBinary(cur, IRInstOperator::IRINST_OP_MOD_I);
    if (!curMod || !isConstVal(curMod->getRHS(), m)) {
        return false;
    }
    auto * dbl = asBinary(curMod->getLHS(), IRInstOperator::IRINST_OP_ADD_I);
    if (!dbl || dbl->getLHS() != dbl->getRHS()) {
        return false;
    }
    auto * call = dynamic_cast<CallInst *>(dbl->getLHS());
    if (!call || call->getCallee() != func || call->getArgCount() != 2 || call->getArg(0) != a) {
        return false;
    }
    auto * half = asBinary(call->getArg(1), IRInstOperator::IRINST_OP_DIV_I);
    if (!half || half->getLHS() != b || !isConstVal(half->getRHS(), 2)) {
        return false;
    }

    // retOdd: ret srem(add(cur,a), m)
    auto * oddMod = asBinary(returnValueOf(retOdd), IRInstOperator::IRINST_OP_MOD_I);
    if (!oddMod || !isConstVal(oddMod->getRHS(), m)) {
        return false;
    }
    auto * sum = asBinary(oddMod->getLHS(), IRInstOperator::IRINST_OP_ADD_I);
    if (!sum || !((sum->getLHS() == cur && sum->getRHS() == a) || (sum->getLHS() == a && sum->getRHS() == cur))) {
        return false;
    }

    outA = a;
    outB = b;
    outMod = m;
    return true;
}

} // namespace

bool ModMulIdiom::run()
{
    if (!func || !mod || func->isBuiltin() || func->getBlocks().empty()) {
        return false;
    }

    Value * a = nullptr;
    Value * b = nullptr;
    int32_t m = 0;
    if (!matchModMul(func, a, b, m)) {
        return false;
    }

    Type * i32Type = IntegerType::getTypeInt32();
    Type * i1Type = IntegerType::getTypeInt1();
    BasicBlock * entry = func->getEntryBlock();

    // slowBB 承接原入口块的全部指令（原递归保持不变）
    BasicBlock * slowBB = func->newBasicBlock();
    std::list<Instruction *> moved(entry->getInstructions().begin(), entry->getInstructions().end());
    entry->getInstructions().clear();
    for (auto * inst : moved) {
        slowBB->addInstruction(inst);
    }

    // 原入口的后继（bb3/bb4）改由 slowBB 引出，更新前驱与可能的 phi 入边
    std::vector<BasicBlock *> oldSuccs(entry->getSuccessors().begin(), entry->getSuccessors().end());
    for (auto * succ : oldSuccs) {
        succ->removePredecessor(entry);
        succ->addPredecessor(slowBB);
        slowBB->addSuccessor(succ);
        for (auto * inst : succ->getInstructions()) {
            auto * phi = dynamic_cast<PhiInst *>(inst);
            if (!phi) {
                break;
            }
            phi->replaceIncomingBlock(entry, slowBB);
        }
    }
    entry->getSuccessors().clear();

    // checkBB: a<m ? fast : slow
    BasicBlock * checkBB = func->newBasicBlock();
    // fastBB: ret mulmod(a,b,m)
    BasicBlock * fastBB = func->newBasicBlock();

    // entry: orab=(a|b); (orab>=0) ? checkBB : slowBB —— 同时保证 a、b 均非负
    auto * orab = new BinaryInst(func, IRInstOperator::IRINST_OP_OR_I, a, b, i32Type);
    entry->addInstruction(orab);
    auto * geZero = new ICmpInst(func, IRInstOperator::IRINST_OP_GE_I, orab, mod->newConstInt32(0), i1Type);
    entry->addInstruction(geZero);
    entry->addInstruction(new CondBranchInst(func, geZero, checkBB, slowBB));
    entry->addSuccessor(checkBB);
    entry->addSuccessor(slowBB);
    checkBB->addPredecessor(entry);
    slowBB->addPredecessor(entry);

    // checkBB: (a < m) ? fastBB : slowBB
    auto * ltMod = new ICmpInst(func, IRInstOperator::IRINST_OP_LT_I, a, mod->newConstInt32(m), i1Type);
    checkBB->addInstruction(ltMod);
    checkBB->addInstruction(new CondBranchInst(func, ltMod, fastBB, slowBB));
    checkBB->addSuccessor(fastBB);
    checkBB->addSuccessor(slowBB);
    fastBB->addPredecessor(checkBB);
    slowBB->addPredecessor(checkBB);

    // fastBB: ret mulmod(a,b,m)
    auto * fast = new MulModInst(func, a, b, m);
    fastBB->addInstruction(fast);
    fastBB->addInstruction(new ReturnInst(func, fast));

    func->getAnalysisCache().invalidateCFGAnalyses();
    return true;
}
