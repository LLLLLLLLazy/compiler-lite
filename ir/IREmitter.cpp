///
/// @file IREmitter.cpp
/// @brief 纯文本 LLVM IR 发射器实现，无 LLVM 库依赖
///

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "IREmitter.h"
#include "Common.h"
#include "IntegerType.h"
#include "VoidType.h"

// ==================== 构造函数 ====================

IREmitter::IREmitter(ast_node * _root, const std::string & _moduleName)
    : root(_root), moduleName(_moduleName)
{
    // 叶子节点
    handlers[ast_operator_type::AST_OP_LEAF_LITERAL_UINT] = &IREmitter::visitLeafUint;
    handlers[ast_operator_type::AST_OP_LEAF_VAR_ID] = &IREmitter::visitLeafVarId;
    handlers[ast_operator_type::AST_OP_LEAF_TYPE] = &IREmitter::visitLeafType;

    // 表达式运算
    handlers[ast_operator_type::AST_OP_ADD] = &IREmitter::visitAdd;
    handlers[ast_operator_type::AST_OP_SUB] = &IREmitter::visitSub;
    handlers[ast_operator_type::AST_OP_MUL] = &IREmitter::visitMul;
    handlers[ast_operator_type::AST_OP_DIV] = &IREmitter::visitDiv;
    handlers[ast_operator_type::AST_OP_MOD] = &IREmitter::visitMod;
    handlers[ast_operator_type::AST_OP_LT] = &IREmitter::visitLt;
    handlers[ast_operator_type::AST_OP_GT] = &IREmitter::visitGt;
    handlers[ast_operator_type::AST_OP_LE] = &IREmitter::visitLe;
    handlers[ast_operator_type::AST_OP_GE] = &IREmitter::visitGe;
    handlers[ast_operator_type::AST_OP_EQ] = &IREmitter::visitEq;
    handlers[ast_operator_type::AST_OP_NE] = &IREmitter::visitNe;
    handlers[ast_operator_type::AST_OP_LAND] = &IREmitter::visitLogicalAnd;
    handlers[ast_operator_type::AST_OP_LOR] = &IREmitter::visitLogicalOr;
    handlers[ast_operator_type::AST_OP_NEG] = &IREmitter::visitNeg;
    handlers[ast_operator_type::AST_OP_NOT] = &IREmitter::visitNot;

    // 语句
    handlers[ast_operator_type::AST_OP_ASSIGN] = &IREmitter::visitAssign;
    handlers[ast_operator_type::AST_OP_RETURN] = &IREmitter::visitReturn;
    handlers[ast_operator_type::AST_OP_IF] = &IREmitter::visitIf;
    handlers[ast_operator_type::AST_OP_WHILE] = &IREmitter::visitWhile;
    handlers[ast_operator_type::AST_OP_BREAK] = &IREmitter::visitBreak;
    handlers[ast_operator_type::AST_OP_CONTINUE] = &IREmitter::visitContinue;

    // 函数调用
    handlers[ast_operator_type::AST_OP_FUNC_CALL] = &IREmitter::visitFuncCall;

    // 函数定义
    handlers[ast_operator_type::AST_OP_FUNC_DEF] = &IREmitter::visitFuncDef;
    handlers[ast_operator_type::AST_OP_FUNC_FORMAL_PARAMS] = &IREmitter::visitFuncFormalParams;

    // 变量声明
    handlers[ast_operator_type::AST_OP_DECL_STMT] = &IREmitter::visitDeclStmt;
    handlers[ast_operator_type::AST_OP_VAR_DECL] = &IREmitter::visitVarDecl;

    // 语句块
    handlers[ast_operator_type::AST_OP_BLOCK] = &IREmitter::visitBlock;

    // 编译单元
    handlers[ast_operator_type::AST_OP_COMPILE_UNIT] = &IREmitter::visitCompileUnit;
}

// ==================== 运行 ====================

