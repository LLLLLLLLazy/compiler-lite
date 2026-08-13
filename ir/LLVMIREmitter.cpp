///
/// @file LLVMIREmitter.cpp
/// @brief LLVM IR 文本发射器
///
/// 只负责将结构化 IR（Module / Function / BasicBlock / Instruction）
/// 序列化为 LLVM IR 文本，并完成 LLVM ABI 要求的文本适配
///

#include "LLVMIREmitter.h"

#include <cstring>
#include <iomanip>
#include <fstream>
#include <sstream>

#include "ArrayType.h"
#include "BasicBlock.h"
#include "ArrayType.h"
#include "Function.h"
#include "GlobalVariable.h"
#include "Instruction.h"
#include "Instructions/CallInst.h"
#include "Module.h"

namespace {

std::string formatFloatGlobalInit(float value)
{
    double asDouble = static_cast<double>(value);
    std::uint64_t bits = 0;
    std::memcpy(&bits, &asDouble, sizeof(bits));

    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << bits;
    return oss.str();
}

std::string formatIntArrayInit(
    Type * type, const std::vector<int32_t> & values, std::size_t & cursor, bool includeType)
{
    auto * arrayType = dynamic_cast<ArrayType *>(type);
    if (arrayType == nullptr) {
        int32_t value = 0;
        if (cursor < values.size()) {
            value = values[cursor];
        }
        ++cursor;
        return type->toString() + " " + std::to_string(value);
    }

    std::vector<std::string> elems;
    Type * elemType = arrayType->getElementType();
    elems.reserve(arrayType->getNumElements());
    for (int32_t i = 0; i < arrayType->getNumElements(); ++i) {
        elems.push_back(formatIntArrayInit(elemType, values, cursor, true));
    }

    std::ostringstream oss;
    if (includeType) {
        oss << type->toString() << " ";
    }
    oss << "[";
    for (std::size_t i = 0; i < elems.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << elems[i];
    }
    oss << "]";
    return oss.str();
}

std::string formatFloatArrayInit(
    Type * type, const std::vector<float> & values, std::size_t & cursor, bool includeType)
{
    auto * arrayType = dynamic_cast<ArrayType *>(type);
    if (arrayType == nullptr) {
        float value = 0.0f;
        if (cursor < values.size()) {
            value = values[cursor];
        }
        ++cursor;
        return type->toString() + " " + formatFloatGlobalInit(value);
    }

    std::vector<std::string> elems;
    Type * elemType = arrayType->getElementType();
    elems.reserve(arrayType->getNumElements());
    for (int32_t i = 0; i < arrayType->getNumElements(); ++i) {
        elems.push_back(formatFloatArrayInit(elemType, values, cursor, true));
    }

    std::ostringstream oss;
    if (includeType) {
        oss << type->toString() << " ";
    }
    oss << "[";
    for (std::size_t i = 0; i < elems.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << elems[i];
    }
    oss << "]";
    return oss.str();
}

std::string formatGlobalInit(GlobalVariable * global)
{
    Type * valueType = global->getValueType();
    if (valueType->isArrayType()) {
        if (global->getInitKind() == GlobalVariable::InitKind::IntArray) {
            std::size_t cursor = 0;
            return formatIntArrayInit(valueType, global->getInitIntArray(), cursor, false);
        }
        if (global->getInitKind() == GlobalVariable::InitKind::FloatArray) {
            std::size_t cursor = 0;
            return formatFloatArrayInit(valueType, global->getInitFloatArray(), cursor, false);
        }
        return "zeroinitializer";
    }

    if (valueType->isFloatType()) {
        return formatFloatGlobalInit(global->getInitFloatValue());
    }

    return std::to_string(global->getInitIntValue());
}

/// @brief 发射需要默认浮点提升的 LLVM 可变参数调用
/// @param call 待发射的调用指令
/// @param lines 结果文本行数组
/// @return true 表示已完成特殊发射，false 表示应使用常规指令发射
bool emitPromotedVariadicCall(CallInst * call, std::vector<std::string> & lines)
{
    if (call == nullptr || call->getCallee() == nullptr || !call->getCallee()->isVarArg()) {
        return false;
    }

    const auto & params = call->getCallee()->getParams();
    const std::size_t fixedParamCount = params.size();
    bool needsPromotion = false;
    for (int32_t i = static_cast<int32_t>(fixedParamCount); i < call->getArgCount(); ++i) {
        if (call->getArg(i)->getType()->isFloatType()) {
            needsPromotion = true;
            break;
        }
    }
    if (!needsPromotion) {
        return false;
    }

    std::vector<std::string> argTypes;
    std::vector<std::string> argNames;
    argTypes.reserve(call->getArgCount());
    argNames.reserve(call->getArgCount());
    for (int32_t i = 0; i < call->getArgCount(); ++i) {
        Value * arg = call->getArg(i);
        if (i >= static_cast<int32_t>(fixedParamCount) && arg->getType()->isFloatType()) {
            std::string promotedName = "%__minic_vararg_fpext_" +
                                       std::to_string(call->getCreationId()) + "_" + std::to_string(i);
            lines.emplace_back("  " + promotedName + " = fpext float " + arg->getIRName() + " to double");
            argTypes.emplace_back("double");
            argNames.push_back(std::move(promotedName));
        } else {
            argTypes.push_back(arg->getType()->toString());
            argNames.push_back(arg->getIRName());
        }
    }

    std::string callText;
    if (call->hasResultValue()) {
        callText = call->getIRName() + " = ";
    }
    callText += "call " + call->getType()->toString() + " (";
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            callText += ", ";
        }
        callText += params[i]->getType()->toString();
    }
    if (!params.empty()) {
        callText += ", ";
    }
    callText += "...) " + call->getCallee()->getIRName() + "(";
    for (int32_t i = 0; i < call->getArgCount(); ++i) {
        if (i > 0) {
            callText += ", ";
        }
        callText += argTypes[i] + " " + argNames[i];
    }
    callText += ")";
    lines.emplace_back("  " + callText);
    return true;
}

} // namespace

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
        std::string initText = formatGlobalInit(global);
        lines.emplace_back(global->getIRName() + " = global " + llvmType(global->getValueType()) + " " + initText);
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
            if (auto * call = dynamic_cast<CallInst *>(inst); emitPromotedVariadicCall(call, lines)) {
                continue;
            }
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

    if (function->isVarArg()) {
        if (!firstParam) {
            signature += ", ";
        }
        signature += "...";
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
