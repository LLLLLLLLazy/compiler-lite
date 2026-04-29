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
#include <cmath>
#include <string>
#include <vector>

#include "AllocaInst.h"
#include "ArrayType.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "Common.h"
#include "CondBranchInst.h"
#include "ConstInt.h"
#include "FPToSIInst.h"
#include "FormalParam.h"
#include "GetElementPtrInst.h"
#include "GlobalVariable.h"
#include "ConstFloat.h"
#include "ICmpInst.h"
#include "IntegerType.h"
#include "LoadInst.h"
#include "PointerType.h"
#include "ReturnInst.h"
#include "SIToFPInst.h"
#include "StoreInst.h"
#include "ZExtInst.h"
#include "FloatType.h"

namespace {

ast_node * getDeclDimsNode(ast_node * declNode)
{
    for (std::size_t i = 2; i < declNode->sons.size(); ++i) {
        if (declNode->sons[i] && declNode->sons[i]->node_type == ast_operator_type::AST_OP_ARRAY_DIMS) {
            return declNode->sons[i];
        }
    }
    return nullptr;
}

ast_node * getDeclInitNode(ast_node * declNode)
{
    for (std::size_t i = 2; i < declNode->sons.size(); ++i) {
        if (declNode->sons[i] && declNode->sons[i]->node_type != ast_operator_type::AST_OP_ARRAY_DIMS) {
            return declNode->sons[i];
        }
    }
    return nullptr;
}

ast_node * getParamDimsNode(ast_node * paramNode)
{
    for (std::size_t i = 2; i < paramNode->sons.size(); ++i) {
        if (paramNode->sons[i] && paramNode->sons[i]->node_type == ast_operator_type::AST_OP_ARRAY_DIMS) {
            return paramNode->sons[i];
        }
    }
    return nullptr;
}

bool isArrayType(Type * type)
{
    return type != nullptr && type->isArrayType();
}

Type * getVariableValueType(Value * var)
{
    if (auto * globalVar = dynamic_cast<GlobalVariable *>(var)) {
        return globalVar->getValueType();
    }

    return var != nullptr ? var->getType() : nullptr;
}

Type * getAddressPointeeType(Value * addr)
{
    if (addr == nullptr) {
        return nullptr;
    }

    if (auto * ptrType = dynamic_cast<PointerType *>(addr->getType())) {
        return const_cast<Type *>(ptrType->getPointeeType());
    }

    return nullptr;
}

} // namespace

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

    constBindings.clear();
    constBindings.emplace_back();
    floatConstBindings.clear();
    floatConstBindings.emplace_back();

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
            Type * paramType = buildDeclaredType(paramNode, true);
            if (!paramType) {
                return false;
            }
            params.push_back(new FormalParam(paramType, paramNode->sons[1]->name));
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
    constBindings.emplace_back();
    floatConstBindings.emplace_back();

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
            constBindings.pop_back();
            floatConstBindings.pop_back();
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
        constBindings.pop_back();
        module->leaveScope();
        module->setCurrentFunction(nullptr);
        return false;
    }

    // 保证当前基本块以合法的终结指令结束
    if (!isTerminated()) {
        if (func->getReturnType()->isVoidType()) {
            emitToBlock(new ReturnInst(func, nullptr));
        } else {
            Value * zeroValue = func->getReturnType()->isFloatType()
                                  ? static_cast<Value *>(module->newConstFloat(0.0f))
                                  : static_cast<Value *>(module->newConstInt(0));
            emitToBlock(new ReturnInst(func, zeroValue));
        }
    }

    constBindings.pop_back();
    floatConstBindings.pop_back();
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
        constBindings.emplace_back();
        floatConstBindings.emplace_back();
    }

    for (auto son : node->sons) {
        if (!visitStatement(son)) {
            if (node->needScope) {
                constBindings.pop_back();
                floatConstBindings.pop_back();
                module->leaveScope();
            }
            return false;
        }
    }

    if (node->needScope) {
        constBindings.pop_back();
        floatConstBindings.pop_back();
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
        retVal = convertValueToType(retVal, func->getReturnType());
        if (!retVal) {
            return false;
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
    Value * addr = visitLValueAddress(node->sons[0]);
    if (!addr) {
        return false;
    }
    Value * rhs = visitExpr(node->sons[1]);
    if (!rhs) {
        return false;
    }

    Type * pointeeType = getAddressPointeeType(addr);
    if (pointeeType == nullptr) {
        minic_log(LOG_ERROR, "赋值目标不是地址类型");
        return false;
    }

    rhs = convertValueToType(rhs, pointeeType);
    if (!rhs) {
        return false;
    }

    emitToBlock(new StoreInst(currentFunction(), rhs, addr));
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
    Type * declType = buildDeclaredType(node, false);
    if (!declType) {
        return false;
    }
    std::string varName = node->sons[1]->name;
    ast_node * initNode = getDeclInitNode(node);

    if (module->getCurrentFunction()) {
        Value * varValue = module->newVarValue(declType, varName);
        if (!varValue) {
            return false;
        }

        AllocaInst * slot = emitAlloca(declType);
        varAllocaMap[varValue] = slot;

        if (node->isConst && !initNode) {
            minic_log(LOG_ERROR, "const 变量(%s)必须初始化", varName.c_str());
            return false;
        }

        if (initNode) {
            if (!emitInitializer(slot, declType, initNode)) {
                return false;
            }
        }

        if (node->isConst && declType->isInt32Type()) {
            int32_t constValue = 0;
            if (evaluateConstIntExpr(initNode, constValue)) {
                constBindings.back()[varName] = constValue;
            }
        } else if (node->isConst && declType->isFloatType()) {
            double constValue = 0.0;
            if (evaluateConstNumberExpr(initNode, constValue)) {
                floatConstBindings.back()[varName] = constValue;
            }
        }

        return true;
    }

    // 处理全局变量声明
    auto * globalVar = dynamic_cast<GlobalVariable *>(module->newVarValue(declType, varName));
    if (!globalVar) {
        minic_log(LOG_ERROR, "全局变量(%s)创建失败", varName.c_str());
        return false;
    }

    if (node->isConst && !initNode) {
        minic_log(LOG_ERROR, "全局 const 变量(%s)必须初始化", varName.c_str());
        return false;
    }

    if (declType->isArrayType()) {
        if (initNode && !(initNode->node_type == ast_operator_type::AST_OP_INIT_LIST && initNode->sons.empty())) {
            minic_log(LOG_ERROR, "当前仅支持零初始化的全局数组(%s)", varName.c_str());
            return false;
        }
        return true;
    }

    if (initNode) {
        if (declType->isFloatType()) {
            double initValue = 0.0;
            if (!evaluateConstNumberExpr(initNode, initValue)) {
                minic_log(LOG_ERROR, "全局变量(%s)只支持常量初始化", varName.c_str());
                return false;
            }
            globalVar->setInitFloatValue(static_cast<float>(initValue));
        } else {
            int32_t initValue = 0;
            if (!evaluateConstIntExpr(initNode, initValue)) {
                minic_log(LOG_ERROR, "全局变量(%s)只支持常量初始化", varName.c_str());
                return false;
            }
            globalVar->setInitIntValue(initValue);
        }
    }

    if (node->isConst && declType->isInt32Type()) {
        int32_t constValue = 0;
        if (evaluateConstIntExpr(initNode, constValue)) {
            constBindings.front()[varName] = constValue;
        }
    } else if (node->isConst && declType->isFloatType()) {
        double constValue = 0.0;
        if (evaluateConstNumberExpr(initNode, constValue)) {
            floatConstBindings.front()[varName] = constValue;
        }
    }

    return true;
}

Type * IRGenerator::buildDeclaredType(ast_node * declNode, bool forParam)
{
    if (!declNode || declNode->sons.empty()) {
        return nullptr;
    }

    Type * type = declNode->sons[0]->type;
    ast_node * dimsNode = nullptr;
    if (declNode->node_type == ast_operator_type::AST_OP_FUNC_FORMAL_PARAM) {
        dimsNode = getParamDimsNode(declNode);
    } else {
        dimsNode = getDeclDimsNode(declNode);
    }

    if (!dimsNode) {
        return type;
    }

    for (auto it = dimsNode->sons.rbegin(); it != dimsNode->sons.rend(); ++it) {
        int32_t dimValue = 0;
        if (!evaluateConstIntExpr(*it, dimValue) || dimValue <= 0) {
            minic_log(LOG_ERROR, "数组维度必须是正整数常量");
            return nullptr;
        }
        type = ArrayType::get(type, dimValue);
    }

    if (forParam && dimsNode->firstDimOmitted) {
        return const_cast<PointerType *>(PointerType::get(type));
    }

    return type;
}

Value * IRGenerator::getAddressOfVariable(Value * var)
{
    if (auto * globalVar = dynamic_cast<GlobalVariable *>(var)) {
        return globalVar;
    }

    return getOrCreateVarSlot(var);
}

Value * IRGenerator::visitLValueAddress(ast_node * node)
{
    if (!node) {
        return nullptr;
    }

    if (node->node_type == ast_operator_type::AST_OP_LEAF_VAR_ID) {
        Value * var = module->findVarValue(node->name);
        if (!var) {
            minic_log(LOG_ERROR, "变量(%s)未定义", node->name.c_str());
            return nullptr;
        }
        return getAddressOfVariable(var);
    }

    if (node->node_type == ast_operator_type::AST_OP_ARRAY_SUBSCRIPT) {
        ast_node * baseNode = node->sons[0];
        ast_node * indexNode = node->sons[1];

        Value * basePtr = nullptr;
        bool baseIsObjectAddress = false;
        if (baseNode->node_type == ast_operator_type::AST_OP_LEAF_VAR_ID) {
            Value * baseVar = module->findVarValue(baseNode->name);
            if (!baseVar) {
                minic_log(LOG_ERROR, "变量(%s)未定义", baseNode->name.c_str());
                return nullptr;
            }
            if (Type * baseValueType = getVariableValueType(baseVar); baseValueType != nullptr && baseValueType->isArrayType()) {
                basePtr = getAddressOfVariable(baseVar);
                if (!basePtr) {
                    return nullptr;
                }
                baseIsObjectAddress = true;
            } else {
                basePtr = visitExpr(baseNode);
            }
        } else {
            Value * baseAddr = visitLValueAddress(baseNode);
            if (!baseAddr) {
                return nullptr;
            }
            basePtr = baseAddr;
            Type * pointeeType = getAddressPointeeType(basePtr);
            if (pointeeType == nullptr || !isArrayType(pointeeType)) {
                basePtr = visitExpr(baseNode);
            } else {
                baseIsObjectAddress = true;
            }
        }

        if (!basePtr) {
            return nullptr;
        }

        Value * indexValue = visitExpr(indexNode);
        if (!indexValue) {
            return nullptr;
        }
        indexValue = ensureI32(indexValue);

        Type * pointeeType = getAddressPointeeType(basePtr);
        if (pointeeType == nullptr) {
            minic_log(LOG_ERROR, "数组下标基对象不是指针");
            return nullptr;
        }

        if (baseIsObjectAddress) {
            return emitGEP(basePtr, indexValue, true);
        }

        return emitGEP(basePtr, indexValue, false);
    }

    minic_log(LOG_ERROR, "不支持的左值节点类型: %d", static_cast<int>(node->node_type));
    return nullptr;
}

Value * IRGenerator::emitGEP(Value * basePtr, Value * index, bool decayArray)
{
    Type * resultPointee = getAddressPointeeType(basePtr);
    if (resultPointee == nullptr) {
        minic_log(LOG_ERROR, "GEP 基对象不是指针");
        return nullptr;
    }

    if (decayArray) {
        auto * arrayType = dynamic_cast<ArrayType *>(resultPointee);
        if (arrayType == nullptr) {
            minic_log(LOG_ERROR, "数组退化/索引要求数组地址");
            return nullptr;
        }
        resultPointee = arrayType->getElementType();
    }

    auto * resultType = const_cast<PointerType *>(PointerType::get(resultPointee));
    auto * gepInst = new GetElementPtrInst(currentFunction(), basePtr, index, resultType, decayArray);
    emitToBlock(gepInst);
    return gepInst;
}

std::size_t IRGenerator::countScalarSlots(Type * type) const
{
    auto * arrayType = dynamic_cast<ArrayType *>(type);
    if (arrayType == nullptr) {
        return 1;
    }

    return static_cast<std::size_t>(arrayType->getNumElements()) * countScalarSlots(arrayType->getElementType());
}

bool IRGenerator::emitZeroInitializer(Value * addr, Type * type)
{
    auto * arrayType = dynamic_cast<ArrayType *>(type);
    if (arrayType == nullptr) {
        Value * zeroValue = type->isFloatType()
                                  ? static_cast<Value *>(module->newConstFloat(0.0f))
                                  : static_cast<Value *>(module->newConstInt(0));
        emitToBlock(new StoreInst(currentFunction(), zeroValue, addr));
        return true;
    }

    for (int32_t i = 0; i < arrayType->getNumElements(); ++i) {
        Value * elemAddr = emitGEP(addr, module->newConstInt(i), true);
        if (!elemAddr || !emitZeroInitializer(elemAddr, arrayType->getElementType())) {
            return false;
        }
    }

    return true;
}

bool IRGenerator::emitArrayInitializer(
    Value * addr, Type * type, const std::vector<ast_node *> & items, std::size_t begin, std::size_t end)
{
    auto * arrayType = dynamic_cast<ArrayType *>(type);
    if (arrayType == nullptr) {
        if (begin >= end) {
            return emitZeroInitializer(addr, type);
        }
        return emitInitializer(addr, type, items[begin]);
    }

    std::size_t cursor = begin;
    Type * elemType = arrayType->getElementType();
    std::size_t subScalarCount = countScalarSlots(elemType);

    for (int32_t i = 0; i < arrayType->getNumElements(); ++i) {
        Value * elemAddr = emitGEP(addr, module->newConstInt(i), true);
        if (!elemAddr) {
            return false;
        }

        if (cursor >= end) {
            if (!emitZeroInitializer(elemAddr, elemType)) {
                return false;
            }
            continue;
        }

        ast_node * item = items[cursor];
        if (dynamic_cast<ArrayType *>(elemType) != nullptr && item->node_type != ast_operator_type::AST_OP_INIT_LIST) {
            std::size_t take = 0;
            while (cursor + take < end &&
                   items[cursor + take]->node_type != ast_operator_type::AST_OP_INIT_LIST &&
                   take < subScalarCount) {
                ++take;
            }
            if (!emitArrayInitializer(elemAddr, elemType, items, cursor, cursor + take)) {
                return false;
            }
            cursor += take;
            continue;
        }

        if (!emitInitializer(elemAddr, elemType, item)) {
            return false;
        }
        ++cursor;
    }

    return true;
}

bool IRGenerator::emitInitializer(Value * addr, Type * type, ast_node * initNode)
{
    auto * arrayType = dynamic_cast<ArrayType *>(type);
    if (arrayType != nullptr) {
        if (initNode == nullptr) {
            return emitZeroInitializer(addr, type);
        }

        if (initNode->node_type == ast_operator_type::AST_OP_INIT_LIST) {
            return emitArrayInitializer(addr, type, initNode->sons, 0, initNode->sons.size());
        }

        std::vector<ast_node *> singleItem{initNode};
        return emitArrayInitializer(addr, type, singleItem, 0, 1);
    }

    if (initNode == nullptr) {
        return emitZeroInitializer(addr, type);
    }

    ast_node * scalarInit = initNode;
    if (scalarInit->node_type == ast_operator_type::AST_OP_INIT_LIST) {
        if (scalarInit->sons.empty()) {
            return emitZeroInitializer(addr, type);
        }
        scalarInit = scalarInit->sons[0];
    }

    Value * initValue = visitExpr(scalarInit);
    if (!initValue) {
        return false;
    }
    initValue = convertValueToType(initValue, type);
    if (!initValue) {
        return false;
    }
    emitToBlock(new StoreInst(currentFunction(), initValue, addr));
    return true;
}

/// @brief 计算整型常量表达式的值
/// @param node 常量表达式节点
/// @param result 输出的计算结果
/// @return true 表示计算成功，false 表示表达式不合法
bool IRGenerator::evaluateConstIntExpr(ast_node * node, int32_t & result)
{
    double number = 0.0;
    if (!evaluateConstNumberExpr(node, number)) {
        return false;
    }

    result = static_cast<int32_t>(number);
    return true;
}

bool IRGenerator::evaluateConstNumberExpr(ast_node * node, double & result)
{
    if (!node) {
        return false;
    }

    switch (node->node_type) {
        case ast_operator_type::AST_OP_LEAF_LITERAL_UINT:
            result = static_cast<double>(node->integer_val);
            return true;

        case ast_operator_type::AST_OP_LEAF_LITERAL_FLOAT:
            result = static_cast<double>(node->float_val);
            return true;

        case ast_operator_type::AST_OP_LEAF_VAR_ID:
            for (auto it = constBindings.rbegin(); it != constBindings.rend(); ++it) {
                auto found = it->find(node->name);
                if (found != it->end()) {
                    result = static_cast<double>(found->second);
                    return true;
                }
            }
            for (auto it = floatConstBindings.rbegin(); it != floatConstBindings.rend(); ++it) {
                auto found = it->find(node->name);
                if (found != it->end()) {
                    result = found->second;
                    return true;
                }
            }
            return false;

        case ast_operator_type::AST_OP_NEG: {
            double operand = 0.0;
            if (!evaluateConstNumberExpr(node->sons[0], operand)) {
                return false;
            }
            result = -operand;
            return true;
        }

        case ast_operator_type::AST_OP_NOT: {
            double operand = 0.0;
            if (!evaluateConstNumberExpr(node->sons[0], operand)) {
                return false;
            }
            result = (operand == 0.0) ? 1.0 : 0.0;
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
            double lhs = 0.0;
            double rhs = 0.0;
            if (!evaluateConstNumberExpr(node->sons[0], lhs) ||
                !evaluateConstNumberExpr(node->sons[1], rhs)) {
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
                    if (rhs == 0.0) {
                        return false;
                    }
                    result = std::fmod(lhs, rhs);
                    return true;
                case ast_operator_type::AST_OP_LT:
                    result = lhs < rhs ? 1.0 : 0.0;
                    return true;
                case ast_operator_type::AST_OP_GT:
                    result = lhs > rhs ? 1.0 : 0.0;
                    return true;
                case ast_operator_type::AST_OP_LE:
                    result = lhs <= rhs ? 1.0 : 0.0;
                    return true;
                case ast_operator_type::AST_OP_GE:
                    result = lhs >= rhs ? 1.0 : 0.0;
                    return true;
                case ast_operator_type::AST_OP_EQ:
                    result = lhs == rhs ? 1.0 : 0.0;
                    return true;
                case ast_operator_type::AST_OP_NE:
                    result = lhs != rhs ? 1.0 : 0.0;
                    return true;
                case ast_operator_type::AST_OP_LAND:
                    result = (lhs != 0.0 && rhs != 0.0) ? 1.0 : 0.0;
                    return true;
                case ast_operator_type::AST_OP_LOR:
                    result = (lhs != 0.0 || rhs != 0.0) ? 1.0 : 0.0;
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

        case ast_operator_type::AST_OP_LEAF_LITERAL_FLOAT:
            return module->newConstFloat(node->float_val);

        case ast_operator_type::AST_OP_LEAF_VAR_ID:
            return visitLeafVarId(node);

        case ast_operator_type::AST_OP_ARRAY_SUBSCRIPT: {
            Value * addr = visitLValueAddress(node);
            if (!addr) {
                return nullptr;
            }
            Type * pointeeType = getAddressPointeeType(addr);
            if (pointeeType == nullptr) {
                minic_log(LOG_ERROR, "数组下标不是有效地址");
                return nullptr;
            }
            if (isArrayType(pointeeType)) {
                return emitGEP(addr, module->newConstInt(0), true);
            }
            auto * loadInst = new LoadInst(currentFunction(), addr, pointeeType);
            emitToBlock(loadInst);
            return loadInst;
        }

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
    for (std::size_t i = 0; i < paramsNode->sons.size(); ++i) {
        auto * argNode = paramsNode->sons[i];
        Value * argValue = visitExpr(argNode);
        if (!argValue) {
            return nullptr;
        }
        if (i < calledFunc->getParams().size()) {
            argValue = convertValueToType(argValue, calledFunc->getParams()[i]->getType());
            if (!argValue) {
                return nullptr;
            }
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
    for (auto it = constBindings.rbegin(); it != constBindings.rend(); ++it) {
        auto found = it->find(node->name);
        if (found != it->end()) {
            return module->newConstInt(found->second);
        }
    }

    for (auto it = floatConstBindings.rbegin(); it != floatConstBindings.rend(); ++it) {
        auto found = it->find(node->name);
        if (found != it->end()) {
            return module->newConstFloat(static_cast<float>(found->second));
        }
    }

    Value * var = module->findVarValue(node->name);
    if (!var) {
        minic_log(LOG_ERROR, "变量(%s)未定义", node->name.c_str());
        return nullptr;
    }

    Type * valueType = getVariableValueType(var);
    if (valueType == nullptr) {
        minic_log(LOG_ERROR, "变量(%s)类型无效", node->name.c_str());
        return nullptr;
    }

    if (valueType->isArrayType()) {
        Value * addr = getAddressOfVariable(var);
        if (!addr) {
            return nullptr;
        }
        return emitGEP(addr, module->newConstInt(0), true);
    }

    Value * addr = getAddressOfVariable(var);
    if (!addr) {
        return nullptr;
    }

    auto * loadInst = new LoadInst(currentFunction(), addr, valueType);
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

    IRInstOperator actualOp = op;
    Type * resultType = IntegerType::getTypeInt();

    if (lhs->getType()->isFloatType() || rhs->getType()->isFloatType()) {
        lhs = ensureFloat(lhs);
        rhs = ensureFloat(rhs);
        resultType = FloatType::getTypeFloat();

        switch (op) {
            case IRInstOperator::IRINST_OP_ADD_I:
                actualOp = IRInstOperator::IRINST_OP_ADD_F;
                break;
            case IRInstOperator::IRINST_OP_SUB_I:
                actualOp = IRInstOperator::IRINST_OP_SUB_F;
                break;
            case IRInstOperator::IRINST_OP_MUL_I:
                actualOp = IRInstOperator::IRINST_OP_MUL_F;
                break;
            case IRInstOperator::IRINST_OP_DIV_I:
                actualOp = IRInstOperator::IRINST_OP_DIV_F;
                break;
            default:
                break;
        }
    } else {
        lhs = ensureI32(lhs);
        rhs = ensureI32(rhs);
    }

    auto * inst = new BinaryInst(currentFunction(), actualOp, lhs, rhs, resultType);
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

    IRInstOperator actualOp = op;

    if (lhs->getType()->isFloatType() || rhs->getType()->isFloatType()) {
        lhs = ensureFloat(lhs);
        rhs = ensureFloat(rhs);

        switch (op) {
            case IRInstOperator::IRINST_OP_LT_I:
                actualOp = IRInstOperator::IRINST_OP_LT_F;
                break;
            case IRInstOperator::IRINST_OP_GT_I:
                actualOp = IRInstOperator::IRINST_OP_GT_F;
                break;
            case IRInstOperator::IRINST_OP_LE_I:
                actualOp = IRInstOperator::IRINST_OP_LE_F;
                break;
            case IRInstOperator::IRINST_OP_GE_I:
                actualOp = IRInstOperator::IRINST_OP_GE_F;
                break;
            case IRInstOperator::IRINST_OP_EQ_I:
                actualOp = IRInstOperator::IRINST_OP_EQ_F;
                break;
            case IRInstOperator::IRINST_OP_NE_I:
                actualOp = IRInstOperator::IRINST_OP_NE_F;
                break;
            default:
                break;
        }
    } else {
        lhs = ensureI32(lhs);
        rhs = ensureI32(rhs);
    }

    auto * inst = new ICmpInst(currentFunction(), actualOp, lhs, rhs, IntegerType::getTypeBool());
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

    if (operand->getType()->isFloatType()) {
        auto * inst = new BinaryInst(currentFunction(), IRInstOperator::IRINST_OP_SUB_F,
                                     module->newConstFloat(0.0f), operand, FloatType::getTypeFloat());
        emitToBlock(inst);
        return inst;
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
    if (value->getType()->isFloatType()) {
        auto * inst = new ICmpInst(currentFunction(), IRInstOperator::IRINST_OP_NE_F,
                                   value, module->newConstFloat(0.0f), IntegerType::getTypeBool());
        emitToBlock(inst);
        return inst;
    }
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
    if (value->getType()->isFloatType()) {
        auto * castInst = new FPToSIInst(currentFunction(), value, IntegerType::getTypeInt());
        emitToBlock(castInst);
        return castInst;
    }
    if (!value->getType()->isInt1Byte()) {
        return value;
    }
    auto * zext = new ZExtInst(currentFunction(), value, IntegerType::getTypeInt());
    emitToBlock(zext);
    return zext;
}

Value * IRGenerator::ensureFloat(Value * value)
{
    if (value->getType()->isFloatType()) {
        return value;
    }

    if (value->getType()->isInt1Byte()) {
        value = ensureI32(value);
    }

    auto * castInst = new SIToFPInst(currentFunction(), value, FloatType::getTypeFloat());
    emitToBlock(castInst);
    return castInst;
}

Value * IRGenerator::convertValueToType(Value * value, Type * targetType)
{
    if (value == nullptr || targetType == nullptr) {
        return nullptr;
    }

    if (value->getType() == targetType) {
        return value;
    }

    if (targetType->isFloatType()) {
        return ensureFloat(value);
    }

    if (targetType->isInt32Type()) {
        return ensureI32(value);
    }

    return value;
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
