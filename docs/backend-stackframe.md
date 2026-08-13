# RISC-V64 后端栈帧布局

## 栈指针与帧指针

当前后端使用 **纯 sp 相对寻址模型（无帧指针）**。`requiresFramePointer()` 恒返回 `false`（`CodeGeneratorRiscV64.cpp:134-138`），因此 s0/fp 既不保存也不建立为帧指针，局部变量、spill、栈传形参全部用 **sp 相对偏移** 访问。

| 寄存器 | 编号 | 角色 | 定义位置 |
|--------|------|------|---------|
| sp | x2 | 真实栈指针，prologue/epilogue 调整它，并作为所有栈访问的基址 | `PlatformRiscV64.h:15` |
| s0/fp | x8 | 保留为帧指针寄存器，但当前不启用（不保存、不设置） | `PlatformRiscV64.h:16` |

代码仍保留帧指针通路（`stackAlloc` 的 `useFramePointer` 分支、`ILocRiscV64::usesFramePointer()`），但因 `requiresFramePointer()` 恒为 false 而始终走 sp 相对寻址；固定大小栈帧无需 s0 也能稳定寻址。

---

## 栈帧逻辑布局

```
高地址
┌────────────────────────────────────────────────────┐
│                  caller 的栈帧                      │
│                                                    │
│  frameSize+0(sp)   第 9 个栈传形参 (int/ptr)        │  caller 写入
│  frameSize+8(sp)   第 10 个栈传形参                 │  caller 写入
│  ...                                               │
├────────────────────────────────────────────────────┤ ← old sp = sp + frameSize
│                                                    │
│  frameSize-8(sp)   ra    (若函数内有调用)          │  callee-saved GPR 保存区
│  frameSize-16(sp)  s1    (若被分配器使用)          │  (注意: s0/fp 当前不保存)
│  frameSize-24(sp)  s2    ...                       │
│  ...                                               │
│                                                    │
│  紧随 GPR 之后:                                    │  callee-saved FPR 保存区
│  fs0, fs1, ... (若启用 callee-saved FPR 且被使用)  │
│                                                    │
├────────────────────────────────────────────────────┤
│                                                    │
│  局部变量 / AllocaInst                              │  局部变量 + 溢出区
│  spill 变量                                         │  (sp 正偏移, 位于保存区下方)
│  ...                                               │
│                                                    │
├────────────────────────────────────────────────────┤
│  可能的 padding (16 字节对齐)                       │
├────────────────────────────────────────────────────┤
│                                                    │
│  0(sp)    本函数调用别人时第 9 个实参               │  outgoing 参数区
│  8(sp)    第 10 个实参                             │  (SP 正偏移访问)
│  ...                                               │
│                                                    │
└────────────────────────────────────────────────────┘ ← sp
低地址
```

### 各区域寻址方式

| 区域 | 基址寄存器 | 偏移 | 示例 |
|------|-----------|------|------|
| 栈传形参 (caller 写入) | sp | frameSize + 槽偏移 | `lw t0, 32(sp)` |
| callee-saved 保存区 | sp | frameSize - (i+1)*8 | `sd ra, 24(sp)` |
| 局部变量 / spill | sp | 保存区下方的正偏移 | `lw t0, 4(sp)` |
| outgoing 实参区 | sp | 0,8,... | `sw t0, 0(sp)` |

---

## 栈帧大小计算

**源码位置**: `backend/riscv64/CodeGeneratorRiscV64.cpp:737-739`

```cpp
const int maxArgs = maxCallArgCount(func);
const int outgoingBytes = maxArgs > 8 ? (maxArgs - 8) * 8 : 0;
const int frameSize = alignTo(savedFrameBytes + localBytes + outgoingBytes, 16);
```

三个组成部分:

| 组成部分 | 变量 | 说明 |
|---------|------|------|
| callee-saved 保存区 | `savedFrameBytes` | `(GPR保存数 + FPR保存数) * 8` |
| 局部变量 + 溢出区 | `localBytes` | 所有 AllocaInst、spill 变量的栈槽之和 |
| outgoing 参数区 | `outgoingBytes` | `max(0, (maxCallArgCount - 8)) * 8` |

