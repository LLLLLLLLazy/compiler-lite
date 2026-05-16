## MiniC 编译器

一个支持 MiniC 语言子集的完整编译器，覆盖从词法/语法分析到 RISC-V64 汇编代码生成的完整编译流水线。

---

## 1. 已实现功能概览

### 1.1 前端（Frontend）

前端使用 **ANTLR4** 解析器，基于 Adaptive LL(\*) 技术构建。

#### 支持的语言特性

| 特性 | 说明 |
|------|------|
| 全局变量 | `int` 类型，支持初始化赋值 |
| 局部变量 | `int` 类型，可在语句块任意位置声明 |
| 多函数定义 | 支持多个函数，返回值为 `int`，暂不支持形参 |
| 算术运算 | `+` `-` `*` `/` `%` |
| 一元运算符 | `+` `-` `!` |
| 比较运算符 | `==` `!=` `<` `>` `<=` `>=` |
| 逻辑运算符 | `&&` `\|\|` |
| 控制流 | `if` / `if-else` / `while` / `break` / `continue` |
| 赋值语句 | 不支持连续赋值 |
| 语句块 | 支持嵌套语句块和变量分层作用域 |
| 整数字面量 | 支持十进制、八进制（`0` 前缀）、十六进制（`0x` 前缀） |
| 注释 | 单行注释 `//` 和块注释 `/* */` |
| 内置函数 | `putint` 等 |

#### 文法（ANTLR4 EBNF）

```antlr
compileUnit: (funcDef | varDecl)* EOF;
funcDef:     T_INT T_ID T_L_PAREN T_R_PAREN block;
block:       T_L_BRACE blockItemList? T_R_BRACE;
blockItem:   statement | varDecl;
varDecl:     basicType varDef (T_COMMA varDef)* T_SEMICOLON;
varDef:      T_ID (T_ASSIGN expr)?;

statement:
    T_RETURN expr T_SEMICOLON                                    # returnStatement
  | lVal T_ASSIGN expr T_SEMICOLON                              # assignStatement
  | T_IF T_L_PAREN expr T_R_PAREN statement (T_ELSE statement)? # ifStatement
  | T_WHILE T_L_PAREN expr T_R_PAREN statement                  # whileStatement
  | T_BREAK T_SEMICOLON                                          # breakStatement
  | T_CONTINUE T_SEMICOLON                                       # continueStatement
  | block                                                        # blockStatement
  | expr? T_SEMICOLON                                            # expressionStatement;

expr:    lOrExp;
lOrExp:  lAndExp (T_LOR lAndExp)*;
lAndExp: eqExp (T_LAND eqExp)*;
eqExp:   relExp (eqOp relExp)*;
relExp:  addExp (relOp addExp)*;
addExp:  mulExp (addOp mulExp)*;
mulExp:  unaryExp (mulOp unaryExp)*;
unaryExp: primaryExp | T_ID T_L_PAREN realParamList? T_R_PAREN | unaryOp unaryExp;
primaryExp: T_L_PAREN expr T_R_PAREN | T_DIGIT | lVal;
lVal:    T_ID;
```

---

### 1.2 中间表示（IR）

编译器构建了一套**结构化块状 IR**，并实现了以下能力：

| 模块 | 说明 |
|------|------|
| 结构化 IR 构建 | 以基本块（BasicBlock）为单位组织指令 |
| AST Lowering | 将 AST 直接降低为标准 LLVM 风格的非 SSA IR |
| LLVM IR 打印 | `LLVMIREmitter` 将结构化 IR 输出为 LLVM IR 文本 |
| 支配树分析 | `DominatorTree`：计算各基本块的支配关系 |
| 支配边界分析 | `DominanceFrontier`：为 SSA 构造提供 φ 插入点 |
| Mem2Reg | 将 `alloca`/`load`/`store` 提升为 SSA φ 函数，完成 SSA 构造 |
| Phi Lowering | 将 SSA φ 函数降低为普通赋值，为后端做准备 |

