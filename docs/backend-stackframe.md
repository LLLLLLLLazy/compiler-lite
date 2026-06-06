# RISC-V64 后端栈帧布局

## 栈指针与帧指针

当前后端使用 **sp + s0/fp 的固定帧指针模型**：

| 寄存器 | 编号 | 角色 | 定义位置 |
|--------|------|------|---------|
| sp | x2 | 真实栈指针，prologue/epilogue 会调整它 | `PlatformRiscV64.h:15` |
| s0/fp | x8 | 帧指针，设为函数入口前的旧 sp，用于访问局部变量、spill、栈传形参 | `PlatformRiscV64.h:16` |

严格说只有 **1 个栈指针 (sp)**；s0 是帧指针，不参与栈伸缩，仅作为稳定的寻址基址。

---

## 栈帧逻辑布局

```
高地址
┌────────────────────────────────────────────────────┐
│                  caller 的栈帧                      │
│                                                    │
│  +0(s0)   第 9 个栈传形参 (int/ptr)                │  caller 写入
│  +8(s0)   第 10 个栈传形参                         │  caller 写入
│  ...                                               │
├────────────────────────────────────────────────────┤ ← s0 / old sp
│                                                    │
│  frameSize-8(sp)   ra    (若有调用)                │  callee-saved GPR 保存区
│  frameSize-16(sp)  s0/fp (始终)                    │
│  frameSize-24(sp)  s1    (若被分配器使用)          │
│  ...                                               │
│  frameSize-(n+1)*8(sp)  最后一个 callee-saved GPR  │
│                                                    │
│  紧随 GPR 之后:                                    │  callee-saved FPR 保存区
│  fs0, fs1, fs2, ... (若启用 callee-saved FPR)     │
│                                                    │
├────────────────────────────────────────────────────┤
│                                                    │
│  -(savedFrameBytes+4)(s0)   局部变量 / AllocaInst  │  局部变量 + 溢出区
│  -(savedFrameBytes+8)(s0)   spill 变量             │  (FP 负偏移访问)
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

| 区域 | 基址寄存器 | 偏移方向 | 示例 |
|------|-----------|---------|------|
| 栈传形参 (caller 写入) | s0 | 正偏移 | `lw t0, 0(s0)` |
| callee-saved 保存区 | sp | 正偏移 (prologue/epilogue 中) | `sd ra, 24(sp)` |
| 局部变量 / spill | s0 | 负偏移 | `lw t0, -32(s0)` |
| outgoing 实参区 | sp | 正偏移 | `sw t0, 0(sp)` |

---

## 栈帧大小计算

**源码位置**: `backend/riscv64/CodeGeneratorRiscV64.cpp:1069-1074`

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

**源码位置**: `backend/riscv64/CodeGeneratorRiscV64.cpp:140-176` (`computeSavedRegs`)

| 寄存器 | 保存条件 | 说明 |
|--------|---------|------|
| ra (x1) | 函数内有 CallInst | 返回地址，call 会覆盖 |
| s0/fp (x8) | 始终 | 后端固定使用 s0 作为帧指针 |
| s1 (x9) | 被寄存器分配器实际使用 | 按需保存 |
| s2-s11 (x18-x27) | 被寄存器分配器实际使用 | 按需保存 |
| fs0-fs11 | 被分配器使用且启用 callee-saved FPR | 可选功能 |

### 保存区偏移计算

prologue 中，callee-saved GPR 按顺序保存到栈帧顶部（从高地址到低地址）:

**源码位置**: `backend/riscv64/ILocRiscV64.cpp:750-761`

```cpp
for (int i = 0; i < static_cast<int>(savedRegs.size()); ++i) {
    int offset = currentFrameSize - (i + 1) * 8;
    emit("sd", regName, std::to_string(offset) + "(sp)");
}
```

即第 i 个保存的寄存器位于 `frameSize - (i+1)*8(sp)`。

callee-saved FPR 紧随 GPR 之后:

**源码位置**: `backend/riscv64/ILocRiscV64.cpp:766-776`

```cpp
for (int i = 0; i < static_cast<int>(savedFPRs.size()); ++i) {
    int offset = currentFrameSize - (savedRegs.size() + i + 1) * 8;
    emit("fsd", fpReg, std::to_string(offset) + "(sp)");
}
```

### 具体示例

假设 `savedRegs = {ra, s0, s2}`，`frameSize = 48`:

```
offset = 48 - 1*8 = 40  →  sd  ra,  40(sp)
offset = 48 - 2*8 = 32  →  sd  s0,  32(sp)
offset = 48 - 3*8 = 24  →  sd  s2,  24(sp)
```

---

## 局部变量 / 溢出区

### 栈槽分配

**源码位置**: `backend/riscv64/CodeGeneratorRiscV64.cpp:983-994`

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

每个栈槽的偏移为 `-(savedFrameBytes + localBytes)`，即 **FP 负偏移**。

### 栈槽对齐与大小

**源码位置**: `backend/riscv64/CodeGeneratorRiscV64.cpp:69-98`

| 类型 | 对齐 | 最小大小 |
|------|------|---------|
| 普通整数 | 4 字节 | 4 字节 |
| 指针类型 | 8 字节 | 8 字节 |
| 向量类型 (RVV) | 16 字节 | 16 字节 |
| 数组类型 | 按元素类型递归 | 按实际大小 |
| 其他 | 4 字节 | 4 字节 |

### 哪些值会分配栈槽

**源码位置**: `backend/riscv64/CodeGeneratorRiscV64.cpp:1048-1067`

```cpp
for (auto & [val, info]: allocMap) {
    if (GreedyRegAllocator::isForcedStackValue(val) || info.regId == -1 || greedyAllocator.isSpilled(val)) {
        stackSlotCandidates.push_back(val);
    }
}
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