bool IREmitter::run()
{
    // 声明内置函数
    emitGlobal("declare void @putint(i32)");
    emitGlobal("declare i32 @getint()");
    emitGlobal("declare void @putch(i32)");
    emitGlobal("declare i32 @getch()");
    emitGlobal("");

    funcTable["putint"] = {"void", 1};
    funcTable["getint"] = {"i32", 0};
    funcTable["putch"] = {"void", 1};
    funcTable["getch"] = {"i32", 0};

    IRValue result = visitNode(root);

    if (result.text.empty() && !result.isVoid) {
        return false;
    }

    // 组装最终文本
    std::ostringstream out;
    out << "; ModuleID = '" << moduleName << "'\n";
    out << "source_filename = \"" << moduleName << "\"\n";
    out << "\n";
    out << globalSection.str();

    moduleText = out.str();
    return true;
}

bool IREmitter::writeToFile(const std::string & filename)
{
    std::ofstream ofs(filename);
    if (!ofs.is_open()) {
        minic_log(LOG_ERROR, "无法打开输出文件: %s", filename.c_str());
        return false;
    }
    ofs << moduleText;
    return true;
}

// ==================== 作用域管理 ====================

void IREmitter::enterScope()
{
    scopeStack.emplace_back();
}

void IREmitter::leaveScope()
{
    if (!scopeStack.empty()) {
        scopeStack.pop_back();
    }
}

void IREmitter::insertVar(const std::string & name, const std::string & reg)
{
    if (!scopeStack.empty()) {
        scopeStack.back()[name] = reg;
    }
}

std::string IREmitter::findVar(const std::string & name)
{
    for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return found->second;
        }
    }
    return "";
}

std::string IREmitter::findCurrentScope(const std::string & name)
{
    if (!scopeStack.empty()) {
        auto & top = scopeStack.back();
        auto found = top.find(name);
        if (found != top.end()) {
            return found->second;
        }
    }
    return "";
}

// ==================== 辅助函数 ====================

std::string IREmitter::nextReg()
{
    return "%" + std::to_string(regCounter++);
}

std::string IREmitter::nextLabel()
{
    return "label" + std::to_string(labelCounter++);
}

std::string IREmitter::typeStr(Type * ty)
{
    if (ty->isVoidType()) {
        return "void";
    }
    return "i32";
}

std::string IREmitter::emitCondValue(const IRValue & value)
{
    std::string reg = nextReg();
    emit(reg + " = icmp ne i32 " + value.text + ", 0");
    return reg;
}

std::string IREmitter::emitBoolToInt(const std::string & boolReg)
{
    std::string reg = nextReg();
    emit(reg + " = zext i1 " + boolReg + " to i32");
    return reg;
}

void IREmitter::emit(const std::string & inst)
{
    funcBody << "  " << inst << "\n";
}

void IREmitter::emitGlobal(const std::string & line)
{
    globalSection << line << "\n";
}

void IREmitter::startBlock(const std::string & label)
{
    funcBody << label << ":\n";
    hasTerminator = false;
}

// ==================== 节点分发 ====================

IREmitter::IRValue IREmitter::visitNode(ast_node * node)
{
    if (!node) {
        return {};
    }

    auto it = handlers.find(node->node_type);
    if (it == handlers.end()) {
        minic_log(LOG_ERROR, "Unknown AST node type: %d", (int) node->node_type);
        return {};
    }

    return (this->*(it->second))(node);
}

// ==================== 编译单元 ====================

IREmitter::IRValue IREmitter::visitCompileUnit(ast_node * node)
{
    enterScope();

    for (auto son : node->sons) {
        IRValue result = visitNode(son);
        if (result.text.empty() && !result.isVoid) {
            return {};
        }
    }

    leaveScope();

    return {"__ok", false};
}

// ==================== 函数定义 ====================

