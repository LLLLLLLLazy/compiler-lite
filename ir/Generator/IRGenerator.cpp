///
/// @file IRGenerator.cpp
/// @brief AST遍历产生结构化线性IR的源文件
///

#include "IRGenerator.h"

#include <cstdint>
#include <string>
#include <vector>

#include "ArgInstruction.h"
#include "BinaryInstruction.h"
#include "Common.h"
#include "CondGotoInstruction.h"
#include "EntryInstruction.h"
#include "ExitInstruction.h"
#include "FuncCallInstruction.h"
#include "GotoInstruction.h"
#include "IntegerType.h"
#include "LabelInstruction.h"
#include "MoveInstruction.h"
#include "UnaryInstruction.h"

IRGenerator::IRGenerator(ast_node * _root, Module * _module) : root(_root), module(_module)
{}

bool IRGenerator::run()
{
    if (!root) {
        return false;
    }

    if (root->node_type != ast_operator_type::AST_OP_COMPILE_UNIT) {
        minic_log(LOG_ERROR, "IRGenerator 只支持从编译单元根节点启动");
        return false;
    }

    if (!declareCompileUnit(root)) {
        return false;
    }

    return visitCompileUnit(root);
}

bool IRGenerator::declareCompileUnit(ast_node * node)
{
    for (auto son: node->sons) {
        if (!son || son->node_type != ast_operator_type::AST_OP_FUNC_DEF) {
            continue;
        }

        ast_node * typeNode = son->sons[0];
        ast_node * nameNode = son->sons[1];
        ast_node * paramsNode = son->sons[2];

        std::vector<FormalParam *> params;
        for (auto paramNode: paramsNode->sons) {
            if (paramNode->sons.size() < 2) {
                continue;
            }
            params.push_back(new FormalParam(paramNode->sons[0]->type, paramNode->sons[1]->name));
        }

        if (!module->newFunction(nameNode->name, typeNode->type, params)) {
            minic_log(LOG_ERROR, "函数(%s)重复定义", nameNode->name.c_str());
            return false;
        }
    }

    return true;
}

bool IRGenerator::visitCompileUnit(ast_node * node)
{
    module->setCurrentFunction(nullptr);

    for (auto son: node->sons) {
        if (!son) {
            continue;
        }

        switch (son->node_type) {
            case ast_operator_type::AST_OP_DECL_STMT:
                if (!visitDeclStmt(son)) {
                    return false;
                }
                break;
            case ast_operator_type::AST_OP_FUNC_DEF:
                if (!visitFuncDef(son)) {
                    return false;
                }
                break;
            default:
                minic_log(LOG_ERROR, "编译单元中不支持的节点类型: %d", static_cast<int>(son->node_type));
                return false;
        }
    }

    return true;
}

bool IRGenerator::visitStatement(ast_node * node)
{
    if (!node) {
        return true;
    }

    switch (node->node_type) {
        case ast_operator_type::AST_OP_BLOCK:
            return visitBlock(node);
        case ast_operator_type::AST_OP_RETURN:
            return visitReturn(node);
        case ast_operator_type::AST_OP_IF:
            return visitIf(node);
        case ast_operator_type::AST_OP_WHILE:
            return visitWhile(node);
        case ast_operator_type::AST_OP_BREAK:
            return visitBreak(node);
        case ast_operator_type::AST_OP_CONTINUE:
            return visitContinue(node);
        case ast_operator_type::AST_OP_ASSIGN:
            return visitAssign(node);
        case ast_operator_type::AST_OP_DECL_STMT:
            return visitDeclStmt(node);
        default:
            return visitExpr(node) != nullptr;
    }
}

bool IRGenerator::visitFuncDef(ast_node * node)
{
    ast_node * typeNode = node->sons[0];
    ast_node * nameNode = node->sons[1];
    ast_node * blockNode = node->sons[3];

    Function * func = module->findFunction(nameNode->name);
    if (!func) {
        minic_log(LOG_ERROR, "函数(%s)未声明", nameNode->name.c_str());
        return false;
    }

    module->setCurrentFunction(func);
    module->enterScope();

    emitInst(new EntryInstruction(func));

    auto * exitLabel = createLabel();
    func->setExitLabel(exitLabel);

    LocalVariable * retValue = nullptr;
    if (!typeNode->type->isVoidType()) {
        retValue = static_cast<LocalVariable *>(module->newVarValue(typeNode->type));
        emitInst(new MoveInstruction(func, retValue, module->newConstInt(0)));
    }
    func->setReturnValue(retValue);

    blockNode->needScope = false;
    if (!visitBlock(blockNode)) {
        module->leaveScope();
        module->setCurrentFunction(nullptr);
        return false;
    }

    emitInst(exitLabel);
    emitInst(new ExitInstruction(func, retValue));

    module->leaveScope();
    module->setCurrentFunction(nullptr);
    breakTargets.clear();
    continueTargets.clear();

    return true;
}

