#!/bin/bash
# 基于 tests 目录真实测试样例的内联阈值基准测试
# 测试不同配置对代码大小和编译性能的影响，给出推荐参数

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MINIC="$PROJECT_ROOT/build/minic"
TEST_DIR="$PROJECT_ROOT/tests"

# 检查 minic 是否存在
if [ ! -f "$MINIC" ]; then
    echo -e "${RED}Error: minic not found at $MINIC${NC}"
    echo "Please build the project first: cmake --build build"
    exit 1
fi

# 创建临时目录
TEMP_DIR=$(mktemp -d /tmp/inline_benchmark_XXXXXX)
trap "rm -rf $TEMP_DIR" EXIT

echo -e "${GREEN}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║  Function Inlining Threshold Benchmark (Real Test Suite)     ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo "Test directory: $TEST_DIR"
echo "Temp directory: $TEMP_DIR"
echo ""

# 选择测试集
TEST_SETS=(
    "2025_function:10:功能测试"
    "2025_performance:10:性能测试"
)

# 配置列表
declare -A CONFIGS
CONFIGS=(
    ["baseline"]="MINIC_DISABLE_PROFITABILITY=1|禁用收益检查(旧行为)"
    ["default"]="0|默认配置(threshold=150, hot=3x)"
    ["conservative"]="MINIC_INLINE_THRESHOLD=70|保守模式(类似GCC)"
    ["aggressive"]="MINIC_INLINE_THRESHOLD=225|激进模式(类似LLVM)"
    ["balanced"]="MINIC_INLINE_THRESHOLD=180 MINIC_INLINE_HOT_MULTIPLIER=3|平衡模式(threshold=180)"
    ["minimal"]="MINIC_INLINE_THRESHOLD=25|最小模式(仅小函数)"
)

CONFIG_ORDER=("baseline" "default" "conservative" "aggressive" "balanced" "minimal")

# 结果存储
declare -A TOTAL_SIZE
declare -A TOTAL_TIME
declare -A SUCCESS_COUNT
declare -A FAIL_COUNT

# 初始化统计
for config in "${CONFIG_ORDER[@]}"; do
    TOTAL_SIZE[$config]=0
    TOTAL_TIME[$config]=0
    SUCCESS_COUNT[$config]=0
    FAIL_COUNT[$config]=0
done

# 编译单个文件
compile_test() {
    local test_file="$1"
    local config_name="$2"
    local env_vars="$3"
    local output_file="$4"

    local start_time=$(date +%s%N)

    if [ "$env_vars" = "0" ]; then
        timeout 30 "$MINIC" "$test_file" -O2 -S -o "$output_file" > /dev/null 2>&1
    else
        timeout 30 env $env_vars "$MINIC" "$test_file" -O2 -S -o "$output_file" > /dev/null 2>&1
    fi

    local exit_code=$?
    local end_time=$(date +%s%N)
    local elapsed=$(( (end_time - start_time) / 1000000 )) # 毫秒

    if [ $exit_code -eq 0 ] && [ -f "$output_file" ]; then
        local size=$(wc -l < "$output_file")
        echo "$size|$elapsed"
        return 0
    else
        echo "0|0"
        return 1
    fi
}

