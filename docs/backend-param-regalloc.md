# 函数形参的寄存器分配：RA 层与 ABI 层的分离

形参在寄存器分配器眼里是**普通虚拟寄存器**；a0-a7/fa0-fa7 只是函数入口的 **ABI 来源位置**，入口处再搬到分配器决定的最终位置。

形参的寄存器分配不是"固定绑定到 a0-a7"，而是分两层处理：

| 层次 | 职责 | 时机 |
|------|------|------|
| RA 层 | 形参当作普通虚拟寄存器，参与 Greedy 分配 | 寄存器分配阶段 |
| ABI 层 | 函数入口将形参从 ABI 位置搬到 RA 决定的最终位置 | 指令选择阶段 |

---

## 1. RA 层：形参当作普通虚拟寄存器分配

### 1.1 活跃区间分析中的建模

形参被看成 CFG 入口前已经定义的 `Value*`，起点是 0。

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

形参的活跃区间从指令编号 0 开始，延伸到所有使用点，与其他 SSA 值完全一样参与：
- 活跃区间计算
- 干涉图构建
- Greedy 寄存器分配

### 1.2 Greedy 分配器处理形参

可用 GPR 池包含 a0-a7、t0-t2/t5-t6、s1-s11（`GreedyRegAllocator.cpp:524-556`；t3-t4 保留作 scratch），形参和普通指令结果一样被分配到物理寄存器或栈槽。

形参可能被分到：

| 目标 | 条件 | 示例 |
|------|------|------|
| a0-a7 | 不跨 call，可能直接用 caller-saved | 短命形参，仅在入口块使用 |
| s1-s11 | 跨 call，更可能分到 callee-saved | 形参在 call 后仍被使用 |
| t0-t6 | 不跨 call 的临时使用 | 形参与其他值干涉，a*/s* 都被占 |
| stack | spill 或强制栈位置 | 寄存器压力过大 |

### 1.3 跨调用约束

跨调用还活着的形参**不能**分到 a0-a7/t*，因为这些是 caller-saved，call 会 clobber。

**源码位置**: `backend/riscv64/GreedyRegAllocator.cpp:785-803`

```cpp
bool GreedyRegAllocator::canAssignReg(LiveInterval * interval, int reg) const
{
    // ...
    if (!isCallerSavedReg(reg)) return true;   // callee-saved (s1-s11) 可以跨调用
    return !intervalCrossesCall(interval);      // caller-saved (a0-a7, t*) 不能跨调用
}
```

### 1.4 栈分配阶段

**源码位置**: `backend/riscv64/CodeGeneratorRiscV64.cpp:657-690`

```cpp
// 为所有形参创建分配信息
for (auto * param: func->getParams()) {
    allocMap.try_emplace(param, RegAllocInfo{});
}

// 超过8个的形参走栈，设置 FP 正偏移位置
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

- 前 8 个整数/浮点形参：由 ABI 寄存器传递，此处**不预设栈位置**
- 超过 8 个的形参：设置为 **FP 正方向偏移**（caller 放好的 incoming argument area）

---

## 2. ABI 层：函数入口搬运形参

### 2.1 ABI 参数分类

**源码位置**: `backend/riscv64/InstSelectorRiscV64.cpp:54-83`

```cpp
enum class AbiArgLocKind { IntReg, FloatReg, Stack };