---

### 1.3 后端（Backend）

目前实现了 **RISC-V64** 目标平台的完整代码生成流水线：

| 模块 | 说明 |
|------|------|
| 指令选择 | `InstSelectorRiscV64`：将 IR 指令映射到 RISC-V64 指令集 |
| 寄存器分配核心数据结构 | 活跃变量、干涉图等基础设施 |
| 活跃区间分析 | `LiveIntervalAnalysis`：计算每个虚拟寄存器的活跃区间 |
| 贪心寄存器分配 | `GreedyRegAllocator`：基于活跃区间的线性扫描贪心分配 |
| 溢出策略 | `HeuristicSpillStrategy`：启发式寄存器溢出决策 |

---

## 2. 项目结构

```
compiler-lite/
├── frontend/
│   ├── antlr4/
│   │   ├── MiniC.g4              # ANTLR4 文法定义
│   │   ├── Antlr4CSTVisitor.cpp  # CST → AST 访问者
│   │   └── Antlr4Executor.cpp    # ANTLR4 前端入口
│   ├── AST.h / AST.cpp           # AST 节点定义
│   └── lowering/                 # AST → IR 降低
├── ir/
│   ├── BasicBlock.h/cpp          # 基本块
│   ├── Function.h/cpp            # 函数
│   ├── Instruction.h/cpp         # IR 指令
│   ├── LLVMIREmitter.h/cpp       # LLVM IR 文本输出
│   ├── analysis/
│   │   ├── DominatorTree         # 支配树
│   │   └── DominanceFrontier     # 支配边界
│   └── passes/
│       ├── Mem2Reg               # mem2reg 优化趟
│       └── PhiLowering           # Phi 降低
├── backend/
│   └── riscv64/
│       ├── InstSelectorRiscV64   # 指令选择
│       ├── GreedyRegAllocator    # 贪心寄存器分配
│       ├── LiveIntervalAnalysis  # 活跃区间分析
│       ├── HeuristicSpillStrategy# 溢出策略
│       └── ILocRiscV64           # RISC-V64 伪指令
├── symboltable/                  # 符号表 & 作用域栈
├── utils/                        # 公共工具
├── tests/                        # 测试用例
└── tools/                        # 测试脚本
```

---

## 3. 构建方法

依赖：`cmake`、`ninja`、`clang++`（或 `g++`）

```bash
# 清理旧缓存（如果需要）
rm -f build/CMakeCache.txt
rm -rf build/CMakeFiles

# 配置并构建
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++
cmake --build build --parallel

# 验证构建结果
ls -l ./build/minic
./build/minic --help
```

---

## 4. 使用方法

```bash
# 输出 LLVM IR 文本
./build/minic -S -I -o output.ll input.c

# 输出 RISC-V64 汇编（默认目标架构）
./build/minic -S -O1 -t RISCV64 -o output.rv64.s input.c

# 输出 AST 可视化图片
./build/minic -S -T -o output.png input.c
```

---

## 5. 测试

### 5.1 本地测试脚本

```bash
# 运行 2023 功能测试集（ANTLR 前端 + RISC-V64 汇编验证）
bash ./tools/run-local-riscv64-tests.sh 2023

# 运行单个测试用例
bash ./tools/run-local-riscv64-tests.sh 2023_func_00_main

# LLVM IR 模式验证（通过 IRCompiler 解释执行）
MINIC_TEST_MODE=llvmir bash ./tools/run-local-tests.sh 2023

# ASM 模式验证（ARM 交叉编译 + qemu）
MINIC_TEST_MODE=asm bash ./tools/run-local-tests.sh 2023
```

### 5.2 测试脚本参数详解

#### 5.2.1 `run-local-tests.sh` — 本地综合测试运行器

支持 LLVM IR、RISCV64 汇编和 AST 三种验证模式。

