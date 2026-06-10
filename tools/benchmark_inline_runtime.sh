#!/bin/bash
# 基于性能测试运行时间的内联阈值基准测试
# 编译性能测试样例，实际执行并测量运行时间

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MINIC="$PROJECT_ROOT/build/minic"
TEST_DIR="$PROJECT_ROOT/tests"
SYLIB="$TEST_DIR/libsysy_riscv.a"

# 检查 minic 是否存在
if [ ! -f "$MINIC" ]; then
    echo -e "${RED}Error: minic not found at $MINIC${NC}"
    echo "Please build the project first: cmake --build build"
    exit 1
fi

# 检查是否有 qemu-riscv64
if ! command -v qemu-riscv64 &> /dev/null; then
    echo -e "${RED}Error: qemu-riscv64 not found${NC}"
    echo "Please install qemu-user: sudo apt-get install qemu-user"
    exit 1
fi

# 创建临时目录
TEMP_DIR=$(mktemp -d /tmp/inline_perf_benchmark_XXXXXX)
trap "rm -rf $TEMP_DIR" EXIT

echo -e "${GREEN}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║     Function Inlining Benchmark (Runtime Performance)        ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo "Test directory: $TEST_DIR"
echo "Temp directory: $TEMP_DIR"
echo ""

# 配置列表
declare -A CONFIGS
CONFIGS=(
    ["baseline"]="MINIC_DISABLE_PROFITABILITY=1|禁用收益检查(旧行为)"
    ["default"]="0|默认配置(threshold=150, hot=3x)"
    ["conservative"]="MINIC_INLINE_THRESHOLD=70|保守模式(类似GCC)"
    ["aggressive"]="MINIC_INLINE_THRESHOLD=225|激进模式(类似LLVM)"
    ["balanced"]="MINIC_INLINE_THRESHOLD=180 MINIC_INLINE_HOT_MULTIPLIER=3|平衡模式"
    ["hot5x"]="MINIC_INLINE_HOT_MULTIPLIER=5|热点5倍(hot=5x)"
)

CONFIG_ORDER=("baseline" "default" "conservative" "aggressive" "balanced" "hot5x")

# 性能测试列表 (选择有明确输入输出的测试)
PERF_TESTS=(
    "2025_perf_01_mm1"
    "2025_perf_03_sort1"
    "2025_perf_crypto-1"
    "2025_perf_h-4-01"
    "2025_perf_matmul1"
)

# 结果存储
declare -A COMPILE_SIZE
declare -A COMPILE_TIME
declare -A RUNTIME_TOTAL
declare -A SUCCESS_COUNT

# 初始化统计
for config in "${CONFIG_ORDER[@]}"; do
    COMPILE_SIZE[$config]=0
    COMPILE_TIME[$config]=0
    RUNTIME_TOTAL[$config]=0
    SUCCESS_COUNT[$config]=0
done

