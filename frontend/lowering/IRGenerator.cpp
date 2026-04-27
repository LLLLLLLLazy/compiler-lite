///
/// @file IRGenerator.cpp
/// @brief AST 遍历产生块结构 IR 的源文件
///
/// 局部变量通过 alloca/load/store 建模；
/// 控制流（if/else、while、break/continue）通过 BasicBlock + 终结指令建模；
/// 短路逻辑（&&、||）通过显式 CFG 建模。
///

#include "IRGenerator.h"

#include <cstdint>
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

/// @brief 构造 AST 到结构化 IR 的生成器
IRGenerator::IRGenerator(ast_node * _root, Module * _module) : root(_root), module(_module)
{}

/// @brief 执行 AST 到结构化 IR 的整体生成流程
/// @return true 表示生成成功，false 表示生成失败
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

/// @brief 预声明编译单元中的所有函数
/// @param node 编译单元根节点
/// @return true 表示预声明成功，false 表示存在重复定义等错误
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

/// @brief 遍历编译单元并生成全局对象与函数体 IR
/// @param node 编译单元根节点
/// @return true 表示遍历成功，false 表示生成失败
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

/// @brief 生成单条语句对应的 IR
/// @param node 语句节点
/// @return true 表示生成成功，false 表示生成失败
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
            // 表达式语句只计算结果，不保留其值
            return visitExpr(node) != nullptr;
    }
}