最终 16 字节对齐: `alignTo(savedFrameBytes + localBytes + outgoingBytes, 16)`

---

## callee-saved 保存区

### 保存策略

**源码位置**: `backend/riscv64/CodeGeneratorRiscV64.cpp:155-217` (`computeSavedRegs`)

| 寄存器 | 保存条件 | 说明 |
|--------|---------|------|
| ra (x1) | 函数内有 CallInst | 返回地址，call 会覆盖；见下方 shrink-wrapping 说明 |
| s0/fp (x8) | 仅当 useFramePointer 时 | 当前 `requiresFramePointer()` 恒为 false，故 **不保存** |
| s1 (x9) | 被寄存器分配器实际使用 | 按需保存 |
| s2-s11 (x18-x27) | 被寄存器分配器实际使用 | 按需保存 |
| fs0-fs11 | 被分配器使用且启用 callee-saved FPR（默认开启） | 见浮点寄存器分配文档 |

### ra 的 shrink-wrapping（默认开启）

`computeSavedRegs` 默认 `enableShrinkWrap = true`（`CodeGeneratorRiscV64.cpp:613`）。当函数包含调用、但**并非所有路径都有调用**（条件性叶子）时，由 `analyzeCallPaths`（`ConditionalLeafAnalysis.cpp`，基于 `ShrinkWrapping` 命名空间的路径/支配分析）判定 `canOptimize`：

- ra 仍加入 `savedRegs` 并在栈帧中占据栈槽（`raSaveOffset()`），但 prologue **不**保存它（`ILocRiscV64::allocStack` 中 `reg == ra && shrinkWrapRA` 时 `continue`，`ILocRiscV64.cpp:795`）；
- 改由首个调用点附近的 `emitCallSiteSaveRA`/`emitCallSiteRestoreRA`（`InstSelectorRiscV64.cpp`）在真正会触发调用的路径上保存/恢复 ra；
- 这样不经过调用的叶子路径上完全省去 `sd ra`/`ld ra`。

若所有路径都有调用（非条件性叶子），则退回传统方式：ra 在 prologue 保存、epilogue 恢复。该标志经 `iloc.setShrinkWrapRA()` 传入指令选择阶段。

### 保存区偏移计算

prologue 中，callee-saved GPR 按顺序保存到栈帧顶部（从高地址到低地址）:

**源码位置**: `backend/riscv64/ILocRiscV64.cpp:791-810`

```cpp
for (int i = 0; i < static_cast<int>(savedRegs.size()); ++i) {
    const int reg = savedRegs[i];
    if (reg == RISCV64_RA_REG_NO && shrinkWrapRA) {
        continue;  // shrink-wrapping: ra 改在调用点保存
    }
    int offset = currentFrameSize - (i + 1) * 8;
    emit("sd", regName, std::to_string(offset) + "(sp)");
}
```

即第 i 个保存的寄存器位于 `frameSize - (i+1)*8(sp)`。

callee-saved FPR 紧随 GPR 之后:

**源码位置**: `backend/riscv64/ILocRiscV64.cpp:814-824`

```cpp
for (int i = 0; i < static_cast<int>(savedFPRs.size()); ++i) {
    int offset = currentFrameSize - (savedRegs.size() + i + 1) * 8;
    emit("fsd", fpReg, std::to_string(offset) + "(sp)");
}
```

### 具体示例

假设 `savedRegs = {ra, s1, s2}`，`frameSize = 48`:

```
offset = 48 - 1*8 = 40  →  sd  ra,  40(sp)
offset = 48 - 2*8 = 32  →  sd  s1,  32(sp)
offset = 48 - 3*8 = 24  →  sd  s2,  24(sp)
```

---

## 局部变量 / 溢出区

### 栈槽分配

**源码位置**: `backend/riscv64/CodeGeneratorRiscV64.cpp:644-655`