IREmitter::IRValue IREmitter::visitFuncDef(ast_node * node)
{
    ast_node * type_node = node->sons[0];
    ast_node * name_node = node->sons[1];
    ast_node * param_node = node->sons[2];
    ast_node * block_node = node->sons[3];

    std::string retType = typeStr(type_node->type);
    std::string funcName = name_node->name;

    // 收集形参
    std::vector<std::string> paramTypes;
    std::vector<std::string> paramNames;
    for (auto & paramChild : param_node->sons) {
        if (paramChild->sons.size() >= 2) {
            paramTypes.push_back(typeStr(paramChild->sons[0]->type));
            paramNames.push_back(paramChild->sons[1]->name);
        }
    }

    // 记录函数信息
    funcTable[funcName] = {retType, (int) paramTypes.size()};

    // 重置函数级计数器
    regCounter = 0;
    labelCounter = 0;
    funcBody.str("");
    funcBody.clear();
    hasTerminator = false;
    inFunction = true;
    currentRetType = retType;
    breakLabels.clear();
    continueLabels.clear();

    // 为每个参数预分配寄存器编号（LLVM IR 中函数参数从 %0 开始）
    // 先跳过参数的寄存器编号
    regCounter = (int) paramTypes.size();

    // 进入函数作用域
    enterScope();

    // entry 标签占一个编号
    std::string entryLabel = std::to_string(regCounter++);

    // 如果非 void，分配 retval alloca
    retValReg = "";
    if (retType != "void") {
        retValReg = nextReg();
        emit(retValReg + " = alloca i32");
        emit("store i32 0, i32* " + retValReg);
    }

    // 出口标签
    exitLabel = nextLabel();

    // 为形参创建 alloca 和 store
    for (size_t i = 0; i < paramNames.size(); i++) {
        std::string paramReg = "%" + std::to_string(i);
        std::string allocaReg = nextReg();
        emit(allocaReg + " = alloca i32");
        emit("store i32 " + paramReg + ", i32* " + allocaReg);
        insertVar(paramNames[i], allocaReg);
    }

    // 函数体不再需要额外作用域
    block_node->needScope = false;

    // 遍历函数体
    IRValue blockResult = visitBlock(block_node);
    if (blockResult.text.empty() && !blockResult.isVoid) {
        leaveScope();
        inFunction = false;
        return {};
    }

    // 如果当前没有终结指令，跳转到出口
    if (!hasTerminator) {
        emit("br label %" + exitLabel);
    }

    // 生成出口基本块
    funcBody << exitLabel << ":\n";
    hasTerminator = false;
    if (!retValReg.empty()) {
        std::string loadReg = nextReg();
        emit(loadReg + " = load i32, i32* " + retValReg);
        emit("ret i32 " + loadReg);
    } else {
        emit("ret void");
    }

    // 退出函数作用域
    leaveScope();

    // 构建函数签名
    std::ostringstream sig;
    sig << "define " << retType << " @" << funcName << "(";
    for (size_t i = 0; i < paramTypes.size(); i++) {
        if (i > 0) sig << ", ";
        sig << paramTypes[i] << " %" << i;
    }
    sig << ") {";

    // 输出完整函数
    emitGlobal(sig.str());
    globalSection << funcBody.str();
    emitGlobal("}");
    emitGlobal("");

    inFunction = false;
    return {"@" + funcName, false};
}

// ==================== 形参 ====================

IREmitter::IRValue IREmitter::visitFuncFormalParams(ast_node * node)
{
    return {"__ok", false};
}

// ==================== 语句块 ====================

IREmitter::IRValue IREmitter::visitBlock(ast_node * node)
{
    if (node->needScope) {
        enterScope();
    }

    IRValue lastVal = {"__ok", false};

    for (auto son : node->sons) {
        if (hasTerminator) {
            break;
        }

        lastVal = visitNode(son);
        if (lastVal.text.empty() && !lastVal.isVoid) {
            if (node->needScope) {
                leaveScope();
            }
            return {};
        }
    }

    if (node->needScope) {
        leaveScope();
    }

    return lastVal;
}

// ==================== return ====================

IREmitter::IRValue IREmitter::visitReturn(ast_node * node)
{
    if (!node->sons.empty()) {
        IRValue retVal = visitNode(node->sons[0]);
        if (retVal.text.empty() && !retVal.isVoid) {
            return {};
        }

        if (!retValReg.empty()) {
            emit("store i32 " + retVal.text + ", i32* " + retValReg);
        }
    }

    emit("br label %" + exitLabel);
    hasTerminator = true;

    return {"__ok", false};
}