AbiArgLoc classifyAbiArg(Type * type, int & intRegCount, int & floatRegCount, int & stackCount)
{
    if (type != nullptr && type->isFloatType()) {
        if (floatRegCount < 8) return {AbiArgLocKind::FloatReg, floatRegCount++};
        ++floatRegCount;
        return {AbiArgLocKind::Stack, stackCount++};
    }
    if (intRegCount < 8) return {AbiArgLocKind::IntReg, intRegCount++};
    return {AbiArgLocKind::Stack, stackCount++};
}
```

RISC-V ABI 规则：整数和浮点参数使用**独立的寄存器计数器**。

| 参数类型 | 前 8 个 | 超出部分 |
|---------|---------|---------|
| 整数 | a0-a7 (x10-x17) | 栈传递 |
| 浮点 | fa0-fa7 (f10-f17) | 栈传递（不回落到整数寄存器） |

### 2.2 入口搬运的调用位置

**源码位置**: `backend/riscv64/InstSelectorRiscV64.cpp:677-690`

```cpp
void InstSelectorRiscV64::run()
{
    iloc.allocStack(func, tmp.reg());   // 1. 生成 prologue
    emitFormalParamMoves();              // 2. 搬运形参 ← ABI 层入口
    // 3. 翻译指令...
}
```

### 2.3 emitFormalParamMoves 核心逻辑

**源码位置**: `backend/riscv64/InstSelectorRiscV64.cpp:2969-3108`

#### 整数形参：从 a0-a7 搬到分配位置

```cpp
if (loc.kind == AbiArgLocKind::IntReg) {
    const int incomingReg = RISCV64_A0_REG_NO + loc.index;  // ABI 来源: a0, a1, ...
    if (info.hasReg()) {
        if (info.regId != incomingReg) {
            regMoves.push_back(RegMove{incomingReg, info.regId});  // 需要搬运
        }
        // info.regId == incomingReg → 不需要 move！
    } else if (info.hasStackSlot) {
        iloc.store_base(incomingReg, info.baseRegId, info.offset, ...);  // 存到栈
    }
}
```

三种情况：

| RA 分配结果 | 入口处理 | 生成的指令 |
|------------|---------|-----------|
| 分配到入参寄存器本身 | 无操作 | （无） |
| 分配到其他寄存器 | 加入 regMoves | `mv s1, a0` |
| 分配到栈槽 | 直接 store | `sw a0, -32(s0)` |

#### 循环搬运打破

当出现 `a0→a1, a1→a0` 这种循环时，借用 scratch 寄存器打破：

**源码位置**: `backend/riscv64/InstSelectorRiscV64.cpp:2925-2962` (`emitGprRegMoves`)

```cpp
while (!regMoves.empty()) {
    // 尝试找到 dst 不再是其他 move 的 src 的 move，直接执行
    ...
    // 所有 move 都阻塞 → 存在循环依赖
    const int cycleSrc = regMoves.front().src;
    iloc.mov_reg(scratchReg, cycleSrc);   // mv t0, a0
    for (auto & move : regMoves) {
        if (move.src == cycleSrc) move.src = scratchReg;  // 替换引用
    }
}
```

示例：

```asm
# param0: a0 → a1
# param1: a1 → a0
# 循环依赖，用 scratch 打破：
mv  t0, a0       # 保存 a0 到 scratch
mv  a1, a0       # 搬 a0 → a1（a0 还没被覆盖）
mv  a0, t0       # 搬 原a0 → a0（从 scratch 恢复）
```

#### 栈传整数参数

**源码位置**: `backend/riscv64/InstSelectorRiscV64.cpp:3033-3052`

超过 8 个的整数参数由 caller 放在栈上；当前无帧指针，按 `frameSize + 槽偏移(sp)` 读取（基址/偏移由 `incomingStackBaseReg()`/`incomingStackOffset()` 给出，`InstSelectorRiscV64.cpp:3110-3118`）：

```cpp
const int base   = incomingStackBaseReg();            // 当前 = sp
const int srcOff = incomingStackOffset(stackOffset);  // 当前 = frameSize + stackOffset
if (info.hasReg()) {
    iloc.load_base(info.regId, base, srcOff, ...);
} else if (info.hasStackSlot && ...) {
    // 从 caller 栈帧加载到临时寄存器，再存到 callee 栈槽
    iloc.load_base(tmp.reg(), base, srcOff, ...);
    iloc.store_base(tmp.reg(), info.baseRegId, info.offset, ...);
}
```

#### 浮点形参

**源码位置**: `backend/riscv64/InstSelectorRiscV64.cpp:3054-3108`

| RA 分配结果 | 入口处理 | 生成的指令 |
|------------|---------|-----------|
| 分配到入参 FPR 本身 | 无操作 | （无） |
| 分配到其他 FPR | 加入 floatRegMoves | `fsgnj.s fa3, fa0` |
| 分配到 GPR | fmv.x.w 搬运 | `fmv.x.w s1, fa0` |
| 分配到栈槽 | fsw 存储 | `fsw fa0, -32(s0)` |

浮点循环搬运通过 `fmv.x.w` 将值暂存到 GPR scratch 打破。

---

## 3. 两层协作的完整流程

```
                    ┌─────────────────────────────┐
                    │        RA 层                │
                    │                             │
                    │  1. 形参在指令 0 处隐式定义  │
                    │     LiveIntervalAnalysis     │
                    │     (line 249)              │
                    │                             │
                    │  2. Greedy 分配器分配        │
                    │     形参 → a0 / s1 / t0 / 栈│
                    │     GreedyRegAllocator      │
                    │     (line 524)              │
                    │                             │
                    │  3. 栈分配：栈传形参设 FP+偏移│
                    │     stackAlloc              │
                    │     (line 686)              │
                    └──────────────┬──────────────┘
                                   │
                                   │  分配结果写入 allocMap
                                   │
                    ┌──────────────▼──────────────┐
                    │        ABI 层               │
                    │                             │
                    │  1. prologue 生成栈帧        │
                    │     allocStack              │
                    │     (ILocRiscV64.cpp:767)   │
                    │                             │
                    │  2. emitFormalParamMoves    │
                    │     将形参从 ABI 位置        │
                    │     搬到 RA 决定的最终位置   │
                    │     (line 2969)             │
                    │                             │
                    │  3. 翻译函数体指令           │
                    │     形参已在最终位置可用     │
                    └─────────────────────────────┘
