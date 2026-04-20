///
/// @file IREmitter.h
/// @brief 纯文本 LLVM IR 发射器，无 LLVM 库依赖
///
#pragma once

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "AST.h"

/// @brief 纯文本 LLVM IR 发射器，遍历 AST 直接生成 .ll 文本
class IREmitter {

public:
    /// @brief 构造函数
    /// @param root AST 根节点
    /// @param moduleName 模块名
    IREmitter(ast_node * root, const std::string & moduleName);

    /// @brief 运行 IR 生成
    bool run();

    /// @brief 将生成的 IR 文本写入文件
    bool writeToFile(const std::string & filename);

    /// @brief 获取生成的 IR 文本
    const std::string & getIRText() const { return moduleText; }

private:
    // ============ 值表示 ============

    /// @brief IR 值（SSA 寄存器或常量）
    struct IRValue {
        std::string text;  // 如 "%1", "42", "@g"
        bool isVoid = false;
    };

    // ============ 访问者方法 ============

    IRValue visitNode(ast_node * node);
    IRValue visitCompileUnit(ast_node * node);
    IRValue visitFuncDef(ast_node * node);
    IRValue visitFuncFormalParams(ast_node * node);
    IRValue visitFuncCall(ast_node * node);
    IRValue visitBlock(ast_node * node);
    IRValue visitReturn(ast_node * node);
    IRValue visitAssign(ast_node * node);
    IRValue visitDeclStmt(ast_node * node);
    IRValue visitVarDecl(ast_node * node);
    IRValue visitAdd(ast_node * node);
    IRValue visitSub(ast_node * node);
    IRValue visitMul(ast_node * node);
    IRValue visitDiv(ast_node * node);
    IRValue visitMod(ast_node * node);
    IRValue visitNeg(ast_node * node);
    IRValue visitLeafUint(ast_node * node);
    IRValue visitLeafVarId(ast_node * node);
    IRValue visitLeafType(ast_node * node);

    // ============ 作用域管理 ============

    void enterScope();
    void leaveScope();
    /// @brief 在当前作用域插入局部变量
    void insertVar(const std::string & name, const std::string & reg);
    /// @brief 查找局部变量的 alloca 寄存器名
    std::string findVar(const std::string & name);
    /// @brief 当前作用域查找
    std::string findCurrentScope(const std::string & name);

    // ============ 辅助函数 ============

    /// @brief 分配新的 SSA 寄存器编号，返回 "%N"
    std::string nextReg();

    /// @brief 分配新的标签编号，返回 "labelN"
    std::string nextLabel();

    /// @brief 类型转 IR 文本
    std::string typeStr(Type * ty);

    /// @brief 向当前函数体追加一条指令
    void emit(const std::string & inst);

    /// @brief 向模块顶层追加声明/定义
    void emitGlobal(const std::string & line);

    // ============ 函数信息 ============

    struct FuncInfo {
        std::string retType;   // "i32" or "void"
        int paramCount;
    };

    // ============ 成员变量 ============

    ast_node * root;
    std::string moduleName;

    /// @brief 完整模块文本
    std::string moduleText;

    /// @brief 全局声明/定义区域
    std::ostringstream globalSection;

    /// @brief 当前函数体
    std::ostringstream funcBody;

    /// @brief SSA 寄存器计数器（每个函数重置）
    int regCounter = 0;

    /// @brief 标签计数器（每个函数重置）
    int labelCounter = 0;

    /// @brief 当前函数的返回值 alloca 寄存器
    std::string retValReg;

    /// @brief 当前函数的出口标签
    std::string exitLabel;

    /// @brief 当前函数的返回类型
    std::string currentRetType;

    /// @brief 是否已在当前基本块写入终结指令
    bool hasTerminator = false;

    /// @brief 已知函数信息表
    std::unordered_map<std::string, FuncInfo> funcTable;

    /// @brief 全局变量集合
    std::unordered_map<std::string, bool> globalVars;

    /// @brief 是否在函数内
    bool inFunction = false;

    /// @brief 作用域栈：变量名 → alloca 寄存器
    std::vector<std::unordered_map<std::string, std::string>> scopeStack;

    /// @brief AST 节点类型到访问函数的映射
    using VisitorFunc = IRValue (IREmitter::*)(ast_node *);
    std::unordered_map<ast_operator_type, VisitorFunc> handlers;
};
