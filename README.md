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

### 5.2 已通过测试

###### 2023 已通过测试

测试命令：

```bash
MINIC_FRONTEND=antlr MINIC_TEST_MODE=llvmir bash ./tools/run-local-tests.sh 2023
```

统计结果：`60 / 100` 通过。

说明：以下列表与 [tests/2023_function/llvmir_passed.txt](/workspace/tests/2023_function/llvmir_passed.txt) 同步。

| 序号 | 用例 |
| --- | --- |
| 1 | `2023_func_00_main` |
| 2 | `2023_func_01_var_defn2` |
| 3 | `2023_func_02_var_defn3` |
| 4 | `2023_func_03_arr_defn2` |
| 5 | `2023_func_04_arr_defn3` |
| 6 | `2023_func_05_arr_defn4` |
| 7 | `2023_func_06_const_var_defn2` |
| 8 | `2023_func_07_const_var_defn3` |
| 9 | `2023_func_10_var_defn_func` |
| 10 | `2023_func_11_add2` |
| 11 | `2023_func_12_addc` |
| 12 | `2023_func_13_sub2` |
| 13 | `2023_func_14_subc` |
| 14 | `2023_func_15_mul` |
| 15 | `2023_func_16_mulc` |
| 16 | `2023_func_17_div` |
| 17 | `2023_func_18_divc` |
| 18 | `2023_func_19_mod` |
| 19 | `2023_func_20_rem` |
| 20 | `2023_func_21_if_test2` |
| 21 | `2023_func_22_if_test3` |
| 22 | `2023_func_23_if_test4` |
| 23 | `2023_func_24_if_test5` |
| 24 | `2023_func_25_while_if` |
| 25 | `2023_func_26_while_test1` |
| 26 | `2023_func_27_while_test2` |
| 27 | `2023_func_29_break` |
| 28 | `2023_func_30_continue` |
| 29 | `2023_func_31_while_if_test1` |
| 30 | `2023_func_32_while_if_test2` |
| 31 | `2023_func_33_while_if_test3` |
| 32 | `2023_func_35_op_priority1` |
| 33 | `2023_func_36_op_priority2` |
| 34 | `2023_func_37_op_priority3` |
| 35 | `2023_func_39_op_priority5` |
| 36 | `2023_func_40_unary_op` |
| 37 | `2023_func_41_unary_op2` |
| 38 | `2023_func_45_comment1` |
| 39 | `2023_func_46_hex_defn` |
| 40 | `2023_func_47_hex_oct_add` |
| 41 | `2023_func_48_assign_complex_expr` |
| 42 | `2023_func_49_if_complex_expr` |
| 43 | `2023_func_52_scope` |
| 44 | `2023_func_63_big_int_mul` |
| 45 | `2023_func_66_exgcd` |
| 46 | `2023_func_67_reverse_output` |
| 47 | `2023_func_71_full_conn` |
| 48 | `2023_func_72_hanoi` |
| 49 | `2023_func_73_int_io` |
| 50 | `2023_func_74_kmp` |
| 51 | `2023_func_77_substr` |
| 52 | `2023_func_79_var_name` |
| 53 | `2023_func_81_skip_spaces` |
| 54 | `2023_func_83_long_array` |
| 55 | `2023_func_87_many_params` |
| 56 | `2023_func_88_many_params2` |
| 57 | `2023_func_90_many_locals` |
| 58 | `2023_func_91_many_locals2` |
| 59 | `2023_func_92_register_alloc` |
| 60 | `2023_func_93_nested_calls` |

###### 2023 还未通过测试

统计结果：`40 / 100` 未通过。

说明：以下列表由 `tests/2023_function` 全量用例减去 [tests/2023_function/llvmir_passed.txt](/workspace/tests/2023_function/llvmir_passed.txt) 计算得到。

