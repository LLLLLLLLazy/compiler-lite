///
/// @file ScalarEvolution.h
/// @brief 保守的标量演化分析
///
/// 当前实现覆盖四类表达式
/// 1. 常量整数
/// 2. 未知值
/// 3. 加法/乘法组成的简单仿射表达式
/// 4. 形如 {start,+,step}<loop> 的整数/指针加法递归
///
/// 并在此基础上提供规范计数循环匹配接口
/// 供循环优化 pass 复用归纳变量与 trip count 识别逻辑
///

#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class BasicBlock;
class CondBranchInst;
class ConstInteger;
class DominatorTree;
class Function;
class ICmpInst;
class Instruction;
class LoopInfo;
class PhiInst;
class Type;
class Value;

class ScalarEvolution {

public:
    enum class ExprKind {
        Constant,
        Unknown,
        Add,
        Multiply,
        AddRecurrence,
    };

    /// @brief 标量演化表达式基类
    class Expr {

    public:
        Expr(ExprKind kind, Type * type);
        virtual ~Expr() = default;

        ExprKind getKind() const
        {
            return kind;
        }

        Type * getType() const
        {
            return type;
        }

    private:
        ExprKind kind;
        Type * type = nullptr;
    };

    /// @brief 整数常量表达式
    class ConstantExpr final : public Expr {

    public:
        ConstantExpr(Type * type, int32_t intValue, ConstInteger * sourceValue = nullptr);

        int32_t getIntValue() const
        {
            return intValue;
        }

        ConstInteger * getSourceValue() const
        {
            return sourceValue;
        }

    private:
        int32_t intValue = 0;
        ConstInteger * sourceValue = nullptr;
    };

    /// @brief 简单双操作数表达式基类
    class BinaryExpr : public Expr {

    public:
        const Expr * getLHS() const
        {
            return lhs;
        }

        const Expr * getRHS() const
        {
            return rhs;
        }

    protected:
        BinaryExpr(ExprKind kind, Type * type, const Expr * lhs, const Expr * rhs);

    private:
        const Expr * lhs = nullptr;
        const Expr * rhs = nullptr;
    };

    /// @brief 整数加法表达式
    class AddExpr final : public BinaryExpr {

    public:
        AddExpr(Type * type, const Expr * lhs, const Expr * rhs);
    };

    /// @brief 整数乘法表达式
    class MultiplyExpr final : public BinaryExpr {

    public:
        MultiplyExpr(Type * type, const Expr * lhs, const Expr * rhs);
    };

    /// @brief 未建模值表达式
    class UnknownExpr final : public Expr {

    public:
        explicit UnknownExpr(Value * value);

        Value * getValue() const
        {
            return value;
        }

    private:
        Value * value = nullptr;
    };

    /// @brief 形如 {start,+,step}<loop> 的加法递归表达式
    ///
    /// 对直接来自 phi 的递归，startValue/backEdgeValue 对应真实 IR 值；
    /// 对由 affine 变换提升出的派生递归，startValue/backEdgeValue 可能为空，
    /// 此时应优先使用 startExpr 和 step 重新物化起始项。
    class AddRecurrenceExpr final : public Expr {

    public:
        enum class StepKind {
            Integer,
            Pointer,
        };

        AddRecurrenceExpr(PhiInst * phi,
                          Value * startValue,
                          const Expr * startExpr,
                          Value * backEdgeValue,
                          int32_t step,
                          StepKind stepKind,
                          BasicBlock * loopHeader,
                          BasicBlock * preheader,
                          BasicBlock * latch);

        PhiInst * getPhi() const
        {
            return phi;
        }

        Value * getStartValue() const
        {
            return startValue;
        }

        const Expr * getStartExpr() const
        {
            return startExpr;
        }

        Value * getBackEdgeValue() const
        {
            return backEdgeValue;
        }

        int32_t getStep() const
        {
            return step;
        }

        StepKind getStepKind() const
        {
            return stepKind;
        }

        bool isIntegerRecurrence() const
        {
            return stepKind == StepKind::Integer;
        }

        bool isPointerRecurrence() const
        {
            return stepKind == StepKind::Pointer;
        }

        BasicBlock * getLoopHeader() const
        {
            return loopHeader;
        }

        BasicBlock * getPreheader() const
        {
            return preheader;
        }

        BasicBlock * getLatch() const
        {
            return latch;
        }

    private:
        PhiInst * phi = nullptr;
        Value * startValue = nullptr;
        const Expr * startExpr = nullptr;
        Value * backEdgeValue = nullptr;
        int32_t step = 0;
        StepKind stepKind = StepKind::Integer;
        BasicBlock * loopHeader = nullptr;
        BasicBlock * preheader = nullptr;
        BasicBlock * latch = nullptr;
    };

    /// @brief 规范计数循环的结构化匹配结果
    struct CanonicalLoop {
        BasicBlock * header = nullptr;
        BasicBlock * preheader = nullptr;
        BasicBlock * body = nullptr;
        BasicBlock * latch = nullptr;
        BasicBlock * exit = nullptr;
        PhiInst * induction = nullptr;
        const AddRecurrenceExpr * recurrence = nullptr;
        ICmpInst * cmp = nullptr;
        CondBranchInst * branch = nullptr;
        ConstInteger * bound = nullptr;
        Value * initialValue = nullptr;
        int32_t initialIntValue = 0;
        bool hasConstInitialValue = false;
        int32_t tripCount = 0;
    };

