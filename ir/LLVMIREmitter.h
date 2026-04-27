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
    /// @brief 构造 LLVM IR 文本发射器
    LLVMIREmitter(Module * _module, std::string _moduleName);

    /// @brief 生成完整的 LLVM IR 文本
    bool run();

    /// @brief 将 LLVM IR 文本写入文件
    bool writeToFile(const std::string & filename) const;

    /// @brief 获取当前生成的 LLVM IR 文本
    [[nodiscard]] const std::string & getIR() const
    {
        return llvmIR;
    }

private:
    Module * module = nullptr;
    std::string moduleName;
    std::string llvmIR;

    /// @brief 输出单个函数的 define 语句块
    void emitFunction(Function * function, std::vector<std::string> & lines);

    /// @brief 格式化函数签名文本
    std::string formatFunctionSignature(Function * function, bool withNames) const;

    /// @brief 将内部类型转换为 LLVM IR 类型字符串
    std::string llvmType(Type * type) const;

    /// @brief 转义字符串中的特殊字符
    std::string escapeString(const std::string & text) const;
};