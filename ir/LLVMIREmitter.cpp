///
/// @file LLVMIREmitter.cpp
/// @brief LLVM IR 文本发射器
///
/// 只负责将结构化 IR（Module / Function / BasicBlock / Instruction）
/// 序列化为 LLVM IR 文本，不承担任何 lowering / 语义补全工作。
///

#include "LLVMIREmitter.h"

#include <fstream>

#include "BasicBlock.h"
#include "Function.h"
#include "GlobalVariable.h"
#include "Instruction.h"
#include "Module.h"

/// @brief 构造 LLVM IR 文本发射器
/// @param _module 待发射的模块
/// @param _moduleName 模块名称
LLVMIREmitter::LLVMIREmitter(Module * _module, std::string _moduleName)
    : module(_module), moduleName(std::move(_moduleName))
{}

/// @brief 生成完整的 LLVM IR 文本
/// @return true 表示生成成功，false 表示模块无效
bool LLVMIREmitter::run()
{
    if (module == nullptr) {
        return false;
    }

    std::vector<std::string> lines;
    lines.emplace_back("; ModuleID = '" + moduleName + "'");
    lines.emplace_back("source_filename = \"" + escapeString(moduleName) + "\"");
    lines.emplace_back("");

    bool hasBuiltin = false;
    for (auto * func : module->getFunctionList()) {
        if (!func->isBuiltin()) {
            continue;
        }

        lines.emplace_back("declare " + formatFunctionSignature(func, false));
        hasBuiltin = true;
    }

    if (hasBuiltin) {
        lines.emplace_back("");
    }

    bool hasGlobal = false;
    for (auto * global : module->getGlobalVariables()) {
        std::string initText = global->getType()->isArrayType() ? "zeroinitializer" : std::to_string(global->getInitIntValue());
        lines.emplace_back(global->getIRName() + " = global " + llvmType(global->getType()) + " " + initText);
        hasGlobal = true;
    }

    if (hasGlobal) {
        lines.emplace_back("");
    }

    bool firstFunction = true;
    for (auto * func : module->getFunctionList()) {
        if (func->isBuiltin()) {
            continue;
        }

        if (!firstFunction) {
            lines.emplace_back("");
        }

        emitFunction(func, lines);
        firstFunction = false;
    }

    llvmIR.clear();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        llvmIR += lines[i];
        if (i + 1 < lines.size()) {
            llvmIR += "\n";
        }
    }

    if (!llvmIR.empty()) {
        llvmIR += "\n";
    }

    return true;
}

/// @brief 将 LLVM IR 文本写入文件
/// @param filename 输出文件名
/// @return true 表示写入成功，false 表示写入失败
bool LLVMIREmitter::writeToFile(const std::string & filename) const
{
    std::ofstream outFile(filename, std::ios::out | std::ios::trunc);
    if (!outFile.is_open()) {
        return false;
    }

    outFile << llvmIR;
    return outFile.good();
}

/// @brief 输出单个函数的 define 语句块
/// @param function 待输出的函数
/// @param lines 结果文本行数组
void LLVMIREmitter::emitFunction(Function * function, std::vector<std::string> & lines)
{
    lines.emplace_back("define " + formatFunctionSignature(function, true) + " {");

    for (auto * bb : function->getBlocks()) {
        // 跳过完全空的基本块，这类块通常是不可达的死代码汇合块
        if (bb->getInstructions().empty()) {
            continue;
        }

        // 为所有基本块输出标签，包括入口块，便于后继块中的 phi 节点按名字引用。
        {
            std::string label = bb->getIRName();
            if (!label.empty() && label.front() == '%') {
                label = label.substr(1);
            }
            if (!label.empty()) {
                lines.emplace_back(label + ":");
            }
        }


        for (auto * inst : bb->getInstructions()) {
            std::string instStr;
            inst->toString(instStr);
            if (!instStr.empty()) {
                lines.emplace_back("  " + instStr);
            }
        }

        // 保护性处理：如果基本块意外没有终结指令，则补一个默认返回。
        if (!bb->isTerminated()) {
            if (function->getReturnType()->isVoidType()) {
                lines.emplace_back("  ret void");
            } else {
                lines.emplace_back("  ret " + llvmType(function->getReturnType()) + " 0");
            }
        }
    }

    lines.emplace_back("}");
}

/// @brief 格式化函数签名文本
/// @param function 待格式化的函数
/// @param withNames 是否输出形参名
/// @return LLVM IR 形式的函数签名
std::string LLVMIREmitter::formatFunctionSignature(Function * function, bool withNames) const
{
    std::string signature = llvmType(function->getReturnType()) + " " + function->getIRName() + "(";

    bool firstParam = true;
    for (auto * param : function->getParams()) {
        if (!firstParam) {
            signature += ", ";
        }

        signature += llvmType(param->getType());
        if (withNames) {
            signature += " " + param->getIRName();
        }

        firstParam = false;
    }

    signature += ")";
    return signature;
}

/// @brief 将内部类型转换为 LLVM IR 类型字符串
/// @param type 待转换的类型
/// @return LLVM IR 类型文本
std::string LLVMIREmitter::llvmType(Type * type) const
{
    return type->toString();
}

/// @brief 转义字符串中的特殊字符
/// @param text 原始字符串
/// @return 转义后的字符串
std::string LLVMIREmitter::escapeString(const std::string & text) const
{
    std::string escaped;
    escaped.reserve(text.size());

    for (char ch : text) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }

    return escaped;
}
