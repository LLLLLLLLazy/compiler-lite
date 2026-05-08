///
/// @file InterproceduralConstProp.cpp
/// @brief 极小规模跨过程常量实参传播优化实现。
///
/// 遍历模块中每个被调函数，若某个形参在所有调用点都接收到相同的常量值，
/// 则直接将该形参的所有使用替换为该常量，使后续 SCCP/InstCombine 能进一步折叠。
///

#include "InterproceduralConstProp.h"

#include <vector>

#include "BasicBlock.h"
#include "CallInst.h"
#include "ConstFloat.h"
#include "ConstInteger.h"
#include "Function.h"
#include "Module.h"
#include "Value.h"

namespace {

/// @brief 判断值是否为本 pass 支持的常量类型（整数或浮点）
/// @param value 待判断的值
/// @return true 表示该值是 ConstInteger 或 ConstFloat
bool isSupportedConstant(Value * value)
{
    return dynamic_cast<ConstInteger *>(value) != nullptr || dynamic_cast<ConstFloat *>(value) != nullptr;
}

/// @brief 判断两个常量值是否在数值上完全相等
/// @param lhs 左操作数
/// @param rhs 右操作数
/// @return true 表示两者类型相同且数值相等
bool sameConstant(Value * lhs, Value * rhs)
{
    if (lhs == rhs) {
        return true;
    }

    auto * lhsInt = dynamic_cast<ConstInteger *>(lhs);
    auto * rhsInt = dynamic_cast<ConstInteger *>(rhs);
    if (lhsInt && rhsInt) {
        return lhsInt->getType() == rhsInt->getType() && lhsInt->getVal() == rhsInt->getVal();
    }

    auto * lhsFloat = dynamic_cast<ConstFloat *>(lhs);
    auto * rhsFloat = dynamic_cast<ConstFloat *>(rhs);
    if (lhsFloat && rhsFloat) {
        return lhsFloat->getBitPattern() == rhsFloat->getBitPattern();
    }

    return false;
}

} // namespace

/// @brief 构造跨过程常量传播 pass
/// @param _mod 待优化的模块
InterproceduralConstProp::InterproceduralConstProp(Module * _mod) : mod(_mod)
{}

/// @brief 执行跨过程常量实参传播
/// @return 若 IR 被修改则返回 true
bool InterproceduralConstProp::run()
{
    if (!mod) {
        return false;
    }

    bool changed = false;
    for (auto * callee : mod->getFunctionList()) {
        if (!callee || callee->isBuiltin() || callee->getBlocks().empty() || callee->getParams().empty()) {
            continue;
        }

        std::vector<Value *> constantArgs(callee->getParams().size(), nullptr);
        std::vector<bool> overdefined(callee->getParams().size(), false);
        bool sawCall = false;

        for (auto * caller : mod->getFunctionList()) {
            if (!caller || caller->isBuiltin()) {
                continue;
            }

            for (auto * bb : caller->getBlocks()) {
                for (auto * inst : bb->getInstructions()) {
                    auto * call = dynamic_cast<CallInst *>(inst);
                    if (!call || call->getCallee() != callee) {
                        continue;
                    }

                    sawCall = true;
                    for (std::size_t i = 0; i < constantArgs.size(); ++i) {
                        Value * arg = call->getArg(static_cast<int32_t>(i));
                        if (!isSupportedConstant(arg)) {
                            overdefined[i] = true;
                            continue;
                        }

                        if (!constantArgs[i]) {
                            constantArgs[i] = arg;
                        } else if (!sameConstant(constantArgs[i], arg)) {
                            overdefined[i] = true;
                        }
                    }
                }
            }
        }

        if (!sawCall) {
            continue;
        }

        for (std::size_t i = 0; i < constantArgs.size(); ++i) {
            if (overdefined[i] || !constantArgs[i]) {
                continue;
            }

            callee->getParams()[i]->replaceAllUseWith(constantArgs[i]);
            changed = true;
        }
    }

    return changed;
}
