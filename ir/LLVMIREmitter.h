///
/// @file LLVMIREmitter.h
/// @brief 基于结构化IR的LLVM IR文本发射器
///

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

class Function;
class Instruction;
class LabelInstruction;
class Module;
class Type;
class Value;

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
    struct MaterializedValue {
        std::string type;
        std::string value;
    };

    struct FunctionContext {
        Function * function = nullptr;
        int nextValueId = 1;
        int nextLabelId = 0;
        int nextSlotId = 0;
        bool blockOpen = true;
        bool blockTerminated = false;
        std::vector<Value *> pendingArgs;
        std::unordered_map<const Value *, std::string> slotMap;
        std::unordered_map<const LabelInstruction *, std::string> labelMap;
    };

    Module * module = nullptr;
    std::string moduleName;
    std::string llvmIR;

    void emitFunction(Function * function, std::vector<std::string> & lines);

    /// Block-structured IR emission path (Phase 3+)
    void emitFunctionBlocks(Function * function, std::vector<std::string> & lines);

    void emitInstruction(Instruction * inst, std::vector<std::string> & lines, FunctionContext & context);

    void declareSlot(Value * value, std::vector<std::string> & lines, FunctionContext & context);

    void storeValue(
        Value * destination, const MaterializedValue & source, std::vector<std::string> & lines,
        FunctionContext & context);

    MaterializedValue materializeValue(Value * value, std::vector<std::string> & lines, FunctionContext & context);

    MaterializedValue castValue(
        const MaterializedValue & value, Type * targetType, std::vector<std::string> & lines,
        FunctionContext & context);

    std::string formatFunctionSignature(Function * function, bool withNames) const;

    std::string llvmType(Type * type) const;

    std::string escapeString(const std::string & text) const;

    std::string nextValueName(FunctionContext & context);

    std::string nextSlotName(FunctionContext & context);

    std::string makeSlotName(const Value * value, FunctionContext & context);

    std::string getOrCreateSlot(const Value * value, FunctionContext & context);

    std::string getOrCreateLabel(const LabelInstruction * label, FunctionContext & context);
};