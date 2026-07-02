///
/// @file LoopParallelize.h
/// @brief 保守的循环并行 pass
///
/// 将安全的大粒度 tile 循环改写为多路并行执行：
/// - 对满足依赖安全条件的规范循环（canonical loop），使用2路并行（__mtstart/__mtend）
/// - 对归约循环（reduction loop），使用4路并行（__mtstart4/__mtend4），
///   并在循环结束后合并各线程的部分归约结果
///

#pragma once

class BasicBlock;
class Function;
class LoopInfo;
class Module;

/// @brief 循环并行优化pass
///
/// 识别函数中满足并行化安全条件的循环，将其改写为多路并行执行。
/// 目前支持两种模式：
/// 1. 规范循环并行：步长>=16、无调用指令、store目标依赖循环变量且根为形参/全局变量、
///    load不与store指向同一根——使用2路clone线程并行
/// 2. 归约循环并行：循环体仅含归约phi更新（add+mod）、无store/call——使用4路线程并行，
///    各线程计算部分归约，结束后通过__mtstorei32/__mtloadi32合并
class LoopParallelize {

public:
    /// @brief 构造函数
    /// @param func 待优化的函数
    /// @param mod 所属的IR模块
    LoopParallelize(Function * func, Module * mod);

    /// @brief 将安全的大粒度 tile 循环改写为多路并行执行
    /// @return 若修改了 IR 则返回 true
    bool run();

private:
    /// @brief 尝试对归约循环头进行4路并行化
    /// @param header 循环头基本块
    /// @param loopInfo 循环信息
    /// @return 若成功并行化则返回true
    bool tryParallelizeReductionHeader(BasicBlock * header, LoopInfo & loopInfo);

    /// @brief 尝试对规范循环头进行2路并行化
    /// @param header 循环头基本块
    /// @param loopInfo 循环信息
    /// @return 若成功并行化则返回true
    bool tryParallelizeHeader(BasicBlock * header, LoopInfo & loopInfo);

    /// @brief 待优化的函数
    Function * func = nullptr;
    /// @brief 所属的IR模块
    Module * mod = nullptr;
};
