# MiniC 编译器

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

### 5.2 已通过测试（2023 功能测试集）

当前已通过 **46 / 100** 个测试用例.

详细用例列表见 [已通过测试.md](已通过测试.md)。

---

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