**用法：**

```bash
bash ./tools/run-local-tests.sh [suite] [testcase]
```

**位置参数：**

| 参数 | 说明 |
|------|------|
| （无参数） | 运行所有套件 |
| `<suite>` | 指定测试套件 |
| `<testcase>` | 指定单个测试用例（自动推断套件） |
| `<suite> <testcase>` | 指定套件和测试用例 |

**套件选项：** `2023` | `2025` | `2025_perf` | `2025_performance` | `2026` | `2026_perf` | `2026_performance` | `all`

**环境变量：**

| 环境变量 | 默认值 | 可选项 | 说明 |
|----------|--------|--------|------|
| `MINIC_BIN` | `./build/minic` | — | minic 编译器路径 |
| `MINIC_FRONTEND` | `antlr` | `antlr` \| `recursive` \| `default` | 前端选择 |
| `MINIC_RUNTIME_SOURCE` | `./tests/sylib.c` | — | 运行时源文件路径 |
| `MINIC_TEST_MODE` | `llvmir` | `llvmir` \| `asm` \| `ast` \| `all` | 测试模式 |
| `MINIC_TEST_TIMEOUT` | `30` | — | 每步超时秒数 |
| `MINIC_ASM_TARGET` | `RISCV64` | `RISCV64` | 汇编后端目标 |
| `RISCV64_GCC_BIN` | `riscv64-linux-gnu-gcc` | — | RISC-V64 交叉编译器 |
| `QEMU_RISCV64_BIN` | 自动检测 | — | QEMU RISC-V64 模拟器 |
| `CLANG_BIN` | `clang` | — | Clang 可执行文件 |

**示例：**

```bash
# LLVM IR 模式运行 2023 套件
MINIC_TEST_MODE=llvmir bash ./tools/run-local-tests.sh 2023

# 汇编模式运行所有套件
MINIC_TEST_MODE=asm bash ./tools/run-local-tests.sh all

# 指定前端和超时
MINIC_FRONTEND=recursive MINIC_TEST_TIMEOUT=60 bash ./tools/run-local-tests.sh 2025
```

---

#### 5.2.2 `run-local-riscv64-tests.sh` — RISC-V64 专项测试运行器

支持汇编运行验证和仅汇编两种模式，并对比 minic-ir-llvm 和 clang 直接编译两条基准线。

**用法：**

```bash
bash ./tools/run-local-riscv64-tests.sh [suite] [testcase]
```

**位置参数：**

| 参数 | 说明 |
|------|------|
| （无参数） | 运行所有套件 |
| `<suite>` | 指定测试套件 |
| `<testcase>` | 指定单个测试用例（自动推断套件） |
| `<suite> <testcase>` | 指定套件和测试用例 |

**套件选项：** `2023` | `2025` | `2025_perf` | `2025_performance` | `2026` | `2026_perf` | `2026_performance` | `all`

**环境变量：**

| 环境变量 | 默认值 | 可选项 | 说明 |
|----------|--------|--------|------|
| `MINIC_BIN` | `./build/minic` | — | minic 编译器路径 |
| `MINIC_FRONTEND` | `antlr` | `antlr` \| `recursive` \| `default` | 前端选择 |
| `MINIC_RISCV64_TEST_MODE` | `asm` | `asm` \| `assemble` | 测试模式（`asm`=编译+链接+运行+对比，`assemble`=仅编译+汇编） |
| `MINIC_RISCV64_TIMEOUT` | `30` | — | 每步超时秒数 |
| `MINIC_TEST_ROOT` | `./tests` | — | 测试根目录 |
| `MINIC_RUNTIME_LIB` | `./tests/libsysy_riscv.a` | — | 运行时库路径 |
| `MINIC_RUNTIME_SOURCE` | `./tests/sylib.c` | — | 运行时源文件路径 |
| `RISCV64_GCC_BIN` | `riscv64-linux-gnu-gcc` | — | RISC-V64 交叉编译器 |
| `QEMU_RISCV64_BIN` | 自动检测 | — | QEMU RISC-V64 模拟器 |
| `CLANG_BIN` | `clang` | — | Clang 可执行文件 |