**源码位置**: `backend/riscv64/CodeGeneratorRiscV64.cpp:1001-1029`

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
        info.setStack(RISCV64_FP_REG_NO, stackOffset);  // +0(s0), +8(s0), ...
        stackOffset += 8;
    }
}
```

整数和浮点参数使用**独立的计数器**，各自超过 8 个后走栈，每槽 8 字节。

---

## outgoing 参数区

本函数调用其他函数时，超过 a0-a7 / fa0-fa7 的实参需要预先存到栈上，供 callee 读取。

**源码位置**: `backend/riscv64/CodeGeneratorRiscV64.cpp:1070-1071`

```cpp
const int maxArgs = maxCallArgCount(func);
const int outgoingBytes = maxArgs > 8 ? (maxArgs - 8) * 8 : 0;
```

`maxCallArgCount` 取函数中**所有调用指令的最大参数个数**，确保 outgoing 区能容纳最"宽"的调用。

outgoing 区位于栈帧底部（SP 附近），用 **SP 正偏移** 访问: `0(sp)`, `8(sp)`, ...

---

## Prologue 生成

**源码位置**: `backend/riscv64/ILocRiscV64.cpp:727-785`

```
addi  sp, sp, -frameSize          # 1. 分配栈帧
sd    ra,  frameSize-8(sp)        # 2. 保存 ra (若有调用)
sd    s0,  frameSize-16(sp)       # 3. 保存 s0/fp (始终)
sd    s1,  frameSize-24(sp)       # 4. 保存 s1 (若被使用)
...                                #    保存其他被使用的 callee-saved GPR
fsd   fs0, offset(sp)             # 5. 保存 callee-saved FPR (若启用)
addi  s0,  sp, frameSize          # 6. 设置帧指针 s0 = sp + frameSize
```

大偏移处理: 若 `frameSize` 超出 12 位有符号立即数范围 (-2048~2047)，改用 `load_imm` + `add` 两条指令。

---

## Epilogue 生成

**源码位置**: `backend/riscv64/InstSelectorRiscV64.cpp:2616-2658`

```
ld    fsN, offset(sp)             # 1. 逆序恢复 callee-saved FPR
...
ld    s1,  frameSize-24(sp)       # 2. 逆序恢复 callee-saved GPR
ld    s0,  frameSize-16(sp)
ld    ra,  frameSize-8(sp)
addi  sp, sp, frameSize           # 3. 恢复栈指针
ret                                # 4. 返回
```

恢复顺序与保存顺序**严格相反**，保证正确性。

---

## 叶子函数栈帧省略

**源码位置**: `backend/riscv64/CodeGeneratorRiscV64.cpp:186-203` (`canOmitLeafFrame`)

当满足以下**全部条件**时，省略整个栈帧:

| 条件 | 原因 |
|------|------|
| 函数内无 CallInst | 不需要保存 ra |
| outgoingArgBytes == 0 | 不需要 outgoing 参数区 |
| savedRegs 仅含 s0/fp | 没有其他 callee-saved 被使用 |
| 无任何值分配到栈 | 不需要局部变量/spill 区 |

省略后: `currentSavedRegs.clear()`, `frameSize = 0`，函数直接使用 `ret` 返回。

---

## 完整示例

### 函数: `int foo(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j)`

假设: 有调用指令，使用了 s1、s2，有 1 个 spill 变量。

```
savedRegs = {ra, s0, s1, s2}  →  savedFrameBytes = 4 * 8 = 32
localBytes = 4 (1 个 int spill)
outgoingBytes = 0 (假设调用的函数参数 ≤ 8)
frameSize = alignTo(32 + 4 + 0, 16) = 48
```

栈帧布局:

```
高地址
┌──────────────────────────────┐
│  caller 的栈帧               │
│  +0(s0)   i (第9个形参)      │  栈传形参
│  +8(s0)   j (第10个形参)     │  栈传形参
├──────────────────────────────┤ ← s0 = sp + 48
│  40(sp)  ra                  │  callee-saved
│  32(sp)  s0/fp               │  callee-saved
│  24(sp)  s1                  │  callee-saved
│  16(sp)  s2                  │  callee-saved
│  -36(s0) spill 变量           │  局部/溢出区
├──────────────────────────────┤
│  padding (12 字节)           │
└──────────────────────────────┘ ← sp
低地址
```

Prologue:

```asm
addi  sp, sp, -48
sd    ra,  40(sp)
sd    s0,  32(sp)
sd    s1,  24(sp)
sd    s2,  16(sp)
addi  s0,  sp, 48
```

形参搬运 (emitFormalParamMoves):

```asm
# a-h 已在 a0-a7，按分配结果搬运
# i 从 +0(s0) 加载，j 从 +8(s0) 加载
```

Epilogue:

```asm
ld    s2,  16(sp)
ld    s1,  24(sp)
ld    s0,  32(sp)
ld    ra,  40(sp)
addi  sp, sp, 48
ret
```