IREmitter::IRValue IREmitter::visitIf(ast_node * node)
{
    ast_node * condNode = node->sons[0];
    ast_node * thenNode = node->sons[1];
    ast_node * elseNode = node->sons.size() >= 3 ? node->sons[2] : nullptr;

    IRValue condVal = visitNode(condNode);
    if (condVal.text.empty() && !condVal.isVoid) {
        return {};
    }

    std::string condReg = emitCondValue(condVal);
    std::string thenLabel = nextLabel();
    std::string endLabel = nextLabel();

    if (!elseNode) {
        emit("br i1 " + condReg + ", label %" + thenLabel + ", label %" + endLabel);
        hasTerminator = true;

        startBlock(thenLabel);
        IRValue thenResult = visitNode(thenNode);
        if (thenResult.text.empty() && !thenResult.isVoid) {
            return {};
        }
        if (!hasTerminator) {
            emit("br label %" + endLabel);
            hasTerminator = true;
        }

        startBlock(endLabel);
        return {"__ok", false};
    }

    std::string elseLabel = nextLabel();
    emit("br i1 " + condReg + ", label %" + thenLabel + ", label %" + elseLabel);
    hasTerminator = true;

    startBlock(thenLabel);
    IRValue thenResult = visitNode(thenNode);
    if (thenResult.text.empty() && !thenResult.isVoid) {
        return {};
    }
    bool thenTerminated = hasTerminator;
    if (!thenTerminated) {
        emit("br label %" + endLabel);
        hasTerminator = true;
    }

    startBlock(elseLabel);
    IRValue elseResult = visitNode(elseNode);
    if (elseResult.text.empty() && !elseResult.isVoid) {
        return {};
    }
    bool elseTerminated = hasTerminator;
    if (!elseTerminated) {
        emit("br label %" + endLabel);
        hasTerminator = true;
    }

    if (thenTerminated && elseTerminated) {
        hasTerminator = true;
        return {"__ok", false};
    }

    startBlock(endLabel);
    return {"__ok", false};
}

IREmitter::IRValue IREmitter::visitWhile(ast_node * node)
{
    ast_node * condNode = node->sons[0];
    ast_node * bodyNode = node->sons[1];

    std::string condLabel = nextLabel();
    std::string bodyLabel = nextLabel();
    std::string endLabel = nextLabel();

    emit("br label %" + condLabel);
    hasTerminator = true;

    startBlock(condLabel);
    IRValue condVal = visitNode(condNode);
    if (condVal.text.empty() && !condVal.isVoid) {
        return {};
    }
    std::string condReg = emitCondValue(condVal);
    emit("br i1 " + condReg + ", label %" + bodyLabel + ", label %" + endLabel);
    hasTerminator = true;

    breakLabels.push_back(endLabel);
    continueLabels.push_back(condLabel);

    startBlock(bodyLabel);
    IRValue bodyResult = visitNode(bodyNode);
    if (bodyResult.text.empty() && !bodyResult.isVoid) {
        breakLabels.pop_back();
        continueLabels.pop_back();
        return {};
    }
    if (!hasTerminator) {
        emit("br label %" + condLabel);
        hasTerminator = true;
    }

    breakLabels.pop_back();
    continueLabels.pop_back();

    startBlock(endLabel);
    return {"__ok", false};
}

IREmitter::IRValue IREmitter::visitBreak(ast_node * node)
{
    (void) node;

    if (breakLabels.empty()) {
        minic_log(LOG_ERROR, "break 语句不在循环内");
        return {};
    }

    emit("br label %" + breakLabels.back());
    hasTerminator = true;
    return {"__ok", false};
}

IREmitter::IRValue IREmitter::visitContinue(ast_node * node)
{
    (void) node;

    if (continueLabels.empty()) {
        minic_log(LOG_ERROR, "continue 语句不在循环内");
        return {};
    }

    emit("br label %" + continueLabels.back());
    hasTerminator = true;
    return {"__ok", false};
}

// ==================== 赋值 ====================

