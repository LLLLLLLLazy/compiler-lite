///
/// @file LLVMIREmitter.cpp
/// @brief LLVM IR 文本发射器（Phase 4 纯打印器）
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

LLVMIREmitter::LLVMIREmitter(Module * _module, std::string _moduleName)
    : module(_module), moduleName(std::move(_moduleName))
{}

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
        lines.emplace_back(
            global->getIRName() + " = global " + llvmType(global->getType()) + " " +
            std::to_string(global->getInitIntValue()));
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

bool LLVMIREmitter::writeToFile(const std::string & filename) const
{
    std::ofstream outFile(filename, std::ios::out | std::ios::trunc);
    if (!outFile.is_open()) {
        return false;
    }

    outFile << llvmIR;
    return outFile.good();
}

void LLVMIREmitter::emitFunction(Function * function, std::vector<std::string> & lines)
{
    lines.emplace_back("define " + formatFunctionSignature(function, true) + " {");

    for (auto * bb : function->getBlocks()) {
        // Skip completely empty blocks (unreachable dead-code sinks)
        if (bb->getInstructions().empty()) {
            continue;
        }

        // Print block label for all blocks (including the entry block, so phi
        // nodes in successor blocks can reference it by name)
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

        // Safety: add a default terminator if the block is somehow unterminated
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

std::string LLVMIREmitter::llvmType(Type * type) const
{
    return type->toString();
}

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

