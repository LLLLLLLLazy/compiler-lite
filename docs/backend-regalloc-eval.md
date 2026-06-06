# 后端寄存器分配评测框架

这套框架把寄存器分配评测拆成三层：正确性、静态质量、动态性能。自家后端矩阵固定覆盖 8 组配置：`none`、三个单开关、三个双开关、三者全开；LLVM 对照 lane 用来把 allocator、LLVM 后端、额外 IR 优化的影响分开。

## 1. 后端指标出口

`minic` 新增机器可读统计输出：

```bash
./build/minic -S -O1 -t RISCV64 \
  --ra-no-callee-saved-fpr --ra-no-coalesce --ra-no-split \
  --ra-stats-json=/tmp/ra.json \
  -o /tmp/test.s tests/2023_function/2023_func_92_register_alloc.c
```

JSON 按函数记录 allocator 指标与最终汇编静态指标，包括 spill、reload/store 估算、copy 消除、split 数、frame size、callee-saved 使用、机器指令数、直接栈访存数和 move 指令数。

## 2. 执行评测

完整离线评测：

```bash
python3 tools/run_ra_eval.py --mode all
python3 tools/analyze_ra_eval.py build/ra-eval/<timestamp>/records.jsonl
```

快速抽样：

```bash
python3 tools/run_ra_eval.py \
  --mode benchmark \
  --suite 2026_performance \
  --case '2026_perf_fft*' \
  --config none \
  --config split \
  --skip-llvm-lanes
```

- 正确性默认覆盖 `2023/2025/2026 function`、`phi_regression`、`float_regression`、`riscv64_regression`、`ra_microbench`，并在 `-O0/-O1` 下跑全部配置。
- 性能默认覆盖真实程序 `2025_performance`、`2026_performance` 以及独立诊断 suite `ra_microbench`，每例先 warm-up 1 次，再正式采样 7 次。
- LLVM lane 默认尝试执行 `llvm_ra_fast/basic/greedy`、`same_ir_clang_o2`、`direct_clang_o2`；若缺少 `llc` 或 `clang`，记录 `skipped + reason`，不让整次评测失败。
- 所有 lane 统一记录 `.text`、move-like 指令、栈 load/store 代理指标，后 3 项统一由 `objdump -d -M no-aliases` 解析。
- `ra_microbench` 单独汇总，不进入真实程序 overall geomean。

## 3. 输出

runner 产出 `records.jsonl` 与全部原始 artifact；analyzer 产出：

- `config_summary.csv`
- `case_summary.csv`
- `external_baselines.csv`
- `llvm_regalloc_summary.csv`
- `backend_gap_summary.csv`
- `microbench_summary.csv`
- `strongest_interactions.csv`
- `worst_regressions.csv`
- `summary.md`

最终报告分成三块：

- **Own RA ablation**：自家 allocator 开关相对 `none` 的收益。
- **LLVM RA sensitivity**：同一 LLVM 后端里 `fast/basic/greedy` 的 allocator 敏感度。
- **Backend decomposition**：`own_ra_effect`、`llvm_ra_spread`、`backend_gap`、`extra_llvm_opt_gap`、`whole_compiler_gap` 五个归因口径。

专项微基准当前覆盖 `control_low_pressure`、`gpr_pressure_hot`、`fpr_pressure_hot`、`cross_call_float`、`long_live_across_call`、`phi_copy_dense`、`copy_chain_no_pressure`、`loop_carried_copy_no_pressure`、`branch_phi_copy_no_pressure`、`mixed_pressure_hot`。若某个 case 没触发其预期信号，报告会把它标成 benchmark 设计失败，而不是直接拿来解释 allocator。

## 4. 自测

```bash
python3 tools/test_ra_eval_common.py
```

该自测覆盖 JSON schema 校验、缺失字段报错、时间统计稳定性、LLVM lane 矩阵、归因公式、跳过 lane 处理和 objdump 指标解析。
