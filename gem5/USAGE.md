# gem5 BOOM v3 测试使用指南

## ✅ 修复完成

BOOM v3 配置已成功修复并可以正常运行！

### 修复的问题

1. ✅ 移除了不兼容的 `numIQEntries` 参数
2. ✅ 正确初始化 LTAGE 分支预测器
3. ✅ 添加 L2XBar 连接 L1 caches
4. ✅ 设置 SEWorkload
5. ✅ 创建中断控制器

## 文件说明

- **boom_v3.py** - BOOM v3 CPU 配置（已修复，可用）
- **test_boom.sh** - 单个测试脚本

## 使用方法

### 基本用法

```bash
# 测试单个源文件
bash gem5/test_boom.sh <source_file> [input_file]
```

### 示例

```bash
# 测试简单函数（无输入）
bash gem5/test_boom.sh tests/2026_function/2026_func_00_main.sy

# 测试性能用例（自动查找 .in 文件）
bash gem5/test_boom.sh tests/2026_performance/2026_perf_01_mm1.sy

# 显式指定输入文件
bash gem5/test_boom.sh tests/2026_performance/2026_perf_01_mm1.sy tests/2026_performance/2026_perf_01_mm1.in
```

### 支持的文件类型

- `.sy` - SysY 源文件
- `.c` - C 源文件

## 测试流程

脚本会自动执行以下步骤：

1. **编译** - 使用 minic 编译为 RISC-V 汇编并链接
2. **QEMU 基准测试** - 快速验证功能正确性
3. **gem5 BOOM v3 模拟** - 详细性能分析

## 输出结果

### 性能统计

- **模拟周期 (ticks)** - gem5 模拟的总时钟周期
- **CPU 周期数** - CPU 实际执行周期
- **IPC** - 每周期指令数（Instructions Per Cycle）

### Cache 统计

- **L1 D-Cache Miss Rate** - L1 数据缓存未命中率
- **L1 I-Cache Miss Rate** - L1 指令缓存未命中率
- **L2 Cache Miss Rate** - L2 缓存未命中率

### 分支预测统计

- **分支预测查询次数** - 总分支预测次数
- **条件分支预测次数** - 条件分支数量
- **预测错误次数** - 预测失败的次数
- **分支预测准确率** - 预测准确率百分比

## 输出文件

- `m5out/stats.txt` - 详细性能统计
- `gem5_boom.log` - 完整运行日志

## BOOM v3 配置

### CPU 参数

- Fetch Width: 8 (8-wide fetch)
- Decode Width: 4 (4-wide decode)
- Issue Width: 4 (4-wide issue)
- Commit Width: 4 (4-wide commit)
- ROB Entries: 128 (128-entry Reorder Buffer)
- Physical Int Registers: 128
- Physical Float Registers: 128
- Load Queue: 32 entries
- Store Queue: 32 entries

### Cache 配置

- L1 I-Cache: 32KB, 8-way
- L1 D-Cache: 32KB, 8-way
- L2 Cache: 256KB, 16-way (可通过 `--l2-size` 参数调整)

### 分支预测器

- LTAGE (TAGE-based predictor)
- 最接近 BOOM v3 的 TAGE-SC-L 预测器

## 性能预期

### 运行时间（相对 QEMU）

- **简单测试** (如 func_00_main): ~1秒 (QEMU: 0.01秒)
- **小规模矩阵** (50x50): ~3-5秒 (QEMU: 0.02秒)
- **中等规模** (100x100): ~10-30秒
- **大规模** (412x412): ~30-120分钟

gem5 比 QEMU 慢 **50-5000倍**，但提供详细的微架构性能分析。

## 优化建议参考

基于 gem5 统计结果：

### 高 D-Cache Miss Rate (>30%)

说明数据访问模式对 cache 不友好，可以考虑：
- 循环分块 (loop tiling)
- 改善数组访问顺序
- 数据对齐优化

### 高 I-Cache Miss Rate (>5%)

说明代码布局有问题，可以考虑：
- 函数重排序
- 循环展开控制
- 代码内联优化

### 低分支预测准确率 (<90%)

说明分支难以预测，可以考虑：
- 减少分支数量
- 使用 conditional move
- 循环展开

### 低 IPC (<1.5)

说明指令级并行度不足，可以考虑：
- 循环展开
- 指令重排序
- 减少数据依赖

## 注意事项

1. **gem5 很慢** - 大规模测试需要耐心等待
2. **不是周期精确** - gem5 是功能级模拟，不是 RTL 仿真
3. **趋势准确** - 虽然绝对值可能有偏差，但优化的相对效果是准确的
4. **先用 QEMU** - 功能测试用 QEMU，性能分析才用 gem5

## 与 QEMU 对比

| 特性 | QEMU | gem5 BOOM v3 |
|------|------|--------------|
| 执行速度 | 快 | 慢 (50-5000x) |
| Cache 模拟 | ❌ | ✅ |
| 流水线模拟 | ❌ | ✅ |
| 乱序执行 | ❌ | ✅ |
| 分支预测 | ❌ | ✅ (LTAGE) |
| IPC 统计 | ❌ | ✅ |
| 适用场景 | 功能测试 | 性能分析、微架构研究 |

## 快速测试示例

```bash
# 1. 简单测试（1秒）
bash gem5/test_boom.sh tests/2026_function/2026_func_00_main.sy

# 2. 查看结果
cat m5out/stats.txt | grep -E "simTicks|numCycles|demandMissRate"

# 3. 查看分支预测
cat m5out/stats.txt | grep branchPred

# 4. 查看完整日志
less gem5_boom.log
```

## 参考资料

- gem5 官方文档: https://www.gem5.org/documentation/
- BOOM 项目: https://github.com/riscv-boom/riscv-boom
- gem5 统计说明: https://www.gem5.org/documentation/general_docs/statistics/

## 问题排查

### 错误：找不到输入文件

确保 `.in` 文件与 `.sy` 文件在同一目录，且名称匹配。

### gem5 运行时间过长

大规模测试确实需要很长时间，建议：
- 先用小规模输入验证
- 或者只测试 AtomicSimpleCPU（快很多，但无性能数据）

### 输出不匹配

某些测试通过 return code 而非 stdout 返回结果，这是正常的。