# 编译并运行单个测试
compile_and_run() {
    local test_name="$1"
    local config_name="$2"
    local env_vars="$3"

    local test_file="$TEST_DIR/2025_performance/${test_name}.c"
    local input_file="$TEST_DIR/2025_performance/${test_name}.in"
    local expected_output="$TEST_DIR/2025_performance/${test_name}.out"
    local asm_file="$TEMP_DIR/${config_name}_${test_name}.s"
    local exe_file="$TEMP_DIR/${config_name}_${test_name}"
    local actual_output="$TEMP_DIR/${config_name}_${test_name}.output"

    # 检查文件存在
    if [ ! -f "$test_file" ]; then
        echo "0|0|0|SKIP"
        return 1
    fi

    # 编译
    local compile_start=$(date +%s%N)
    if [ "$env_vars" = "0" ]; then
        timeout 30 "$MINIC" "$test_file" -O2 -S -o "$asm_file" > /dev/null 2>&1
    else
        timeout 30 env $env_vars "$MINIC" "$test_file" -O2 -S -o "$asm_file" > /dev/null 2>&1
    fi

    if [ $? -ne 0 ] || [ ! -f "$asm_file" ]; then
        echo "0|0|0|COMPILE_FAIL"
        return 1
    fi

    local compile_end=$(date +%s%N)
    local compile_time=$(( (compile_end - compile_start) / 1000000 ))
    local asm_size=$(wc -l < "$asm_file")

    # 链接
    if ! gcc -march=rv64gc -static "$asm_file" "$SYLIB" -o "$exe_file" > /dev/null 2>&1; then
        echo "$asm_size|$compile_time|0|LINK_FAIL"
        return 1
    fi

    # 运行 (测量3次取平均)
    local total_runtime=0
    local run_count=0

    for i in 1 2 3; do
        local run_start=$(date +%s%N)

        if [ -f "$input_file" ]; then
            timeout 10 qemu-riscv64 "$exe_file" < "$input_file" > "$actual_output" 2>/dev/null
        else
            timeout 10 qemu-riscv64 "$exe_file" > "$actual_output" 2>/dev/null
        fi

        local run_status=$?
        local run_end=$(date +%s%N)

        if [ $run_status -eq 0 ]; then
            local runtime=$(( (run_end - run_start) / 1000000 ))
            total_runtime=$((total_runtime + runtime))
            run_count=$((run_count + 1))
        fi
    done

    if [ $run_count -eq 0 ]; then
        echo "$asm_size|$compile_time|0|RUN_FAIL"
        return 1
    fi

    local avg_runtime=$((total_runtime / run_count))

    # 验证输出 (可选)
    # if [ -f "$expected_output" ]; then
    #     if ! diff -q "$actual_output" "$expected_output" > /dev/null 2>&1; then
    #         echo "$asm_size|$compile_time|$avg_runtime|OUTPUT_MISMATCH"
    #         return 1
    #     fi
    # fi

    echo "$asm_size|$compile_time|$avg_runtime|OK"
    return 0
}

echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${CYAN}性能测试 (2025_performance) - 运行时间测量${NC}"
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo "测试样例: ${#PERF_TESTS[@]} 个"
echo "每个样例运行 3 次取平均值"
echo ""

# 对每个测试用例
test_count=0
for test_name in "${PERF_TESTS[@]}"; do
    test_count=$((test_count + 1))
    echo -e "${MAGENTA}[${test_count}/${#PERF_TESTS[@]}] ${test_name}${NC}"

    # 对每个配置
    for config in "${CONFIG_ORDER[@]}"; do
        IFS='|' read -r env_vars desc <<< "${CONFIGS[$config]}"

        printf "  %-20s ... " "$config"

        result=$(compile_and_run "$test_name" "$config" "$env_vars")
        IFS='|' read -r asm_size compile_time runtime status <<< "$result"

        if [ "$status" = "OK" ]; then
            COMPILE_SIZE[$config]=$((${COMPILE_SIZE[$config]} + asm_size))
            COMPILE_TIME[$config]=$((${COMPILE_TIME[$config]} + compile_time))
            RUNTIME_TOTAL[$config]=$((${RUNTIME_TOTAL[$config]} + runtime))
            SUCCESS_COUNT[$config]=$((${SUCCESS_COUNT[$config]} + 1))

            printf "${GREEN}✓${NC} compile:%4dms runtime:%5dms size:%5d\n" "$compile_time" "$runtime" "$asm_size"
        else
            printf "${RED}✗ %s${NC}\n" "$status"
        fi
    done

    echo ""
done

# 计算并显示总结
echo -e "${GREEN}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║                    基准测试结果 (汇总)                        ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""

printf "${CYAN}%-15s %8s %10s %12s %12s %12s${NC}\n" \
    "配置" "成功数" "运行时间" "编译时间" "代码大小" "运行时归一"
echo "─────────────────────────────────────────────────────────────────────────────"

# 以 baseline 为基准
baseline_runtime=${RUNTIME_TOTAL[baseline]}
if [ $baseline_runtime -eq 0 ]; then
    baseline_runtime=1
fi

best_runtime=999999999
best_config=""
best_speedup=0

