# MiniC 编译器设计文档

## 1. 系统架构

编译器采用经典的三段式架构：**前端 → 中端优化 → 后端代码生成**，目标平台为 RISC-V64。

```
┌─────────────────────────────────────────────────────────────────┐
│                         SysY 源程序 (.sy)                        │
└─────────────────────────────┬───────────────────────────────────┘
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  前端 (Frontend)                                                 │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐   │
│  │ 词法/语法分析 │→│ CST → AST    │→│ AST → 结构化 IR       │   │
│  │ (ANTLR4)     │  │ (Visitor)    │  │ (IRGenerator)        │   │
│  └──────────────┘  └──────────────┘  └──────────────────────┘   │
└─────────────────────────────┬───────────────────────────────────┘
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  中端 (IR + 优化)                                                │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌───────────────┐  │
│  │ 非 SSA IR │→│ Mem2Reg  │→│ SSA IR   │→│ 多轮优化 Pass  │  │
│  │ (alloca/  │  │ (φ 插入) │  │          │  │ (GVN/LICM/…  │  │
│  │  load/    │  └──────────┘  └──────────┘  │  循环变换等)  │  │
│  │  store)   │                              └───────┬───────┘  │
│  └──────────┘                                      ▼           │
│                                           ┌───────────────┐   │
│                                           │ Phi Lowering  │   │
│                                           │ (降级为普通赋值)│   │
│                                           └───────────────┘   │
└─────────────────────────────┬───────────────────────────────────┘
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  后端 (Backend, RISC-V64)                                        │
│  ┌────────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────┐  │
│  │ 指令选择    │→│ 活跃区间  │→│ 寄存器分配 │→│ 汇编代码生成 │  │
│  │ (InstSel)  │  │ 分析      │  │ (Greedy)  │  │ (CodeGen)    │  │
│  └────────────┘  └──────────┘  └──────────┘  └──────────────┘  │
└─────────────────────────────┬───────────────────────────────────┘
                              ▼
                      RISC-V64 汇编 (.s)
```

## 2. 模块划分

### 2.1 前端模块 (`frontend/`)

| 子模块 | 文件 | 职责 |
|--------|------|------|
| 文法定义 | `antlr4/MiniC.g4` | SysY 语言 ANTLR4 文法（EBNF 描述） |
| CST 遍历 | `antlr4/Antlr4CSTVisitor.cpp` | ANTLR 生成的 CST → 自定义 AST 节点 |
| 前端入口 | `antlr4/Antlr4Executor.cpp` | 词法/语法分析调度 |
| AST 定义 | `AST.h / AST.cpp` | 抽象语法树节点类型定义 |
| IR 生成 | `lowering/IRGenerator.cpp` | AST → 结构化非 SSA IR 的 Lowering |

### 2.2 中端 IR 模块 (`ir/`)

| 子模块 | 文件 | 职责 |
|--------|------|------|
| IR 基础 | `Value.h/cpp`, `User.h/cpp`, `Use.h/cpp` | SSA 值、使用者、Use-Def 链 |
| 基本块 | `BasicBlock.h/cpp` | 基本块管理（指令列表、前驱/后继） |
| 函数 | `Function.h/cpp` | 函数定义、形参、基本块列表 |
| 指令集 | `Instructions/*.h/cpp` | 完整 IR 指令（Binary/ICmp/Phi/Branch/Call/Load/Store/GEP 等） |
| 类型系统 | `Types/*.h/cpp` | Integer/Float/Pointer/Array/Function/Void 类型 |
| IR 输出 | `LLVMIREmitter.h/cpp` | 结构化 IR → LLVM IR 文本（调试用） |

### 2.3 分析模块 (`ir/analysis/`)

