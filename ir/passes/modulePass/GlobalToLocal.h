///
/// @file GlobalToLocal.h
/// @brief 将 main 专属全局标量下沉为局部槽位的 pass
///
/// 如果下沉到非 main 函数中，可能出现很多问题。比如这个就不能下沉：
///
/// int counter = 0;
/// int next() {
///     counter++;
///     return counter;
/// }
///
/// 所以只把'所有使用都只出现在 main 里的标量全局变量'下沉成 main 入口块里的局部槽位和等价初始化.
///

#pragma once

class AllocaInst;
class Function;
class GlobalVariable;
class Module;

class GlobalToLocal {

public:
    /// @brief 构造全局转局部 pass
    explicit GlobalToLocal(Module * module);

    /// @brief 执行全局转局部优化
    /// @return 若本轮修改了 IR 则返回 true
    bool run();

private:
    /// @brief 获取可作为下沉目标的 main 函数
    Function * getMainFunction() const;

    /// @brief 判断一个全局标量是否可安全下沉到 main
    bool canInternalizeToMain(GlobalVariable * global, Function * mainFunc) const;

    /// @brief 在入口块中创建局部槽位
    AllocaInst * createEntrySlot(Function * func, GlobalVariable * global) const;

    /// @brief 在入口块中补上等价的初始化 store
    bool materializeScalarInitializer(Function * func, GlobalVariable * global, AllocaInst * slot) const;

    Module * module = nullptr;
};