for config in "${CONFIG_ORDER[@]}"; do
    success=${SUCCESS_COUNT[$config]}
    runtime=${RUNTIME_TOTAL[$config]}
    compile_time=${COMPILE_TIME[$config]}
    code_size=${COMPILE_SIZE[$config]}

    if [ $success -eq 0 ]; then
        printf "%-15s %8d %10s %12s %12s %12s\n" \
            "$config" "$success" "N/A" "N/A" "N/A" "N/A"
        continue
    fi

    # 归一化运行时间
    normalized=$(awk "BEGIN {printf \"%.2f%%\", ($runtime * 100.0) / $baseline_runtime}")

    # 加速比 (越小越好，<100%表示更快)
    speedup=$(awk "BEGIN {printf \"%.2f\", 100.0 - ($runtime * 100.0) / $baseline_runtime}")

    # 记录最佳
    if [ $runtime -lt $best_runtime ] && [ $runtime -gt 0 ]; then
        best_runtime=$runtime
        best_config=$config
        best_speedup=$speedup
    fi

    # 高亮最佳结果
    if [ "$config" = "$best_config" ]; then
        printf "${GREEN}%-15s %8d %10dms %12dms %12d %12s ★${NC}\n" \
            "$config" "$success" "$runtime" "$compile_time" "$code_size" "$normalized"
    else
        printf "%-15s %8d %10dms %12dms %12d %12s\n" \
            "$config" "$success" "$runtime" "$compile_time" "$code_size" "$normalized"
    fi
done

echo ""

# 性能分析
echo -e "${GREEN}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║                   性能分析 (运行时间)                          ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""

echo -e "${CYAN}📊 运行时间对比 (相对于 baseline):${NC}"
echo ""

for config in "${CONFIG_ORDER[@]}"; do
    if [ "$config" = "baseline" ]; then
        continue
    fi

    success=${SUCCESS_COUNT[$config]}
    if [ $success -eq 0 ]; then
        continue
    fi

    runtime=${RUNTIME_TOTAL[$config]}
    diff=$((runtime - baseline_runtime))
    percent=$(awk "BEGIN {printf \"%.2f\", ($diff * 100.0) / $baseline_runtime}")

    if (( $(awk "BEGIN {print ($diff < 0)}") )); then
        abs_diff=${diff#-}
        echo -e "   ${GREEN}$config: -${abs_diff}ms ($percent%) - 更快 ⚡${NC}"
    elif [ $diff -eq 0 ]; then
        echo "   $config: 相同"
    else
        echo -e "   ${YELLOW}$config: +${diff}ms (+$percent%) - 更慢${NC}"
    fi
done

echo ""

# 代码大小对比
echo -e "${CYAN}📦 代码大小对比 (相对于 baseline):${NC}"
echo ""

baseline_size=${COMPILE_SIZE[baseline]}
for config in "${CONFIG_ORDER[@]}"; do
    if [ "$config" = "baseline" ]; then
        continue
    fi

    success=${SUCCESS_COUNT[$config]}
    if [ $success -eq 0 ]; then
        continue
    fi

    size=${COMPILE_SIZE[$config]}
    diff=$((size - baseline_size))
    percent=$(awk "BEGIN {printf \"%.2f\", ($diff * 100.0) / $baseline_size}")

    if [ $diff -lt 0 ]; then
        abs_diff=${diff#-}
        echo -e "   ${GREEN}$config: -${abs_diff} 行 ($percent%)${NC}"
    elif [ $diff -eq 0 ]; then
        echo "   $config: 相同"
    else
        echo -e "   ${YELLOW}$config: +${diff} 行 (+$percent%)${NC}"
    fi
done

echo ""

# 推荐配置
echo -e "${GREEN}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║                  推荐配置 (基于运行时性能)                     ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""

if [ -z "$best_config" ]; then
    echo -e "${RED}无法确定最佳配置${NC}"
    exit 1
fi

echo -e "${CYAN}🏆 运行时性能最佳: ${GREEN}${best_config}${NC}"
echo ""

IFS='|' read -r env_vars desc <<< "${CONFIGS[$best_config]}"

case "$best_config" in
    "baseline")
        echo "最佳配置: baseline (禁用收益检查)"
        echo "  说明: 旧版行为，无成本模型限制"
        echo "  注意: 这可能表示当前测试集对内联非常友好"
        ;;
    "default")
        echo -e "${GREEN}✓ 推荐: default (默认配置)${NC}"
        echo "  - 阈值: 150"
        echo "  - 热点倍数: 3x"
        echo "  - 性能提升: ${best_speedup}%"
        echo "  - 使用方式: ./build/minic program.c -O2"
        ;;
    "conservative")
        echo -e "${GREEN}✓ 推荐: conservative (保守配置)${NC}"
        echo "  - 阈值: 70"
        echo "  - 性能提升: ${best_speedup}%"
        echo "  - 使用方式: MINIC_INLINE_THRESHOLD=70 ./build/minic program.c -O2"
        echo "  - 特点: 类似 GCC，更小的代码"
        ;;
    "aggressive")
        echo -e "${GREEN}✓ 推荐: aggressive (激进配置)${NC}"
        echo "  - 阈值: 225"
        echo "  - 性能提升: ${best_speedup}%"
        echo "  - 使用方式: MINIC_INLINE_THRESHOLD=225 ./build/minic program.c -O2"
        echo "  - 特点: 类似 LLVM，更多内联"
        ;;
    "balanced")
        echo -e "${GREEN}✓ 推荐: balanced (平衡配置)${NC}"
        echo "  - 阈值: 180"
        echo "  - 热点倍数: 3x"
        echo "  - 性能提升: ${best_speedup}%"
        echo "  - 使用方式: MINIC_INLINE_THRESHOLD=180 ./build/minic program.c -O2"
        ;;
    "hot5x")
        echo -e "${GREEN}✓ 推荐: hot5x (热点5倍)${NC}"
        echo "  - 阈值: 150"
        echo "  - 热点倍数: 5x"
        echo "  - 性能提升: ${best_speedup}%"
        echo "  - 使用方式: MINIC_INLINE_HOT_MULTIPLIER=5 ./build/minic program.c -O2"
        echo "  - 特点: 循环内函数更激进内联"
        ;;
