///
/// @file LLVMIREmitter.h
/// @brief LLVM IR 文本发射器
///
/// 职责：遍历 Module / Function / BasicBlock / Instruction 结构化 IR，
/// 将每条指令的 toString() 输出序列化为标准 LLVM IR 文本。
/// 不承担任何 lowering / 语义补全工作。
///

#pragma once

#include <string>
#include <vector>

class Function;
class Module;
class Type;

class LLVMIREmitter {

public:
    LLVMIREmitter(Module * _module, std::string _moduleName);

    bool run();

    bool writeToFile(const std::string & filename) const;

    [[nodiscard]] const std::string & getIR() const
    {
        return llvmIR;
    }

private:
    Module * module = nullptr;
    std::string moduleName;
    std::string llvmIR;

    /// Emit a single function as a LLVM IR define block
    void emitFunction(Function * function, std::vector<std::string> & lines);

    std::string formatFunctionSignature(Function * function, bool withNames) const;

    std::string llvmType(Type * type) const;

    std::string escapeString(const std::string & text) const;
};