#pragma once

#include "ILocRiscV64.h"

/// @brief RISCV64机器指令级局部peephole优化。
class RiscV64Peephole {
public:
	/// @brief 在ILoc指令序列上执行安全的局部重写。
	/// @return 是否修改了指令序列。
	bool run(ILocRiscV64 & iloc);
};