IREmitter::IRValue IREmitter::visitAssign(ast_node * node)
{
    ast_node * lhs_node = node->sons[0];
    ast_node * rhs_node = node->sons[1];

    IRValue rhs = visitNode(rhs_node);
    if (rhs.text.empty() && !rhs.isVoid) {
        return {};
    }

    // 查找局部变量
    std::string lhsReg = findVar(lhs_node->name);
    if (!lhsReg.empty()) {
        emit("store i32 " + rhs.text + ", i32* " + lhsReg);
        return rhs;
    }

    // 查找全局变量
    if (globalVars.count(lhs_node->name)) {
        emit("store i32 " + rhs.text + ", i32* @" + lhs_node->name);
        return rhs;
    }

    minic_log(LOG_ERROR, "变量 %s 未定义", lhs_node->name.c_str());
    return {};
}

// ==================== 变量声明 ====================

IREmitter::IRValue IREmitter::visitDeclStmt(ast_node * node)
{
    for (auto & child : node->sons) {
        IRValue result = visitVarDecl(child);
        if (result.text.empty() && !result.isVoid) {
            return {};
        }
    }
    return {"__ok", false};
}

IREmitter::IRValue IREmitter::visitVarDecl(ast_node * node)
{
    Type * declType = node->sons[0]->type;
    std::string varName = node->sons[1]->name;
    std::string ty = typeStr(declType);

    if (inFunction) {
        // 局部变量
        if (!findCurrentScope(varName).empty()) {
            minic_log(LOG_ERROR, "变量(%s)已经存在", varName.c_str());
            return {};
        }

        std::string allocaReg = nextReg();
        emit(allocaReg + " = alloca i32");
        insertVar(varName, allocaReg);

        // 初始化
        if (node->sons.size() >= 3) {
            IRValue initVal = visitNode(node->sons[2]);
            if (initVal.text.empty() && !initVal.isVoid) {
                return {};
            }
            emit("store i32 " + initVal.text + ", i32* " + allocaReg);
        }

        return {allocaReg, false};
    } else {
        // 全局变量
        std::string initStr = "0";

        if (node->sons.size() >= 3) {
            ast_node * initNode = node->sons[2];
            if (initNode->node_type == ast_operator_type::AST_OP_LEAF_LITERAL_UINT) {
                initStr = std::to_string(initNode->integer_val);
            } else {
                minic_log(LOG_ERROR, "全局变量(%s)只支持常量初始化", varName.c_str());
                return {};
            }
        }

        emitGlobal("@" + varName + " = global i32 " + initStr);
        globalVars[varName] = true;

        return {"@" + varName, false};
    }
}

// ==================== 二元运算 ====================

IREmitter::IRValue IREmitter::visitAdd(ast_node * node)
{
    IRValue lhs = visitNode(node->sons[0]);
    if (lhs.text.empty()) return {};
    IRValue rhs = visitNode(node->sons[1]);
    if (rhs.text.empty()) return {};

    std::string reg = nextReg();
    emit(reg + " = add i32 " + lhs.text + ", " + rhs.text);
    return {reg, false};
}

IREmitter::IRValue IREmitter::visitSub(ast_node * node)
{
    IRValue lhs = visitNode(node->sons[0]);
    if (lhs.text.empty()) return {};
    IRValue rhs = visitNode(node->sons[1]);
    if (rhs.text.empty()) return {};

    std::string reg = nextReg();
    emit(reg + " = sub i32 " + lhs.text + ", " + rhs.text);
    return {reg, false};
}

IREmitter::IRValue IREmitter::visitMul(ast_node * node)
{
    IRValue lhs = visitNode(node->sons[0]);
    if (lhs.text.empty()) return {};
    IRValue rhs = visitNode(node->sons[1]);
    if (rhs.text.empty()) return {};

    std::string reg = nextReg();
    emit(reg + " = mul i32 " + lhs.text + ", " + rhs.text);
    return {reg, false};
}

IREmitter::IRValue IREmitter::visitDiv(ast_node * node)
{
    IRValue lhs = visitNode(node->sons[0]);
    if (lhs.text.empty()) return {};
    IRValue rhs = visitNode(node->sons[1]);
    if (rhs.text.empty()) return {};

    std::string reg = nextReg();
    emit(reg + " = sdiv i32 " + lhs.text + ", " + rhs.text);
    return {reg, false};
}

