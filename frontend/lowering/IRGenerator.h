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

class IRGenerator {

public:
    IRGenerator(ast_node * root, Module * _module);

    ~IRGenerator() = default;

    bool run();

private:
    bool declareCompileUnit(ast_node * node);

    bool visitCompileUnit(ast_node * node);

    bool visitStatement(ast_node * node);

    bool visitFuncDef(ast_node * node);

    bool visitBlock(ast_node * node);

    bool visitReturn(ast_node * node);

    bool visitIf(ast_node * node);

    bool visitWhile(ast_node * node);

    bool visitBreak(ast_node * node);

    bool visitContinue(ast_node * node);

    bool visitAssign(ast_node * node);

    bool visitDeclStmt(ast_node * node);

    bool visitVarDecl(ast_node * node);

    Value * visitExpr(ast_node * node);

    Value * visitFuncCall(ast_node * node);

    Value * visitLeafVarId(ast_node * node);

    Value * emitBinary(ast_node * node, IRInstOperator op);

    Value * emitICmp(ast_node * node, IRInstOperator op);

    Value * emitNeg(ast_node * node);

    Value * emitNot(ast_node * node);

    Value * emitBoolize(Value * value);

    Value * ensureI32(Value * value);

    bool evaluateGlobalIntConstExpr(ast_node * node, int32_t & result);

    Function * currentFunction() const;

    // ---- Block IR infrastructure ----

    /// Create a new BasicBlock in the current function
    BasicBlock * newBlock();

    /// Set the current insertion block
    void switchToBlock(BasicBlock * bb);

    /// True if currentBlock's last instruction is a terminator
    bool isTerminated() const;

    /// Append inst to currentBlock
    void emitToBlock(Instruction * inst);

    /// Insert an alloca at the entry-block alloca point
    AllocaInst * emitAlloca(Type * type);

    /// Return (or lazily create) the alloca slot for a local variable/param
    AllocaInst * getOrCreateVarSlot(Value * var);

private:
    ast_node * root;
    Module * module;

    // Current block-building context – reset per function
    BasicBlock * currentBlock = nullptr;
    BasicBlock * entryBlock = nullptr;


    // Maps LocalVariable / FormalParam (as lookup keys) -> AllocaInst slot
    std::unordered_map<Value *, AllocaInst *> varAllocaMap;

    std::vector<BasicBlock *> breakTargets;
    std::vector<BasicBlock *> continueTargets;
};
