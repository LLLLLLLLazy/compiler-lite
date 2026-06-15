///
/// @file DeadGlobalStoreElim.h
/// @brief 死全局写消除 pass
///
/// 识别整个模块内「只写不读」且地址从不逃逸的全局变量（典型如仅被初始化、
/// 后续从未读取的全局数组），删除其上的全部 store。这些写入对程序的可观测
/// 行为没有任何影响，属于标准死代码消除。
///
/// 判定（whole-module，保守安全）：从全局变量出发沿 def-use 链遍历，仅允许
/// 经由「以该值为基址的 GEP」继续派生地址，叶子用途必须全部是「以该地址为
/// 指针操作数的 store」。一旦出现 load（被读取）、地址作为值被 store/传参/
/// 比较等（逃逸），或任何无法识别的用途，则放弃该全局。
///
/// 放在 DeadFunctionElim 之后运行：死函数中的读已被清除，使更多全局可判死；
/// 且此时尚未 Mem2Reg，全局访问是「由循环归纳变量直接计算的 GEP」形态，
/// 删除 store 后这些 GEP 即变为无用户死指令，交由下游 DeadInstElim 清扫，
/// 不会残留死的归纳变量环
///

#pragma once

class Module;

class DeadGlobalStoreElim {

public:
    /// @brief 构造死全局写消除 pass
    /// @param module 待处理模块
    explicit DeadGlobalStoreElim(Module * module);

    /// @brief 删除只写不读、地址不逃逸的全局变量上的全部 store
    /// @return true 表示移除了至少一条 store
    bool run();

private:
    Module * module = nullptr;
};