# 遍历测试集
for test_set_info in "${TEST_SETS[@]}"; do
    IFS=':' read -r test_set max_tests description <<< "$test_set_info"

    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${CYAN}测试集: ${description} (${test_set})${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo ""

    # 获取测试文件列表
    mapfile -t test_files < <(find "$TEST_DIR/$test_set" -name "*.c" -type f | grep -v sylib | head -n "$max_tests")

    if [ ${#test_files[@]} -eq 0 ]; then
        echo -e "${YELLOW}⚠ 未找到测试文件，跳过此测试集${NC}"
        echo ""
        continue
    fi

    echo "找到 ${#test_files[@]} 个测试文件"
    echo ""

    # 对每个配置运行测试
    for config in "${CONFIG_ORDER[@]}"; do
        IFS='|' read -r env_vars desc <<< "${CONFIGS[$config]}"

        echo -e "${YELLOW}► $desc${NC}"

        set_success=0
        set_fail=0
        set_size=0
        set_time=0

        for test_file in "${test_files[@]}"; do
            base_name=$(basename "$test_file" .c)
            output_file="$TEMP_DIR/${config}_${base_name}.s"

            result=$(compile_test "$test_file" "$config" "$env_vars" "$output_file")
            if [ $? -eq 0 ]; then
                IFS='|' read -r size elapsed <<< "$result"
                set_success=$((set_success + 1))
                set_size=$((set_size + size))
                set_time=$((set_time + elapsed))
            else
                set_fail=$((set_fail + 1))
            fi
        done

        SUCCESS_COUNT[$config]=$((${SUCCESS_COUNT[$config]} + set_success))
        FAIL_COUNT[$config]=$((${FAIL_COUNT[$config]} + set_fail))
        TOTAL_SIZE[$config]=$((${TOTAL_SIZE[$config]} + set_size))
        TOTAL_TIME[$config]=$((${TOTAL_TIME[$config]} + set_time))

        echo "  成功: $set_success, 失败: $set_fail, 总代码行数: $set_size, 总时间: ${set_time}ms"
    done

    echo ""
done

# 计算并显示总结
echo -e "${GREEN}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║                        基准测试结果                           ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""

printf "${CYAN}%-20s %10s %10s %12s %12s %12s${NC}\n" \
    "配置" "成功数" "失败数" "代码总行数" "编译时间(ms)" "归一化大小"
echo "────────────────────────────────────────────────────────────────────────"

# 以 baseline 为基准计算归一化
baseline_size=${TOTAL_SIZE[baseline]}
if [ $baseline_size -eq 0 ]; then
    baseline_size=1
fi

best_size=999999999
best_config=""
best_time=999999999
best_time_config=""

for config in "${CONFIG_ORDER[@]}"; do
    IFS='|' read -r env_vars desc <<< "${CONFIGS[$config]}"

    success=${SUCCESS_COUNT[$config]}
    fail=${FAIL_COUNT[$config]}
    size=${TOTAL_SIZE[$config]}
    time=${TOTAL_TIME[$config]}

    # 归一化大小 (相对于 baseline)
    if [ $baseline_size -gt 0 ]; then
        normalized=$(awk "BEGIN {printf \"%.2f%%\", ($size / $baseline_size) * 100}")
    else
        normalized="N/A"
    fi

    # 记录最佳
    if [ $size -gt 0 ] && [ $size -lt $best_size ]; then
        best_size=$size
        best_config=$config
    fi

    if [ $time -gt 0 ] && [ $time -lt $best_time ]; then
        best_time=$time
        best_time_config=$config
    fi

    # 高亮最佳结果
    if [ "$config" = "$best_config" ]; then
        printf "${GREEN}%-20s %10d %10d %12d %12d %12s ★${NC}\n" \
            "$config" "$success" "$fail" "$size" "$time" "$normalized"
    else
        printf "%-20s %10d %10d %12d %12d %12s\n" \
            "$config" "$success" "$fail" "$size" "$time" "$normalized"
    fi
done

echo ""

# 分析和推荐
echo -e "${GREEN}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║                        分析与推荐                             ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""

echo -e "${CYAN}📊 性能分析:${NC}"
echo ""

# 代码大小分析
echo "1. 代码大小对比:"
for config in "${CONFIG_ORDER[@]}"; do
    if [ "$config" = "baseline" ]; then
        continue
    fi

    size=${TOTAL_SIZE[$config]}
    baseline_size=${TOTAL_SIZE[baseline]}

    if [ $baseline_size -gt 0 ]; then
        diff=$((size - baseline_size))
        percent=$(awk "BEGIN {printf \"%.1f\", (($diff * 100.0) / $baseline_size)}")

        if [ $diff -lt 0 ]; then
            echo -e "   ${GREEN}$config: $diff 行 ($percent%) - 代码更小${NC}"
        elif [ $diff -eq 0 ]; then
            echo "   $config: 相同"
        else
            echo -e "   ${YELLOW}$config: +$diff 行 (+$percent%) - 代码更大${NC}"
        fi
    fi
done
echo ""

# 编译时间分析
echo "2. 编译时间对比:"
baseline_time=${TOTAL_TIME[baseline]}
for config in "${CONFIG_ORDER[@]}"; do
    if [ "$config" = "baseline" ]; then
        continue
    fi

    time=${TOTAL_TIME[$config]}

    if [ $baseline_time -gt 0 ]; then
        diff=$((time - baseline_time))
        percent=$(awk "BEGIN {printf \"%.1f\", (($diff * 100.0) / $baseline_time)}")

        if [ $diff -lt 0 ]; then
            echo -e "   ${GREEN}$config: ${diff}ms ($percent%) - 编译更快${NC}"
        elif [ $diff -eq 0 ]; then
            echo "   $config: 相同"
        else
            echo -e "   ${YELLOW}$config: +${diff}ms (+$percent%) - 编译更慢${NC}"
        fi
    fi
done
echo ""

# 推荐配置
echo -e "${CYAN}🎯 推荐配置:${NC}"
echo ""

# 计算综合得分 (代码大小权重 70%, 编译时间权重 30%)
declare -A SCORES
for config in "${CONFIG_ORDER[@]}"; do
    if [ "$config" = "baseline" ]; then
        SCORES[$config]=100
        continue
    fi

    size=${TOTAL_SIZE[$config]}
    time=${TOTAL_TIME[$config]}
    baseline_size=${TOTAL_SIZE[baseline]}
    baseline_time=${TOTAL_TIME[baseline]}

    if [ $baseline_size -gt 0 ] && [ $baseline_time -gt 0 ]; then
        # 得分：代码越小越好，时间越快越好
        size_score=$(awk "BEGIN {printf \"%.2f\", 100 - ((($size - $baseline_size) * 100.0) / $baseline_size)}")
        time_score=$(awk "BEGIN {printf \"%.2f\", 100 - ((($time - $baseline_time) * 100.0) / $baseline_time)}")
        total_score=$(awk "BEGIN {printf \"%.2f\", ($size_score * 0.7) + ($time_score * 0.3)}")
        SCORES[$config]=$total_score
    else
        SCORES[$config]=0
    fi
done

# 找出最佳配置
best_score=-999
best_recommend=""
for config in "${CONFIG_ORDER[@]}"; do
    if [ "$config" = "baseline" ]; then
        continue
    fi

    score=${SCORES[$config]}
    if (( $(awk "BEGIN {print ($score > $best_score)}") )); then
        best_score=$score
        best_recommend=$config
    fi
done

echo "基于测试结果的推荐:"
echo ""

case "$best_recommend" in
    "default")
        echo -e "${GREEN}✓ 推荐: default (默认配置)${NC}"
        echo "  - 阈值: 150"
        echo "  - 热点倍数: 3x"
        echo "  - 适用场景: 通用场景，平衡的代码大小和性能"
        echo "  - 使用方式: ./build/minic program.c -O2"
        ;;
    "conservative")
        echo -e "${GREEN}✓ 推荐: conservative (保守配置)${NC}"
        echo "  - 阈值: 70"
        echo "  - 适用场景: 代码大小敏感，类似 GCC 行为"
        echo "  - 使用方式: MINIC_INLINE_THRESHOLD=70 ./build/minic program.c -O2"
        ;;
    "aggressive")
        echo -e "${GREEN}✓ 推荐: aggressive (激进配置)${NC}"
        echo "  - 阈值: 225"
        echo "  - 适用场景: 性能优先，允许代码膨胀"
        echo "  - 使用方式: MINIC_INLINE_THRESHOLD=225 ./build/minic program.c -O2"
        ;;
    "balanced")
        echo -e "${GREEN}✓ 推荐: balanced (平衡配置)${NC}"
        echo "  - 阈值: 180"
        echo "  - 热点倍数: 3x"
        echo "  - 适用场景: 略微激进但仍然平衡"
        echo "  - 使用方式: MINIC_INLINE_THRESHOLD=180 ./build/minic program.c -O2"
        ;;
    "minimal")
        echo -e "${GREEN}✓ 推荐: minimal (最小配置)${NC}"
        echo "  - 阈值: 25"
        echo "  - 适用场景: 极度代码大小敏感"
        echo "  - 使用方式: MINIC_INLINE_THRESHOLD=25 ./build/minic program.c -O2"
        ;;
