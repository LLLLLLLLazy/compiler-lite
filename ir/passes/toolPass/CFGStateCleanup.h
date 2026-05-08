///
/// @file CFGStateCleanup.h
/// @brief CFG 边集与 phi incoming 一致性清理工具
///

#pragma once

class Function;

/// @brief 清理函数 CFG 中悬空或失配的前驱/后继边与 phi incoming
/// @param func 待清理的函数
/// @return true 表示至少移除了一个失效项
bool sanitizeCFGState(Function * func);