```cpp
int localBytes = 0;
auto assignStackSlot = [&](Value * val) {
    auto & info = allocMap[val];
    if (info.hasStackSlot) return;

    localBytes = alignTo(localBytes, stackSlotAlignment(stackObjectType(val)));
    localBytes += stackSlotSize(val);
    info.setStack(RISCV64_FP_REG_NO, -(savedFrameBytes + localBytes));
};
```

每个栈槽先按 **旧 sp（逻辑 FP）负偏移** `-(savedFrameBytes + localBytes)` 记录；由于 `useFramePointer` 为 false，`stackAlloc` 结尾会统一把这些槽重写为 **sp 正偏移** `frameSize + offset`（`CodeGeneratorRiscV64.cpp:740-746`）。

### 栈槽对齐与大小

**源码位置**: `backend/riscv64/CodeGeneratorRiscV64.cpp:56-98`

| 类型 | 对齐 | 最小大小 |
|------|------|---------|
| 普通整数 | 4 字节 | 4 字节 |
| 指针类型 | 8 字节 | 8 字节 |
| 向量类型 (RVV) | 16 字节 | 8192 字节（RVV 1.0 最大 VLEN） |
| 数组类型 | 按元素类型递归 | 按实际大小 |
| 其他 | 4 字节 | 4 字节 |

### 哪些值会分配栈槽

**源码位置**: `backend/riscv64/CodeGeneratorRiscV64.cpp:709-734`

```cpp
for (auto & [val, info]: allocMap) {
    if (val == nullptr) continue;
    if (GreedyRegAllocator::isForcedStackValue(val) || info.regId == -1 || greedyAllocator.isSpilled(val)) {
        stackSlotCandidates.push_back(val);
    }
}
// 随后按 Value::getCreationId() 这一确定性全序键排序后再逐个分配栈槽，
// 保证同一份 IR 每次生成完全相同的栈布局（上板缓存依赖此确定性）
```

三种情况:

| 情况 | 判定 | 示例 |
|------|------|------|
| 强制栈分配 | `isForcedStackValue(val)` — AllocaInst | 局部数组 `int a[10]` |
| 未分配寄存器 | `info.regId == -1` | 栈传形参 |
| 被溢出 | `isSpilled(val)` | 寄存器压力过大时的 spill |

---

## 栈传形参区

超过 8 个的形参由 caller 通过栈传递，位于 **FP 正方向偏移**。

**源码位置**: `backend/riscv64/CodeGeneratorRiscV64.cpp:662-690`

```cpp
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
        info.setStack(RISCV64_FP_REG_NO, stackOffset);  // 入口前旧sp正偏移, 之后重写为 frameSize+offset(sp)
        stackOffset += 8;
    }
}
```

整数和浮点参数使用**独立的计数器**，各自超过 8 个后走栈，每槽 8 字节。

---

## outgoing 参数区

本函数调用其他函数时，超过 a0-a7 / fa0-fa7 的实参需要预先存到栈上，供 callee 读取。

**源码位置**: `backend/riscv64/CodeGeneratorRiscV64.cpp:737-738`

```cpp
const int maxArgs = maxCallArgCount(func);
const int outgoingBytes = maxArgs > 8 ? (maxArgs - 8) * 8 : 0;
```

`maxCallArgCount` 取函数中**所有调用指令的最大参数个数**，确保 outgoing 区能容纳最"宽"的调用。

outgoing 区位于栈帧底部（SP 附近），用 **SP 正偏移** 访问: `0(sp)`, `8(sp)`, ...

---

## Prologue 生成

**源码位置**: `backend/riscv64/ILocRiscV64.cpp:767-834`

```
addi  sp, sp, -frameSize          # 1. 分配栈帧
sd    ra,  frameSize-8(sp)        # 2. 保存 ra (若函数内有调用且未做 ra shrink-wrapping)
sd    s1,  frameSize-16(sp)       # 3. 保存被分配器使用的 callee-saved GPR
...                                #    (注意: s0/fp 当前不保存)
fsd   fs0, offset(sp)             # 4. 保存被使用的 callee-saved FPR (若启用)
# 仅当 usesFramePointer() 为真时才追加 addi s0, sp, frameSize —— 当前不会发生
# 若 shrinkWrapRA 为真，第 2 步跳过，ra 改在调用点保存 (见 callee-saved 保存区一节)
```

