///
/// @file IRGenerator.h
/// @brief AST遍历产生结构化线性IR的头文件
///

#pragma once

#include <vector>

#include "AST.h"
#include "Module.h"

class LabelInstruction;
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

    Value * emitUnary(ast_node * node, IRInstOperator op);

    Value * emitBoolize(Value * value);

    LabelInstruction * createLabel();

    void emitInst(Instruction * inst);

    Function * currentFunction() const;

private:
    ast_node * root;
    Module * module;
    std::vector<LabelInstruction *> breakTargets;
    std::vector<LabelInstruction *> continueTargets;
};