bool IRGenerator::visitBlock(ast_node * node)
{
    if (node->needScope) {
        module->enterScope();
    }

    for (auto son: node->sons) {
        if (!visitStatement(son)) {
            if (node->needScope) {
                module->leaveScope();
            }
            return false;
        }
    }

    if (node->needScope) {
        module->leaveScope();
    }

    return true;
}

bool IRGenerator::visitReturn(ast_node * node)
{
    Function * func = currentFunction();
    if (!func) {
        return false;
    }

    if (!node->sons.empty()) {
        Value * retVal = visitExpr(node->sons[0]);
        if (!retVal) {
            return false;
        }

        if (func->getReturnValue()) {
            emitInst(new MoveInstruction(func, func->getReturnValue(), retVal));
        }
    }

    emitInst(new GotoInstruction(func, func->getExitLabel()));
    return true;
}

bool IRGenerator::visitIf(ast_node * node)
{
    Function * func = currentFunction();
    Value * condValue = visitExpr(node->sons[0]);
    if (!condValue) {
        return false;
    }

    auto * thenLabel = createLabel();
    auto * endLabel = createLabel();
    auto * condValueBool = emitBoolize(condValue);

    if (node->sons.size() < 3 || node->sons[2] == nullptr) {
        emitInst(new CondGotoInstruction(func, condValueBool, thenLabel, endLabel));
        emitInst(thenLabel);
        if (!visitStatement(node->sons[1])) {
            return false;
        }
        emitInst(new GotoInstruction(func, endLabel));
        emitInst(endLabel);
        return true;
    }

    auto * elseLabel = createLabel();
    emitInst(new CondGotoInstruction(func, condValueBool, thenLabel, elseLabel));

    emitInst(thenLabel);
    if (!visitStatement(node->sons[1])) {
        return false;
    }
    emitInst(new GotoInstruction(func, endLabel));

    emitInst(elseLabel);
    if (!visitStatement(node->sons[2])) {
        return false;
    }
    emitInst(new GotoInstruction(func, endLabel));

    emitInst(endLabel);
    return true;
}

bool IRGenerator::visitWhile(ast_node * node)
{
    Function * func = currentFunction();
    auto * condLabel = createLabel();
    auto * bodyLabel = createLabel();
    auto * endLabel = createLabel();

    emitInst(condLabel);

    Value * condValue = visitExpr(node->sons[0]);
    if (!condValue) {
        return false;
    }
    emitInst(new CondGotoInstruction(func, emitBoolize(condValue), bodyLabel, endLabel));

    breakTargets.push_back(endLabel);
    continueTargets.push_back(condLabel);

    emitInst(bodyLabel);
    if (!visitStatement(node->sons[1])) {
        breakTargets.pop_back();
        continueTargets.pop_back();
        return false;
    }
    emitInst(new GotoInstruction(func, condLabel));

    breakTargets.pop_back();
    continueTargets.pop_back();

    emitInst(endLabel);
    return true;
}

bool IRGenerator::visitBreak(ast_node * node)
{
    (void) node;

    if (breakTargets.empty()) {
        minic_log(LOG_ERROR, "break 语句不在循环内");
        return false;
    }

    emitInst(new GotoInstruction(currentFunction(), breakTargets.back()));
    return true;
}

bool IRGenerator::visitContinue(ast_node * node)
{
    (void) node;

    if (continueTargets.empty()) {
        minic_log(LOG_ERROR, "continue 语句不在循环内");
        return false;
    }

    emitInst(new GotoInstruction(currentFunction(), continueTargets.back()));
    return true;
}

bool IRGenerator::visitAssign(ast_node * node)
{
    Value * lhs = module->findVarValue(node->sons[0]->name);
    if (!lhs) {
        minic_log(LOG_ERROR, "变量(%s)未定义", node->sons[0]->name.c_str());
        return false;
    }

    Value * rhs = visitExpr(node->sons[1]);
    if (!rhs) {
        return false;
    }

    emitInst(new MoveInstruction(currentFunction(), lhs, rhs));
    return true;
}