| 序号 | 用例 |
| --- | --- |
| 1 | `2023_func_08_const_array_defn` |
| 2 | `2023_func_09_func_defn` |
| 3 | `2023_func_28_while_test3` |
| 4 | `2023_func_34_arr_expr_len` |
| 5 | `2023_func_38_op_priority4` |
| 6 | `2023_func_42_empty_stmt` |
| 7 | `2023_func_43_logi_assign` |
| 8 | `2023_func_44_stmt_expr` |
| 9 | `2023_func_50_short_circuit` |
| 10 | `2023_func_51_short_circuit3` |
| 11 | `2023_func_53_scope2` |
| 12 | `2023_func_54_hidden_var` |
| 13 | `2023_func_55_sort_test1` |
| 14 | `2023_func_56_sort_test2` |
| 15 | `2023_func_57_sort_test3` |
| 16 | `2023_func_58_sort_test4` |
| 17 | `2023_func_59_sort_test5` |
| 18 | `2023_func_60_sort_test6` |
| 19 | `2023_func_61_sort_test7` |
| 20 | `2023_func_62_percolation` |
| 21 | `2023_func_64_calculator` |
| 22 | `2023_func_65_color` |
| 23 | `2023_func_68_brainfk` |
| 24 | `2023_func_69_expr_eval` |
| 25 | `2023_func_70_dijkstra` |
| 26 | `2023_func_75_max_flow` |
| 27 | `2023_func_76_n_queens` |
| 28 | `2023_func_78_side_effect` |
| 29 | `2023_func_80_chaos_token` |
| 30 | `2023_func_82_long_func` |
| 31 | `2023_func_84_long_array2` |
| 32 | `2023_func_85_long_code` |
| 33 | `2023_func_86_long_code2` |
| 34 | `2023_func_89_many_globals` |
| 35 | `2023_func_94_nested_loops` |
| 36 | `2023_func_95_float` |
| 37 | `2023_func_96_matrix_add` |
| 38 | `2023_func_97_matrix_sub` |
| 39 | `2023_func_98_matrix_mul` |
| 40 | `2023_func_99_matrix_tran` |

###### 2023 AST 已通过测试

测试命令：

```bash
MINIC_FRONTEND=antlr MINIC_TEST_MODE=ast bash ./tools/run-local-tests.sh 2023
```

统计结果：`46 / 100` 通过。

说明：以下列表与 [tests/2023_function/ast_passed.txt](/workspace/tests/2023_function/ast_passed.txt) 同步。

| 序号 | 用例 |
| --- | --- |
| 1 | `2023_func_00_main` |
| 2 | `2023_func_01_var_defn2` |
| 3 | `2023_func_02_var_defn3` |
| 4 | `2023_func_09_func_defn` |
| 5 | `2023_func_10_var_defn_func` |
| 6 | `2023_func_11_add2` |
| 7 | `2023_func_14_subc` |
| 8 | `2023_func_15_mul` |
| 9 | `2023_func_17_div` |
| 10 | `2023_func_19_mod` |
| 11 | `2023_func_20_rem` |
| 12 | `2023_func_21_if_test2` |
| 13 | `2023_func_22_if_test3` |
| 14 | `2023_func_23_if_test4` |
| 15 | `2023_func_24_if_test5` |
| 16 | `2023_func_25_while_if` |
| 17 | `2023_func_26_while_test1` |
| 18 | `2023_func_27_while_test2` |
| 19 | `2023_func_28_while_test3` |
| 20 | `2023_func_29_break` |
| 21 | `2023_func_30_continue` |
| 22 | `2023_func_31_while_if_test1` |
| 23 | `2023_func_32_while_if_test2` |
| 24 | `2023_func_33_while_if_test3` |
| 25 | `2023_func_35_op_priority1` |
| 26 | `2023_func_36_op_priority2` |
| 27 | `2023_func_37_op_priority3` |
| 28 | `2023_func_38_op_priority4` |
| 29 | `2023_func_39_op_priority5` |
| 30 | `2023_func_40_unary_op` |
| 31 | `2023_func_41_unary_op2` |
| 32 | `2023_func_43_logi_assign` |
| 33 | `2023_func_45_comment1` |
| 34 | `2023_func_46_hex_defn` |
| 35 | `2023_func_47_hex_oct_add` |
| 36 | `2023_func_48_assign_complex_expr` |
| 37 | `2023_func_49_if_complex_expr` |
| 38 | `2023_func_50_short_circuit` |
| 39 | `2023_func_52_scope` |
| 40 | `2023_func_53_scope2` |
| 41 | `2023_func_67_reverse_output` |
| 42 | `2023_func_72_hanoi` |
| 43 | `2023_func_78_side_effect` |
| 44 | `2023_func_89_many_globals` |
| 45 | `2023_func_91_many_locals2` |
| 46 | `2023_func_92_register_alloc` |

###### 2023 AST 还未通过测试

统计结果：`54 / 100` 未通过。