| 模块 | 职责 |
|------|------|
| `DominatorTree` | 支配树构建与查询 |
| `DominanceFrontier` | 支配边界计算（SSA φ 节点插入依据） |
| `LoopInfo` | 自然循环识别（回边检测 + 循环体收集） |
| `SCEV` | 标量演化分析（循环归纳变量识别） |
| `MemoryAccess` | 指针/内存依赖分析 |
| `PureFunctionAnalysis` | 纯函数判定（无副作用、确定性） |
| `FunctionSideEffectAnalysis` | 函数副作用分析 |
| `AnalysisCache` | 分析结果缓存（避免重复计算） |

### 2.4 优化 Pass 模块 (`ir/passes/`)

详见第 3 节优化策略。

### 2.5 后端模块 (`backend/riscv64/`)

| 子模块 | 文件 | 职责 |
|--------|------|------|
| 指令选择 | `InstSelectorRiscV64` | IR 指令 → RISC-V64 伪指令（ILoc）映射 |
| 活跃区间 | `LiveIntervalAnalysis` | 虚拟寄存器活跃区间计算 |
| 干涉图 | `InterferenceGraph` | 寄存器干涉图构建 |
| 寄存器分配 | `GreedyRegAllocator` | 贪心线性扫描寄存器分配 |
| 区间分裂 | `LiveIntervalSplitter` | 活跃区间分裂（减少溢出） |
| 寄存器合并 | `RegCoalescer` | 消除冗余 copy 指令 |
| 重物化 | `Rematerialization` | 将溢出恢复替换为立即数重算 |
| 溢出策略 | `SpillStrategy`, `SpillManager` | 寄存器溢出决策与栈槽管理 |
| 局部临时 | `LocalTempManager` | 函数体内临时变量栈空间管理 |
| 收缩包装 | `ShrinkWrapping` | 延迟/提前 callee-saved 寄存器保存恢复 |
| 窥孔优化 | `RiscV64Peephole` | RISC-V64 汇编级窥孔优化 |
| 条件叶分析 | `ConditionalLeafAnalysis` | 条件分支块叶子属性分析 |
| 代码生成 | `CodeGeneratorRiscV64` | 汇编输出主流程 |

### 2.6 符号表模块 (`symboltable/`)

| 模块 | 职责 |
|------|------|
| `Module` | 编译单元顶层容器（全局变量、函数列表、常量池） |
| `ScopeStack` | 嵌套作用域栈（支持块级作用域） |

### 2.7 工具模块 (`utils/`)

| 模块 | 职责 |
|------|------|
| `BitMap` | 位图（用于数据流分析的位向量） |
| `Set` | 集合数据结构 |
| `StorageSet` | 基于哈希的类型/值唯一化存储 |
| `Common` | 日志、调试输出等公共工具 |

## 3. 优化策略

编译器采用**多级、多轮固定点迭代**的优化策略，遵循"变换 → 清理 → 再变换"的节奏，确保每次优化都工作在干净的 IR 上。

### 3.1 优化流水线总览

优化流水线按顺序分为四个阶段：

```
┌──────────────────────────────────────────────────────────────────┐
│ 阶段一：模块级过程间优化 + SSA 构造                                │
├──────────────────────────────────────────────────────────────────┤
│ InterproceduralConstProp → SmallFunctionInline → DeadFunctionElim │
│ → DeadGlobalStoreElim → GlobalToLocal → PureCallCSE               │
│ → ArrayScalarize → Mem2Reg → GVN → PureCallMemoize                │
│ → TailRecursionElim                                               │
├──────────────────────────────────────────────────────────────────┤
│ 阶段二：循环优化（定点迭代，多轮）                                   │
├──────────────────────────────────────────────────────────────────┤
│ { LocalMemoryOpt → GVN → LICM → CanonicalizeLoop → DeadInstElim   │
│   → LoopFusion → RangeModSimplify → LoopExitValueRewrite          │
│   → DeadInstElim → RemoveEmptyLoop → ... } ×18 轮                 │
│   → LoopTiling → LoopStrengthReduce → LoopVectorize               │
│   → SimpleLoopUnroll → IndVarSimplify → ConstProp → PureCallCSE   │
│   → LICM → PureCallLoopCache → PhiToSelect                       │
│   → BoundedBitLoopSolver → InstCombine → UnreachableBlockElim     │
│   → DeadInstElim → CFGSimplify                                   │
├──────────────────────────────────────────────────────────────────┤
│ 阶段三：晚期模块级清理                                              │
├──────────────────────────────────────────────────────────────────┤
│ PostFixedPointLoopCleanup → PostInlineCleanup                     │
│ → PostInlineGlobalCleanup → LateLoopOpt → PostLateLoopCFGCleanup  │
├──────────────────────────────────────────────────────────────────┤
│ 阶段四：Phi 降级 + 后端代码生成                                     │
├──────────────────────────────────────────────────────────────────┤
│ PreLoweringPhiToSelect → PhiLowering → 指令选择 → 寄存器分配       │
│ → 窥孔优化 → 汇编输出                                              │
└──────────────────────────────────────────────────────────────────┘
```