/// @brief 生成函数定义对应的 IR
/// @param node 函数定义节点
/// @return true 表示生成成功，false 表示生成失败
bool IRGenerator::visitFuncDef(ast_node * node)
{
    (void) node->sons[0]; // 返回类型节点仅在 declareCompileUnit 中使用
    ast_node * nameNode = node->sons[1];
    ast_node * blockNode = node->sons[3];

    Function * func = module->findFunction(nameNode->name);
    if (!func) {
        minic_log(LOG_ERROR, "函数(%s)未声明", nameNode->name.c_str());
        return false;
    }

    module->setCurrentFunction(func);
    module->enterScope();

    // 重置当前函数的块级 IR 生成状态
    varAllocaMap.clear();
    breakTargets.clear();
    continueTargets.clear();

    // 创建入口块，所有 alloca 都插入到这里
    entryBlock = func->newBasicBlock();
    currentBlock = entryBlock;

    // 为形参创建栈槽并保存实参初值
    for (auto * param : func->getParams()) {
        if (param->getName().empty()) {
            continue;
        }

        // 为形参名在作用域栈中创建一个局部变量条目
        Value * paramLocal = module->newVarValue(param->getType(), param->getName());
        if (!paramLocal) {
            module->leaveScope();
            module->setCurrentFunction(nullptr);
            return false;
        }

        AllocaInst * slot = emitAlloca(param->getType());
        varAllocaMap[paramLocal] = slot;

        // 将传入的形参值写入对应栈槽
        emitToBlock(new StoreInst(func, param, slot));
    }

    // 遍历函数体
    blockNode->needScope = false;
    if (!visitBlock(blockNode)) {
        module->leaveScope();
        module->setCurrentFunction(nullptr);
        return false;
    }

    // 保证当前基本块以合法的终结指令结束
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

/// @brief 生成语句块对应的 IR
/// @param node 语句块节点
/// @return true 表示生成成功，false 表示生成失败
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

/// @brief 生成返回语句对应的 IR
/// @param node return 语句节点
/// @return true 表示生成成功，false 表示生成失败
bool IRGenerator::visitReturn(ast_node * node)
{
    Function * func = currentFunction();
    if (!func) {
        return false;
    }

    ReturnInst * retInst;
    if (!node->sons.empty()) {
        if (func->getReturnType()->isVoidType()) {
            minic_log(LOG_ERROR, "void 函数(%s)不能返回值", func->getName().c_str());
            return false;
        }

        Value * retVal = visitExpr(node->sons[0]);
        if (!retVal) {
            return false;
        }
        if (func->getReturnType()->isInt32Type() && retVal->getType()->isInt1Byte()) {
            retVal = ensureI32(retVal);
        }
        retInst = new ReturnInst(func, retVal);
    } else {
        if (!func->getReturnType()->isVoidType()) {
            minic_log(LOG_ERROR, "非 void 函数(%s)必须返回值", func->getName().c_str());
            return false;
        }
        retInst = new ReturnInst(func, nullptr);
    }

    emitToBlock(retInst);

    // 切换到新的不可达块，便于继续遍历后续语句
    switchToBlock(newBlock());
    return true;
}

/// @brief 生成 if/if-else 语句对应的 IR
/// @param node 条件语句节点
/// @return true 表示生成成功，false 表示生成失败
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

/// @brief 生成 while 循环对应的 IR
/// @param node while 语句节点
/// @return true 表示生成成功，false 表示生成失败
bool IRGenerator::visitWhile(ast_node * node)
{
    Function * func = currentFunction();

    BasicBlock * condBB = newBlock();
    BasicBlock * bodyBB = newBlock();
    BasicBlock * exitBB = newBlock();

    // 先从当前块跳转到条件判断块
    BasicBlock * fromBB = currentBlock;
    emitToBlock(new BranchInst(func, condBB));
    fromBB->linkSuccessor(condBB);

    // 构造条件判断块
    switchToBlock(condBB);
    Value * condValue = visitExpr(node->sons[0]);
    if (!condValue) {
        return false;
    }
    Value * condBool = emitBoolize(condValue);
    emitToBlock(new CondBranchInst(func, condBool, bodyBB, exitBB));
    condBB->linkSuccessor(bodyBB);
    condBB->linkSuccessor(exitBB);

    // 构造循环体块
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

/// @brief 生成 break 语句对应的 IR
/// @param node break 语句节点
/// @return true 表示生成成功，false 表示生成失败
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

/// @brief 生成 continue 语句对应的 IR
/// @param node continue 语句节点
/// @return true 表示生成成功，false 表示生成失败
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

/// @brief 生成赋值语句对应的 IR
/// @param node 赋值语句节点
/// @return true 表示生成成功，false 表示生成失败
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

/// @brief 生成声明语句中各变量的 IR
/// @param node 声明语句节点
/// @return true 表示生成成功，false 表示生成失败
bool IRGenerator::visitDeclStmt(ast_node * node)
{
    for (auto child : node->sons) {
        if (!visitVarDecl(child)) {
            return false;
        }
    }
    return true;
}

/// @brief 生成单个变量声明对应的 IR
/// @param node 变量声明节点
/// @return true 表示生成成功，false 表示生成失败
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

    // 处理全局变量声明
    auto * globalVar = dynamic_cast<GlobalVariable *>(module->newVarValue(declType, varName));
    if (!globalVar) {
        minic_log(LOG_ERROR, "全局变量(%s)创建失败", varName.c_str());
        return false;
    }

    if (node->sons.size() >= 3) {
        int32_t initValue = 0;
        if (!evaluateGlobalIntConstExpr(node->sons[2], initValue)) {
            minic_log(LOG_ERROR, "全局变量(%s)只支持常量初始化", varName.c_str());
            return false;
        }
        globalVar->setInitIntValue(initValue);
    }

    return true;
}

/// @brief 计算全局整型常量初始化表达式的值
/// @param node 常量表达式节点
/// @param result 输出的计算结果
/// @return true 表示计算成功，false 表示表达式不合法
bool IRGenerator::evaluateGlobalIntConstExpr(ast_node * node, int32_t & result)
{
    if (!node) {
        return false;
    }

    switch (node->node_type) {
        case ast_operator_type::AST_OP_LEAF_LITERAL_UINT:
            result = static_cast<int32_t>(node->integer_val);
            return true;

        case ast_operator_type::AST_OP_NEG: {
            int32_t operand = 0;
            if (!evaluateGlobalIntConstExpr(node->sons[0], operand)) {
                return false;
            }
            result = -operand;
            return true;
        }

        case ast_operator_type::AST_OP_NOT: {
            int32_t operand = 0;
            if (!evaluateGlobalIntConstExpr(node->sons[0], operand)) {
                return false;
            }
            result = !operand;
            return true;
        }

        case ast_operator_type::AST_OP_ADD:
        case ast_operator_type::AST_OP_SUB:
        case ast_operator_type::AST_OP_MUL:
        case ast_operator_type::AST_OP_DIV:
        case ast_operator_type::AST_OP_MOD:
        case ast_operator_type::AST_OP_LT:
        case ast_operator_type::AST_OP_GT:
        case ast_operator_type::AST_OP_LE:
        case ast_operator_type::AST_OP_GE:
        case ast_operator_type::AST_OP_EQ:
        case ast_operator_type::AST_OP_NE:
        case ast_operator_type::AST_OP_LAND:
        case ast_operator_type::AST_OP_LOR: {
            int32_t lhs = 0;
            int32_t rhs = 0;
            if (!evaluateGlobalIntConstExpr(node->sons[0], lhs) ||
                !evaluateGlobalIntConstExpr(node->sons[1], rhs)) {
                return false;
            }

            switch (node->node_type) {
                case ast_operator_type::AST_OP_ADD:
                    result = lhs + rhs;
                    return true;
                case ast_operator_type::AST_OP_SUB:
                    result = lhs - rhs;
                    return true;
                case ast_operator_type::AST_OP_MUL:
                    result = lhs * rhs;
                    return true;
                case ast_operator_type::AST_OP_DIV:
                    if (rhs == 0) {
                        return false;
                    }
                    result = lhs / rhs;
                    return true;
                case ast_operator_type::AST_OP_MOD:
                    if (rhs == 0) {
                        return false;
                    }
                    result = lhs % rhs;
                    return true;
                case ast_operator_type::AST_OP_LT:
                    result = lhs < rhs;
                    return true;
                case ast_operator_type::AST_OP_GT:
                    result = lhs > rhs;
                    return true;
                case ast_operator_type::AST_OP_LE:
                    result = lhs <= rhs;
                    return true;
                case ast_operator_type::AST_OP_GE:
                    result = lhs >= rhs;
                    return true;
                case ast_operator_type::AST_OP_EQ:
                    result = lhs == rhs;
                    return true;
                case ast_operator_type::AST_OP_NE:
                    result = lhs != rhs;
                    return true;
                case ast_operator_type::AST_OP_LAND:
                    result = (lhs != 0) && (rhs != 0);
                    return true;
                case ast_operator_type::AST_OP_LOR:
                    result = (lhs != 0) || (rhs != 0);
                    return true;
                default:
                    return false;
            }
        }

        default:
            return false;
    }
}

/// @brief 生成表达式并返回其结果值
/// @param node 表达式节点
/// @return 表达式对应的 IR 值，失败时返回空指针
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

/// @brief 生成函数调用表达式对应的 IR
/// @param node 函数调用节点
/// @return 调用结果值，失败时返回空指针
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

/// @brief 生成变量访问表达式对应的 IR
/// @param node 标识符节点
/// @return 变量加载后的值，失败时返回空指针
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

/// @brief 生成二元算术表达式对应的 IR
/// @param node 表达式节点
/// @param op 目标 IR 操作码
/// @return 生成出的指令值，失败时返回空指针
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

/// @brief 生成整数比较表达式对应的 IR
/// @param node 表达式节点
/// @param op 目标 IR 比较操作码
/// @return 生成出的比较结果值，失败时返回空指针
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

/// @brief 生成一元取负表达式对应的 IR
/// @param node 表达式节点
/// @return 生成出的结果值，失败时返回空指针
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

/// @brief 生成逻辑非表达式对应的 IR
/// @param node 表达式节点
/// @return 生成出的结果值，失败时返回空指针
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

/// @brief 将整型值规范化为布尔值
/// @param value 输入值
/// @return 已经是布尔值或新生成的布尔比较结果
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

/// @brief 将 i1 类型的值扩展为 i32
/// @param value 输入值
/// @return 原值或新生成的零扩展结果
Value * IRGenerator::ensureI32(Value * value)
{
    if (!value->getType()->isInt1Byte()) {
        return value;
    }
    auto * zext = new ZExtInst(currentFunction(), value, IntegerType::getTypeInt());
    emitToBlock(zext);
    return zext;
}

/// @brief 获取当前正在生成的函数对象
/// @return 当前函数对象
Function * IRGenerator::currentFunction() const
{
    return module->getCurrentFunction();
}

/// @brief 在当前函数中创建一个新的基本块
/// @return 新创建的基本块
BasicBlock * IRGenerator::newBlock()
{
    return currentFunction()->newBasicBlock();
}

/// @brief 切换当前指令插入位置到指定基本块
/// @param bb 目标基本块
void IRGenerator::switchToBlock(BasicBlock * bb)
{
    currentBlock = bb;
}

/// @brief 判断当前基本块是否已以终结指令结束
/// @return true 表示已终结，false 表示仍可继续追加指令
bool IRGenerator::isTerminated() const
{
    return currentBlock != nullptr && currentBlock->isTerminated();
}

/// @brief 向当前基本块追加一条指令
/// @param inst 待追加的指令
void IRGenerator::emitToBlock(Instruction * inst)
{
    if (currentBlock) {
        currentBlock->addInstruction(inst);
    }
}

/// @brief 在入口基本块插入栈分配指令
/// @param type 待分配对象的类型
/// @return 新创建的 alloca 指令
AllocaInst * IRGenerator::emitAlloca(Type * type)
{
    auto * alloca = new AllocaInst(currentFunction(), type);
    // 插入到入口块开头，确保所有 alloca 都位于普通指令之前。
    // 即使在遍历中较晚生成，放在前面也不会影响语义；alloca 之间顺序反转是无害的。
    entryBlock->getInstructions().push_front(alloca);
    alloca->setParentBlock(entryBlock);
    return alloca;
}

/// @brief 获取变量对应的栈槽，必要时延迟创建
/// @param var 目标变量
/// @return 变量对应的 alloca 指令
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
