///
/// @file IRGenerator.cpp
/// @brief AST 遍历产生块结构 IR 的源文件
///
/// 局部变量通过 alloca/load/store 建模；
/// 控制流（if/else、while、break/continue）通过 BasicBlock + 终结指令建模；
/// 短路逻辑（&&、||）通过显式 CFG 建模。
///

#include "IRGenerator.h"

#include <string>
#include <vector>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "Common.h"
#include "CondBranchInst.h"
#include "ConstInt.h"
#include "FormalParam.h"
#include "GlobalVariable.h"
#include "ICmpInst.h"
#include "IntegerType.h"
#include "LoadInst.h"
#include "ReturnInst.h"
#include "StoreInst.h"
#include "ZExtInst.h"

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
    for (auto son : node->sons) {
        if (!son || son->node_type != ast_operator_type::AST_OP_FUNC_DEF) {
            continue;
        }

        ast_node * typeNode = son->sons[0];
        ast_node * nameNode = son->sons[1];
        ast_node * paramsNode = son->sons[2];

        std::vector<FormalParam *> params;
        for (auto paramNode : paramsNode->sons) {
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

    for (auto son : node->sons) {
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
            // Expression statement: evaluate and discard result
            return visitExpr(node) != nullptr;
    }
}

bool IRGenerator::visitFuncDef(ast_node * node)
{
    (void) node->sons[0]; // typeNode – only used in declareCompileUnit
    ast_node * nameNode = node->sons[1];
    ast_node * blockNode = node->sons[3];

    Function * func = module->findFunction(nameNode->name);
    if (!func) {
        minic_log(LOG_ERROR, "函数(%s)未声明", nameNode->name.c_str());
        return false;
    }

    module->setCurrentFunction(func);
    module->enterScope();

    // Reset per-function block IR state
    varAllocaMap.clear();
    breakTargets.clear();
    continueTargets.clear();

    // Create entry block – all allocas will be inserted here
    entryBlock = func->newBasicBlock();
    currentBlock = entryBlock;

    // Create alloca slots for function parameters and store their values
    for (auto * param : func->getParams()) {
        if (param->getName().empty()) {
            continue;
        }

        // Create a LocalVariable entry in the scope stack for this param name
        Value * paramLocal = module->newVarValue(param->getType(), param->getName());
        if (!paramLocal) {
            module->leaveScope();
            module->setCurrentFunction(nullptr);
            return false;
        }

        AllocaInst * slot = emitAlloca(param->getType());
        varAllocaMap[paramLocal] = slot;

        // Store the actual param value into the alloca slot
        emitToBlock(new StoreInst(func, param, slot));
    }

    // Visit function body
    blockNode->needScope = false;
    if (!visitBlock(blockNode)) {
        module->leaveScope();
        module->setCurrentFunction(nullptr);
        return false;
    }

    // Ensure the current block is properly terminated
    if (!isTerminated()) {
        if (func->getReturnType()->isVoidType()) {
            emitToBlock(new ReturnInst(func, nullptr));
        } else {
            emitToBlock(new ReturnInst(func, module->newConstInt(0)));
        }
    }

    module->leaveScope();
    module->setCurrentFunction(nullptr);
    return true;
}

bool IRGenerator::visitBlock(ast_node * node)
{
    if (node->needScope) {
        module->enterScope();
    }

    for (auto son : node->sons) {
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

    ReturnInst * retInst;
    if (!node->sons.empty()) {
        Value * retVal = visitExpr(node->sons[0]);
        if (!retVal) {
            return false;
        }
        if (func->getReturnType()->isInt32Type() && retVal->getType()->isInt1Byte()) {
            retVal = ensureI32(retVal);
        }
        retInst = new ReturnInst(func, retVal);
    } else {
        retInst = new ReturnInst(func, nullptr);
    }

    emitToBlock(retInst);

    // Switch to a fresh unreachable block so visitBlock can continue
    switchToBlock(newBlock());
    return true;
}

bool IRGenerator::visitIf(ast_node * node)
{
    Function * func = currentFunction();

    Value * condValue = visitExpr(node->sons[0]);
    if (!condValue) {
        return false;
    }
    Value * condBool = emitBoolize(condValue);

    bool hasElse = (node->sons.size() >= 3 && node->sons[2] != nullptr);

    BasicBlock * thenBB = newBlock();
    BasicBlock * mergeBB = newBlock();
    BasicBlock * condFromBB = currentBlock;

    if (!hasElse) {
        emitToBlock(new CondBranchInst(func, condBool, thenBB, mergeBB));
        condFromBB->linkSuccessor(thenBB);
        condFromBB->linkSuccessor(mergeBB);

        switchToBlock(thenBB);
        if (!visitStatement(node->sons[1])) {
            return false;
        }
        if (!isTerminated()) {
            BasicBlock * thenEnd = currentBlock;
            emitToBlock(new BranchInst(func, mergeBB));
            thenEnd->linkSuccessor(mergeBB);
        }
    } else {
        BasicBlock * elseBB = newBlock();
        emitToBlock(new CondBranchInst(func, condBool, thenBB, elseBB));
        condFromBB->linkSuccessor(thenBB);
        condFromBB->linkSuccessor(elseBB);

        switchToBlock(thenBB);
        if (!visitStatement(node->sons[1])) {
            return false;
        }
        if (!isTerminated()) {
            BasicBlock * thenEnd = currentBlock;
            emitToBlock(new BranchInst(func, mergeBB));
            thenEnd->linkSuccessor(mergeBB);
        }

        switchToBlock(elseBB);
        if (!visitStatement(node->sons[2])) {
            return false;
        }
        if (!isTerminated()) {
            BasicBlock * elseEnd = currentBlock;
            emitToBlock(new BranchInst(func, mergeBB));
            elseEnd->linkSuccessor(mergeBB);
        }
    }

    switchToBlock(mergeBB);
    return true;
}

bool IRGenerator::visitWhile(ast_node * node)
{
    Function * func = currentFunction();

    BasicBlock * condBB = newBlock();
    BasicBlock * bodyBB = newBlock();
    BasicBlock * exitBB = newBlock();

    // Jump from current block to the condition check
    BasicBlock * fromBB = currentBlock;
    emitToBlock(new BranchInst(func, condBB));
    fromBB->linkSuccessor(condBB);

    // Build condition block
    switchToBlock(condBB);
    Value * condValue = visitExpr(node->sons[0]);
    if (!condValue) {
        return false;
    }
    Value * condBool = emitBoolize(condValue);
    emitToBlock(new CondBranchInst(func, condBool, bodyBB, exitBB));
    condBB->linkSuccessor(bodyBB);
    condBB->linkSuccessor(exitBB);

    // Build body block
    breakTargets.push_back(exitBB);
    continueTargets.push_back(condBB);

    switchToBlock(bodyBB);
    if (!visitStatement(node->sons[1])) {
        breakTargets.pop_back();
        continueTargets.pop_back();
        return false;
    }
    if (!isTerminated()) {
        BasicBlock * bodyEnd = currentBlock;
        emitToBlock(new BranchInst(func, condBB));
        bodyEnd->linkSuccessor(condBB);
    }

    breakTargets.pop_back();
    continueTargets.pop_back();

    switchToBlock(exitBB);
    return true;
}

bool IRGenerator::visitBreak(ast_node * node)
{
    (void) node;

    if (breakTargets.empty()) {
        minic_log(LOG_ERROR, "break 语句不在循环内");
        return false;
    }

    BasicBlock * fromBB = currentBlock;
    emitToBlock(new BranchInst(currentFunction(), breakTargets.back()));
    fromBB->linkSuccessor(breakTargets.back());

    switchToBlock(newBlock());
    return true;
}

bool IRGenerator::visitContinue(ast_node * node)
{
    (void) node;

    if (continueTargets.empty()) {
        minic_log(LOG_ERROR, "continue 语句不在循环内");
        return false;
    }

    BasicBlock * fromBB = currentBlock;
    emitToBlock(new BranchInst(currentFunction(), continueTargets.back()));
    fromBB->linkSuccessor(continueTargets.back());

    switchToBlock(newBlock());
    return true;
}

bool IRGenerator::visitAssign(ast_node * node)
{
    Value * var = module->findVarValue(node->sons[0]->name);
    if (!var) {
        minic_log(LOG_ERROR, "变量(%s)未定义", node->sons[0]->name.c_str());
        return false;
    }

    Value * rhs = visitExpr(node->sons[1]);
    if (!rhs) {
        return false;
    }

    if (var->getType()->isInt32Type() && rhs->getType()->isInt1Byte()) {
        rhs = ensureI32(rhs);
    }

    auto * globalVar = dynamic_cast<GlobalVariable *>(var);
    if (globalVar) {
        emitToBlock(new StoreInst(currentFunction(), rhs, globalVar));
    } else {
        AllocaInst * slot = getOrCreateVarSlot(var);
        if (!slot) {
            return false;
        }
        emitToBlock(new StoreInst(currentFunction(), rhs, slot));
    }

    return true;
}

bool IRGenerator::visitDeclStmt(ast_node * node)
{
    for (auto child : node->sons) {
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

    if (module->getCurrentFunction()) {
        Value * varValue = module->newVarValue(declType, varName);
        if (!varValue) {
            return false;
        }

        AllocaInst * slot = emitAlloca(declType);
        varAllocaMap[varValue] = slot;

        if (node->sons.size() >= 3) {
            Value * initValue = visitExpr(node->sons[2]);
            if (!initValue) {
                return false;
            }
            if (declType->isInt32Type() && initValue->getType()->isInt1Byte()) {
                initValue = ensureI32(initValue);
            }
            emitToBlock(new StoreInst(currentFunction(), initValue, slot));
        }

        return true;
    }

    // Global variable
    auto * globalVar = dynamic_cast<GlobalVariable *>(module->newVarValue(declType, varName));
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
            return emitICmp(node, IRInstOperator::IRINST_OP_LT_I);
        case ast_operator_type::AST_OP_GT:
            return emitICmp(node, IRInstOperator::IRINST_OP_GT_I);
        case ast_operator_type::AST_OP_LE:
            return emitICmp(node, IRInstOperator::IRINST_OP_LE_I);
        case ast_operator_type::AST_OP_GE:
            return emitICmp(node, IRInstOperator::IRINST_OP_GE_I);
        case ast_operator_type::AST_OP_EQ:
            return emitICmp(node, IRInstOperator::IRINST_OP_EQ_I);
        case ast_operator_type::AST_OP_NE:
            return emitICmp(node, IRInstOperator::IRINST_OP_NE_I);

        case ast_operator_type::AST_OP_NEG:
            return emitNeg(node);
        case ast_operator_type::AST_OP_NOT:
            return emitNot(node);

        case ast_operator_type::AST_OP_LAND: {
            Function * func = currentFunction();
            AllocaInst * resultSlot = emitAlloca(IntegerType::getTypeInt());
            emitToBlock(new StoreInst(func, module->newConstInt(0), resultSlot));

            BasicBlock * rhsBB = newBlock();
            BasicBlock * endBB = newBlock();

            Value * lhsVal = visitExpr(node->sons[0]);
            if (!lhsVal) {
                return nullptr;
            }
            Value * lhsBool = emitBoolize(lhsVal);

            BasicBlock * condBlock = currentBlock;
            emitToBlock(new CondBranchInst(func, lhsBool, rhsBB, endBB));
            condBlock->linkSuccessor(rhsBB);
            condBlock->linkSuccessor(endBB);

            switchToBlock(rhsBB);
            Value * rhsVal = visitExpr(node->sons[1]);
            if (!rhsVal) {
                return nullptr;
            }
            Value * rhsBool = emitBoolize(rhsVal);
            Value * rhsI32 = ensureI32(rhsBool);
            emitToBlock(new StoreInst(func, rhsI32, resultSlot));
            BasicBlock * rhsEnd = currentBlock;
            emitToBlock(new BranchInst(func, endBB));
            rhsEnd->linkSuccessor(endBB);

            switchToBlock(endBB);
            auto * result = new LoadInst(func, resultSlot, IntegerType::getTypeInt());
            emitToBlock(result);
            return result;
        }

        case ast_operator_type::AST_OP_LOR: {
            Function * func = currentFunction();
            AllocaInst * resultSlot = emitAlloca(IntegerType::getTypeInt());
            emitToBlock(new StoreInst(func, module->newConstInt(1), resultSlot));

            BasicBlock * rhsBB = newBlock();
            BasicBlock * endBB = newBlock();

            Value * lhsVal = visitExpr(node->sons[0]);
            if (!lhsVal) {
                return nullptr;
            }
            Value * lhsBool = emitBoolize(lhsVal);

            BasicBlock * condBlock = currentBlock;
            emitToBlock(new CondBranchInst(func, lhsBool, endBB, rhsBB));
            condBlock->linkSuccessor(endBB);
            condBlock->linkSuccessor(rhsBB);

            switchToBlock(rhsBB);
            Value * rhsVal = visitExpr(node->sons[1]);
            if (!rhsVal) {
                return nullptr;
            }
            Value * rhsBool = emitBoolize(rhsVal);
            Value * rhsI32 = ensureI32(rhsBool);
            emitToBlock(new StoreInst(func, rhsI32, resultSlot));
            BasicBlock * rhsEnd = currentBlock;
            emitToBlock(new BranchInst(func, endBB));
            rhsEnd->linkSuccessor(endBB);

            switchToBlock(endBB);
            auto * result = new LoadInst(func, resultSlot, IntegerType::getTypeInt());
            emitToBlock(result);
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
    for (auto argNode : paramsNode->sons) {
        Value * argValue = visitExpr(argNode);
        if (!argValue) {
            return nullptr;
        }
        if (argValue->getType()->isInt1Byte()) {
            argValue = ensureI32(argValue);
        }
        args.push_back(argValue);
    }

    if (args.size() != calledFunc->getParams().size()) {
        minic_log(LOG_ERROR, "函数(%s)参数个数不匹配", funcName.c_str());
        return nullptr;
    }

    Function * func = currentFunction();
    auto * callInst = new CallInst(func, calledFunc, args, calledFunc->getReturnType());
    emitToBlock(callInst);

    if (calledFunc->getReturnType()->isVoidType()) {
        return module->newConstInt(0);
    }

    return callInst;
}

Value * IRGenerator::visitLeafVarId(ast_node * node)
{
    Value * var = module->findVarValue(node->name);
    if (!var) {
        minic_log(LOG_ERROR, "变量(%s)未定义", node->name.c_str());
        return nullptr;
    }

    auto * globalVar = dynamic_cast<GlobalVariable *>(var);
    if (globalVar) {
        auto * loadInst = new LoadInst(currentFunction(), globalVar, globalVar->getType());
        emitToBlock(loadInst);
        return loadInst;
    }

    AllocaInst * slot = getOrCreateVarSlot(var);
    if (!slot) {
        return nullptr;
    }

    auto * loadInst = new LoadInst(currentFunction(), slot, slot->getAllocaType());
    emitToBlock(loadInst);
    return loadInst;
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

    lhs = ensureI32(lhs);
    rhs = ensureI32(rhs);

    auto * inst = new BinaryInst(currentFunction(), op, lhs, rhs, IntegerType::getTypeInt());
    emitToBlock(inst);
    return inst;
}

Value * IRGenerator::emitICmp(ast_node * node, IRInstOperator op)
{
    Value * lhs = visitExpr(node->sons[0]);
    if (!lhs) {
        return nullptr;
    }
    Value * rhs = visitExpr(node->sons[1]);
    if (!rhs) {
        return nullptr;
    }

    lhs = ensureI32(lhs);
    rhs = ensureI32(rhs);

    auto * inst = new ICmpInst(currentFunction(), op, lhs, rhs, IntegerType::getTypeBool());
    emitToBlock(inst);
    return inst;
}

Value * IRGenerator::emitNeg(ast_node * node)
{
    Value * operand = visitExpr(node->sons[0]);
    if (!operand) {
        return nullptr;
    }
    operand = ensureI32(operand);

    auto * inst = new BinaryInst(currentFunction(), IRInstOperator::IRINST_OP_SUB_I,
                                 module->newConstInt(0), operand, IntegerType::getTypeInt());
    emitToBlock(inst);
    return inst;
}

Value * IRGenerator::emitNot(ast_node * node)
{
    Value * operand = visitExpr(node->sons[0]);
    if (!operand) {
        return nullptr;
    }
    operand = ensureI32(operand);

    auto * inst = new ICmpInst(currentFunction(), IRInstOperator::IRINST_OP_EQ_I,
                               operand, module->newConstInt(0), IntegerType::getTypeBool());
    emitToBlock(inst);
    return inst;
}

Value * IRGenerator::emitBoolize(Value * value)
{
    if (value->getType()->isInt1Byte()) {
        return value;
    }
    auto * inst = new ICmpInst(currentFunction(), IRInstOperator::IRINST_OP_NE_I,
                               value, module->newConstInt(0), IntegerType::getTypeBool());
    emitToBlock(inst);
    return inst;
}

Value * IRGenerator::ensureI32(Value * value)
{
    if (!value->getType()->isInt1Byte()) {
        return value;
    }
    auto * zext = new ZExtInst(currentFunction(), value, IntegerType::getTypeInt());
    emitToBlock(zext);
    return zext;
}

Function * IRGenerator::currentFunction() const
{
    return module->getCurrentFunction();
}

BasicBlock * IRGenerator::newBlock()
{
    return currentFunction()->newBasicBlock();
}

void IRGenerator::switchToBlock(BasicBlock * bb)
{
    currentBlock = bb;
}

bool IRGenerator::isTerminated() const
{
    return currentBlock != nullptr && currentBlock->isTerminated();
}

void IRGenerator::emitToBlock(Instruction * inst)
{
    if (currentBlock) {
        currentBlock->addInstruction(inst);
    }
}

AllocaInst * IRGenerator::emitAlloca(Type * type)
{
    auto * alloca = new AllocaInst(currentFunction(), type);
    // Insert at the front of the entry block so all allocas appear before any
    // regular instructions, regardless of when (during the traversal) they
    // were emitted.  The order among allocas is reversed but that is harmless.
    entryBlock->getInstructions().push_front(alloca);
    alloca->setParentBlock(entryBlock);
    return alloca;
}

AllocaInst * IRGenerator::getOrCreateVarSlot(Value * var)
{
    auto it = varAllocaMap.find(var);
    if (it != varAllocaMap.end()) {
        return it->second;
    }

    auto * slot = emitAlloca(var->getType());
    varAllocaMap[var] = slot;
    return slot;
}
