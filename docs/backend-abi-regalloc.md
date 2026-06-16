# a0-a7 ：ABI 参数寄存器与全局分配寄存器

## 核心结论

a0-a7 是"可分配的 caller-saved 寄存器"。平时参与全局寄存器分配；到了函数入口、函数调用、函数返回这些 ABI 边界，再由指令选择阶段插入必要的搬运。

当前后端把 IR `Value*` 当作虚拟寄存器建模。函数形参、普通指令结果、`CallInst` 返回值都先以虚拟寄存器身份参与活跃区间分析和 Greedy 分配，后端不会在 IR 阶段把形参永久绑定到 a0-a7。a0-a7/fa0-fa7 只是 ABI 边界上的传入、传出位置。

栈帧方面只有 `sp` (x2) 是真实栈指针；后端另固定使用 `s0/fp` (x8) 作为帧指针。`sp` 在 prologue/epilogue 中移动，`s0` 建好后通常保持不变，用于稳定访问形参栈槽、局部变量和 spill。

**源码位置**: `backend/riscv64/PlatformRiscV64.h:15-16`

```cpp
#define RISCV64_SP_REG_NO 2
#define RISCV64_FP_REG_NO 8
```

---

## 1. a0-a7 被放进全局 GPR 分配池

`GreedyRegAllocator::buildRegisterPool` 将 a0-a7 (x10-x17) 纳入可用寄存器池，与 t0-t6、s1-s11 一起参与全局分配。

**源码位置**: `backend/riscv64/GreedyRegAllocator.cpp:524-556`

```cpp
std::vector<int> GreedyRegAllocator::buildRegisterPool(Function * func) const
{
    // t3-t4 (28-29) 保留为scratch寄存器，t5-t6 (30-31) 参与全局分配
    std::vector<int> regs = {
        5, 6, 7,                          // t0-t2
        10, 11, 12, 13, 14, 15, 16, 17,  // a0-a7  ← 参与全局分配
        9, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,  // s1-s11
        30, 31,                           // t5-t6
    };
    return regs;
}
```

**不参与分配的寄存器**:

| 寄存器 | 编号 | 原因 |
|--------|------|------|
| zero | x0 | 恒为0 |
| ra | x1 | 返回地址，prologue/epilogue管理 |
| sp | x2 | 栈指针 |
| gp | x3 | 全局指针 |
| tp | x4 | 线程指针 |
| s0/fp | x8 | 帧指针，后端固定使用 |
| t3-t4 | x28-x29 | 保留为scratch寄存器 |

因此 allocator 会正常把虚拟寄存器（包括形参）分配到 a0-a7。

---

## 2. 跨 call 活着的值不能分到 a0-a7

a0-a7 是 caller-saved，函数调用会破坏它们。分配器通过 `canAssignReg` 约束保证正确性。

**源码位置**: `backend/riscv64/GreedyRegAllocator.cpp:785-803`

```cpp
bool GreedyRegAllocator::canAssignReg(LiveInterval * interval, int reg) const
{
    if (isVectorInterval(interval)) {
        return !intervalCrossesCall(interval);
    }
    if (isFloatInterval(interval)) {
        if (!isCallerSavedFloatReg(reg)) return true;
        return !intervalCrossesCall(interval);
    }
    if (!isCallerSavedReg(reg)) return true;   // callee-saved (s1-s11) 可以跨调用
    return !intervalCrossesCall(interval);      // caller-saved (a0-a7, t*) 不能跨调用
}
```

**caller-saved 判定**: `backend/riscv64/GreedyRegAllocator.cpp:714-717`

```cpp
bool GreedyRegAllocator::isCallerSavedReg(int reg)
{
    return (reg >= 5 && reg <= 7) || (reg >= 10 && reg <= 17) || (reg >= 28 && reg <= 31);
    //      t0-t2              a0-a7                    t3-t6
}
```

**含义**: 如果一个值的 live interval 覆盖了某个 call 指令，就不能分到 a0-a7 / t* 这些 caller-saved 寄存器，只能去 s1-s11 或 spill 到栈。