IREmitter::IRValue IREmitter::visitMod(ast_node * node)
{
    IRValue lhs = visitNode(node->sons[0]);
    if (lhs.text.empty()) return {};
    IRValue rhs = visitNode(node->sons[1]);
    if (rhs.text.empty()) return {};

    std::string reg = nextReg();
    emit(reg + " = srem i32 " + lhs.text + ", " + rhs.text);
    return {reg, false};
}

IREmitter::IRValue IREmitter::visitLt(ast_node * node)
{
    IRValue lhs = visitNode(node->sons[0]);
    if (lhs.text.empty()) return {};
    IRValue rhs = visitNode(node->sons[1]);
    if (rhs.text.empty()) return {};

    std::string boolReg = nextReg();
    emit(boolReg + " = icmp slt i32 " + lhs.text + ", " + rhs.text);
    return {emitBoolToInt(boolReg), false};
}

IREmitter::IRValue IREmitter::visitGt(ast_node * node)
{
    IRValue lhs = visitNode(node->sons[0]);
    if (lhs.text.empty()) return {};
    IRValue rhs = visitNode(node->sons[1]);
    if (rhs.text.empty()) return {};

    std::string boolReg = nextReg();
    emit(boolReg + " = icmp sgt i32 " + lhs.text + ", " + rhs.text);
    return {emitBoolToInt(boolReg), false};
}

IREmitter::IRValue IREmitter::visitLe(ast_node * node)
{
    IRValue lhs = visitNode(node->sons[0]);
    if (lhs.text.empty()) return {};
    IRValue rhs = visitNode(node->sons[1]);
    if (rhs.text.empty()) return {};

    std::string boolReg = nextReg();
    emit(boolReg + " = icmp sle i32 " + lhs.text + ", " + rhs.text);
    return {emitBoolToInt(boolReg), false};
}

IREmitter::IRValue IREmitter::visitGe(ast_node * node)
{
    IRValue lhs = visitNode(node->sons[0]);
    if (lhs.text.empty()) return {};
    IRValue rhs = visitNode(node->sons[1]);
    if (rhs.text.empty()) return {};

    std::string boolReg = nextReg();
    emit(boolReg + " = icmp sge i32 " + lhs.text + ", " + rhs.text);
    return {emitBoolToInt(boolReg), false};
}

IREmitter::IRValue IREmitter::visitEq(ast_node * node)
{
    IRValue lhs = visitNode(node->sons[0]);
    if (lhs.text.empty()) return {};
    IRValue rhs = visitNode(node->sons[1]);
    if (rhs.text.empty()) return {};

    std::string boolReg = nextReg();
    emit(boolReg + " = icmp eq i32 " + lhs.text + ", " + rhs.text);
    return {emitBoolToInt(boolReg), false};
}

IREmitter::IRValue IREmitter::visitNe(ast_node * node)
{
    IRValue lhs = visitNode(node->sons[0]);
    if (lhs.text.empty()) return {};
    IRValue rhs = visitNode(node->sons[1]);
    if (rhs.text.empty()) return {};

    std::string boolReg = nextReg();
    emit(boolReg + " = icmp ne i32 " + lhs.text + ", " + rhs.text);
    return {emitBoolToInt(boolReg), false};
}

IREmitter::IRValue IREmitter::visitLogicalAnd(ast_node * node)
{
    IRValue lhs = visitNode(node->sons[0]);
    if (lhs.text.empty()) return {};

    std::string resultPtr = nextReg();
    emit(resultPtr + " = alloca i32");
    emit("store i32 0, i32* " + resultPtr);

    std::string rhsLabel = nextLabel();
    std::string endLabel = nextLabel();

    std::string lhsCond = emitCondValue(lhs);
    emit("br i1 " + lhsCond + ", label %" + rhsLabel + ", label %" + endLabel);
    hasTerminator = true;

    startBlock(rhsLabel);
    IRValue rhs = visitNode(node->sons[1]);
    if (rhs.text.empty()) return {};
    std::string rhsCond = emitCondValue(rhs);
    std::string rhsInt = emitBoolToInt(rhsCond);
    emit("store i32 " + rhsInt + ", i32* " + resultPtr);
    emit("br label %" + endLabel);
    hasTerminator = true;

    startBlock(endLabel);
    std::string reg = nextReg();
    emit(reg + " = load i32, i32* " + resultPtr);
    return {reg, false};
}

