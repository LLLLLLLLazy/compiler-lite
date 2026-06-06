# 活跃区间分析：反向数据流固定点迭代

当前活跃性分析**不是 DFS，也不是 BFS**，而是**反向数据流固定点迭代**。正确性不依赖遍历顺序，而依赖"迭代到固定点"。遍历顺序只影响收敛速度。

---

## 整体流程

**源码位置**: `backend/riscv64/LiveIntervalAnalysis.cpp:217-531` (`computeLiveIntervals`)

```
┌─────────────────────────────────────────────────────────────┐
│  Phase 1: 指令编号 & 局部 def/use 收集 (line 257-280)      │
│  按 func->getBlocks() 正序遍历，为每条指令编号              │
├─────────────────────────────────────────────────────────────┤
│  Phase 2: CFG 级活跃性传播 (line 285-311)                  │
│  反向数据流固定点迭代，计算 liveIn/liveOut                  │
├─────────────────────────────────────────────────────────────┤
│  Phase 3: 逐块反向扫描生成区间 (line 317-374)              │
│  按基本块正序，块内指令逆序，构造 LiveInterval              │
├─────────────────────────────────────────────────────────────┤
│  Phase 4: 循环多定义值保守扩展 (line 382-426)              │
│  PhiLowering 后同一 Value 多次定义的保守处理                │
├─────────────────────────────────────────────────────────────┤
│  Phase 5: 跨循环活跃性补足 (line 432-502)                  │
│  对跨越自然循环的值补足整个循环体区间                       │
├─────────────────────────────────────────────────────────────┤
│  Phase 6: 溢出权重计算 (line 504-530)                      │
│  基于循环深度加权                                          │
└─────────────────────────────────────────────────────────────┘
```

---

## Phase 1: 指令编号 & 局部 def/use 收集

**源码位置**: `LiveIntervalAnalysis.cpp:257-280`

按 `func->getBlocks()` 的存储顺序（即基本块在函数中的排列顺序）正序遍历，为每条指令分配递增编号：

```cpp
for (auto * bb : blocks) {
    for (auto * inst : bb->getInstructions()) {
        int instNum = nextInstNum++;
        instNumbering[inst] = instNum;

        // 收集 use
        for (Value * usedValue : instructionUses(inst)) {
            recordUse(usedValue, bb, instNum);
            if (needsInterval(usedValue) && blockDef[bb].find(usedValue) == blockDef[bb].end()) {
                blockUse[bb].insert(usedValue);
            }
        }

        // 收集 def
        if (Value * definedValue = instructionDef(inst); needsInterval(definedValue)) {
            recordDef(definedValue, bb, instNum);
            blockDef[bb].insert(definedValue);
        }
    }
}
```

同时统计每个基本块的 `blockUse[bb]`（向上暴露的使用）和 `blockDef[bb]`（块内定义），供 Phase 2 使用。

### 形参的隐式定义

**源码位置**: `LiveIntervalAnalysis.cpp:249-255`

形参在指令编号 0 处隐式定义，活跃区间起点为 0：

```cpp
for (auto * param : params) {
    if (needsInterval(param)) {
        recordDef(param, nullptr, 0);
    }
}
```

### 不需要活跃区间的 Value

**源码位置**: `LiveIntervalAnalysis.cpp:155-188`

| Value 类型 | 原因 |
|-----------|------|
| ConstInteger | 常量，直接用立即数 |
| ConstFloat | 浮点常量 |
| GlobalVariable | 通过地址访问 |
| RegVariable | 物理寄存器，已分配 |
| Function | 函数对象，不是变量 |
| BasicBlock | 基本块标签 |
| AllocaInst | 一定分配在栈上 |

---

## Phase 2: CFG 级活跃性传播（反向数据流固定点迭代）

**源码位置**: `LiveIntervalAnalysis.cpp:285-311`

这是核心的活跃性传播阶段，使用**经典的数据流分析框架**：

### 数据流方程

```
liveOut[bb] = ⋃ liveIn[succ]    对 bb 的所有后继 succ
liveIn[bb]  = blockUse[bb] ∪ (liveOut[bb] - blockDef[bb])
```

### 迭代实现

```cpp
bool changed = true;
while (changed) {
    changed = false;
    for (auto it = blocks.rbegin(); it != blocks.rend(); ++it) {  // 基本块逆序
        BasicBlock * bb = *it;
        ValueSet newOut;
        for (BasicBlock * succ : bb->getSuccessors()) {
            auto succLiveIt = liveIn.find(succ);
            if (succLiveIt != liveIn.end()) {
                newOut.insert(succLiveIt->second.begin(), succLiveIt->second.end());
            }
        }

        ValueSet newIn = blockUse[bb];
        for (Value * value : newOut) {
            if (blockDef[bb].find(value) == blockDef[bb].end()) {
                newIn.insert(value);
            }
        }

        if (newOut != liveOut[bb] || newIn != liveIn[bb]) {
            liveOut[bb] = std::move(newOut);
            liveIn[bb] = std::move(newIn);
            changed = true;
        }
    }
}
```

