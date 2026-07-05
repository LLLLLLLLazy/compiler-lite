# 优化 Pass 清单与对应情况

> 基于 `compiler-lite` 远程最新版（commit `b1f3409`），共 37 个 Pass

---

## 测试覆盖基准

唯一可引用的量化证据来自 `测试结果说明.md`：

| 平台 | 测试集 | -O0 | -O1 |
|------|--------|-----|-----|
| COJ | 2023 | 100/100 | 100/100 |
| COJ | 2025 | 140/140 | 140/140 |

无独立性能基准测试数据，以下 Pass 的效果描述局限于**客观功能说明**和**代码状态**。

---

## 一、模块级 Pass（阶段一）

| # | Pass | 状态 | 说明 |
|---|------|------|------|
| 1 | **InterproceduralConstProp** | 已启用 | 若形参在所有调用点接收同一常量，替换形参为该常量 |
| 2 | **SmallFunctionInline（第1轮）** | 已启用 | 内联指令数 <200 且 alloca <256B 且形参 ≤8 的非递归函数。每次内联一条调用，循环 ≤256 次 |
| 3 | **DeadFunctionElim** | 已启用 | 从 main BFS 标记可达函数，删除不可达用户函数 |
| 4 | **DeadGlobalStoreElim** | 已启用 | 删除模块内"只写不读"且地址不逃逸的全局变量上的 store。放在 DeadFunctionElim 之后、Mem2Reg 之前 |
| 5 | **GlobalToLocal** | 已启用 | 只被 main 使用的标量全局下沉为局部 alloca；其他函数中只读全局建立入口缓存 |

## 二、函数级 Pass（阶段二，一次性）

| # | Pass | 状态 | 说明 |
|---|------|------|------|
| 6 | **PureCallCSE** | 已启用 | 同一基本块内参数相同的纯函数调用去重 |
| 7 | **ArrayScalarize** | 已启用 | 常量下标访问的局部小数组拆为独立标量 |
| 8 | **Mem2Reg** | 已启用 | Cytron IDF 算法，alloca/load/store → SSA φ。全部后续优化的基础 |
| 9 | **GVN** | 已启用 | 沿支配树扫描，相同操作数+操作码去重 |
| 10 | **TailRecursionElim** | 已启用 | `return self(args)` → 跳回函数头的循环 |

## 三、晚期模块级 Pass（阶段三）

| # | Pass | 状态 | 说明 |
|---|------|------|------|
| 11 | **PostInlineCleanup**（第2轮内联） | 已启用 | 阶段二瘦身后的函数再次扫描内联，后续补 Mem2Reg+GVN+LICM+InstCombine |
| 12 | **PostInlineGlobalCleanup** | 已启用 | DeadFunctionElim + GlobalToLocal + Mem2Reg+GVN+LICM |

## 四、定点迭代 Pass（阶段四，≤18 轮）

### 子组 1：值优化 + 循环规范化

| # | Pass | 状态 | 说明 |
|---|------|------|------|
| 13 | **LocalMemoryOpt** | 已启用 | 非逃逸 alloca 的 store-to-load 转发、冗余 load 消除、死 store 消除 |
| 14 | **GVN**（第2次） | 已启用 | 定点迭代中复用 |
| 15 | **LICM** | 已启用 | 循环不变量外提到 preheader |
| 16 | **CanonicalizeLoop** | 已启用 | 建立 preheader + 唯一 latch + dedicated exits |
| 17 | **DeadInstElim** | 已启用 | 沿 def-use 链标记存活指令，删除未标记的死代码 |

### 子组 2：循环变换

| # | Pass | 状态 | 说明 |
|---|------|------|------|
| 18 | **LoopExitValueRewrite** | 已启用 | 基于 SCEV 的循环出口值闭式替换（仿射递推、模加递推、2 的幂除法递推） |
| 19 | **RemoveEmptyLoop** | 已启用 | 删除无副作用且出口无依赖的自然循环 |
| 20 | **LoopTiling** | 已启用 | 二维循环 32×32 分块 |
| 21 | **LoopStrengthReduce** | 已启用 | `gep(base, i_phi)` → 指针 phi + `ptr+=stride` |
| 22 | **MatMulInterchange** | 已启用 | j-k 循环交换，使最内层步长为 1 |

### 子组 3：向量化 / 展开

| # | Pass | 状态 | 说明 |
|---|------|------|------|
| 23 | **LoopVectorize** | 按开关启用 | RVV strip-mining 向量化，当 `--riscv64-rvv=on` 时加入 |
| 24 | **SimpleLoopUnroll** | 已启用 | 迭代次数 ≤16 的编译期可知循环完全展开 |
| 25 | **IndVarSimplify** | 已启用 | 整数计数器退出条件 → 指针比较退出条件 |
| 26 | **ConstProp** | 已启用 | SCCP 稀疏条件常量传播 |

### 子组 4：晚期值优化

| # | Pass | 状态 | 说明 |
|---|------|------|------|
| 27 | **PureCallLoopCache** | 已启用 | 循环内实参不变的纯调用，跨迭代复用缓存值 |
| 28 | **PhiToSelect** | 已启用 | 简单合流处 φ → select 指令 |
| 29 | **BoundedBitLoopSolver** | 已启用 | 有界位迭代循环求解：抽象解释 srem/sdiv 逐位循环，替换为原生指令 |
| 30 | **InstCombine** | 已启用 | `x+0→x`、`x*1→x` 等代数简化 |
| 31 | **UnreachableBlockElim** | 已启用 | 删除从入口不可达的基本块 |
| 32 | **CFGSimplify** | 已启用 | 合并空块、折叠冗余跳转、线程化空条件块 |

## 五、后置 Pass（阶段五）

| # | Pass | 状态 | 说明 |
|---|------|------|------|
| 33 | **LateInline**（第3轮内联） | 已启用 | 循环优化瘦身后的函数再次内联 |
| 34 | **PostFixedPointLoopCleanup** | 已启用 | 清理优化引入的合成 CFG 中转块 |
| 35 | **LateLoopOpt** | 已启用 | CanonicalizeLoop + LoopConstantPromotion + LoopRotate |

## 六、降级 Pass

| # | Pass | 状态 | 说明 |
|---|------|------|------|
| 36 | **PreLoweringPhiToSelect** | 已启用 | φ 降级前再做一次 PhiToSelect |
| 37 | **PhiLowering** | 已启用 | SSA φ → CopyInst，交付后端 |

---

## 情况总结

### 有明确证据的

- **正确性**：COJ 2025 140/140 测试全部通过，含 -O1 优化，说明所有 Pass 的变换保持了语义正确性

### 无独立量化数据的

- 各 Pass 的实际贡献（指令数减少比例、运行时间改善）无独立基准测试
- 三轮内联的各自命中次数无统计输出
- 定点迭代的收敛轮次无统计输出
- LICM 外提的指令数、LSR 替换的乘法数、Peephole 消除的指令数等无统计