```

---

## 4. 具体场景示例

### 场景 A：形参分配到入参寄存器本身（零开销）

```c
int foo(int a, int b) { return a + b; }
```

- a 从 a0 传入，分配到 a0 → **不生成 move**
- b 从 a1 传入，分配到 a1 → **不生成 move**

```asm
# prologue (省略)
# emitFormalParamMoves: 无操作
add  a0, a0, a1    # a + b，结果已在 a0
ret
```

### 场景 B：形参分配到 callee-saved（跨调用存活）

```c
int foo(int a) { bar(); return a; }
```

- a 从 a0 传入，a 的活跃区间跨越 call → 不能分到 a0
- 分配器将 a 分配到 s1

```asm
# prologue (无帧指针，全部 sp 相对寻址)
addi  sp, sp, -16
sd    ra, 8(sp)        # 保存 ra (函数内有调用)
sd    s1, 0(sp)        # 保存 s1 (被分配器使用)

# emitFormalParamMoves
mv    s1, a0           # a: a0 → s1

# 函数体
call  bar
mv    a0, s1           # 返回 a
ld    s1, 0(sp)        # 恢复 s1
ld    ra, 8(sp)        # 恢复 ra
addi  sp, sp, 16
ret
```

### 场景 C：循环搬运

```c
int foo(int a, int b) { /* a 分到 a1, b 分到 a0 */ }
```

- a 从 a0 传入，分配到 a1
- b 从 a1 传入，分配到 a0

```asm
# emitFormalParamMoves: a0→a1, a1→a0 (循环依赖)
mv    t0, a0           # scratch 保存 a0 (cycleSrc)
mv    a0, a1           # a1 → a0
mv    a1, t0           # 原 a0(在 t0) → a1
```

### 场景 D：形参 spill 到栈

```c
int foo(int a, int b, int c, ...) { /* 寄存器压力极大，a 被 spill */ }
```

- a 从 a0 传入，被 spill 到栈槽（sp 相对偏移）

```asm
# emitFormalParamMoves
sw    a0, offset(sp)     # a: a0 → 栈槽 (sp 相对寻址)
```

### 场景 E：栈传形参

```c
int foo(int a1, int a2, ..., int a9) { /* a9 是第 9 个参数 */ }
```

- a9 由 caller 放在栈帧顶部（`frameSize(sp)`），分配器可能分到 s2

```asm
# emitFormalParamMoves
lw    s2, frameSize(sp)  # a9: 从 caller 栈帧加载到 s2
```

---

## 5. 调用点实参传递（对称过程）

**源码位置**: `backend/riscv64/InstSelectorRiscV64.cpp:2336-2559` (`translate_call`)

调用点做的是**反向操作**：将实参从 RA 分配的位置搬到 ABI 约定的 a0-a7/fa0-fa7/栈。

```cpp
// 整数实参放入 a0-a7
if (loc.kind == AbiArgLocKind::IntReg) {
    loadValueToReg(RISCV64_A0_REG_NO + loc.index, arg, inst);
}
// 栈传实参
else if (loc.kind == AbiArgLocKind::Stack) {
    iloc.store_base(value.reg, RISCV64_SP_REG_NO, loc.index * 8, ...);
}
```

如果实参已经在目标 a 寄存器中（RA 恰好分到了 a0），同样不生成 move。

---

## 6. 一句话总结

形参在寄存器分配器眼里是普通虚拟寄存器；a0-a7/fa0-fa7 只是函数入口的 ABI 来源位置，入口处再搬到分配器决定的最终位置。两层分离使得 RA 可以自由选择最优分配，而 ABI 约定仅在入口/调用点这两个边界由指令选择阶段保证。