bool IRGenerator::visitDeclStmt(ast_node * node)
{
    for (auto child: node->sons) {
        if (!visitVarDecl(child)) {
            return false;
        }
    }
    return true;
}

bool IRGenerator::visitVarDecl(ast_node * node)
{
    Type * declType = node->sons[0]->type;
    std::string varName = node->sons[1]->name;

    Value * varValue = module->newVarValue(declType, varName);
    if (!varValue) {
        return false;
    }

    if (module->getCurrentFunction()) {
        if (node->sons.size() >= 3) {
            Value * initValue = visitExpr(node->sons[2]);
            if (!initValue) {
                return false;
            }
            emitInst(new MoveInstruction(currentFunction(), varValue, initValue));
        }
        return true;
    }

    auto * globalVar = dynamic_cast<GlobalVariable *>(varValue);
    if (!globalVar) {
        minic_log(LOG_ERROR, "全局变量(%s)创建失败", varName.c_str());
        return false;
    }

    if (node->sons.size() >= 3) {
        ast_node * initNode = node->sons[2];
        if (initNode->node_type != ast_operator_type::AST_OP_LEAF_LITERAL_UINT) {
            minic_log(LOG_ERROR, "全局变量(%s)只支持常量初始化", varName.c_str());
            return false;
        }
        globalVar->setInitIntValue(static_cast<int32_t>(initNode->integer_val));
    }

    return true;
}

Value * IRGenerator::visitExpr(ast_node * node)
{
    if (!node) {
        return nullptr;
    }

    switch (node->node_type) {
        case ast_operator_type::AST_OP_LEAF_LITERAL_UINT:
            return module->newConstInt(static_cast<int32_t>(node->integer_val));
        case ast_operator_type::AST_OP_LEAF_VAR_ID:
            return visitLeafVarId(node);
        case ast_operator_type::AST_OP_FUNC_CALL:
            return visitFuncCall(node);
        case ast_operator_type::AST_OP_ADD:
            return emitBinary(node, IRInstOperator::IRINST_OP_ADD_I);
        case ast_operator_type::AST_OP_SUB:
            return emitBinary(node, IRInstOperator::IRINST_OP_SUB_I);
        case ast_operator_type::AST_OP_MUL:
            return emitBinary(node, IRInstOperator::IRINST_OP_MUL_I);
        case ast_operator_type::AST_OP_DIV:
            return emitBinary(node, IRInstOperator::IRINST_OP_DIV_I);
        case ast_operator_type::AST_OP_MOD:
            return emitBinary(node, IRInstOperator::IRINST_OP_MOD_I);
        case ast_operator_type::AST_OP_LT:
            return emitBinary(node, IRInstOperator::IRINST_OP_LT_I);
        case ast_operator_type::AST_OP_GT:
            return emitBinary(node, IRInstOperator::IRINST_OP_GT_I);
        case ast_operator_type::AST_OP_LE:
            return emitBinary(node, IRInstOperator::IRINST_OP_LE_I);
        case ast_operator_type::AST_OP_GE:
            return emitBinary(node, IRInstOperator::IRINST_OP_GE_I);
        case ast_operator_type::AST_OP_EQ:
            return emitBinary(node, IRInstOperator::IRINST_OP_EQ_I);
        case ast_operator_type::AST_OP_NE:
            return emitBinary(node, IRInstOperator::IRINST_OP_NE_I);
        case ast_operator_type::AST_OP_NEG:
            return emitUnary(node, IRInstOperator::IRINST_OP_NEG_I);
        case ast_operator_type::AST_OP_NOT:
            return emitUnary(node, IRInstOperator::IRINST_OP_NOT_I);
        case ast_operator_type::AST_OP_LAND: {
            Function * func = currentFunction();
            auto * result = static_cast<LocalVariable *>(module->newVarValue(IntegerType::getTypeInt()));
            emitInst(new MoveInstruction(func, result, module->newConstInt(0)));

            auto * rhsLabel = createLabel();
            auto * endLabel = createLabel();

            Value * lhsValue = visitExpr(node->sons[0]);
            if (!lhsValue) {
                return nullptr;
            }

            emitInst(new CondGotoInstruction(func, emitBoolize(lhsValue), rhsLabel, endLabel));
            emitInst(rhsLabel);

            Value * rhsValue = visitExpr(node->sons[1]);
            if (!rhsValue) {
                return nullptr;
            }

            emitInst(new MoveInstruction(func, result, emitBoolize(rhsValue)));
            emitInst(new GotoInstruction(func, endLabel));
            emitInst(endLabel);
            return result;
        }
        case ast_operator_type::AST_OP_LOR: {
            Function * func = currentFunction();
            auto * result = static_cast<LocalVariable *>(module->newVarValue(IntegerType::getTypeInt()));
            emitInst(new MoveInstruction(func, result, module->newConstInt(1)));

            auto * rhsLabel = createLabel();
            auto * endLabel = createLabel();

            Value * lhsValue = visitExpr(node->sons[0]);
            if (!lhsValue) {
                return nullptr;
            }

            emitInst(new CondGotoInstruction(func, emitBoolize(lhsValue), endLabel, rhsLabel));
            emitInst(rhsLabel);

            Value * rhsValue = visitExpr(node->sons[1]);
            if (!rhsValue) {
                return nullptr;
            }

            emitInst(new MoveInstruction(func, result, emitBoolize(rhsValue)));
            emitInst(new GotoInstruction(func, endLabel));
            emitInst(endLabel);
            return result;
        }
        default:
            minic_log(LOG_ERROR, "表达式不支持的节点类型: %d", static_cast<int>(node->node_type));
            return nullptr;
    }
}