### 关键特征

| 特征 | 说明 |
|------|------|
| 遍历方向 | 基本块列表**逆序** (`rbegin()` → `rend()`) |
| 迭代方式 | **朴素迭代** (round-robin)，无 worklist |
| 终止条件 | `changed == false`，即 liveIn/liveOut 不再变化（到达固定点） |
| 正确性保证 | 依赖**迭代到固定点**，不依赖遍历顺序 |
| 收敛性 | 活跃变量分析是 **forward monotone** 框架的对偶（反向），格上的单调函数必然收敛 |

### 为什么不是 DFS / BFS

- **DFS**: 需要显式栈，沿深度优先遍历，当前代码没有
- **BFS**: 需要显式队列，沿广度优先遍历，当前代码没有
- **Worklist**: 只把发生变化的块加入工作列表，减少无效迭代，当前代码也没有
- **当前实现**: 简单的 round-robin，每轮遍历所有基本块，直到不动点

### 遍历顺序对收敛的影响

逆序遍历 (`rbegin()`) 对**反向数据流问题**通常比正序更快收敛，因为信息自然地从后向前传播。但这只影响迭代轮数，不影响最终结果。

最坏情况下需要 O(N) 轮迭代（N 为基本块数），实际中通常 2-3 轮即可收敛。

---

## Phase 3: 逐块反向扫描生成区间

**源码位置**: `LiveIntervalAnalysis.cpp:317-374`

在 Phase 2 计算出 liveIn/liveOut 后，逐块构造 LiveInterval。

### 算法

1. 按基本块**正序**遍历
2. 每个块内，按指令**逆序**扫描
3. 用 `liveEnd` 映射跟踪每个值当前打开的活跃段右端点

```cpp
for (auto * bb : blocks) {
    const int blockStart = blockFirstInst[bb];
    const int blockEnd = blockLastInst[bb] + 1;
    std::unordered_map<Value *, int> liveEnd;

    // liveOut 中的值从块尾开始活跃
    for (Value * value : liveOut[bb]) {
        if (needsInterval(value)) {
            liveEnd[value] = blockEnd;
        }
    }

    // 块内指令逆序扫描
    for (auto instIt = insts.rbegin(); instIt != insts.rend(); ++instIt) {
        const int pos = instNumbering[inst];

        // def: 关闭活跃段
        if (Value * def = instructionDef(inst); needsInterval(def)) {
            auto openIt = liveEnd.find(def);
            if (openIt != liveEnd.end()) {
                getOrCreateInterval(def)->addSegment(pos, openIt->second);
                liveEnd.erase(openIt);
            } else {
                getOrCreateInterval(def)->addSegment(pos, pos + 1);  // 局部死定义
            }
        }

        // use: 打开活跃段（或更新右端点）
        for (Value * use : instructionUses(inst)) {
            if (!needsInterval(use)) continue;
            auto [_, inserted] = liveEnd.emplace(use, pos + 1);
            if (!inserted) {
                // 反向扫描时保留更靠后的右端点
            }
        }
    }

    // 块首仍打开的值：从 blockStart 延伸到 liveEnd 位置
    for (const auto & [value, end] : liveEnd) {
        getOrCreateInterval(value)->addSegment(blockStart, end);
    }
}
```

### 为什么块内逆序扫描

逆序扫描使得：
- 遇到 **use** 时打开（或延伸）活跃段的右端点
- 遇到 **def** 时关闭活跃段，生成 `[def位置, use/块尾位置)` 的区间
- 这自然产生了从定义点到最后使用点的活跃区间

---

## Phase 4: 循环多定义值保守扩展

**源码位置**: `LiveIntervalAnalysis.cpp:382-426`

PhiLowering 会把循环携带值改写成"同一个逻辑 Value 的多次定义"。当前 split lane 仍按 Value 的整体区间决定 call-site transfer，因此对这类值做保守扩展。

**条件**: 同一 Value 有多个定义点，且定义/使用触及循环体 (loopDepth > 0)。

**处理**: 将所有定义点和使用点的范围合并为一条保守总段：

```cpp
if (firstPos != INT_MAX && lastPos != INT_MIN) {
    interval->addSegment(firstPos, lastPos + 1);
}
```

---

## Phase 5: 跨循环活跃性补足

**源码位置**: `LiveIntervalAnalysis.cpp:432-502`

对跨越自然循环的值，补足整个循环体区间，避免回边值在 split lane 被误判为可中途改位。

### 算法

1. 收集所有自然循环的指令范围 `[loopStart, loopEnd)`
2. 对每个活跃区间，检查其使用/定义是否与循环有交叉
3. 若有交叉，补足整个循环体区间：