**示例：**

```bash
# 运行 2023 套件的汇编验证
bash ./tools/run-local-riscv64-tests.sh 2023

# 仅汇编模式（不运行）
MINIC_RISCV64_TEST_MODE=assemble bash ./tools/run-local-riscv64-tests.sh 2025

# 运行单个用例
bash ./tools/run-local-riscv64-tests.sh 2023 2023_func_00_main
```

---

#### 5.2.3 `run-float-regression.sh` — 浮点回归测试运行器

验证浮点相关编译器的正确性。

**用法：**

```bash
bash ./tools/run-float-regression.sh [mode] [testcase]
```

**位置参数：**

| 参数 | 说明 |
|------|------|
| （无参数） | 运行所有浮点回归测试（默认 `all` 模式） |
| `<mode>` | 指定测试模式 |
| `<testcase>` | 指定单个测试用例 |
| `<mode> <testcase>` | 指定模式和测试用例 |

**模式选项：** `ll`（LLVM IR 验证）| `asm`（RISCV64 汇编验证）| `all`（两者都运行）

**环境变量：**

| 环境变量 | 默认值 | 可选项 | 说明 |
|----------|--------|--------|------|
| `MINIC_BIN` | `./build/minic` | — | minic 编译器路径 |
| `MINIC_FLOAT_TEST_DIR` | `./tests/float_regression` | — | 浮点回归测试目录 |
| `MINIC_RUNTIME_SOURCE` | `./tests/sylib.c` | — | 运行时源文件路径 |
| `MINIC_RUNTIME_LIB` | `./tests/libsysy_riscv.a` | — | 运行时库路径 |
| `MINIC_FRONTEND` | `antlr` | `antlr` \| `recursive` \| `default` | 前端选择 |
| `MINIC_FLOAT_TEST_MODE` | `all` | `ll` \| `asm` \| `all` | 测试模式 |
| `MINIC_FLOAT_LL_OPT_LEVEL` | `1` | `0` \| `1` | LLVM IR 优化级别 |
| `MINIC_FLOAT_ASM_OPT_LEVEL` | `1` | `0` \| `1` | 汇编优化级别 |
| `CLANG_BIN` | `clang` | — | Clang 可执行文件 |
| `RISCV64_GCC_BIN` | `riscv64-linux-gnu-gcc` | — | RISC-V64 交叉编译器 |
| `QEMU_RISCV64_BIN` | 自动检测 | — | QEMU RISC-V64 模拟器 |

**示例：**

```bash
# 运行所有浮点回归测试
bash ./tools/run-float-regression.sh

# 仅 LLVM IR 模式
bash ./tools/run-float-regression.sh ll

# 指定优化级别
MINIC_FLOAT_ASM_OPT_LEVEL=0 bash ./tools/run-float-regression.sh asm
```

---

#### 5.2.4 `run-phi-regression.sh` — Phi 节点回归测试运行器

验证 SSA phi 节点相关编译的正确性。

**用法：**

```bash
bash ./tools/run-phi-regression.sh [mode] [testcase]
```

**位置参数：**

| 参数 | 说明 |
|------|------|
| （无参数） | 运行所有 phi 回归测试（默认 `all` 模式） |
| `<mode>` | 指定测试模式 |
| `<testcase>` | 指定单个测试用例 |
| `<mode> <testcase>` | 指定模式和测试用例 |

**模式选项：** `ll`（LLVM IR 验证）| `asm`（RISCV64 汇编验证）| `all`（两者都运行）

**环境变量：**