说明：以下列表由 `tests/2023_function` 全量用例减去 [tests/2023_function/ast_passed.txt](/workspace/tests/2023_function/ast_passed.txt) 计算得到。

| 序号 | 用例 |
| --- | --- |
| 1 | `2023_func_03_arr_defn2` |
| 2 | `2023_func_04_arr_defn3` |
| 3 | `2023_func_05_arr_defn4` |
| 4 | `2023_func_06_const_var_defn2` |
| 5 | `2023_func_07_const_var_defn3` |
| 6 | `2023_func_08_const_array_defn` |
| 7 | `2023_func_12_addc` |
| 8 | `2023_func_13_sub2` |
| 9 | `2023_func_16_mulc` |
| 10 | `2023_func_18_divc` |
| 11 | `2023_func_34_arr_expr_len` |
| 12 | `2023_func_42_empty_stmt` |
| 13 | `2023_func_44_stmt_expr` |
| 14 | `2023_func_51_short_circuit3` |
| 15 | `2023_func_54_hidden_var` |
| 16 | `2023_func_55_sort_test1` |
| 17 | `2023_func_56_sort_test2` |
| 18 | `2023_func_57_sort_test3` |
| 19 | `2023_func_58_sort_test4` |
| 20 | `2023_func_59_sort_test5` |
| 21 | `2023_func_60_sort_test6` |
| 22 | `2023_func_61_sort_test7` |
| 23 | `2023_func_62_percolation` |
| 24 | `2023_func_63_big_int_mul` |
| 25 | `2023_func_64_calculator` |
| 26 | `2023_func_65_color` |
| 27 | `2023_func_66_exgcd` |
| 28 | `2023_func_68_brainfk` |
| 29 | `2023_func_69_expr_eval` |
| 30 | `2023_func_70_dijkstra` |
| 31 | `2023_func_71_full_conn` |
| 32 | `2023_func_73_int_io` |
| 33 | `2023_func_74_kmp` |
| 34 | `2023_func_75_max_flow` |
| 35 | `2023_func_76_n_queens` |
| 36 | `2023_func_77_substr` |
| 37 | `2023_func_79_var_name` |
| 38 | `2023_func_80_chaos_token` |
| 39 | `2023_func_81_skip_spaces` |
| 40 | `2023_func_82_long_func` |
| 41 | `2023_func_83_long_array` |
| 42 | `2023_func_84_long_array2` |
| 43 | `2023_func_85_long_code` |
| 44 | `2023_func_86_long_code2` |
| 45 | `2023_func_87_many_params` |
| 46 | `2023_func_88_many_params2` |
| 47 | `2023_func_90_many_locals` |
| 48 | `2023_func_93_nested_calls` |
| 49 | `2023_func_94_nested_loops` |
| 50 | `2023_func_95_float` |
| 51 | `2023_func_96_matrix_add` |
| 52 | `2023_func_97_matrix_sub` |
| 53 | `2023_func_98_matrix_mul` |
| 54 | `2023_func_99_matrix_tran` |

###### 2023 RISCV64 已通过测试

测试命令：

```bash
MINIC_FRONTEND=antlr bash ./tools/run-local-riscv64-tests.sh 2023
```

统计结果：`46 / 100` 通过。

说明：以下列表与 [tests/2023_function/asm_passed.txt](/workspace/tests/2023_function/asm_passed.txt) 同步。