Value * IRGenerator::visitFuncCall(ast_node * node)
{
    std::string funcName = node->sons[0]->name;
    Function * calledFunc = module->findFunction(funcName);
    if (!calledFunc) {
        minic_log(LOG_ERROR, "函数(%s)未定义或声明", funcName.c_str());
        return nullptr;
    }

    std::vector<Value *> args;
    ast_node * paramsNode = node->sons[1];
    for (auto argNode: paramsNode->sons) {
        Value * argValue = visitExpr(argNode);
        if (!argValue) {
            return nullptr;
        }
        args.push_back(argValue);
    }

    if (args.size() != calledFunc->getParams().size()) {
        minic_log(LOG_ERROR, "函数(%s)参数个数不匹配", funcName.c_str());
        return nullptr;
    }

    Function * func = currentFunction();
    if (func) {
        func->setExistFuncCall(true);
        if (static_cast<int>(args.size()) > func->getMaxFuncCallArgCnt()) {
            func->setMaxFuncCallArgCnt(static_cast<int>(args.size()));
        }
    }

    auto * callInst = new FuncCallInstruction(func, calledFunc, args, calledFunc->getReturnType());
    emitInst(callInst);

    if (calledFunc->getReturnType()->isVoidType()) {
        return module->newConstInt(0);
    }

    return callInst;
}

Value * IRGenerator::visitLeafVarId(ast_node * node)
{
    Value * value = module->findVarValue(node->name);
    if (!value) {
        minic_log(LOG_ERROR, "变量(%s)未定义", node->name.c_str());
    }
    return value;
}

Value * IRGenerator::emitBinary(ast_node * node, IRInstOperator op)
{
    Value * lhs = visitExpr(node->sons[0]);
    if (!lhs) {
        return nullptr;
    }

    Value * rhs = visitExpr(node->sons[1]);
    if (!rhs) {
        return nullptr;
    }

    auto * inst = new BinaryInstruction(currentFunction(), op, lhs, rhs, IntegerType::getTypeInt());
    emitInst(inst);
    return inst;
}

Value * IRGenerator::emitUnary(ast_node * node, IRInstOperator op)
{
    Value * operand = visitExpr(node->sons[0]);
    if (!operand) {
        return nullptr;
    }

    auto * inst = new UnaryInstruction(currentFunction(), op, operand, IntegerType::getTypeInt());
    emitInst(inst);
    return inst;
}

Value * IRGenerator::emitBoolize(Value * value)
{
    if (auto * constVal = dynamic_cast<ConstInt *>(value)) {
        return module->newConstInt(constVal->getVal() != 0 ? 1 : 0);
    }

    auto * inst = new BinaryInstruction(
        currentFunction(), IRInstOperator::IRINST_OP_NE_I, value, module->newConstInt(0), IntegerType::getTypeInt());
    emitInst(inst);
    return inst;
}

LabelInstruction * IRGenerator::createLabel()
{
    return new LabelInstruction(currentFunction());
}

void IRGenerator::emitInst(Instruction * inst)
{
    if (currentFunction()) {
        currentFunction()->getInterCode().addInst(inst);
    }
}

Function * IRGenerator::currentFunction() const
{
    return module->getCurrentFunction();
}