| 环境变量 | 默认值 | 可选项 | 说明 |
|----------|--------|--------|------|
| `MINIC_BIN` | `./build/minic` | — | minic 编译器路径 |
| `MINIC_PHI_TEST_DIR` | `./tests/phi_regression` | — | Phi 回归测试目录 |
| `MINIC_RUNTIME_SOURCE` | `./tests/sylib.c` | — | 运行时源文件路径 |
| `MINIC_RUNTIME_LIB` | `./tests/libsysy_riscv.a` | — | 运行时库路径 |
| `MINIC_FRONTEND` | `antlr` | `antlr` \| `recursive` \| `default` | 前端选择 |
| `MINIC_PHI_TEST_MODE` | `all` | `ll` \| `asm` \| `all` | 测试模式 |
| `MINIC_PHI_LL_OPT_LEVEL` | `1` | `0` \| `1` | LLVM IR 优化级别 |
| `MINIC_PHI_ASM_OPT_LEVEL` | `1` | `0` \| `1` | 汇编优化级别 |
| `CLANG_BIN` | `clang` | — | Clang 可执行文件 |
| `RISCV64_GCC_BIN` | `riscv64-linux-gnu-gcc` | — | RISC-V64 交叉编译器 |
| `QEMU_RISCV64_BIN` | 自动检测 | — | QEMU RISC-V64 模拟器 |

**示例：**

```bash
# 运行所有 phi 回归测试
bash ./tools/run-phi-regression.sh

# 仅汇编模式
bash ./tools/run-phi-regression.sh asm

# 运行单个用例
bash ./tools/run-phi-regression.sh ll phi_test_01
```

---

#### 5.2.5 `run_ra_eval.py` — 寄存器分配评估矩阵执行器

执行 RISC-V64 寄存器分配评估矩阵，包括正确性验证和性能基准测试。

**用法：**

```bash
python3 tools/run_ra_eval.py [OPTIONS]
```

**命令行参数：**

| 参数 | 类型 | 默认值 | 可选项 | 说明 |
|------|------|--------|--------|------|
| `--mode` | str | `all` | `correctness` \| `benchmark` \| `all` | 运行模式 |
| `--output-dir` | Path | `build/ra-eval/<timestamp>` | — | 原始产物和记录的输出目录 |
| `--suite` | str（可追加） | 内置默认值 | — | 覆盖所选模式的测试套件，可多次指定 |
| `--case` | str（可追加） | `[]` | — | 用例名称或前缀 glob（如 `2026_perf_fft*`），可多次指定 |
| `--config` | str（可追加） | 所有配置 | 见下表 | 限制运行的 RA 配置，可多次指定 |
| `--repeat` | int | `7` | — | 每个用例/配置的基准测量运行次数 |
| `--warmup` | int | `1` | — | 每个用例/配置的预热运行次数 |
| `--timeout` | int | `120` | — | 每步超时秒数 |
| `--skip-microbench` | flag | `False` | — | 跳过诊断套件 `tests/ra_microbench` |
| `--skip-llvm-lanes` | flag | `False` | — | 跳过 LLVM 对照 lane |
| `--minic-bin` | Path | `build/minic` | — | minic 编译器可执行文件路径 |
| `--runtime-lib` | Path | `tests/libsysy_riscv.a` | — | RISC-V64 运行时库路径 |
| `--clang-bin` | str | `clang` | — | Clang 可执行文件名/路径 |
| `--llc-bin` | str | `llc` | — | LLVM `llc` 可执行文件名/路径 |
| `--riscv64-gcc` | str | `riscv64-linux-gnu-gcc` | — | RISC-V64 GCC 交叉编译器 |
| `--objdump-bin` | str | `riscv64-linux-gnu-objdump` | — | RISC-V64 objdump 可执行文件名/路径 |
| `--qemu` | str | 自动检测 | — | QEMU RISC-V64 用户态模拟器路径 |

**`--config` 可选项（RA 寄存器分配配置）：**