IREmitter::IRValue IREmitter::visitLogicalOr(ast_node * node)
{
    IRValue lhs = visitNode(node->sons[0]);
    if (lhs.text.empty()) return {};

    std::string resultPtr = nextReg();
    emit(resultPtr + " = alloca i32");
    emit("store i32 1, i32* " + resultPtr);

    std::string rhsLabel = nextLabel();
    std::string endLabel = nextLabel();

    std::string lhsCond = emitCondValue(lhs);
    emit("br i1 " + lhsCond + ", label %" + endLabel + ", label %" + rhsLabel);
    hasTerminator = true;

    startBlock(rhsLabel);
    IRValue rhs = visitNode(node->sons[1]);
    if (rhs.text.empty()) return {};
    std::string rhsCond = emitCondValue(rhs);
    std::string rhsInt = emitBoolToInt(rhsCond);
    emit("store i32 " + rhsInt + ", i32* " + resultPtr);
    emit("br label %" + endLabel);
    hasTerminator = true;

    startBlock(endLabel);
    std::string reg = nextReg();
    emit(reg + " = load i32, i32* " + resultPtr);
    return {reg, false};
}

IREmitter::IRValue IREmitter::visitNeg(ast_node * node)
{
    IRValue operand = visitNode(node->sons[0]);
    if (operand.text.empty()) return {};

    std::string reg = nextReg();
    emit(reg + " = sub i32 0, " + operand.text);
    return {reg, false};
}

IREmitter::IRValue IREmitter::visitNot(ast_node * node)
{
    IRValue operand = visitNode(node->sons[0]);
    if (operand.text.empty()) return {};

    std::string boolReg = nextReg();
    emit(boolReg + " = icmp eq i32 " + operand.text + ", 0");
    return {emitBoolToInt(boolReg), false};
}

// ==================== 叶子节点 ====================

IREmitter::IRValue IREmitter::visitLeafUint(ast_node * node)
{
    return {std::to_string(node->integer_val), false};
}

IREmitter::IRValue IREmitter::visitLeafVarId(ast_node * node)
{
    // 局部变量
    std::string allocaReg = findVar(node->name);
    if (!allocaReg.empty()) {
        std::string reg = nextReg();
        emit(reg + " = load i32, i32* " + allocaReg);
        return {reg, false};
    }

    // 全局变量
    if (globalVars.count(node->name)) {
        std::string reg = nextReg();
        emit(reg + " = load i32, i32* @" + node->name);
        return {reg, false};
    }

    minic_log(LOG_ERROR, "变量 %s 未定义", node->name.c_str());
    return {};
}

IREmitter::IRValue IREmitter::visitLeafType(ast_node * node)
{
    return {"__ok", false};
}

// ==================== 函数调用 ====================

IREmitter::IRValue IREmitter::visitFuncCall(ast_node * node)
{
    std::string funcName = node->sons[0]->name;
    ast_node * paramsNode = node->sons[1];

    // 查找函数
    auto it = funcTable.find(funcName);
    if (it == funcTable.end()) {
        minic_log(LOG_ERROR, "函数(%s)未定义或声明", funcName.c_str());
        return {};
    }

    const FuncInfo & fi = it->second;

    // 收集实参
    std::vector<IRValue> args;
    for (auto son : paramsNode->sons) {
        IRValue argVal = visitNode(son);
        if (argVal.text.empty() && !argVal.isVoid) {
            return {};
        }
        args.push_back(argVal);
    }

    if ((int) args.size() != fi.paramCount) {
        minic_log(LOG_ERROR, "函数(%s)参数个数不匹配", funcName.c_str());
        return {};
    }

    // 构建参数列表
    std::string argStr;
    for (size_t i = 0; i < args.size(); i++) {
        if (i > 0) argStr += ", ";
        argStr += "i32 " + args[i].text;
    }

    if (fi.retType == "void") {
        emit("call void @" + funcName + "(" + argStr + ")");
        return {"__ok", true};
    } else {
        std::string reg = nextReg();
        emit(reg + " = call i32 @" + funcName + "(" + argStr + ")");
        return {reg, false};
    }
}
