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
#include <limits>
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
#include "ConstInteger.h"
#include "FCmpInst.h"
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

Type * getArrayScalarType(Type * type)
{
    while (auto * arrayType = dynamic_cast<ArrayType *>(type)) {
        type = arrayType->getElementType();
    }
    return type;
}

constexpr std::size_t kFlatZeroInitThreshold = 256;

std::vector<int32_t> packStringLiteralWords(const std::string & text)
{
    std::vector<unsigned char> bytes(text.begin(), text.end());
    bytes.push_back('\0');

    std::size_t wordCount = (bytes.size() + 3) / 4;
    std::vector<int32_t> words(wordCount, 0);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        words[i / 4] |= static_cast<int32_t>(bytes[i]) << ((i % 4) * 8);
    }

    return words;
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

    prePopulateGlobalConstBindings(root);

    if (!declareCompileUnit(root)) {
        return false;
    }

    return visitCompileUnit(root);
}

/// @brief 预扫描全局 const 声明，在函数预声明前填充 constBindings
/// @param node 编译单元根节点
void IRGenerator::prePopulateGlobalConstBindings(ast_node * node)
{
	for (auto son : node->sons) {
		if (!son || son->node_type != ast_operator_type::AST_OP_DECL_STMT) {
			continue;
		}
		for (auto child : son->sons) {
			if (!child || !child->isConst) {
				continue;
			}

			Type * declType = buildDeclaredType(child, false);
			if (!declType) {
				continue;
			}

			// 仅标量 const 值可用于数组维度表达式
			if (!declType->isInt32Type() && !declType->isFloatType()) {
				continue;
			}

			ast_node * initNode = getDeclInitNode(child);
			if (!initNode) {
				continue;
			}

			std::string varName = child->sons[1]->name;

			if (declType->isInt32Type()) {
				int32_t constValue = 0;
                if (evaluateConstIntInitializerExpr(initNode, constValue)) {
					constBindings.front()[varName] = constValue;
				}
			} else if (declType->isFloatType()) {
				double constValue = 0.0;
				if (evaluateConstNumberExpr(initNode, constValue)) {
					floatConstBindings.front()[varName] = constValue;
				}
			}
		}
	}
}