| 配置名 | 启用的特性 |
|--------|-----------|
| `none` | 无（全部禁用） |
| `callee_saved_fpr` | 被调用者保存浮点寄存器 |
| `coalesce` | 寄存器合并 |
| `split` | 寄存器分裂 |
| `callee_saved_fpr+coalesce` | 被调用者保存浮点 + 合并 |
| `callee_saved_fpr+split` | 被调用者保存浮点 + 分裂 |
| `coalesce+split` | 合并 + 分裂 |
| `callee_saved_fpr+coalesce+split` | 全部启用 |

**环境变量：**

| 环境变量 | 对应参数 | 默认值 |
|----------|----------|--------|
| `MINIC_RA_EVAL_TIMEOUT` | `--timeout` | `120` |
| `MINIC_BIN` | `--minic-bin` | `build/minic` |
| `MINIC_RUNTIME_LIB` | `--runtime-lib` | `tests/libsysy_riscv.a` |
| `CLANG_BIN` | `--clang-bin` | `clang` |
| `LLC_BIN` | `--llc-bin` | `llc` |
| `RISCV64_GCC_BIN` | `--riscv64-gcc` | `riscv64-linux-gnu-gcc` |
| `RISCV64_OBJDUMP_BIN` | `--objdump-bin` | `riscv64-linux-gnu-objdump` |
| `QEMU_RISCV64_BIN` | `--qemu` | `""`（自动检测） |

**内置测试套件：**

- 正确性套件：`2023_function`、`2025_function`、`2026_function`、`phi_regression`、`float_regression`、`riscv64_regression`、`ra_microbench`
- 性能套件：真实程序 `2025_performance`、`2026_performance`，以及默认独立汇总的诊断套件 `ra_microbench`
- LLVM lane：`llvm_ra_fast`、`llvm_ra_basic`、`llvm_ra_greedy`、`same_ir_clang_o2`、`direct_clang_o2`

**示例：**

```bash
# 运行完整评估矩阵（正确性 + 基准测试）
python3 tools/run_ra_eval.py

# 仅运行正确性验证
python3 tools/run_ra_eval.py --mode correctness

# 仅基准测试，指定配置和重复次数
python3 tools/run_ra_eval.py --mode benchmark --config coalesce --config split --repeat 10

# 指定用例和超时
python3 tools/run_ra_eval.py --case "2026_perf_fft*" --timeout 300

# 只跑自家后端 lane，跳过 LLVM 对照
python3 tools/run_ra_eval.py --skip-llvm-lanes

# 指定工具路径
python3 tools/run_ra_eval.py --minic-bin /path/to/minic --qemu /path/to/qemu-riscv64-static
```

---

#### 5.2.6 `analyze_ra_eval.py` — 寄存器分配评估记录分析器

分析由 `run_ra_eval.py` 生成的原始寄存器分配评估记录，生成 CSV 和 Markdown 报告。

**用法：**

```bash
python3 tools/analyze_ra_eval.py records [OPTIONS]
```

**命令行参数：**

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `records`（位置参数） | Path | （必填） | `run_ra_eval.py` 输出的 `records.jsonl` 文件路径 |
| `--output-dir` | Path | records 文件所在目录 | CSV/Markdown 报告的输出目录 |

**输出文件：**

| 文件 | 说明 |
|------|------|
| `config_summary.csv` | 各 RA 配置的运行时排名汇总 |
| `case_summary.csv` | 各用例/配置的详细指标 |
| `external_baselines.csv` | LLVM / Clang lane 相对 `own:none` 的兼容输出 |
| `llvm_regalloc_summary.csv` | 同一 LLVM 后端内 `fast/basic/greedy` 的 allocator 敏感度 |
| `backend_gap_summary.csv` | 逐 case 的五段归因口径 |
| `microbench_summary.csv` | 专项微基准的诊断信号与归因结果 |
| `strongest_interactions.csv` | 特性间最强交互效应 |
| `worst_regressions.csv` | 最严重性能回退 |
| `summary.md` | Markdown 综合报告 |

