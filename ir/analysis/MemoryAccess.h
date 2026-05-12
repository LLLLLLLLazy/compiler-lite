///
/// @file MemoryAccess.h
/// @brief 共享的指针根对象与内存访问查询工具
///

#pragma once

#include <functional>
#include <unordered_set>

class BasicBlock;
class CallInst;
class GlobalVariable;
class Module;
class StoreInst;
class Value;
struct MemoryLocation;

/// @brief 沿 GEP 链回溯找到指针根对象
/// @param value 待查询指针值
/// @return 根对象，可能是 alloca、全局变量、形参或其他值
Value * getPointerRoot(Value * value);

/// @brief 判断全局变量是否在模块中未被任何 store 或派生地址 store 改写
/// @param mod 所属模块
/// @param global 待检查全局变量
/// @return true 表示该全局变量只读
bool isReadOnlyGlobal(Module * mod, GlobalVariable * global);

/// @brief 判断地址根对象是否为局部栈对象
/// @param ptr 待查询指针值
/// @return true 表示根对象为 alloca
bool isLocalMemory(Value * ptr);

/// @brief 判断指针是否为允许纯函数读取的地址
/// @param mod 所属模块
/// @param ptr 待查询指针值
/// @return true 表示该地址属于局部内存、形参或只读全局
bool isAllowedReadPointer(Module * mod, Value * ptr);

/// @brief 判断 store 是否仅写入非逃逸局部对象
/// @param store 待检查 store
/// @return true 表示该写入对函数外不可见
bool isNonEscapingLocalStore(StoreInst * store);

/// @brief 判断 store 是否可能改写给定精确位点
/// @param store 待检查 store
/// @param location 目标位点
/// @return true 表示两者可能别名
bool storeMayAliasLocation(StoreInst * store, const MemoryLocation & location);

/// @brief 判断一组基本块是否可能改写某个 load 的地址
/// @param pointer load 的地址操作数
/// @param blocks 待检查基本块集合
/// @param callMayClobber 用于判断调用是否可能改写调用者可见内存
/// @return true 表示这些块中的 store/call 可能观测性地改写该地址
bool blocksMayClobberLoad(Value * pointer,
                          const std::unordered_set<BasicBlock *> & blocks,
                          const std::function<bool(CallInst *)> & callMayClobber);