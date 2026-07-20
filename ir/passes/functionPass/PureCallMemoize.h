///
/// @file PureCallMemoize.h
/// @brief 递归纯函数记忆化 pass。
///
/// 对具有重叠子问题的纯自递归函数插入固定容量哈希缓存。
/// 当前支持 1 个或 2 个 i32 参数、i32 返回值的函数。
///
/// 缓存以完整参数元组为键，参数大小不再受稠密数组边界限制；
/// 哈希冲突会覆盖旧槽，只影响命中率，不影响程序正确性。
///

#pragma once

#include <cstdint>

class Function;
class GlobalVariable;
class Module;

class PureCallMemoize {

public:
    PureCallMemoize(Function * func, Module * mod);

    /// @return true 表示 IR 被修改
    bool run();

private:
    struct MemoGlobals {
        GlobalVariable * key0 = nullptr;
        GlobalVariable * key1 = nullptr;
        GlobalVariable * value = nullptr;
        GlobalVariable * epochTag = nullptr;
        GlobalVariable * currentEpoch = nullptr;
        GlobalVariable * recursionDepth = nullptr;
    };

    /// @brief 判断是否存在一条执行路径会执行至少两次自递归调用
    bool hasOverlappingSelfCalls();

    /// @brief 创建固定容量哈希缓存及生命周期状态
    MemoGlobals createMemoGlobals(int32_t paramCount);

    /// @brief 插入哈希查询、写回和 epoch/depth 管理
    bool transformFunction(const MemoGlobals & globals, int32_t paramCount);

    Function * func = nullptr;
    Module * mod = nullptr;

    static constexpr int32_t kHashCapacity = 1 << 16;
    static constexpr int32_t kHashMask = kHashCapacity - 1;
};
