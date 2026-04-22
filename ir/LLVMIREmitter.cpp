///
/// @file LLVMIREmitter.cpp
/// @brief 基于结构化IR的LLVM IR文本发射器
///

#include "LLVMIREmitter.h"

#include <fstream>

#include "ArgInstruction.h"
#include "BinaryInstruction.h"
#include "CondGotoInstruction.h"
#include "ConstInt.h"
#include "EntryInstruction.h"
#include "ExitInstruction.h"
#include "FormalParam.h"
#include "FuncCallInstruction.h"
#include "Function.h"
#include "GlobalVariable.h"
#include "GotoInstruction.h"
#include "Instruction.h"
#include "IntegerType.h"
#include "LabelInstruction.h"
#include "LocalVariable.h"
#include "Module.h"
#include "MoveInstruction.h"
#include "UnaryInstruction.h"

namespace {

bool isComparisonOp(IRInstOperator op)
{
    switch (op) {
        case IRInstOperator::IRINST_OP_LT_I:
        case IRInstOperator::IRINST_OP_GT_I:
        case IRInstOperator::IRINST_OP_LE_I:
        case IRInstOperator::IRINST_OP_GE_I:
        case IRInstOperator::IRINST_OP_EQ_I:
        case IRInstOperator::IRINST_OP_NE_I:
            return true;
        default:
            return false;
    }
}

std::string llvmBinaryOpcode(IRInstOperator op)
{
    switch (op) {
        case IRInstOperator::IRINST_OP_ADD_I:
            return "add";
        case IRInstOperator::IRINST_OP_SUB_I:
            return "sub";
        case IRInstOperator::IRINST_OP_MUL_I:
            return "mul";
        case IRInstOperator::IRINST_OP_DIV_I:
            return "sdiv";
        case IRInstOperator::IRINST_OP_MOD_I:
            return "srem";
        default:
            return "";
    }
}

std::string llvmComparePredicate(IRInstOperator op)
{
    switch (op) {
        case IRInstOperator::IRINST_OP_LT_I:
            return "slt";
        case IRInstOperator::IRINST_OP_GT_I:
            return "sgt";
        case IRInstOperator::IRINST_OP_LE_I:
            return "sle";
        case IRInstOperator::IRINST_OP_GE_I:
            return "sge";
        case IRInstOperator::IRINST_OP_EQ_I:
            return "eq";
        case IRInstOperator::IRINST_OP_NE_I:
            return "ne";
        default:
            return "";
    }
}

} // namespace

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
    for (auto * func: module->getFunctionList()) {
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
    for (auto * global: module->getGlobalVariables()) {
        lines.emplace_back(
            global->getIRName() + " = global " + llvmType(global->getType()) + " " +
            std::to_string(global->getInitIntValue()));
        hasGlobal = true;
    }

    if (hasGlobal) {
        lines.emplace_back("");
    }

    bool firstFunction = true;
    for (auto * func: module->getFunctionList()) {
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
    for (std::size_t index = 0; index < lines.size(); ++index) {
        llvmIR += lines[index];
        if (index + 1 < lines.size()) {
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
    FunctionContext context;
    context.function = function;

    lines.emplace_back("define " + formatFunctionSignature(function, true) + " {");

    for (auto * param: function->getParams()) {
        declareSlot(param, lines, context);
    }

    for (auto * local: function->getVarValues()) {
        declareSlot(local, lines, context);
    }

    for (auto * inst: function->getInterCode().getInsts()) {
        if (inst->hasResultValue()) {
            declareSlot(inst, lines, context);
        }
    }

    for (auto * param: function->getParams()) {
        storeValue(param, {llvmType(param->getType()), param->getIRName()}, lines, context);
    }

    for (auto * inst: function->getInterCode().getInsts()) {
        emitInstruction(inst, lines, context);
    }

    if (context.blockOpen && !context.blockTerminated) {
        if (function->getReturnType()->isVoidType()) {
            lines.emplace_back("  ret void");
        } else {
            lines.emplace_back("  ret " + llvmType(function->getReturnType()) + " 0");
        }
    }

    lines.emplace_back("}");
}

void LLVMIREmitter::emitInstruction(Instruction * inst, std::vector<std::string> & lines, FunctionContext & context)
{
    if (dynamic_cast<EntryInstruction *>(inst) != nullptr) {
        return;
    }

    if (auto * labelInst = dynamic_cast<LabelInstruction *>(inst); labelInst != nullptr) {
        std::string labelName = getOrCreateLabel(labelInst, context);

        if (context.blockOpen && !context.blockTerminated) {
            lines.emplace_back("  br label %" + labelName);
        }

        lines.emplace_back(labelName + ":");
        context.blockOpen = true;
        context.blockTerminated = false;
        return;
    }

    if (auto * argInst = dynamic_cast<ArgInstruction *>(inst); argInst != nullptr) {
        context.pendingArgs.push_back(argInst->getOperand(0));
        return;
    }

    if (auto * moveInst = dynamic_cast<MoveInstruction *>(inst); moveInst != nullptr) {
        storeValue(moveInst->getOperand(0), materializeValue(moveInst->getOperand(1), lines, context), lines, context);
        context.blockOpen = true;
        return;
    }

    if (auto * binaryInst = dynamic_cast<BinaryInstruction *>(inst); binaryInst != nullptr) {
        MaterializedValue lhs = castValue(materializeValue(binaryInst->getOperand(0), lines, context), inst->getType(), lines, context);
        MaterializedValue rhs = castValue(materializeValue(binaryInst->getOperand(1), lines, context), inst->getType(), lines, context);

        if (isComparisonOp(binaryInst->getOp())) {
            std::string compareValue = nextValueName(context);
            lines.emplace_back(
                "  " + compareValue + " = icmp " + llvmComparePredicate(binaryInst->getOp()) + " " + lhs.type + " " +
                lhs.value + ", " + rhs.value);

            std::string extendValue = nextValueName(context);
            lines.emplace_back("  " + extendValue + " = zext i1 " + compareValue + " to i32");
            storeValue(binaryInst, {"i32", extendValue}, lines, context);
        } else {
            std::string valueName = nextValueName(context);
            lines.emplace_back(
                "  " + valueName + " = " + llvmBinaryOpcode(binaryInst->getOp()) + " " + lhs.type + " " + lhs.value +
                ", " + rhs.value);
            storeValue(binaryInst, {lhs.type, valueName}, lines, context);
        }

        context.blockOpen = true;
        return;
    }

    if (auto * unaryInst = dynamic_cast<UnaryInstruction *>(inst); unaryInst != nullptr) {
        MaterializedValue operand = castValue(materializeValue(unaryInst->getOperand(0), lines, context), inst->getType(), lines, context);

        if (unaryInst->getOp() == IRInstOperator::IRINST_OP_NEG_I) {
            std::string valueName = nextValueName(context);
            lines.emplace_back("  " + valueName + " = sub " + operand.type + " 0, " + operand.value);
            storeValue(unaryInst, {operand.type, valueName}, lines, context);
        } else if (unaryInst->getOp() == IRInstOperator::IRINST_OP_NOT_I) {
            std::string compareValue = nextValueName(context);
            lines.emplace_back("  " + compareValue + " = icmp eq " + operand.type + " " + operand.value + ", 0");

            std::string extendValue = nextValueName(context);
            lines.emplace_back("  " + extendValue + " = zext i1 " + compareValue + " to i32");
            storeValue(unaryInst, {"i32", extendValue}, lines, context);
        }

        context.blockOpen = true;
        return;
    }

    if (auto * callInst = dynamic_cast<FuncCallInstruction *>(inst); callInst != nullptr) {
        std::vector<std::string> argList;

        if (callInst->getOperandsNum() != 0) {
            for (int32_t index = 0; index < callInst->getOperandsNum(); ++index) {
                MaterializedValue arg = materializeValue(callInst->getOperand(index), lines, context);
                argList.emplace_back(arg.type + " " + arg.value);
            }
        } else {
            for (auto * argValue: context.pendingArgs) {
                MaterializedValue arg = materializeValue(argValue, lines, context);
                argList.emplace_back(arg.type + " " + arg.value);
            }
        }

        std::string callText = "  ";
        if (!callInst->getType()->isVoidType()) {
            std::string callValue = nextValueName(context);
            callText += callValue + " = ";
            callText += "call " + llvmType(callInst->calledFunction->getReturnType()) + " " + callInst->calledFunction->getIRName() + "(";

            for (std::size_t index = 0; index < argList.size(); ++index) {
                if (index != 0) {
                    callText += ", ";
                }
                callText += argList[index];
            }

            callText += ")";
            lines.emplace_back(callText);
            storeValue(callInst, {llvmType(callInst->calledFunction->getReturnType()), callValue}, lines, context);
        } else {
            callText += "call " + llvmType(callInst->calledFunction->getReturnType()) + " " + callInst->calledFunction->getIRName() + "(";

            for (std::size_t index = 0; index < argList.size(); ++index) {
                if (index != 0) {
                    callText += ", ";
                }
                callText += argList[index];
            }

            callText += ")";
            lines.emplace_back(callText);
        }

        context.pendingArgs.clear();
        context.blockOpen = true;
        return;
    }

    if (auto * condGotoInst = dynamic_cast<CondGotoInstruction *>(inst); condGotoInst != nullptr) {
        MaterializedValue cond = materializeValue(condGotoInst->getOperand(0), lines, context);
        cond = castValue(cond, IntegerType::getTypeBool(), lines, context);

        lines.emplace_back(
            "  br i1 " + cond.value + ", label %" + getOrCreateLabel(condGotoInst->getTrueTarget(), context) + ", label %" +
            getOrCreateLabel(condGotoInst->getFalseTarget(), context));

        context.blockOpen = false;
        context.blockTerminated = true;
        return;
    }

    if (auto * gotoInst = dynamic_cast<GotoInstruction *>(inst); gotoInst != nullptr) {
        lines.emplace_back("  br label %" + getOrCreateLabel(gotoInst->getTarget(), context));
        context.blockOpen = false;
        context.blockTerminated = true;
        return;
    }

    if (auto * exitInst = dynamic_cast<ExitInstruction *>(inst); exitInst != nullptr) {
        if (exitInst->getOperandsNum() == 0 || context.function->getReturnType()->isVoidType()) {
            lines.emplace_back("  ret void");
        } else {
            MaterializedValue retValue = castValue(
                materializeValue(exitInst->getOperand(0), lines, context), context.function->getReturnType(), lines, context);
            lines.emplace_back("  ret " + retValue.type + " " + retValue.value);
        }

        context.blockOpen = false;
        context.blockTerminated = true;
    }
}

void LLVMIREmitter::declareSlot(Value * value, std::vector<std::string> & lines, FunctionContext & context)
{
    std::string slotName = getOrCreateSlot(value, context);
    lines.emplace_back("  " + slotName + " = alloca " + llvmType(value->getType()));
}

void LLVMIREmitter::storeValue(
    Value * destination, const MaterializedValue & source, std::vector<std::string> & lines, FunctionContext & context)
{
    MaterializedValue castedSource = castValue(source, destination->getType(), lines, context);

    if (auto * global = dynamic_cast<GlobalVariable *>(destination); global != nullptr) {
        lines.emplace_back(
            "  store " + castedSource.type + " " + castedSource.value + ", " + castedSource.type + "* " +
            global->getIRName());
        return;
    }

    lines.emplace_back(
        "  store " + castedSource.type + " " + castedSource.value + ", " + castedSource.type + "* " +
        getOrCreateSlot(destination, context));
}

LLVMIREmitter::MaterializedValue LLVMIREmitter::materializeValue(
    Value * value, std::vector<std::string> & lines, FunctionContext & context)
{
    if (auto * constInt = dynamic_cast<ConstInt *>(value); constInt != nullptr) {
        return {llvmType(constInt->getType()), std::to_string(constInt->getVal())};
    }

    if (auto * global = dynamic_cast<GlobalVariable *>(value); global != nullptr) {
        std::string loadValue = nextValueName(context);
        std::string typeName = llvmType(global->getType());
        lines.emplace_back("  " + loadValue + " = load " + typeName + ", " + typeName + "* " + global->getIRName());
        return {typeName, loadValue};
    }

    std::string loadValue = nextValueName(context);
    std::string typeName = llvmType(value->getType());
    lines.emplace_back(
        "  " + loadValue + " = load " + typeName + ", " + typeName + "* " + getOrCreateSlot(value, context));
    return {typeName, loadValue};
}

LLVMIREmitter::MaterializedValue LLVMIREmitter::castValue(
    const MaterializedValue & value, Type * targetType, std::vector<std::string> & lines, FunctionContext & context)
{
    std::string targetTypeName = llvmType(targetType);
    if (value.type == targetTypeName) {
        return value;
    }

    if (value.type == "i1" && targetTypeName == "i32") {
        std::string extendValue = nextValueName(context);
        lines.emplace_back("  " + extendValue + " = zext i1 " + value.value + " to i32");
        return {"i32", extendValue};
    }

    if (value.type == "i32" && targetTypeName == "i1") {
        std::string compareValue = nextValueName(context);
        lines.emplace_back("  " + compareValue + " = icmp ne i32 " + value.value + ", 0");
        return {"i1", compareValue};
    }

    return value;
}

std::string LLVMIREmitter::formatFunctionSignature(Function * function, bool withNames) const
{
    std::string signature = llvmType(function->getReturnType()) + " " + function->getIRName() + "(";

    bool firstParam = true;
    for (auto * param: function->getParams()) {
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

    for (char ch: text) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }

    return escaped;
}

std::string LLVMIREmitter::nextValueName(FunctionContext & context)
{
    return "%v" + std::to_string(context.nextValueId++);
}

std::string LLVMIREmitter::nextSlotName(FunctionContext & context)
{
    return "%slot." + std::to_string(context.nextSlotId++);
}

std::string LLVMIREmitter::makeSlotName(const Value * value, FunctionContext & context)
{
    std::string baseName = value->getIRName();
    if (!baseName.empty() && (baseName.front() == '%' || baseName.front() == '@')) {
        baseName.erase(baseName.begin());
    }

    if (baseName.empty()) {
        return nextSlotName(context);
    }

    return "%" + baseName + ".addr";
}

std::string LLVMIREmitter::getOrCreateSlot(const Value * value, FunctionContext & context)
{
    auto iter = context.slotMap.find(value);
    if (iter != context.slotMap.end()) {
        return iter->second;
    }

    std::string slotName = makeSlotName(value, context);
    context.slotMap.emplace(value, slotName);
    return slotName;
}

std::string LLVMIREmitter::getOrCreateLabel(const LabelInstruction * label, FunctionContext & context)
{
    auto iter = context.labelMap.find(label);
    if (iter != context.labelMap.end()) {
        return iter->second;
    }

    std::string labelName = "label" + std::to_string(context.nextLabelId++);
    context.labelMap.emplace(label, labelName);
    return labelName;
}