| 序号 | 用例 |
| --- | --- |
| 1 | `2023_func_00_main` |
| 2 | `2023_func_01_var_defn2` |
| 3 | `2023_func_02_var_defn3` |
| 4 | `2023_func_09_func_defn` |
| 5 | `2023_func_10_var_defn_func` |
| 6 | `2023_func_11_add2` |
| 7 | `2023_func_14_subc` |
| 8 | `2023_func_15_mul` |
| 9 | `2023_func_17_div` |
| 10 | `2023_func_19_mod` |
| 11 | `2023_func_20_rem` |
| 12 | `2023_func_21_if_test2` |
| 13 | `2023_func_22_if_test3` |
| 14 | `2023_func_23_if_test4` |
| 15 | `2023_func_24_if_test5` |
| 16 | `2023_func_25_while_if` |
| 17 | `2023_func_26_while_test1` |
| 18 | `2023_func_27_while_test2` |
| 19 | `2023_func_28_while_test3` |
| 20 | `2023_func_29_break` |
| 21 | `2023_func_30_continue` |
| 22 | `2023_func_31_while_if_test1` |
| 23 | `2023_func_32_while_if_test2` |
| 24 | `2023_func_33_while_if_test3` |
| 25 | `2023_func_35_op_priority1` |
| 26 | `2023_func_36_op_priority2` |
| 27 | `2023_func_37_op_priority3` |
| 28 | `2023_func_38_op_priority4` |
| 29 | `2023_func_39_op_priority5` |
| 30 | `2023_func_40_unary_op` |
| 31 | `2023_func_41_unary_op2` |
| 32 | `2023_func_43_logi_assign` |
| 33 | `2023_func_45_comment1` |
| 34 | `2023_func_46_hex_defn` |
| 35 | `2023_func_47_hex_oct_add` |
| 36 | `2023_func_48_assign_complex_expr` |
| 37 | `2023_func_49_if_complex_expr` |
| 38 | `2023_func_50_short_circuit` |
| 39 | `2023_func_52_scope` |
| 40 | `2023_func_53_scope2` |
| 41 | `2023_func_67_reverse_output` |
| 42 | `2023_func_72_hanoi` |
| 43 | `2023_func_78_side_effect` |
| 44 | `2023_func_89_many_globals` |
| 45 | `2023_func_91_many_locals2` |
| 46 | `2023_func_92_register_alloc` |

###### 2023 RISCV64 还未通过测试

统计结果：`54 / 100` 未通过。

说明：以下列表由 `tests/2023_function` 全量用例减去 [tests/2023_function/asm_passed.txt](/workspace/tests/2023_function/asm_passed.txt) 计算得到。

| 序号 | 用例 |
| --- | --- |
| 1 | `2023_func_03_arr_defn2` |
| 2 | `2023_func_04_arr_defn3` |
| 3 | `2023_func_05_arr_defn4` |
| 4 | `2023_func_06_const_var_defn2` |
| 5 | `2023_func_07_const_var_defn3` |
| 6 | `2023_func_08_const_array_defn` |
| 7 | `2023_func_12_addc` |
| 8 | `2023_func_13_sub2` |
| 9 | `2023_func_16_mulc` |
| 10 | `2023_func_18_divc` |
| 11 | `2023_func_34_arr_expr_len` |
| 12 | `2023_func_42_empty_stmt` |
| 13 | `2023_func_44_stmt_expr` |
| 14 | `2023_func_51_short_circuit3` |
| 15 | `2023_func_54_hidden_var` |
| 16 | `2023_func_55_sort_test1` |
| 17 | `2023_func_56_sort_test2` |
| 18 | `2023_func_57_sort_test3` |
| 19 | `2023_func_58_sort_test4` |
| 20 | `2023_func_59_sort_test5` |
| 21 | `2023_func_60_sort_test6` |
| 22 | `2023_func_61_sort_test7` |
| 23 | `2023_func_62_percolation` |
| 24 | `2023_func_63_big_int_mul` |
| 25 | `2023_func_64_calculator` |
| 26 | `2023_func_65_color` |
| 27 | `2023_func_66_exgcd` |
| 28 | `2023_func_68_brainfk` |
| 29 | `2023_func_69_expr_eval` |
| 30 | `2023_func_70_dijkstra` |
| 31 | `2023_func_71_full_conn` |
| 32 | `2023_func_73_int_io` |
| 33 | `2023_func_74_kmp` |
| 34 | `2023_func_75_max_flow` |
| 35 | `2023_func_76_n_queens` |
| 36 | `2023_func_77_substr` |
| 37 | `2023_func_79_var_name` |
| 38 | `2023_func_80_chaos_token` |
| 39 | `2023_func_81_skip_spaces` |
| 40 | `2023_func_82_long_func` |
| 41 | `2023_func_83_long_array` |
| 42 | `2023_func_84_long_array2` |
| 43 | `2023_func_85_long_code` |
| 44 | `2023_func_86_long_code2` |
| 45 | `2023_func_87_many_params` |
| 46 | `2023_func_88_many_params2` |
| 47 | `2023_func_90_many_locals` |
| 48 | `2023_func_93_nested_calls` |
| 49 | `2023_func_94_nested_loops` |
| 50 | `2023_func_95_float` |
| 51 | `2023_func_96_matrix_add` |
| 52 | `2023_func_97_matrix_sub` |
| 53 | `2023_func_98_matrix_mul` |
| 54 | `2023_func_99_matrix_tran` |


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