esac

echo ""
echo "详细说明:"
echo "  • 测试了 ${#PERF_TESTS[@]} 个性能测试样例"
echo "  • 每个样例运行 3 次取平均值"
echo "  • 以运行时间为主要指标"
echo "  • ${best_config} 配置运行时间最短"
echo ""

# 场景化建议
echo -e "${CYAN}📋 不同场景的选择建议:${NC}"
echo ""

# 找出代码最小的配置
smallest_size=999999999
smallest_config=""
for config in "${CONFIG_ORDER[@]}"; do
    size=${COMPILE_SIZE[$config]}
    if [ $size -gt 0 ] && [ $size -lt $smallest_size ]; then
        smallest_size=$size
        smallest_config=$config
    fi
done

echo -e "${BLUE}1. 性能优先场景 (Performance):${NC}"
echo "   推荐: $best_config"
echo "   最快运行时间: ${best_runtime}ms"
echo ""

echo -e "${BLUE}2. 代码大小优先场景 (Code Size):${NC}"
echo "   推荐: $smallest_config"
echo "   最小代码: ${smallest_size} 行"
echo ""

echo -e "${BLUE}3. 平衡场景 (Balanced):${NC}"
if [ "$best_config" = "default" ] || [ "$best_config" = "balanced" ]; then
    echo "   推荐: $best_config (性能和大小兼顾)"
else
    echo "   推荐: default 或 balanced"
fi
echo ""

echo -e "${CYAN}📝 环境变量参考:${NC}"
echo ""
echo "  MINIC_INLINE_THRESHOLD=<value>      # 覆盖默认阈值 (默认: 150)"
echo "  MINIC_INLINE_HOT_MULTIPLIER=<value> # 覆盖热点倍数 (默认: 3)"
echo "  MINIC_DISABLE_PROFITABILITY=1       # 禁用收益检查"
echo "  MINIC_OPT_REMARKS=1                 # 显示优化决策"
echo ""

echo -e "${GREEN}测试完成！${NC}"
echo ""
echo "提示: 可以使用以下命令重新运行特定配置的性能测试："
echo "  MINIC_INLINE_THRESHOLD=<value> ./build/minic test.c -O2"
