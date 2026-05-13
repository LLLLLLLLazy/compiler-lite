///
/// @file IRGenerator.h
/// @brief AST 遍历产生块结构 IR 的头文件
///

#pragma once

#include <list>
#include <unordered_map>
#include <vector>

#include "AST.h"
#include "Instruction.h"
#include "Module.h"

class AllocaInst;
class BasicBlock;
class Value;
class Type;

class IRGenerator {

public:
    /// @brief 构造 AST 到结构化 IR 的生成器
    IRGenerator(ast_node * root, Module * _module);

    /// @brief 析构函数
    ~IRGenerator() = default;

    /// @brief 执行 AST 到结构化 IR 的生成流程
    bool run();

private:
    /// @brief 预声明编译单元中的函数符号
    bool declareCompileUnit(ast_node * node);

    /// @brief 预收集编译单元顶层的标量 const 绑定
    bool precollectGlobalConstBindings(ast_node * node);

    /// @brief 遍历编译单元并生成全局对象与函数体 IR
    bool visitCompileUnit(ast_node * node);

    /// @brief 生成单条语句对应的 IR
    bool visitStatement(ast_node * node);

    /// @brief 生成函数定义对应的 IR
    bool visitFuncDef(ast_node * node);

    /// @brief 生成语句块对应的 IR
    bool visitBlock(ast_node * node);

    /// @brief 生成返回语句对应的 IR
    bool visitReturn(ast_node * node);

    /// @brief 生成条件语句对应的 IR
    bool visitIf(ast_node * node);

    /// @brief 生成 while 循环对应的 IR
    bool visitWhile(ast_node * node);

    /// @brief 生成 for 循环对应的 IR
    bool visitFor(ast_node * node);

    /// @brief 生成 break 语句对应的 IR
    bool visitBreak(ast_node * node);

    /// @brief 生成 continue 语句对应的 IR
    bool visitContinue(ast_node * node);

    /// @brief 生成赋值语句对应的 IR
    bool visitAssign(ast_node * node);

    /// @brief 生成声明语句对应的 IR
    bool visitDeclStmt(ast_node * node);

    /// @brief 生成单个变量声明对应的 IR
    bool visitVarDecl(ast_node * node);

    /// @brief 生成局部 static 变量对应的全局存储
    bool visitStaticLocalVarDecl(ast_node * node);

    /// @brief 生成左值对应的地址
    Value * visitLValueAddress(ast_node * node);

    /// @brief 生成表达式并返回结果值
    Value * visitExpr(ast_node * node);

    /// @brief 生成函数调用表达式并返回结果值
    Value * visitFuncCall(ast_node * node);

    /// @brief 将字符串字面量具象化为匿名全局对象地址
    Value * materializeStringLiteral(ast_node * node);

    /// @brief 生成变量访问表达式并返回加载后的值
    Value * visitLeafVarId(ast_node * node);

    /// @brief 预扫描全局 const 声明，在函数预声明前填充 constBindings
    void prePopulateGlobalConstBindings(ast_node * node);

    /// @brief 获取声明节点的最终类型
    Type * buildDeclaredType(ast_node * declNode, bool forParam = false);

    /// @brief 校验初始化器是否满足编译期常量约束
    bool isConstInitializer(Type * type, ast_node * initNode);

    /// @brief 计算整型常量表达式
    bool evaluateConstIntExpr(ast_node * node, int32_t & result);

    /// @brief 计算用于整型初始化的常量表达式，允许数值常量到 int 的编译期转换
    bool evaluateConstIntInitializerExpr(ast_node * node, int32_t & result);

    /// @brief 计算数组维度所需的整型常量表达式
    bool evaluateConstArrayBoundExpr(ast_node * node, int32_t & result);

    /// @brief 计算数值型常量表达式（int/float 混合）
    bool evaluateConstNumberExpr(ast_node * node, double & result);

    /// @brief 获取变量对象的地址（全局变量或局部栈槽）
    Value * getAddressOfVariable(Value * var);

    /// @brief 生成数组/指针元素地址
    Value * emitGEP(Value * basePtr, Value * index, bool decayArray);

    /// @brief 按 C 初始化规则对地址执行初始化
    bool emitInitializer(Value * addr, Type * type, ast_node * initNode);

    /// @brief 将聚合初始化列表写入数组对象
    bool emitArrayInitializer(
        Value * addr, Type * type, const std::vector<ast_node *> & items, std::size_t begin, std::size_t end);

    /// @brief 将对象全部清零
    bool emitZeroInitializer(Value * addr, Type * type);

    /// @brief 用扁平循环对大数组执行运行时清零
    bool emitFlatLoopZeroInitializer(Value * addr, Type * type);

    /// @brief 将全局数组的常量初始化列表扁平化为标量值向量
    bool collectGlobalArrayInitScalars(
        Type * type, const std::vector<ast_node *> & items, std::size_t begin, std::size_t end,
        std::vector<int32_t> & intValues, std::vector<float> & floatValues);