**call 指令编号收集**: `backend/riscv64/GreedyRegAllocator.cpp:122-126`

```cpp
for (auto & [inst, num] : instNumbering) {
    if (dynamic_cast<CallInst *>(inst) != nullptr) {
        callInstNumbers.push_back(num);
    }
}
```

---

## 3. 函数入口：形参从 ABI 位置搬到分配位置

形参在 ABI 上进入函数时确实在 a0-a7 / fa0-fa7，但 allocator 可能把形参分到任意寄存器（比如 s2、t1、甚至还是 a0），也可能 spill 到栈。所以指令选择一开始会调用 `emitFormalParamMoves()`。

**调用位置**: `backend/riscv64/InstSelectorRiscV64.cpp:677-690`

```cpp
void InstSelectorRiscV64::run()
{
    iloc.allocStack(func, tmp.reg());   // 1. 生成 prologue
    emitFormalParamMoves();              // 2. 搬运形参
    // 3. 翻译指令...
}
```

### 3.1 emitFormalParamMoves 逻辑

**源码位置**: `backend/riscv64/InstSelectorRiscV64.cpp:2969-3108`

#### 整数形参处理 (line 3015-3029)

```cpp
if (loc.kind == AbiArgLocKind::IntReg) {
    const int incomingReg = RISCV64_A0_REG_NO + loc.index;  // 例如第1个参数: a0
    if (info.hasReg()) {
        if (info.regId != incomingReg) {
            regMoves.push_back(RegMove{incomingReg, info.regId});  // 需要移动
        }
        // 如果 info.regId == incomingReg，什么都不做！
    } else if (info.hasStackSlot) {
        iloc.store_base(incomingReg, info.baseRegId, info.offset, ...);  // 存到栈
    }
}
```

三种情况:

| 分配结果 | 处理 | 示例 |
|---------|------|------|
| 分配到入参寄存器本身 | 无操作 | param0 在 a0 传入，分配到 a0 → 不搬 |
| 分配到其他寄存器 | 加入 regMoves | param0 在 a0 传入，分配到 s1 → `mv s1, a0` |
| 分配到栈 | 直接 store | param0 在 a0 传入，分配到栈 → `sw a0, offset(s0)` |

#### 循环依赖打破 (`emitGprRegMoves`, line 2925-2962)

当出现 `a0→a1, a1→a0` 这种循环搬运时，用 scratch 寄存器打破:

```
mv  scratch, a0    # 先保存原 a0
mv  a0, a1         # 搬原 a1 → a0
mv  a1, scratch    # 搬原 a0 → a1
```

scratch 寄存器通过 `tempMgr.borrowExcluding` 借用，排除所有入参寄存器和目标寄存器 (line 2982-3005)。

#### 栈传参数 (line 3033-3052)

超过 8 个的参数由 caller 通过栈传递；当前无帧指针，按 `frameSize + 槽偏移(sp)` 读取（基址/偏移由 `incomingStackBaseReg()`/`incomingStackOffset()` 决定）:

```cpp
const int base   = incomingStackBaseReg();            // usesFramePointer()? s0 : sp（当前 sp）
const int srcOff = incomingStackOffset(stackOffset);  // ? abiOffset : frameSize + abiOffset
if (info.hasReg()) {
    iloc.load_base(info.regId, base, srcOff, ...);
} else if (info.hasStackSlot && ...) {
    // 从 caller 栈帧加载到临时寄存器，再存到 callee 栈槽
    iloc.load_base(tmp.reg(), base, srcOff, ...);
    iloc.store_base(tmp.reg(), info.baseRegId, info.offset, ...);
}
```

#### 浮点形参 (line 3054-3086)

浮点形参从 fa0-fa7 传入，处理方式与整数类似，但额外支持:
- 分配到 FPR: 浮点寄存器间移动 (含循环依赖打破，用 `fmv.x.w`/`fmv.w.x` 经 GPR 中转)
- 分配到 GPR: 用 `fmv.x.w` 将浮点寄存器位模式移入整数寄存器
- 分配到栈: 用 `fsw` 存储到栈