### 3.2 优化 Pass 分类详解

#### 3.2.1 过程间优化

| Pass | 策略 |
|------|------|
| `InterproceduralConstProp` | 跨函数常量传播，将编译期已知的实参/返回值替换为常量 |
| `SmallFunctionInline` | 小函数内联展开，消除调用开销，扩大后续优化视野 |
| `DeadFunctionElim` | 消除无调用者的死函数，释放其对全局变量的引用 |
| `DeadGlobalStoreElim` | 消除对只写不读且地址不逃逸的全局变量的死 store |
| `GlobalToLocal` | 将仅被 main 使用的标量全局变量下沉为 main 的局部变量 |

#### 3.2.2 SSA 构造与基础优化

| Pass | 策略 |
|------|------|
| `Mem2Reg` | 经典 SSA 构造：将 alloca/load/store 提升为 φ 函数，消除内存访问 |
| `GVN` | 全局值编号：识别语义等价的表达式并消除冗余计算 |
| `ConstProp` | 常量传播与折叠：将编译期可确定的表达式替换为常量 |
| `DeadInstElim` | 死指令消除：移除无使用者的指令 |
| `InstCombine` | 指令合并：代数化简（如 `x+0→x`、`x*2→x<<1`） |
| `CFGSimplify` | CFG 简化：合并空基本块、消除不可达代码 |
| `UnreachableBlockElim` | 消除不可达基本块 |

#### 3.2.3 纯函数与递归优化

| Pass | 策略 |
|------|------|
| `PureCallCSE` | 纯函数调用的公共子表达式消除（同一基本块内 + 支配关系） |
| `PureCallMemoize` | **递归纯函数记忆化**：对纯递归函数插入全局缓存表，将 O(2^N) 降至 O(N×W)。自动推断参数界（通过 GEP 访问的数组维度），支持超界自动降级 |
| `PureCallLoopCache` | 循环内纯函数调用缓存：对实参循环不变的纯调用，用 φ 节点跨迭代复用结果 |
| `TailRecursionElim` | 尾递归消除：将尾递归改写为参数 φ 循环，消除调用开销 |

#### 3.2.4 循环分析与规范化

| Pass | 策略 |
|------|------|
| `CanonicalizeLoop` | 循环规范化：保证循环具有单一前驱头块和 latch 块 |
| `LoopRotate` | 循环旋转：将条件判断移到循环末尾，减少分支指令 |
| `LoopInfo` (分析) | 自然循环识别：基于回边的循环检测与循环体收集 |
| `SCEV` (分析) | 标量演化分析：识别循环归纳变量及其递推关系 |

#### 3.2.5 循环变换与优化