    /// @brief 统计聚合对象包含的标量元素个数
    std::size_t countScalarSlots(Type * type) const;

    /// @brief 根据操作数类型分发二元算术表达式
    Value * emitBinary(ast_node * node, IRInstOperator intOp, IRInstOperator floatOp = IRInstOperator::IRINST_OP_MAX);

    /// @brief 生成整数二元算术表达式对应的 IR
    Value * emitIntBinary(Value * lhs, Value * rhs, IRInstOperator op);

    /// @brief 生成浮点二元算术表达式对应的 IR
    Value * emitFloatBinary(Value * lhs, Value * rhs, IRInstOperator op);

    /// @brief 根据操作数类型分发比较表达式
    Value * emitCmp(ast_node * node, IRInstOperator intOp, IRInstOperator floatOp);

    /// @brief 生成整数比较表达式对应的 IR
    Value * emitICmp(Value * lhs, Value * rhs, IRInstOperator op);

    /// @brief 生成浮点比较表达式对应的 IR
    Value * emitFCmp(Value * lhs, Value * rhs, IRInstOperator op);

    /// @brief 生成一元取负表达式对应的 IR
    Value * emitNeg(ast_node * node);

    /// @brief 生成逻辑非表达式对应的 IR
    Value * emitNot(ast_node * node);

    /// @brief 生成自增/自减表达式对应的 IR
    Value * emitIncDec(ast_node * node, bool increment, bool prefix);

    /// @brief 生成整数逻辑非表达式对应的 IR
    Value * emitIntNot(Value * operand);

    /// @brief 生成浮点逻辑非表达式对应的 IR
    Value * emitFloatNot(Value * operand);

    /// @brief 根据值类型分发 boolize 逻辑
    Value * emitBoolize(Value * value);

    /// @brief 将整数值规约为 i1 值
    Value * emitIntBoolize(Value * value);

    /// @brief 将浮点值规约为 i1 值
    Value * emitFloatBoolize(Value * value);

    /// @brief 将 i1 值零扩展为 i32 值
    Value * materializeBoolToInt32(Value * value);

    /// @brief 将整型操作数规范化为 i32 值
    Value * normalizeIntegerOperand(Value * value);

    /// @brief 将 float 值转换为 i32 值
    Value * castFloatToInt32(Value * value);

    /// @brief 将数值转换为 i32 值
    Value * convertToInt32(Value * value);

    /// @brief 将 i32 值转换为 float 值
    Value * castInt32ToFloat(Value * value);

    /// @brief 将数值转换为 float 值
    Value * convertToFloat(Value * value);

    /// @brief 将数组指针按目标指针类型执行必要的首元素退化
    Value * decayArrayPointerToType(Value * value, Type * targetType);

    /// @brief 按目标类型执行必要的数值转换
    Value * convertValueToType(Value * value, Type * targetType);

    /// @brief 计算全局变量初始化表达式的整型常量值
    bool evaluateGlobalIntConstExpr(ast_node * node, int32_t & result);

    /// @brief 计算全局数组初始化列表并展开为标量常量序列
    bool evaluateGlobalArrayInitializer(Type * type, ast_node * initNode, std::vector<double> & values);

    /// @brief 递归收集数组初始化列表中的常量值
    bool collectGlobalArrayInitializer(
        Type * type, const std::vector<ast_node *> & items, std::size_t begin, std::size_t end,
        std::vector<double> & values);

    /// @brief 获取当前正在生成的函数
    Function * currentFunction() const;

    // ---- 块结构 IR 构造辅助接口 ----

    /// @brief 在当前函数中创建一个新的基本块
    BasicBlock * newBlock();

    /// @brief 设置当前插入指令的基本块
    void switchToBlock(BasicBlock * bb);

    /// @brief 判断当前基本块是否已经以终结指令结束
    bool isTerminated() const;

    /// @brief 将指令追加到当前基本块末尾
    void emitToBlock(Instruction * inst);

    /// @brief 在入口基本块的 alloca 区域插入栈分配指令
    AllocaInst * emitAlloca(Type * type);

    /// @brief 获取变量对应的栈槽，必要时延迟创建
    AllocaInst * getOrCreateVarSlot(Value * var);

private:
    ast_node * root;
    Module * module;

    // 当前函数的块构造上下文，每个函数开始时重置
    BasicBlock * currentBlock = nullptr;
    BasicBlock * entryBlock = nullptr;


    // 记录 LocalVariable/FormalParam 到对应栈槽的映射
    std::unordered_map<Value *, AllocaInst *> varAllocaMap;
    std::unordered_map<Value *, GlobalVariable *> staticLocalMap;
    std::size_t nextStaticLocalId = 0;

    std::vector<BasicBlock *> breakTargets;
    std::vector<BasicBlock *> continueTargets;

    std::vector<std::unordered_map<std::string, int32_t>> constBindings;
    std::vector<std::unordered_map<std::string, double>> floatConstBindings;
    std::unordered_map<std::string, GlobalVariable *> stringLiteralGlobals;
    std::size_t nextStringLiteralId = 0;
};