---

## 4. 调用别的函数前：按 ABI 把实参放回 a0-a7

`translate_call` 仍然遵守 ABI：前 8 个整数实参加载到 a0-a7，浮点实参加载到 fa0-fa7，超出的存到栈，然后 call，返回值从 a0/fa0 取。

**源码位置**: `backend/riscv64/InstSelectorRiscV64.cpp:2336-2559`

```cpp
// 整数实参放入 a0-a7
if (loc.kind == AbiArgLocKind::IntReg) {
    loadValueToReg(RISCV64_A0_REG_NO + loc.index, arg, inst);
}
// 栈传实参
else if (loc.kind == AbiArgLocKind::Stack) {
    iloc.store_base(value.reg, RISCV64_SP_REG_NO, loc.index * 8, ...);
}

// call 指令
iloc.call_fun(call->getCallee()->getName());

// 返回值从 a0 取
if (call->hasResultValue()) {
    storeResult(call, RISCV64_A0_REG_NO, inst);
}
```

**关键保证**: 如果某个本地值还要在 call 后继续用，它不会被分配到 a0-a7（由 `canAssignReg` 的跨调用约束保证），所以 call 前写 a0-a7 不会破坏活值。

### 4.1 CallInst 中虚拟寄存器到 ABI 位置的映射

IR 调用点仍然只描述"调用谁、传哪些 `Value*`":

```llvm
%r = call i32 @bar(i32 %x, i32 %y)
```

假设寄存器分配结果是 `%x -> s1`、`%y -> t0`、`%r -> s2`，指令选择阶段会在调用边界生成类似:

```asm
mv   a0, s1      # 第 1 个整数实参进入 ABI 参数寄存器 a0
mv   a1, t0      # 第 2 个整数实参进入 ABI 参数寄存器 a1
call bar
mv   s2, a0      # 调用返回值从 ABI 返回寄存器 a0 搬到 %r 的分配位置
```

如果 `%x` 已经被分配到 a0，或者 `%r` 被分配到 a0，对应的 move 可以自然消失。超过 8 个整数实参、或超过 8 个浮点实参，会落到当前函数栈帧底部的 outgoing 参数区，用 `0(sp)`, `8(sp)` 这类地址传递。

---

## 5. 形参在活跃区间分析中的建模

形参被视为在指令编号 0 处隐式定义，活跃区间从 0 开始延伸到所有使用点。

**源码位置**: `backend/riscv64/LiveIntervalAnalysis.cpp:249-255`

```cpp
// 处理函数形参：形参在CFG入口前隐式定义，活跃区间起点为0
auto & params = func->getParams();
for (auto * param : params) {
    if (needsInterval(param)) {
        recordDef(param, nullptr, 0);
    }
}
```

这使得形参与其他 SSA 值一样参与 Greedy 寄存器分配的完整流程（干涉图构建、分配、溢出等）。

---

## 6. 栈分配阶段对形参的处理

**源码位置**: `backend/riscv64/CodeGeneratorRiscV64.cpp:657-690`

```cpp
// 为所有形参创建分配信息
for (auto * param: func->getParams()) {
    allocMap.try_emplace(param, RegAllocInfo{});
}

// RISC-V ABI：整数和浮点参数使用独立的寄存器计数器
int intIdx = 0, floatIdx = 0, stackOffset = 0;
for (auto * param : func->getParams()) {
    const bool isFloat = param->getType()->isFloatType();
    bool onStack = false;

    if (isFloat) {
        if (floatIdx >= 8) onStack = true;
        floatIdx++;
    } else {
        if (intIdx >= 8) onStack = true;
        intIdx++;
    }

    if (onStack) {
        auto & info = allocMap[param];
        info.regId = -1;
        info.setStack(RISCV64_FP_REG_NO, stackOffset);  // FP正方向偏移
        stackOffset += 8;
    }
}
```

