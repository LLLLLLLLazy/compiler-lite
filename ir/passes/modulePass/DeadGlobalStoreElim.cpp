///
/// @file DeadGlobalStoreElim.cpp
/// @brief 死全局写消除 pass 实现
///

#include "DeadGlobalStoreElim.h"

#include <list>
#include <unordered_set>
#include <vector>

#include "BasicBlock.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "GlobalVariable.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "Module.h"
#include "StoreInst.h"
#include "Use.h"
#include "Value.h"

namespace {

/// @brief 判断全局是否「只写不读、地址不逃逸」，并收集其上的全部 store
///
/// 从全局出发沿 def-use 链遍历：仅允许「以当前值为基址的 GEP」继续派生地址，
/// 叶子用途必须是「以该地址为指针操作数的 store」。出现 load / 地址被当作值
/// 使用（store 的值操作数、传参、比较等）/ 无法识别的用途，均视为不可消除
///
/// @param global 待检查全局变量
/// @param stores 输出该全局上可删除的 store 集合
/// @return true 表示该全局只写不读且地址不逃逸
bool collectDeadGlobalStores(GlobalVariable * global, std::vector<StoreInst *> & stores)
{
    if (global == nullptr) {
        return false;
    }

    std::vector<Value *> worklist;
    std::unordered_set<Value *> visited;
    worklist.push_back(global);
    visited.insert(global);

    while (!worklist.empty()) {
        Value * value = worklist.back();
        worklist.pop_back();

        for (auto * use : value->getUseList()) {
            auto * user = dynamic_cast<Instruction *>(use->getUser());
            if (user == nullptr) {
                return false;
            }

            // 经由「以当前值为基址的 GEP」继续派生地址
            if (auto * gep = dynamic_cast<GetElementPtrInst *>(user)) {
                if (gep->getBasePointer() != value) {
                    // 地址被当作索引使用，视为逃逸
                    return false;
                }
                if (visited.insert(gep).second) {
                    worklist.push_back(gep);
                }
                continue;
            }

            // 仅「以该地址为指针操作数」的 store 是可删除的死写
            if (auto * store = dynamic_cast<StoreInst *>(user)) {
                if (store->getValueOperand() == value) {
                    // 地址作为被存的值写入内存，逃逸
                    return false;
                }
                if (store->getPointerOperand() != value) {
                    return false;
                }
                stores.push_back(store);
                continue;
            }

            // load（被读取）以及 call / cmp 等其它任何用途都使该全局不可消除
            return false;
        }
    }

    return true;
}

/// @brief 从所有函数中清扫已标记为 dead 的指令
/// @param module 待清扫模块
void sweepDeadInstructions(Module * module)
{
    for (auto * func : module->getFunctionList()) {
        if (func == nullptr || func->isBuiltin() || func->getBlocks().empty()) {
            continue;
        }
        for (auto * bb : func->getBlocks()) {
            auto & insts = bb->getInstructions();
            for (auto it = insts.begin(); it != insts.end();) {
                Instruction * inst = *it;
                if (inst == nullptr || !inst->isDead()) {
                    ++it;
                    continue;
                }
                auto next = std::next(it);
                insts.erase(it);
                delete inst;
                it = next;
            }
        }
    }
}

} // namespace

/// @brief 构造死全局写消除 pass
/// @param _module 待处理模块
DeadGlobalStoreElim::DeadGlobalStoreElim(Module * _module) : module(_module)
{}

/// @brief 执行死全局写消除
/// @return 若本轮删除了至少一条 store 则返回 true
bool DeadGlobalStoreElim::run()
{
    if (module == nullptr) {
        return false;
    }

    bool changed = false;
    std::vector<GlobalVariable *> globals(module->getGlobalVariables().begin(),
                                          module->getGlobalVariables().end());

    for (auto * global : globals) {
        if (global == nullptr) {
            continue;
        }

        std::vector<StoreInst *> deadStores;
        if (!collectDeadGlobalStores(global, deadStores) || deadStores.empty()) {
            continue;
        }

        // 删除该全局上的全部 store：清空操作数并标记 dead，
        // 喂给这些 store 的 GEP 随之变为无用户死指令，交由下游 DeadInstElim 清扫
        for (auto * store : deadStores) {
            store->clearOperands();
            store->setDead(true);
        }
        changed = true;
    }

    if (changed) {
        sweepDeadInstructions(module);
    }

    return changed;
}