    /// @brief 构造时绑定函数及所需循环分析依赖
    /// @param func 待分析函数
    /// @param domTree 已构建的支配树
    /// @param loopInfo 可复用的循环信息 若为空则按需构造
    ScalarEvolution(Function * func, DominatorTree * domTree, LoopInfo * loopInfo = nullptr);

    /// @brief 获取某个值对应的 SCEV 表达式
    /// @param value 待分析值
    /// @return 分析结果 若无法建模则返回 UnknownExpr
    const Expr * getSCEV(Value * value);

    /// @brief 获取某个值是否是加法递归表达式
    /// @param value 待分析值
    /// @return 若是 {start,+,step}<loop> 则返回对应表达式 否则返回 nullptr
    const AddRecurrenceExpr * getAddRecurrence(Value * value);

    /// @brief 匹配形如 for(i = const; i < const; i += const) 的规范计数循环
    /// @param header 循环头
    /// @param loop 输出匹配结果
    /// @return 匹配成功返回 true
    bool matchCanonicalLoop(BasicBlock * header, CanonicalLoop & loop);

    /// @brief 判断值是否依赖给定循环的递推变量
    /// @param value 待判断值
    /// @param loopHeader 目标循环头
    /// @return 若值表达式中包含该循环的 add recurrence 则返回 true
    bool dependsOnLoop(Value * value, BasicBlock * loopHeader);

private:
    LoopInfo * ensureLoopInfo();
    const Expr * analyzeValue(Value * value);
    const Expr * analyzeBinary(class BinaryInst * binary);
    const UnknownExpr * createUnknown(Value * value);
    const ConstantExpr * createConstant(Type * type, int32_t intValue, ConstInteger * sourceValue = nullptr);
    const Expr * createAdd(Type * type, const Expr * lhs, const Expr * rhs);
    const Expr * createMultiply(Type * type, const Expr * lhs, const Expr * rhs);
    const AddRecurrenceExpr * createAddRecurrence(PhiInst * phi,
                                                  Value * startValue,
                                                  const Expr * startExpr,
                                                  Value * backEdgeValue,
                                                  int32_t step,
                                                  AddRecurrenceExpr::StepKind stepKind,
                                                  BasicBlock * loopHeader,
                                                  BasicBlock * preheader,
                                                  BasicBlock * latch);
    /// @brief 尝试将 addrec 与循环不变量相加提升为新的 add recurrence
    const Expr * tryCreateAffineAddRecurrenceForAdd(Type * type, const Expr * lhs, const Expr * rhs);
    /// @brief 尝试将整数 addrec 按常量倍数缩放为新的 add recurrence
    const Expr * tryCreateAffineAddRecurrenceForMultiply(Type * type, const Expr * lhs, const Expr * rhs);

    bool matchAddRecurrence(PhiInst * phi,
                            BasicBlock * loopHeader,
                            const std::unordered_set<BasicBlock *> & loopBody,
                            Value *& startValue,
                            Value *& backEdgeValue,
                            int32_t & step,
                            AddRecurrenceExpr::StepKind & stepKind,
                            BasicBlock *& preheader,
                            BasicBlock *& latch) const;
    bool findUniqueLoopEntryAndLatch(BasicBlock * loopHeader,
                                     const std::unordered_set<BasicBlock *> & loopBody,
                                     BasicBlock *& preheader,
                                     BasicBlock *& latch) const;
    bool matchConstStep(Value * value,
                        PhiInst * phi,
                        int32_t & step,
                        AddRecurrenceExpr::StepKind & stepKind) const;
    bool hasSingleBranchTo(BasicBlock * bb, BasicBlock * target) const;
    /// @brief 判断表达式是否不依赖指定循环的递推变量
    bool isLoopInvariantExpr(const Expr * expr, BasicBlock * loopHeader);
    bool tryEvaluateConstantInt(const Expr * expr, int32_t & constant) const;
    bool dependsOnLoopExpr(const Expr * expr,
                           BasicBlock * loopHeader,
                           std::unordered_set<const Expr *> & exprVisiting,
                           std::unordered_set<Value *> & valueVisiting);
    bool dependsOnLoopValue(Value * value,
                            BasicBlock * loopHeader,
                            std::unordered_set<const Expr *> & exprVisiting,
                            std::unordered_set<Value *> & valueVisiting);
    /// @brief 返回表达式可直接复用的现有 SSA 值 若不存在则返回 nullptr
    Value * getRepresentativeValue(const Expr * expr) const;
    int32_t computeTripCount(int32_t initialValue, int32_t bound, int32_t step) const;

    Function * func = nullptr;
    DominatorTree * domTree = nullptr;
    LoopInfo * loopInfo = nullptr;
    std::unique_ptr<LoopInfo> ownedLoopInfo;
    std::unordered_map<Value *, const Expr *> exprCache;
    std::vector<std::unique_ptr<Expr>> exprArena;
};