大偏移处理: 若 `frameSize` 超出 12 位有符号立即数范围 (-2048~2047)，改用 `load_imm` + `add` 两条指令。

---

## Epilogue 生成

**源码位置**: `backend/riscv64/InstSelectorRiscV64.cpp:3123-3170` (`emitEpilogue`)

```
ld    fsN, offset(sp)             # 1. 逆序恢复 callee-saved FPR
...
ld    s1,  frameSize-16(sp)       # 2. 逆序恢复 callee-saved GPR (无 s0/fp)
ld    ra,  frameSize-8(sp)        #    (若 shrinkWrapRA 为真则跳过，ra 已在调用点恢复)
addi  sp, sp, frameSize           # 3. 恢复栈指针
ret                                # 4. 返回
```

恢复顺序与保存顺序**严格相反**，保证正确性。开启 ra shrink-wrapping 时，epilogue 跳过 `ld ra`（`InstSelectorRiscV64.cpp:3141` 处 `reg == ra && getShrinkWrapRA()` 时 `continue`）。

---

## 叶子函数栈帧省略

**说明**：当前后端没有名为 `canOmitLeafFrame` 的函数；栈帧省略是自然发生的。当函数无调用（不保存 ra）、无被使用的 callee-saved GPR/FPR、且无任何局部/溢出/AllocaInst 栈对象时，`computeSavedRegs` 返回空列表、`stackAlloc` 算出 `frameSize == 0`:

| 条件 | 结果 |
|------|------|
| 函数内无 CallInst | 不保存 ra |
| 无被分配器使用的 s1-s11 / fs* | savedRegs / savedFPRs 为空 |
| 无局部变量、spill、AllocaInst | localBytes == 0 |
| outgoingBytes == 0 | 无 outgoing 参数区 |

此时 `allocStack`（`frameSize == 0 && savedRegs.empty()` 时直接 return）不产生 prologue，`emitEpilogue` 也只发射一条 `ret`。注意 s0/fp 任何情况下都不在 savedRegs 中（无帧指针）。

---

## 完整示例

### 函数: `int foo(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j)`

假设: 有调用指令，使用了 s1、s2，有 1 个 int spill 变量（s0/fp 不保存）。

```
savedRegs = {ra, s1, s2}  →  savedFrameBytes = 3 * 8 = 24   (s0/fp 不保存)
localBytes = 4 (1 个 int spill)
outgoingBytes = 0 (假设调用的函数参数 ≤ 8)
frameSize = alignTo(24 + 4 + 0, 16) = 32
```

栈帧布局:

```
高地址
┌──────────────────────────────┐
│  caller 的栈帧               │
│  32(sp)  i (第9个形参)       │  栈传形参 (frameSize+0)
│  40(sp)  j (第10个形参)      │  栈传形参 (frameSize+8)
├──────────────────────────────┤ ← old sp = sp + 32
│  24(sp)  ra                  │  callee-saved (frameSize-8)
│  16(sp)  s1                  │  callee-saved (frameSize-16)
│   8(sp)  s2                  │  callee-saved (frameSize-24)
│   4(sp)  spill 变量           │  局部/溢出区
│   0(sp)  padding             │
└──────────────────────────────┘ ← sp
低地址
```

Prologue:

```asm
addi  sp, sp, -32
sd    ra,  24(sp)
sd    s1,  16(sp)
sd    s2,  8(sp)
# 不保存 s0、不建立帧指针
```

形参搬运 (emitFormalParamMoves):

```asm
# a-h 已在 a0-a7，按分配结果搬运
# i 从 32(sp) 加载，j 从 40(sp) 加载 (frameSize + 槽偏移)
```

Epilogue:

```asm
ld    s2,  8(sp)
ld    s1,  16(sp)
ld    ra,  24(sp)
addi  sp, sp, 32
ret
```