**示例：**

```bash
# 分析评估记录（报告输出到 records.jsonl 同目录）
python3 tools/analyze_ra_eval.py build/ra-eval/20260516-120000/records.jsonl

# 指定报告输出目录
python3 tools/analyze_ra_eval.py build/ra-eval/20260516-120000/records.jsonl --output-dir /tmp/reports
```

---

#### 5.2.7 GDB 调试脚本

以下脚本用于交叉编译后通过 QEMU 启动 GDB 调试，均采用相同的位置参数格式。

| 脚本 | 用途 |
|------|------|
| `arm32-build-gdb.sh` | minic 编译 → ARM32 汇编 → 交叉编译 → QEMU GDB |
| `arm32-direct-gdb.sh` | 直接 ARM32 GCC 交叉编译 → QEMU GDB |
| `arm32-build-run.sh` | clang 直接编译 + IRCompiler + minic ARM32 汇编 → QEMU 运行 |
| `arm64-build-gdb.sh` | minic 编译 → ARM64 汇编 → 交叉编译 → QEMU GDB |
| `arm64-direct-gdb.sh` | 直接 ARM64 GCC 交叉编译 → QEMU GDB |
| `riscv64-build-gdb.sh` | minic 编译 → RISC-V64 汇编 → 交叉编译 → QEMU GDB |
| `riscv64-direct-gdb.sh` | 直接 RISC-V64 GCC 交叉编译 → QEMU GDB |

**GDB 调试脚本用法（`*-build-gdb.sh`、`*-direct-gdb.sh`）：**

```bash
bash ./tools/<script> <workspace_dir> <testcase_name>
```

| 参数 | 说明 |
|------|------|
| `<workspace_dir>` | 项目根目录路径 |
| `<testcase_name>` | 测试用例文件名（不含扩展名） |

**`arm32-build-run.sh` 用法：**

```bash
bash ./tools/arm32-build-run.sh [rundir] [casename]
```

| 参数 | 默认值 | 说明 |
|------|--------|------|
| （无参数） | `rundir="."`, `casename="test1-1"` | 使用默认值 |
| `<casename>` | — | 用例名称（仅一个参数时） |
| `<rundir> <casename>` | — | 运行目录和用例名称 |

**示例：**

```bash
# RISC-V64 GDB 调试
bash ./tools/riscv64-build-gdb.sh /path/to/compiler-lite 2023_func_00_main

# ARM32 综合运行
bash ./tools/arm32-build-run.sh ./tests 2023_func_00_main
```

---

#### 5.2.8 `test_ra_eval_common.py` — RA 评估工具单元测试

`ra_eval_common.py` 共享模块的单元自测，使用 `unittest` 框架，无自定义参数。

**用法：**

```bash
python3 tools/test_ra_eval_common.py
```

### 5.3 已通过测试

参考 [测试结果说明](./测试结果说明.md) 文档，包含 CI 和 COJ 上的测试结果统计。

## 6. 手动完整链路示例

### LLVM IR + IRCompiler 解释执行

```bash
./build/minic -S -I -o /tmp/test.ir ./tests/2023_function/2023_func_00_main.c
./tools/IRCompiler/Linux-x86_64/Ubuntu-22.04/IRCompiler -R /tmp/test.ir
printf "\nEXIT=%s\n" $?
```

### RISC-V64 汇编 + qemu 运行

```bash
./build/minic -S -O1 -t RISCV64 -o /tmp/test.rv64.s ./tests/2023_function/2023_func_00_main.c
riscv64-linux-gnu-gcc -static -o /tmp/test.rv64 /tmp/test.rv64.s ./tests/std.c
qemu-riscv64-static /tmp/test.rv64
printf "\nEXIT=%s\n" $?
```

---

## 7. 打包源码

```bash
cd build
cpack --config CPackSourceConfig.cmake
```
