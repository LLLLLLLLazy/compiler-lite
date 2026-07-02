#!/bin/bash
# gem5 BOOM v3 测试脚本
# 使用方法：
#   bash gem5/test_boom.sh <source_file> [input_file]
#
# 示例：
#   bash gem5/test_boom.sh tests/2026_performance/2026_perf_01_mm1.sy
#   bash gem5/test_boom.sh tests/2026_performance/2026_perf_01_mm1.sy tests/2026_performance/2026_perf_01_mm1.in

set -e

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

GEM5_ROOT=/home/code/gem5
GEM5_BIN=$GEM5_ROOT/build/RISCV/gem5.opt
BOOM_CONFIG=$SCRIPT_DIR/boom_v3.py

# 检查参数
if [ $# -lt 1 ]; then
    echo "用法: $0 <source_file> [input_file]"
    echo
    echo "示例:"
    echo "  $0 tests/2026_performance/2026_perf_01_mm1.sy"
    echo "  $0 tests/2026_performance/2026_perf_01_mm1.sy tests/2026_performance/2026_perf_01_mm1.in"
    echo "  $0 tests/2026_function/2026_func_00_main.sy"
    exit 1
fi

SOURCE_FILE=$1
INPUT_FILE=$2

# 检查源文件是否存在
if [ ! -f "$SOURCE_FILE" ]; then
    echo "错误: 源文件不存在: $SOURCE_FILE"
    exit 1
fi

# 推断文件类型和基础名
BASENAME=$(basename "$SOURCE_FILE")
BASENAME_NO_EXT="${BASENAME%.*}"
EXT="${BASENAME##*.}"

# 如果没有指定输入文件，尝试自动查找
if [ -z "$INPUT_FILE" ]; then
    DIR=$(dirname "$SOURCE_FILE")
    if [ -f "$DIR/$BASENAME_NO_EXT.in" ]; then
        INPUT_FILE="$DIR/$BASENAME_NO_EXT.in"
    fi
fi

# 推断期望输出文件
DIR=$(dirname "$SOURCE_FILE")
OUTPUT_FILE="$DIR/$BASENAME_NO_EXT.out"

echo "================================================"
echo "gem5 BOOM v3 RISC-V 测试"
echo "================================================"
echo "源文件:   $SOURCE_FILE"
echo "输入文件: ${INPUT_FILE:-无}"
echo "期望输出: ${OUTPUT_FILE:-未知}"
echo "================================================"
echo

# 临时文件路径
TMP_ASM="/tmp/gem5_test_${BASENAME_NO_EXT}.s"
TMP_BIN="/tmp/gem5_test_${BASENAME_NO_EXT}"

# 编译
echo "=== 步骤 1: 编译 ==="
cd "$REPO_ROOT"

if [ "$EXT" = "sy" ] || [ "$EXT" = "c" ]; then
    echo "编译 SysY/C 源码为 RISC-V 汇编..."
    ./build/minic -S -O 1 -o "$TMP_ASM" "$SOURCE_FILE"

    echo "链接生成可执行文件..."
    riscv64-linux-gnu-gcc -static -o "$TMP_BIN" "$TMP_ASM" tests/libsysy_riscv.a
else
    echo "错误: 不支持的文件类型: .$EXT (仅支持 .sy 和 .c)"
    exit 1
fi

echo "✅ 编译完成: $TMP_BIN"
echo

# QEMU 基准测试
echo "=== 步骤 2: QEMU 基准测试 ==="
if [ -n "$INPUT_FILE" ]; then
    qemu-riscv64-static "$TMP_BIN" < "$INPUT_FILE" > /tmp/qemu_output.txt 2>&1 || true
else
    qemu-riscv64-static "$TMP_BIN" > /tmp/qemu_output.txt 2>&1 || true
fi

QEMU_OUTPUT=$(cat /tmp/qemu_output.txt | grep -E "^-?[0-9]+$" | tail -1 || echo "")
if [ -n "$QEMU_OUTPUT" ]; then
    echo "QEMU 输出: $QEMU_OUTPUT"
else
    echo "QEMU 输出: (无数字输出)"
fi

# 验证输出正确性
if [ -f "$OUTPUT_FILE" ]; then
    EXPECTED=$(cat "$OUTPUT_FILE" | head -1)
    if [ "$QEMU_OUTPUT" = "$EXPECTED" ]; then
        echo "✅ 输出正确"
    else
        echo "❌ 输出不匹配！期望: $EXPECTED, 实际: $QEMU_OUTPUT"
    fi
fi
echo

# gem5 BOOM v3 测试
echo "=== 步骤 3: gem5 BOOM v3 模拟 ==="
echo "开始时间: $(date '+%Y-%m-%d %H:%M:%S')"
echo

GEM5_ARGS="--cmd=$TMP_BIN --sys-clock=2GHz --l2-size=256kB"
if [ -n "$INPUT_FILE" ]; then
    GEM5_ARGS="$GEM5_ARGS --input=$INPUT_FILE"
fi

# 清理旧输出
rm -rf m5out 2>/dev/null

cd "$REPO_ROOT"
time $GEM5_BIN --outdir=m5out $BOOM_CONFIG $GEM5_ARGS > gem5_boom.log 2>&1

echo
echo "结束时间: $(date '+%Y-%m-%d %H:%M:%S')"
echo

# 提取结果
GEM5_OUTPUT=$(grep "^-\?[0-9]\+$" gem5_boom.log | tail -1 || echo "")
EXIT_TICK=$(grep "Exiting @ tick" gem5_boom.log | tail -1 || echo "")

echo "gem5 输出: ${GEM5_OUTPUT:-无}"
echo "$EXIT_TICK"
echo

# 结果汇总
echo "================================================"
echo "测试结果"
echo "================================================"

if [ -f "m5out/stats.txt" ]; then
    TICKS=$(grep "simTicks" m5out/stats.txt | head -1 | awk '{print $2}')
    IPC=$(grep "^system.cpu.ipc_total" m5out/stats.txt 2>/dev/null | head -1 | awk '{print $2}')
    CYCLES=$(grep "system.cpu.numCycles" m5out/stats.txt | head -1 | awk '{print $2}')

    # Cache 统计
    DCACHE_MISS=$(grep "dcache.demandMissRate::total" m5out/stats.txt 2>/dev/null | head -1 | awk '{print $2}')
    ICACHE_MISS=$(grep "icache.demandMissRate::total" m5out/stats.txt 2>/dev/null | head -1 | awk '{print $2}')
    L2_MISS=$(grep "l2cache.demandMissRate::total" m5out/stats.txt 2>/dev/null | head -1 | awk '{print $2}')

    # 分支预测统计
    BP_LOOKUPS=$(grep "system.cpu.branchPred.lookups" m5out/stats.txt 2>/dev/null | head -1 | awk '{print $2}')
    BP_COND=$(grep "system.cpu.branchPred.condPredicted" m5out/stats.txt 2>/dev/null | head -1 | awk '{print $2}')
    BP_INCORRECT=$(grep "system.cpu.branchPred.condIncorrect" m5out/stats.txt 2>/dev/null | head -1 | awk '{print $2}')

    echo "性能统计:"
    echo "  模拟周期 (ticks):     ${TICKS:-N/A}"
    echo "  CPU 周期数:            ${CYCLES:-N/A}"
    echo "  IPC:                   ${IPC:-N/A}"
    echo
    echo "Cache 统计:"
    echo "  L1 D-Cache Miss Rate:  ${DCACHE_MISS:-N/A}"
    echo "  L1 I-Cache Miss Rate:  ${ICACHE_MISS:-N/A}"
    echo "  L2 Cache Miss Rate:    ${L2_MISS:-N/A}"
    echo
    if [ -n "$BP_LOOKUPS" ] && [ "$BP_LOOKUPS" != "0" ]; then
        echo "分支预测统计:"
        echo "  分支预测查询次数:     $BP_LOOKUPS"
        echo "  条件分支预测次数:     ${BP_COND:-N/A}"
        echo "  预测错误次数:         ${BP_INCORRECT:-N/A}"
        if [ -n "$BP_COND" ] && [ -n "$BP_INCORRECT" ] && [ "$BP_COND" != "0" ]; then
            ACCURACY=$(awk "BEGIN {printf \"%.2f\", (1 - $BP_INCORRECT / $BP_COND) * 100}")
            echo "  分支预测准确率:       ${ACCURACY}%"
        fi
        echo
    fi

    echo "输出验证:"
    if [ -f "$OUTPUT_FILE" ]; then
        EXPECTED=$(cat "$OUTPUT_FILE" | head -1)
        printf "  期望输出: %s\n" "$EXPECTED"
        printf "  QEMU 输出: %s\n" "${QEMU_OUTPUT:-无}"
        printf "  gem5 输出: %s\n" "${GEM5_OUTPUT:-无}"

        if [ "$GEM5_OUTPUT" = "$EXPECTED" ]; then
            echo "  ✅ gem5 输出正确"
        else
            echo "  ❌ gem5 输出不匹配"
        fi
    else
        printf "  QEMU 输出: %s\n" "${QEMU_OUTPUT:-无}"
        printf "  gem5 输出: %s\n" "${GEM5_OUTPUT:-无}"
    fi
    echo

    echo "详细统计: m5out/stats.txt"
    echo "运行日志: gem5_boom.log"
else
    echo "❌ 未找到 gem5 统计文件"
fi

echo "================================================"