- 前 8 个整数/浮点形参: 由 ABI 寄存器传递，此处**不设置栈位置**
- 超过 8 个的形参: 设置为 **FP 正方向偏移** 的栈位置（caller 栈帧中），每槽 8 字节

---

## 7. 栈帧布局

**源码位置**: `backend/riscv64/CodeGeneratorRiscV64.cpp:632-639` (注释), `line 739` (计算)

```
高地址
┌──────────────────────────────┐
│       caller 的栈帧           │
│       栈传形参                │  ← 超过8个的形参，位于 frameSize+偏移(sp)
├──────────────────────────────┤ ← old sp = sp + frameSize
│       ra + callee-saved      │  ← savedFrameBytes (不含 s0/fp)
│       局部变量 / 溢出变量     │  ← localBytes (保存区下方)
│       outgoing 参数区         │  ← 调用其他函数时超过8个的实参
└──────────────────────────────┘ ← SP
低地址
```

当前没有帧指针，所有偏移都相对 `sp`（`old sp = sp + frameSize`），所以:

- `frameSize(sp)`, `frameSize+8(sp)`, ... 是 caller 放好的栈传形参。
- `frameSize-8(sp)`, `frameSize-16(sp)`, ... 是保存区，保存 `ra`、被用到的 `s1-s11`、可选 `fs*`（**不含 s0/fp**）。
- 保存区下方较小的 `sp` 正偏移是本函数局部对象、`alloca` 和 spill 栈槽。
- `0(sp)`, `8(sp)`, ... 是本函数调用别人时使用的 outgoing 参数区。

栈帧大小计算 (`line 737-739`):

```cpp
const int maxArgs = maxCallArgCount(func);
const int outgoingBytes = maxArgs > 8 ? (maxArgs - 8) * 8 : 0;
const int frameSize = alignTo(savedFrameBytes + localBytes + outgoingBytes, 16);
```

---

## 8. Prologue / Epilogue

### Prologue

**源码位置**: `backend/riscv64/ILocRiscV64.cpp:767-834`

```
addi  sp, sp, -framesize       # 分配栈帧
sd    ra,  offset(sp)          # 保存 ra (若函数有调用且未做 ra shrink-wrapping)
sd    s1,  offset(sp)          # 保存被分配器使用的 callee-saved GPR
...                            # (注意: s0/fp 当前不保存)
fsd   fs0, offset(sp)          # 保存被使用的 callee-saved FPR (若启用)
# addi s0, sp, framesize 仅在 usesFramePointer() 时生成 —— 当前不会
# 若 shrinkWrapRA 为真，上面的 sd ra 跳过，改在调用点保存 (见 §8 callee-saved 保存策略)
```

因此函数体内的栈寻址都基于 sp:

```asm
lw    t0, 12(sp)     # 局部变量或 spill，基于 sp
sd    t1, 0(sp)      # 第 9 个实参的值写入 outgoing 参数区
```

生成代码会先把对应虚拟值装入某个寄存器，再 store 到 `0(sp)`。

### Epilogue

**源码位置**: `backend/riscv64/InstSelectorRiscV64.cpp:3123-3170` (`emitEpilogue`)

逆序恢复 callee-saved 寄存器，恢复 SP，执行 `ret`。

### Callee-saved 保存策略

**源码位置**: `backend/riscv64/CodeGeneratorRiscV64.cpp:155-217`

| 寄存器 | 保存条件 |
|--------|---------|
| ra (x1) | 函数内有调用指令时（条件性叶子默认走 ra shrink-wrapping，见下） |
| s0/fp (x8) | 当前不保存（`requiresFramePointer()` 恒为 false） |
| s1-s11 | 仅当被寄存器分配器实际使用时 |
| fs0-fs11 | 被使用且启用 callee-saved FPR（默认开启）时 |