esac

echo ""
echo "不同场景的选择建议:"
echo ""
echo -e "${BLUE}1. 性能优先场景 (Performance):${NC}"
echo "   推荐: aggressive (MINIC_INLINE_THRESHOLD=225)"
echo "   适用: 性能测试、计算密集型应用"
echo ""
echo -e "${BLUE}2. 代码大小优先场景 (Code Size):${NC}"
echo "   推荐: conservative (MINIC_INLINE_THRESHOLD=70)"
echo "   适用: 嵌入式系统、资源受限环境"
echo ""
echo -e "${BLUE}3. 平衡场景 (Balanced):${NC}"
echo "   推荐: default (threshold=150, hot=3x)"
echo "   适用: 大多数应用，兼顾性能和代码大小"
echo ""

echo -e "${CYAN}📝 环境变量参考:${NC}"
echo ""
echo "  MINIC_INLINE_THRESHOLD=<value>      # 覆盖默认阈值 (默认: 150)"
echo "  MINIC_INLINE_HOT_MULTIPLIER=<value> # 覆盖热点倍数 (默认: 3)"
echo "  MINIC_DISABLE_PROFITABILITY=1       # 禁用收益检查"
echo "  MINIC_OPT_REMARKS=1                 # 显示优化决策"
echo ""

echo -e "${GREEN}测试完成！${NC}"