/// @brief 预声明编译单元中的所有函数
/// @param node 编译单元根节点
/// @return true 表示预声明成功，false 表示存在重复定义等错误
bool IRGenerator::declareCompileUnit(ast_node * node)
{
    bool foundMain = false;

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

        if (nameNode->name == "main") {
            if (!typeNode->type->isInt32Type() || !params.empty()) {
                minic_log(LOG_ERROR, "main 函数必须且只能定义为无参 int main()");
                for (auto * param : params) {
                    delete param;
                }
                return false;
            }
            foundMain = true;
        }

        if (!module->newFunction(nameNode->name, typeNode->type, params)) {
            minic_log(LOG_ERROR, "函数(%s)重复定义", nameNode->name.c_str());
            return false;
        }
    }

    if (!foundMain) {
        minic_log(LOG_ERROR, "程序必须且只能包含一个无参 int main() 函数");
        return false;
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
        case ast_operator_type::AST_OP_FOR:
            return visitFor(node);
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
    staticLocalMap.clear();
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
                                  : static_cast<Value *>(module->newConstInt32(0));
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

/// @brief 生成 for 循环对应的 IR
/// @param node for 语句节点，孩子依次为 init、cond、step、body，前三者可省略
/// @return true 表示生成成功，false 表示生成失败
bool IRGenerator::visitFor(ast_node * node)
{
    Function * func = currentFunction();

    module->enterScope();
    constBindings.emplace_back();
    floatConstBindings.emplace_back();

    auto leaveForScope = [&]() {
        constBindings.pop_back();
        floatConstBindings.pop_back();
        module->leaveScope();
    };

    ast_node * initNode = node->sons.size() > 0 ? node->sons[0] : nullptr;
    ast_node * condNode = node->sons.size() > 1 ? node->sons[1] : nullptr;
    ast_node * stepNode = node->sons.size() > 2 ? node->sons[2] : nullptr;
    ast_node * bodyNode = node->sons.size() > 3 ? node->sons[3] : nullptr;

    if (initNode && !visitStatement(initNode)) {
        leaveForScope();
        return false;
    }

    BasicBlock * condBB = newBlock();
    BasicBlock * bodyBB = newBlock();
    BasicBlock * stepBB = newBlock();
    BasicBlock * exitBB = newBlock();

    BasicBlock * fromBB = currentBlock;
    emitToBlock(new BranchInst(func, condBB));
    fromBB->linkSuccessor(condBB);

    switchToBlock(condBB);
    if (condNode) {
        Value * condValue = visitExpr(condNode);
        if (!condValue) {
            leaveForScope();
            return false;
        }
        Value * condBool = emitBoolize(condValue);
        emitToBlock(new CondBranchInst(func, condBool, bodyBB, exitBB));
        condBB->linkSuccessor(exitBB);
    } else {
        emitToBlock(new BranchInst(func, bodyBB));
    }
    condBB->linkSuccessor(bodyBB);

    breakTargets.push_back(exitBB);
    continueTargets.push_back(stepBB);

    switchToBlock(bodyBB);
    if (!visitStatement(bodyNode)) {
        breakTargets.pop_back();
        continueTargets.pop_back();
        leaveForScope();
        return false;
    }
    if (!isTerminated()) {
        BasicBlock * bodyEnd = currentBlock;
        emitToBlock(new BranchInst(func, stepBB));
        bodyEnd->linkSuccessor(stepBB);
    }

    switchToBlock(stepBB);
    if (stepNode && !visitStatement(stepNode)) {
        breakTargets.pop_back();
        continueTargets.pop_back();
        leaveForScope();
        return false;
    }
    if (!isTerminated()) {
        BasicBlock * stepEnd = currentBlock;
        emitToBlock(new BranchInst(func, condBB));
        stepEnd->linkSuccessor(condBB);
    }

    breakTargets.pop_back();
    continueTargets.pop_back();
    leaveForScope();

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
        if (node->isStatic) {
            return visitStaticLocalVarDecl(node);
        }

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

        if (node->isConst && !isConstInitializer(declType, initNode)) {
            minic_log(LOG_ERROR, "const 变量(%s)必须使用编译期常量初始化", varName.c_str());
            return false;
        }

        if (initNode) {
            if (!emitInitializer(slot, declType, initNode)) {
                return false;
            }
        }

        if (node->isConst && declType->isInt32Type()) {
            int32_t constValue = 0;
            if (evaluateConstIntInitializerExpr(initNode, constValue)) {
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

    if (initNode && !isConstInitializer(declType, initNode)) {
        minic_log(LOG_ERROR, "全局变量(%s)只支持常量初始化", varName.c_str());
        return false;
    }

    if (declType->isArrayType()) {
        if (initNode && initNode->node_type == ast_operator_type::AST_OP_INIT_LIST && !initNode->sons.empty()) {
            std::vector<int32_t> intVals;
            std::vector<float> floatVals;
            if (!collectGlobalArrayInitScalars(declType, initNode->sons, 0, initNode->sons.size(),
                                               intVals, floatVals)) {
                minic_log(LOG_ERROR, "全局数组(%s)初始化表达式不是编译期常量", varName.c_str());
                return false;
            }
            if (!floatVals.empty()) {
                globalVar->setInitFloatArray(floatVals);
            } else {
                globalVar->setInitIntArray(intVals);
            }
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
            if (!evaluateConstIntInitializerExpr(initNode, initValue)) {
                minic_log(LOG_ERROR, "全局变量(%s)只支持常量初始化", varName.c_str());
                return false;
            }
            globalVar->setInitIntValue(initValue);
        }
    }

    if (node->isConst && declType->isInt32Type()) {
        int32_t constValue = 0;
        if (evaluateConstIntInitializerExpr(initNode, constValue)) {
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

bool IRGenerator::visitStaticLocalVarDecl(ast_node * node)
{
    Type * declType = buildDeclaredType(node, false);
    if (!declType) {
        return false;
    }

    std::string varName = node->sons[1]->name;
    ast_node * initNode = getDeclInitNode(node);

    Value * localAlias = module->newVarValue(declType, varName);
    if (!localAlias) {
        return false;
    }

    if (node->isConst && !initNode) {
        minic_log(LOG_ERROR, "static const 变量(%s)必须初始化", varName.c_str());
        return false;
    }

    if (initNode && !isConstInitializer(declType, initNode)) {
        minic_log(LOG_ERROR, "static 变量(%s)只支持常量初始化", varName.c_str());
        return false;
    }

    GlobalVariable * globalVar = nullptr;
    while (globalVar == nullptr) {
        std::string globalName = "__static_" + currentFunction()->getName() + "_" + varName + "_" +
                                 std::to_string(nextStaticLocalId++);
        globalVar = module->newSyntheticGlobalVariable(declType, globalName);
    }
    staticLocalMap[localAlias] = globalVar;

    if (declType->isArrayType()) {
        if (initNode && initNode->node_type == ast_operator_type::AST_OP_INIT_LIST && !initNode->sons.empty()) {
            std::vector<int32_t> intVals;
            std::vector<float> floatVals;
            if (!collectGlobalArrayInitScalars(declType, initNode->sons, 0, initNode->sons.size(),
                                               intVals, floatVals)) {
                minic_log(LOG_ERROR, "static 数组(%s)初始化表达式不是编译期常量", varName.c_str());
                return false;
            }
            if (!floatVals.empty()) {
                globalVar->setInitFloatArray(floatVals);
            } else {
                globalVar->setInitIntArray(intVals);
            }
        }
        return true;
    }

    if (initNode) {
        if (declType->isFloatType()) {
            double initValue = 0.0;
            if (!evaluateConstNumberExpr(initNode, initValue)) {
                minic_log(LOG_ERROR, "static 变量(%s)只支持常量初始化", varName.c_str());
                return false;
            }
            globalVar->setInitFloatValue(static_cast<float>(initValue));
        } else {
            int32_t initValue = 0;
            if (!evaluateConstIntInitializerExpr(initNode, initValue)) {
                minic_log(LOG_ERROR, "static 变量(%s)只支持常量初始化", varName.c_str());
                return false;
            }
            globalVar->setInitIntValue(initValue);
        }
    }

    if (node->isConst && declType->isInt32Type()) {
        int32_t constValue = 0;
        if (evaluateConstIntInitializerExpr(initNode, constValue)) {
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

/// @brief 校验初始化器中的表达式是否满足编译期常量约束
/// @param type 被初始化对象的类型
/// @param initNode 初始化器节点
/// @return true 表示初始化器合法，false 表示包含非法的非常量表达式
bool IRGenerator::isConstInitializer(Type * type, ast_node * initNode)
{
    if (initNode == nullptr) {
        return true;
    }

    if (initNode->node_type == ast_operator_type::AST_OP_INIT_LIST) {
        Type * nestedType = type;
        if (auto * arrayType = dynamic_cast<ArrayType *>(type)) {
            nestedType = arrayType->getElementType();
        }
        for (auto * child : initNode->sons) {
            if (!isConstInitializer(nestedType, child)) {
                return false;
            }
        }
        return true;
    }

    if (type != nullptr && type->isFloatType()) {
        double value = 0.0;
        return evaluateConstNumberExpr(initNode, value);
    }

    int32_t value = 0;
    return evaluateConstIntInitializerExpr(initNode, value);
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
        if (!evaluateConstArrayBoundExpr(*it, dimValue) || dimValue < 0) {
            minic_log(LOG_ERROR, "数组维度必须是非负整数常量表达式");
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

    auto staticIt = staticLocalMap.find(var);
    if (staticIt != staticLocalMap.end()) {
        return staticIt->second;
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
        indexValue = convertToInt32(indexValue);

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

bool IRGenerator::emitFlatLoopZeroInitializer(Value * addr, Type * type)
{
    std::size_t scalarCount = countScalarSlots(type);
    if (scalarCount == 0 || scalarCount > static_cast<std::size_t>(std::numeric_limits<int32_t>::max())) {
        minic_log(LOG_ERROR, "数组零初始化规模超出支持范围");
        return false;
    }

    Type * scalarType = type;
    Value * scalarBaseAddr = addr;
    while (auto * arrayType = dynamic_cast<ArrayType *>(scalarType)) {
        scalarBaseAddr = emitGEP(scalarBaseAddr, module->newConstInt32(0), true);
        if (!scalarBaseAddr) {
            return false;
        }
        scalarType = arrayType->getElementType();
    }

    auto * indexSlot = emitAlloca(IntegerType::getTypeInt32());
    emitToBlock(new StoreInst(currentFunction(), module->newConstInt32(0), indexSlot));

    Function * func = currentFunction();
    BasicBlock * condBB = newBlock();
    BasicBlock * bodyBB = newBlock();
    BasicBlock * exitBB = newBlock();

    BasicBlock * fromBB = currentBlock;
    emitToBlock(new BranchInst(func, condBB));
    fromBB->linkSuccessor(condBB);

    switchToBlock(condBB);
    auto * indexValue = new LoadInst(func, indexSlot, IntegerType::getTypeInt32());
    emitToBlock(indexValue);
    auto * condValue = new ICmpInst(
        func,
        IRInstOperator::IRINST_OP_LT_I,
        indexValue,
        module->newConstInt32(static_cast<int32_t>(scalarCount)),
        IntegerType::getTypeInt1());
    emitToBlock(condValue);
    emitToBlock(new CondBranchInst(func, condValue, bodyBB, exitBB));
    condBB->linkSuccessor(bodyBB);
    condBB->linkSuccessor(exitBB);

    switchToBlock(bodyBB);
    auto * bodyIndex = new LoadInst(func, indexSlot, IntegerType::getTypeInt32());
    emitToBlock(bodyIndex);
    Value * elemAddr = emitGEP(scalarBaseAddr, bodyIndex, false);
    if (!elemAddr) {
        return false;
    }

    Value * zeroValue = scalarType->isFloatType()
                            ? static_cast<Value *>(module->newConstFloat(0.0f))
                            : static_cast<Value *>(module->newConstInt32(0));
    emitToBlock(new StoreInst(func, zeroValue, elemAddr));

    auto * nextIndex = new BinaryInst(
        func,
        IRInstOperator::IRINST_OP_ADD_I,
        bodyIndex,
        module->newConstInt32(1),
        IntegerType::getTypeInt32());
    emitToBlock(nextIndex);
    emitToBlock(new StoreInst(func, nextIndex, indexSlot));

    BasicBlock * bodyEnd = currentBlock;
    emitToBlock(new BranchInst(func, condBB));
    bodyEnd->linkSuccessor(condBB);

    switchToBlock(exitBB);
    return true;
}

bool IRGenerator::emitZeroInitializer(Value * addr, Type * type)
{
    auto * arrayType = dynamic_cast<ArrayType *>(type);
    if (arrayType == nullptr) {
        Value * zeroValue = type->isFloatType()
                      ? static_cast<Value *>(module->newConstFloat(0.0f))
                      : static_cast<Value *>(module->newConstInt32(0));
        emitToBlock(new StoreInst(currentFunction(), zeroValue, addr));
        return true;
    }

    if (countScalarSlots(type) >= kFlatZeroInitThreshold) {
        return emitFlatLoopZeroInitializer(addr, type);
    }

    for (int32_t i = 0; i < arrayType->getNumElements(); ++i) {
        Value * elemAddr = emitGEP(addr, module->newConstInt32(i), true);
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
        Value * elemAddr = emitGEP(addr, module->newConstInt32(i), true);
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

bool IRGenerator::collectGlobalArrayInitScalars(
    Type * type, const std::vector<ast_node *> & items, std::size_t begin, std::size_t end,
    std::vector<int32_t> & intValues, std::vector<float> & floatValues)
{
    auto * arrayType = dynamic_cast<ArrayType *>(type);
    if (arrayType == nullptr) {
        if (begin >= end) {
            intValues.push_back(0);
            return true;
        }
        ast_node * item = items[begin];
        if (item->node_type == ast_operator_type::AST_OP_INIT_LIST) {
            if (item->sons.empty()) {
                intValues.push_back(0);
                return true;
            }
            item = item->sons[0];
        }
        Type * scalarType = type;
        while (auto * innerArray = dynamic_cast<ArrayType *>(scalarType)) {
            scalarType = innerArray->getElementType();
        }
        if (scalarType->isFloatType()) {
            double val = 0.0;
            if (!evaluateConstNumberExpr(item, val)) return false;
            floatValues.push_back(static_cast<float>(val));
        } else {
            int32_t val = 0;
            if (!evaluateConstIntInitializerExpr(item, val)) return false;
            intValues.push_back(val);
        }
        return true;
    }

    std::size_t cursor = begin;
    Type * elemType = arrayType->getElementType();
    std::size_t subScalarCount = countScalarSlots(elemType);

    for (int32_t i = 0; i < arrayType->getNumElements(); ++i) {
        if (cursor >= end) {
            for (std::size_t k = 0; k < subScalarCount; ++k) {
                intValues.push_back(0);
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
            if (!collectGlobalArrayInitScalars(elemType, items, cursor, cursor + take, intValues, floatValues)) {
                return false;
            }
            cursor += take;
            continue;
        }

        if (item->node_type == ast_operator_type::AST_OP_INIT_LIST) {
            if (!collectGlobalArrayInitScalars(elemType, item->sons, 0, item->sons.size(), intValues, floatValues)) {
                return false;
            }
        } else {
            if (!collectGlobalArrayInitScalars(elemType, items, cursor, cursor + 1, intValues, floatValues)) {
                return false;
            }
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

bool IRGenerator::evaluateGlobalArrayInitializer(Type * type, ast_node * initNode, std::vector<double> & values)
{
    auto * arrayType = dynamic_cast<ArrayType *>(type);
    if (arrayType != nullptr) {
        if (initNode == nullptr) {
            return collectGlobalArrayInitializer(type, {}, 0, 0, values);
        }

        if (initNode->node_type == ast_operator_type::AST_OP_INIT_LIST) {
            return collectGlobalArrayInitializer(type, initNode->sons, 0, initNode->sons.size(), values);
        }

        std::vector<ast_node *> singleItem{initNode};
        return collectGlobalArrayInitializer(type, singleItem, 0, 1, values);
    }

    if (initNode == nullptr) {
        values.push_back(0.0);
        return true;
    }

    ast_node * scalarInit = initNode;
    if (scalarInit->node_type == ast_operator_type::AST_OP_INIT_LIST) {
        if (scalarInit->sons.empty()) {
            values.push_back(0.0);
            return true;
        }
        scalarInit = scalarInit->sons[0];
    }

    double value = 0.0;
    if (!evaluateConstNumberExpr(scalarInit, value)) {
        return false;
    }

    values.push_back(value);
    return true;
}

bool IRGenerator::collectGlobalArrayInitializer(
    Type * type, const std::vector<ast_node *> & items, std::size_t begin, std::size_t end, std::vector<double> & values)
{
    auto * arrayType = dynamic_cast<ArrayType *>(type);
    if (arrayType == nullptr) {
        if (begin >= end) {
            return evaluateGlobalArrayInitializer(type, nullptr, values);
        }
        return evaluateGlobalArrayInitializer(type, items[begin], values);
    }

    std::size_t cursor = begin;
    Type * elemType = arrayType->getElementType();
    std::size_t subScalarCount = countScalarSlots(elemType);

    for (int32_t i = 0; i < arrayType->getNumElements(); ++i) {
        if (cursor >= end) {
            if (!evaluateGlobalArrayInitializer(elemType, nullptr, values)) {
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
            if (!collectGlobalArrayInitializer(elemType, items, cursor, cursor + take, values)) {
                return false;
            }
            cursor += take;
            continue;
        }

        if (!evaluateGlobalArrayInitializer(elemType, item, values)) {
            return false;
        }
        ++cursor;
    }

    return true;
}

/// @brief 计算整型常量表达式的值
/// @param node 常量表达式节点
/// @param result 输出的计算结果
/// @return true 表示计算成功，false 表示表达式不合法
bool IRGenerator::evaluateConstIntExpr(ast_node * node, int32_t & result)
{
    if (!node) {
        return false;
    }

    switch (node->node_type) {
        case ast_operator_type::AST_OP_LEAF_LITERAL_UINT:
            result = static_cast<int32_t>(node->integer_val);
            return true;

        case ast_operator_type::AST_OP_LEAF_VAR_ID: {
            for (auto it = constBindings.rbegin(); it != constBindings.rend(); ++it) {
                auto found = it->find(node->name);
                if (found != it->end()) {
                    result = found->second;
                    return true;
                }
            }
            return false;
        }

        case ast_operator_type::AST_OP_NEG: {
            int32_t operand = 0;
            if (!evaluateConstIntExpr(node->sons[0], operand)) {
                return false;
            }
            result = -operand;
            return true;
        }

        case ast_operator_type::AST_OP_ADD:
        case ast_operator_type::AST_OP_SUB:
        case ast_operator_type::AST_OP_MUL:
        case ast_operator_type::AST_OP_DIV:
        case ast_operator_type::AST_OP_MOD: {
            int32_t lhs = 0;
            int32_t rhs = 0;
            if (!evaluateConstIntExpr(node->sons[0], lhs) ||
                !evaluateConstIntExpr(node->sons[1], rhs)) {
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
                default:
                    return false;
            }
        }

        default:
            return false;
    }
}

bool IRGenerator::evaluateConstIntInitializerExpr(ast_node * node, int32_t & result)
{
    if (evaluateConstIntExpr(node, result)) {
        return true;
    }

    double numeric = 0.0;
    if (!evaluateConstNumberExpr(node, numeric) || !std::isfinite(numeric)) {
        return false;
    }

    double truncated = std::trunc(numeric);
    if (truncated < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
        truncated > static_cast<double>(std::numeric_limits<int32_t>::max())) {
        return false;
    }

    result = static_cast<int32_t>(truncated);
    return true;
}

/// @brief 计算数组维度使用的整型常量表达式
/// @param node 常量表达式节点
/// @param result 输出的计算结果
/// @return true 表示计算成功，false 表示表达式不满足数组维度约束
bool IRGenerator::evaluateConstArrayBoundExpr(ast_node * node, int32_t & result)
{
    return evaluateConstIntExpr(node, result);
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

        case ast_operator_type::AST_OP_ADD:
        case ast_operator_type::AST_OP_SUB:
        case ast_operator_type::AST_OP_MUL:
        case ast_operator_type::AST_OP_DIV:
        case ast_operator_type::AST_OP_MOD: {
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
            return module->newConstInt32(static_cast<int32_t>(node->integer_val));

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
                return emitGEP(addr, module->newConstInt32(0), true);
            }
            auto * loadInst = new LoadInst(currentFunction(), addr, pointeeType);
            emitToBlock(loadInst);
            return loadInst;
        }

        case ast_operator_type::AST_OP_FUNC_CALL:
            return visitFuncCall(node);

        case ast_operator_type::AST_OP_ADD:
            return emitBinary(node, IRInstOperator::IRINST_OP_ADD_I, IRInstOperator::IRINST_OP_ADD_F);
        case ast_operator_type::AST_OP_SUB:
            return emitBinary(node, IRInstOperator::IRINST_OP_SUB_I, IRInstOperator::IRINST_OP_SUB_F);
        case ast_operator_type::AST_OP_MUL:
            return emitBinary(node, IRInstOperator::IRINST_OP_MUL_I, IRInstOperator::IRINST_OP_MUL_F);
        case ast_operator_type::AST_OP_DIV:
            return emitBinary(node, IRInstOperator::IRINST_OP_DIV_I, IRInstOperator::IRINST_OP_DIV_F);
        case ast_operator_type::AST_OP_MOD:
            return emitBinary(node, IRInstOperator::IRINST_OP_MOD_I);

        case ast_operator_type::AST_OP_LT:
            return emitCmp(node, IRInstOperator::IRINST_OP_LT_I, IRInstOperator::IRINST_OP_LT_F);
        case ast_operator_type::AST_OP_GT:
            return emitCmp(node, IRInstOperator::IRINST_OP_GT_I, IRInstOperator::IRINST_OP_GT_F);
        case ast_operator_type::AST_OP_LE:
            return emitCmp(node, IRInstOperator::IRINST_OP_LE_I, IRInstOperator::IRINST_OP_LE_F);
        case ast_operator_type::AST_OP_GE:
            return emitCmp(node, IRInstOperator::IRINST_OP_GE_I, IRInstOperator::IRINST_OP_GE_F);
        case ast_operator_type::AST_OP_EQ:
            return emitCmp(node, IRInstOperator::IRINST_OP_EQ_I, IRInstOperator::IRINST_OP_EQ_F);
        case ast_operator_type::AST_OP_NE:
            return emitCmp(node, IRInstOperator::IRINST_OP_NE_I, IRInstOperator::IRINST_OP_NE_F);

        case ast_operator_type::AST_OP_NEG:
            return emitNeg(node);
        case ast_operator_type::AST_OP_NOT:
            return emitNot(node);
        case ast_operator_type::AST_OP_PRE_INC:
            return emitIncDec(node, true, true);
        case ast_operator_type::AST_OP_PRE_DEC:
            return emitIncDec(node, false, true);
        case ast_operator_type::AST_OP_POST_INC:
            return emitIncDec(node, true, false);
        case ast_operator_type::AST_OP_POST_DEC:
            return emitIncDec(node, false, false);

        case ast_operator_type::AST_OP_LAND: {
            Function * func = currentFunction();
            AllocaInst * resultSlot = emitAlloca(IntegerType::getTypeInt32());
            emitToBlock(new StoreInst(func, module->newConstInt32(0), resultSlot));

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
            Value * rhsI32 = materializeBoolToInt32(rhsBool);
            emitToBlock(new StoreInst(func, rhsI32, resultSlot));
            BasicBlock * rhsEnd = currentBlock;
            emitToBlock(new BranchInst(func, endBB));
            rhsEnd->linkSuccessor(endBB);

            switchToBlock(endBB);
            auto * result = new LoadInst(func, resultSlot, IntegerType::getTypeInt32());
            emitToBlock(result);
            return result;
        }

        case ast_operator_type::AST_OP_LOR: {
            Function * func = currentFunction();
            AllocaInst * resultSlot = emitAlloca(IntegerType::getTypeInt32());
            emitToBlock(new StoreInst(func, module->newConstInt32(1), resultSlot));

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
            Value * rhsI32 = materializeBoolToInt32(rhsBool);
            emitToBlock(new StoreInst(func, rhsI32, resultSlot));
            BasicBlock * rhsEnd = currentBlock;
            emitToBlock(new BranchInst(func, endBB));
            rhsEnd->linkSuccessor(endBB);

            switchToBlock(endBB);
            auto * result = new LoadInst(func, resultSlot, IntegerType::getTypeInt32());
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
    std::vector<Value *> args;
    ast_node * paramsNode = node->sons[1];
    if (funcName == "starttime" || funcName == "stoptime") {
        if (!paramsNode->sons.empty()) {
            minic_log(LOG_ERROR, "函数(%s)参数个数不匹配", funcName.c_str());
            return nullptr;
        }
        const int64_t lineNo = node->sons[0]->line_no >= 0 ? node->sons[0]->line_no : 0;
        args.push_back(module->newConstInt32(static_cast<int32_t>(lineNo)));
        funcName = (funcName == "starttime") ? "_sysy_starttime" : "_sysy_stoptime";
    }

    Function * calledFunc = module->findFunction(funcName);
    if (!calledFunc) {
        minic_log(LOG_ERROR, "函数(%s)未定义或声明", funcName.c_str());
        return nullptr;
    }

    std::size_t fixedParamCount = calledFunc->getParams().size();
    if (args.empty()) {
        for (std::size_t i = 0; i < paramsNode->sons.size(); ++i) {
            auto * argNode = paramsNode->sons[i];
            Type * expectedType = i < fixedParamCount ? calledFunc->getParams()[i]->getType() : nullptr;
            Value * argValue = nullptr;
            if (argNode->node_type == ast_operator_type::AST_OP_LEAF_STRING_LITERAL) {
                if (expectedType == nullptr && calledFunc->isVarArg()) {
                    minic_log(LOG_ERROR, "字符串字面量只能作为固定位置的指针形参传递");
                    return nullptr;
                }
                if (expectedType != nullptr && !expectedType->isPointerType()) {
                    minic_log(LOG_ERROR, "字符串字面量只能传递给指针形参");
                    return nullptr;
                }
                argValue = materializeStringLiteral(argNode);
            } else {
                argValue = visitExpr(argNode);
            }
            if (!argValue) {
                return nullptr;
            }
            if (expectedType != nullptr) {
                argValue = convertValueToType(argValue, expectedType);
                if (!argValue) {
                    return nullptr;
                }
            }
            args.push_back(argValue);
        }
    }

    if ((!calledFunc->isVarArg() && args.size() != fixedParamCount) ||
        (calledFunc->isVarArg() && args.size() < fixedParamCount)) {
        minic_log(LOG_ERROR, "函数(%s)参数个数不匹配", funcName.c_str());
        return nullptr;
    }

    Function * func = currentFunction();
    auto * callInst = new CallInst(func, calledFunc, args, calledFunc->getReturnType());
    emitToBlock(callInst);

    if (calledFunc->getReturnType()->isVoidType()) {
        return module->newConstInt32(0);
    }

    return callInst;
}

/// @brief 将字符串字面量具象化为匿名全局对象并返回首元素地址
/// @param node 字符串字面量节点
/// @return 指向打包字符串首元素的指针值，失败时返回空指针
Value * IRGenerator::materializeStringLiteral(ast_node * node)
{
    if (node == nullptr || node->node_type != ast_operator_type::AST_OP_LEAF_STRING_LITERAL) {
        return nullptr;
    }

    auto found = stringLiteralGlobals.find(node->name);
    GlobalVariable * global = found != stringLiteralGlobals.end() ? found->second : nullptr;
    if (global == nullptr) {
        std::vector<int32_t> words = packStringLiteralWords(node->name);
        Type * arrayType = ArrayType::get(IntegerType::getTypeInt32(), static_cast<int32_t>(words.size()));

        while (global == nullptr) {
            std::string globalName = "__sysy_str_" + std::to_string(nextStringLiteralId++);
            global = module->newSyntheticGlobalVariable(arrayType, globalName);
        }

        global->setInitIntArray(words);
        stringLiteralGlobals.emplace(node->name, global);
    }

    return emitGEP(global, module->newConstInt32(0), true);
}

/// @brief 生成变量访问表达式对应的 IR
/// @param node 标识符节点
/// @return 变量加载后的值，失败时返回空指针
Value * IRGenerator::visitLeafVarId(ast_node * node)
{
    for (auto it = constBindings.rbegin(); it != constBindings.rend(); ++it) {
        auto found = it->find(node->name);
        if (found != it->end()) {
            return module->newConstInt32(found->second);
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
        return emitGEP(addr, module->newConstInt32(0), true);
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
Value * IRGenerator::emitBinary(ast_node * node, IRInstOperator intOp, IRInstOperator floatOp)
{
    // 将左结合的同操作符链展平为迭代，避免深层递归导致栈溢出
    std::vector<ast_node *> rhsOps;
    ast_node * cur = node;
    while (cur && cur->node_type == node->node_type && cur->sons.size() >= 2) {
        rhsOps.push_back(cur->sons[1]);
        cur = cur->sons[0];
    }

    Value * result = visitExpr(cur);
    if (!result) {
        return nullptr;
    }

    for (int i = static_cast<int>(rhsOps.size()) - 1; i >= 0; --i) {
        Value * rhs = visitExpr(rhsOps[i]);
        if (!rhs) {
            return nullptr;
        }

        if (result->getType()->isFloatType() || rhs->getType()->isFloatType()) {
            if (floatOp == IRInstOperator::IRINST_OP_MAX) {
                minic_log(LOG_ERROR, "浮点类型不支持该二元运算");
                return nullptr;
            }
            result = emitFloatBinary(result, rhs, floatOp);
        } else {
            result = emitIntBinary(result, rhs, intOp);
        }
        if (!result) {
            return nullptr;
        }
    }
    return result;
}

Value * IRGenerator::emitIntBinary(Value * lhs, Value * rhs, IRInstOperator op)
{
    lhs = normalizeIntegerOperand(lhs);
    if (!lhs) {
        return nullptr;
    }
    rhs = normalizeIntegerOperand(rhs);
    if (!rhs) {
        return nullptr;
    }

    auto * inst = new BinaryInst(currentFunction(), op, lhs, rhs, IntegerType::getTypeInt32());
    emitToBlock(inst);
    return inst;
}

Value * IRGenerator::emitFloatBinary(Value * lhs, Value * rhs, IRInstOperator op)
{
    lhs = convertToFloat(lhs);
    if (!lhs) {
        return nullptr;
    }
    rhs = convertToFloat(rhs);
    if (!rhs) {
        return nullptr;
    }

    auto * inst = new BinaryInst(currentFunction(), op, lhs, rhs, FloatType::getTypeFloat());
    emitToBlock(inst);
    return inst;
}

/// @brief 根据操作数类型分发比较表达式
/// @param node 表达式节点
/// @param intOp 整数比较操作码
/// @param floatOp 浮点比较操作码
/// @return 生成出的比较结果值，失败时返回空指针
Value * IRGenerator::emitCmp(ast_node * node, IRInstOperator intOp, IRInstOperator floatOp)
{
    Value * lhs = visitExpr(node->sons[0]);
    if (!lhs) {
        return nullptr;
    }
    Value * rhs = visitExpr(node->sons[1]);
    if (!rhs) {
        return nullptr;
    }

    if (lhs->getType()->isFloatType() || rhs->getType()->isFloatType()) {
        return emitFCmp(lhs, rhs, floatOp);
    } else {
        return emitICmp(lhs, rhs, intOp);
    }
}

/// @brief 生成整数比较表达式对应的 IR
/// @param lhs 左操作数
/// @param rhs 右操作数
/// @param op 整数比较操作码
/// @return 生成出的比较结果值，失败时返回空指针
Value * IRGenerator::emitICmp(Value * lhs, Value * rhs, IRInstOperator op)
{
    lhs = normalizeIntegerOperand(lhs);
    if (!lhs) {
        return nullptr;
    }
    rhs = normalizeIntegerOperand(rhs);
    if (!rhs) {
        return nullptr;
    }

    auto * inst = new ICmpInst(currentFunction(), op, lhs, rhs, IntegerType::getTypeInt1());
    emitToBlock(inst);
    return inst;
}

/// @brief 生成浮点比较表达式对应的 IR
/// @param lhs 左操作数
/// @param rhs 右操作数
/// @param op 浮点比较操作码
/// @return 生成出的比较结果值，失败时返回空指针
Value * IRGenerator::emitFCmp(Value * lhs, Value * rhs, IRInstOperator op)
{
    lhs = convertToFloat(lhs);
    if (!lhs) {
        return nullptr;
    }
    rhs = convertToFloat(rhs);
    if (!rhs) {
        return nullptr;
    }

    auto * inst = new FCmpInst(currentFunction(), op, lhs, rhs, IntegerType::getTypeInt1());
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

    operand = normalizeIntegerOperand(operand);
    if (!operand) {
        return nullptr;
    }

    auto * inst = new BinaryInst(currentFunction(), IRInstOperator::IRINST_OP_SUB_I,
                                 module->newConstInt32(0), operand, IntegerType::getTypeInt32());
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

    if (operand->getType()->isFloatType()) {
        return emitFloatNot(operand);
    }

    return emitIntNot(operand);
}

Value * IRGenerator::emitIncDec(ast_node * node, bool increment, bool prefix)
{
    if (!node || node->sons.empty()) {
        return nullptr;
    }

    Value * addr = visitLValueAddress(node->sons[0]);
    if (!addr) {
        return nullptr;
    }

    Type * valueType = getAddressPointeeType(addr);
    if (valueType == nullptr || valueType->isArrayType()) {
        minic_log(LOG_ERROR, "自增/自减操作数必须是标量左值");
        return nullptr;
    }

    auto * oldValue = new LoadInst(currentFunction(), addr, valueType);
    emitToBlock(oldValue);

    Value * newValue = nullptr;
    if (valueType->isFloatType()) {
        auto op = increment ? IRInstOperator::IRINST_OP_ADD_F : IRInstOperator::IRINST_OP_SUB_F;
        newValue = new BinaryInst(currentFunction(), op, oldValue, module->newConstFloat(1.0f), valueType);
    } else {
        Value * oldI32 = normalizeIntegerOperand(oldValue);
        if (!oldI32) {
            return nullptr;
        }
        auto op = increment ? IRInstOperator::IRINST_OP_ADD_I : IRInstOperator::IRINST_OP_SUB_I;
        newValue = new BinaryInst(currentFunction(), op, oldI32, module->newConstInt32(1), IntegerType::getTypeInt32());
        if (valueType != IntegerType::getTypeInt32()) {
            newValue = convertValueToType(newValue, valueType);
            if (!newValue) {
                return nullptr;
            }
        }
    }

    emitToBlock(dynamic_cast<Instruction *>(newValue));
    emitToBlock(new StoreInst(currentFunction(), newValue, addr));

    return prefix ? newValue : oldValue;
}

Value * IRGenerator::emitIntNot(Value * operand)
{
    operand = normalizeIntegerOperand(operand);
    if (!operand) {
        return nullptr;
    }

    auto * inst = new ICmpInst(currentFunction(), IRInstOperator::IRINST_OP_EQ_I,
                               operand, module->newConstInt32(0), IntegerType::getTypeInt1());
    emitToBlock(inst);
    return inst;
}

Value * IRGenerator::emitFloatNot(Value * operand)
{
    auto * inst = new FCmpInst(currentFunction(), IRInstOperator::IRINST_OP_EQ_F,
                               operand, module->newConstFloat(0.0f), IntegerType::getTypeInt1());
    emitToBlock(inst);
    return inst;
}

/// @brief 根据值类型分发 boolize 逻辑
/// @param value 输入值
/// @return 已经是 i1 值或新生成的 i1 比较结果
Value * IRGenerator::emitBoolize(Value * value)
{
    if (value->getType()->isFloatType()) {
        return emitFloatBoolize(value);
    }

    return emitIntBoolize(value);
}

Value * IRGenerator::emitIntBoolize(Value * value)
{
    if (value->getType()->isInt1Type()) {
        return value;
    }

    value = normalizeIntegerOperand(value);
    if (!value) {
        return nullptr;
    }

    auto * inst = new ICmpInst(currentFunction(), IRInstOperator::IRINST_OP_NE_I,
                               value, module->newConstInt32(0), IntegerType::getTypeInt1());
    emitToBlock(inst);
    return inst;
}

Value * IRGenerator::emitFloatBoolize(Value * value)
{
    auto * inst = new FCmpInst(currentFunction(), IRInstOperator::IRINST_OP_NE_F,
                               value, module->newConstFloat(0.0f), IntegerType::getTypeInt1());
    emitToBlock(inst);
    return inst;
}

Value * IRGenerator::materializeBoolToInt32(Value * value)
{
    if (value == nullptr || !value->getType()->isInt1Type()) {
        return value;
    }

    auto * zext = new ZExtInst(currentFunction(), value, IntegerType::getTypeInt32());
    emitToBlock(zext);
    return zext;
}

Value * IRGenerator::normalizeIntegerOperand(Value * value)
{
    if (value == nullptr) {
        return nullptr;
    }

    if (value->getType()->isFloatType()) {
        minic_log(LOG_ERROR, "需要整型操作数");
        return nullptr;
    }

    return materializeBoolToInt32(value);
}

Value * IRGenerator::castFloatToInt32(Value * value)
{
    if (value == nullptr || !value->getType()->isFloatType()) {
        return value;
    }

    auto * castInst = new FPToSIInst(currentFunction(), value, IntegerType::getTypeInt32());
    emitToBlock(castInst);
    return castInst;
}

Value * IRGenerator::convertToInt32(Value * value)
{
    if (value == nullptr) {
        return nullptr;
    }

    if (value->getType()->isFloatType()) {
        return castFloatToInt32(value);
    }

    return normalizeIntegerOperand(value);
}

Value * IRGenerator::castInt32ToFloat(Value * value)
{
    if (value == nullptr) {
        return nullptr;
    }

    if (value->getType()->isFloatType()) {
        return value;
    }

    if (!value->getType()->isInt32Type()) {
        minic_log(LOG_ERROR, "需要 i32 操作数进行整型到浮点转换");
        return nullptr;
    }

    auto * castInst = new SIToFPInst(currentFunction(), value, FloatType::getTypeFloat());
    emitToBlock(castInst);
    return castInst;
}

Value * IRGenerator::convertToFloat(Value * value)
{
    if (value == nullptr || value->getType()->isFloatType()) {
        return value;
    }

    if (value->getType()->isInt1Type()) {
        value = materializeBoolToInt32(value);
    }

    return castInt32ToFloat(value);
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
        return convertToFloat(value);
    }

    if (targetType->isInt32Type()) {
        return convertToInt32(value);
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