```cpp
if ((hasDefBeforeLoop && (usedInLoop || usedAtOrAfterLoop)) ||
    (hasDefInsideLoop && usedOutsideLoop)) {
    interval->addSegment(loop.start, loop.end);
}
```

---

## Phase 6: 溢出权重计算

**源码位置**: `LiveIntervalAnalysis.cpp:504-530`

基于循环深度加权，循环越深溢出代价越高：

```cpp
for (auto * interval : intervals) {
    int maxDepth = 0;
    for (int pos : interval->getUsePositions()) {
        auto it = instNumToLoopDepth.find(pos);
        if (it != instNumToLoopDepth.end()) {
            maxDepth = std::max(maxDepth, it->second);
        }
    }
    interval->maxLoopDepth = maxDepth;
    interval->calcSpillWeight(maxDepth);
}
```

---

## 干涉图构建

**源码位置**: `LiveIntervalAnalysis.cpp:538-565` (`buildInterferenceGraph`)

在活跃区间计算完成后，根据区间重叠关系构建干涉图：

```cpp
// 按区间 start 排序，用于提前终止内层循环
std::sort(sortedIdx.begin(), sortedIdx.end(), [this](int a, int b) {
    return intervals[a]->getStart() < intervals[b]->getStart();
});

for (int ii = 0; ii < n; ++ii) {
    int i = sortedIdx[ii];
    for (int jj = ii + 1; jj < n; ++jj) {
        int j = sortedIdx[jj];
        // 一旦 j 的 start >= i 的 end，后续都不会重叠
        if (intervals[j]->getStart() >= intervals[i]->getEnd()) break;
        if (intervals[i]->overlaps(*intervals[j])) {
            interferenceGraph->addEdge(i, j);
        }
    }
}
```

**优化**: 按 start 排序后，内层循环可以提前终止（`break`），将 O(n²) 的最坏复杂度在实践中大幅降低。

---

## 完整数据流图

```
                    ┌──────────────────────┐
                    │  func->getBlocks()   │
                    │  基本块列表          │
                    └──────────┬───────────┘
                               │
                    ┌──────────▼───────────┐
                    │  Phase 1: 指令编号    │
                    │  正序遍历基本块      │
                    │  收集 blockUse/Def   │
                    │  形参在 0 处隐式定义 │
                    └──────────┬───────────┘
                               │
                    ┌──────────▼───────────┐
                    │  Phase 2: 活跃性传播  │
                    │  反向数据流固定点迭代 │
                    │  逆序遍历基本块      │
                    │  liveOut = ⋃liveIn   │
                    │  liveIn = use∪(out-def)│
                    │  直到不动点          │
                    └──────────┬───────────┘
                               │
                    ┌──────────▼───────────┐
                    │  Phase 3: 生成区间    │
                    │  正序遍历基本块      │
                    │  块内指令逆序扫描    │
                    │  构造 LiveInterval   │
                    └──────────┬───────────┘
                               │
                    ┌──────────▼───────────┐
                    │  Phase 4: 循环多定义  │
                    │  保守扩展            │
                    └──────────┬───────────┘
                               │
                    ┌──────────▼───────────┐
                    │  Phase 5: 跨循环补足  │
                    │  补足循环体区间      │
                    └──────────┬───────────┘
                               │
                    ┌──────────▼───────────┐
                    │  Phase 6: 溢出权重    │
                    │  循环深度加权        │
                    └──────────┬───────────┘
                               │
                    ┌──────────▼───────────┐
                    │  buildInterferenceGraph│
                    │  区间重叠 → 干涉边   │
                    │  按 start 排序优化   │
                    └──────────────────────┘
```

---

## 与经典算法的对比

| 方面 | 当前实现 | 经典 round-robin | Worklist 算法 |
|------|---------|-----------------|--------------|
| 遍历策略 | 基本块逆序 round-robin | 任意顺序 round-robin | 只遍历变化的块 |
| 数据结构 | `unordered_map<BB, ValueSet>` | 同左 | worklist 队列/栈 |
| 终止条件 | `changed == false` | 同左 | worklist 为空 |
| 收敛保证 | 格上单调函数，必然收敛 | 同左 | 同左 |
| 最坏迭代轮数 | O(N) | O(N) | O(N) |
| 实际迭代轮数 | 通常 2-3 轮 | 通常 2-3 轮 | 通常 1-2 轮 |
| 实现复杂度 | 简单 | 简单 | 中等 |

当前实现选择了最简单的 round-robin 方案，对于当前编译器面对的函数规模（基本块数通常 < 100）已经足够高效。

---

## 一句话总结

当前活跃性分析是**反向数据流固定点迭代**：按基本块逆序反复传播 `liveOut = ⋃liveIn[succ]` 和 `liveIn = use ∪ (out - def)`，直到不动点；正确性依赖迭代收敛，不依赖 DFS/BFS；遍历顺序只影响收敛速度。