| Pass | 策略 |
|------|------|
| `LICM` | 循环不变量外提：将循环内结果不变的指令移至循环前 |
| `LoopStrengthReduce` | 循环强度削弱：用加法替代归纳变量相关的乘法 |
| `IndVarSimplify` | 归纳变量简化：消除冗余的归纳变量，优化循环边界检查 |
| `SimpleLoopUnroll` | 循环展开：完全展开小循环或部分展开以增加指令级并行 |
| `LoopTiling` | 循环分块：将嵌套循环分解为 tile，改善 cache 局部性 |
| `LoopFusion` | 循环融合：合并相邻且迭代空间相同的循环 |
| `LoopVectorize` | 循环向量化：将循环体转换为 RISC-V V 扩展向量指令 |
| `LoopParallelize` | 循环并行化：将可并行的循环转换为多线程任务 |
| `RemoveEmptyLoop` | 消除空循环与死循环 |
| `MatMulInterchange` | 矩阵乘法循环交换：优化循环嵌套顺序改善访存模式 |
| `LoopExitValueRewrite` | 循环出口值改写：用闭合公式替换循环累加的计算 |
| `BoundedBitLoopSolver` | 有限位宽循环求解器：对位宽有限归纳变量进行编译期求值 |
| `RangeModSimplify` | 范围取模简化：对循环归纳变量的取模运算进行范围分析简化 |
| `LocalMemoryOpt` | 局部内存优化：消除对已持有相同值的内存位置的冗余写回 |
| `LoopConstantPromotion` | 循环常量提升 |

#### 3.2.6 后端优化

| 优化 | 策略 |
|------|------|
| 指令选择 | 树模式匹配将 IR 指令映射为最优 RISC-V64 指令序列 |
| 寄存器分配 | 贪心线性扫描 + 活跃区间分裂 + 寄存器合并 + 重物化 |
| 溢出优化 | 启发式溢出代价分析，优先溢出活跃区间长的变量 |
| 收缩包装 | 将 callee-saved 寄存器保存/恢复推迟到实际使用点 |
| 窥孔优化 | RISC-V64 汇编级指令模式匹配（如 `addi; ld` → `ld` 带偏移） |
| 条件叶分析 | 将不调用函数的分支块尽早返回减少栈帧维护 |


### 3.3 关键优化策略说明

#### 3.3.1 定点迭代框架

循环相关优化（LICM、GVN、LoopFusion 等）被组织在定点迭代循环中（默认 18 轮），每轮内进行"变换 → 死指令消除 → 再变换"的循环。这种设计使得先前优化的效果能被后续轮次利用——例如一轮 LICM 外提代码后，下一轮 GVN 能发现新的优化机会。

#### 3.3.2 纯函数缓存双层优化

编译器对纯函数调用实施两层缓存优化：
- **函数级（PureCallMemoize）**：对纯递归函数（如背包问题的 knapsack_naive），插入全局记忆化缓存表，将指数级时间复杂度降至多项式级。通过分析 GEP 访问的全局数组维度自动推断参数上界，超界自动降级保证正确性。
- **循环级（PureCallLoopCache）**：对循环内实参不变的纯函数调用，在循环头插入 cache/valid φ 节点，跨迭代复用计算结果。

#### 3.3.3 循环变换组合

循环优化采用"分析-变换-规范化"的组合策略：
1. **SCEV** 分析归纳变量演化规律
2. **CanonicalizeLoop** 确保循环结构规范
3. 应用变换（LICM → LoopFusion → LoopTiling → LoopStrengthReduce）
4. **DeadInstElim + CFGSimplify** 清理死代码和冗余控制流
5. 重复以上过程直到收敛

#### 3.3.4 后端寄存器分配

采用基于活跃区间的贪心线性扫描算法，辅以三项增强技术：
- **活跃区间分裂**（LiveIntervalSplitter）：将长活跃区间切分，降低干涉度
- **寄存器合并**（RegCoalescer）：消除不必要的 copy 指令
- **重物化**（Rematerialization）：对于可用少量指令重新计算的值（如常量加载），不溢出到栈而直接重算

---

## 4. 构建方法

依赖：`cmake`、`clang++`（或 `g++`）

```bash
# 配置并构建
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel

# 验证
ls -l ./build/compiler
./build/compiler --help
```