**ra shrink-wrapping（默认 `enableShrinkWrap = true`）**：当函数有调用但并非所有路径都调用（条件性叶子，由 `ConditionalLeafAnalysis.cpp` 的 `analyzeCallPaths` 判定 `canOptimize`）时，ra 仍占栈槽但不在 prologue 保存，改由首个调用点附近的 `emitCallSiteSaveRA`/`emitCallSiteRestoreRA` 保存恢复，使纯叶子路径省去 `sd ra`/`ld ra`。`ILocRiscV64::allocStack`（`:795`）与 `emitEpilogue`（`:3141`）据 `shrinkWrapRA` 标志跳过 ra 的保存/恢复。

叶子函数（无调用、无栈传参数、无栈分配值）的 savedRegs 为空、frameSize 为 0，自然省略整个栈帧（代码中没有 `canOmitLeafFrame` 函数）。

---

## 9. 完整时序图

```
时间线：
─────────────────────────────────────────────────────────────────────
  caller 设置 a0-a7     prologue      emitFormalParamMoves    函数体执行      epilogue
       │                  │                │                    │              │
       │  实参值在 a0-a7  │ 保存 callee-  │ 将形参从 a0-a7    │ a0-a7 可被   │ 恢复 callee-
       │  / fa0-fa7       │ saved        │ 搬到最终位置      │ 自由使用     │ saved, ret
       │                  │               │                    │              │
       ▼                  ▼               ▼                    ▼              ▼
  [a0-a7=实参]       [a0-a7 不变]    [a0-a7 被释放]      [a0-a7 作为普通   [a0-a7=返回值]
  [fa0-fa7=浮点实参]                                    分配寄存器使用]
```

---

## 10. 具体场景示例

### 场景 A：形参分配到入参寄存器本身（零开销）

```
函数 foo(int a, int b):
  - a 从 a0 传入，分配器将 a 分配到 a0 → 无需移动
  - b 从 a1 传入，分配器将 b 分配到 a1 → 无需移动
```

### 场景 B：形参分配到 callee-saved 寄存器（跨调用存活）

```
函数 foo(int a):
  - a 从 a0 传入，a 的活跃区间跨越 call → 不能分到 a0
  - 分配器将 a 分配到 s1 → mv s1, a0
```

### 场景 C：循环搬运

```
函数 foo(int a, int b):
  - a 从 a0 传入，分配到 a1
  - b 从 a1 传入，分配到 a0
  → 循环依赖: a0→a1, a1→a0
  → 用 scratch 打破:
      mv  t0, a0
      mv  a0, a1
      mv  a1, t0
```

### 场景 D：非形参值分配到 a0-a7

```
函数 foo():
  int x = 1;    // x 的活跃区间不跨调用
  int y = x+1;  // y 可能被分配到 a0
  return y;     // mv a0, a0 (无操作，y 已在 a0)
```

### 场景 E：调用点实参已在目标 a 寄存器

```
  int x = ...;  // x 被分配到 a0
  foo(x);       // x 需要放入 a0 → 已在 a0，无需移动
```

---

## 11. 协调机制总结

a0-a7 同时承担 ABI 参数寄存器和全局分配寄存器两个角色，通过以下三层保证不冲突:

| 层次 | 机制 | 源码位置 |
|------|------|---------|
| 分配层 | a0-a7 纳入全局 GPR 池，任何值都可能分到 a0-a7 | `GreedyRegAllocator.cpp:533` |
| 约束层 | 跨调用的值不能分到 a0-a7 (caller-saved)，只能去 s1-s11 或栈 | `GreedyRegAllocator.cpp:799-803` |
| 搬运层 | 函数入口 `emitFormalParamMoves` 将形参从 ABI 位置搬到分配位置；调用点 `translate_call` 将实参放回 ABI 位置 | `InstSelectorRiscV64.cpp:685, 2336` |

**一句话**: a0-a7 在"两个 call 之间"的普通代码里当 caller-saved 临时寄存器用；在 ABI 边界（函数入口/调用/返回）由指令选择阶段插入搬运，保证 ABI 约定不被破坏。
