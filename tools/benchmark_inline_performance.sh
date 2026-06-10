#!/bin/bash
# 基于代码质量指标的内联阈值基准测试
# 使用代码大小、指令数等静态指标预测运行时性能

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

# 检查 minic 是否存在
if [ ! -f "$MINIC" ]; then
    echo -e "${RED}Error: minic not found at $MINIC${NC}"
    echo "Please build the project first: cmake --build build"
    exit 1
fi

# 创建临时目录
TEMP_DIR=$(mktemp -d /tmp/inline_perf_benchmark_XXXXXX)
trap "rm -rf $TEMP_DIR" EXIT

echo -e "${GREEN}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║   Function Inlining Benchmark (Performance-Oriented)         ║${NC}"
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
    ["balanced"]="MINIC_INLINE_THRESHOLD=180|平衡模式(threshold=180)"
    ["hot5x"]="MINIC_INLINE_HOT_MULTIPLIER=5|热点5倍(hot=5x)"
)

CONFIG_ORDER=("baseline" "default" "conservative" "aggressive" "balanced" "hot5x")

# 结果存储
declare -A TOTAL_LINES      # 总代码行数
declare -A TOTAL_INSTRS     # 总指令数（估算）
declare -A TOTAL_CALLS      # call指令数
declare -A TOTAL_BRANCHES   # 分支指令数
declare -A COMPILE_TIME     # 编译时间
declare -A SUCCESS_COUNT    # 成功数

# 初始化统计
for config in "${CONFIG_ORDER[@]}"; do
    TOTAL_LINES[$config]=0
    TOTAL_INSTRS[$config]=0
    TOTAL_CALLS[$config]=0
    TOTAL_BRANCHES[$config]=0
    COMPILE_TIME[$config]=0
    SUCCESS_COUNT[$config]=0
done

# 分析汇编文件
analyze_asm() {
    local asm_file="$1"

    if [ ! -f "$asm_file" ]; then
        echo "0|0|0|0"
        return 1
    fi

    local total_lines=$(wc -l < "$asm_file")

    # 统计实际指令（排除标签、伪指令、注释）
    local instr_count=$(grep -E '^\s+[a-z]' "$asm_file" | wc -l)

    # 统计 call 指令
    local call_count=$(grep -E '^\s+(call|jal|jalr)' "$asm_file" | wc -l)

    # 统计分支指令
    local branch_count=$(grep -E '^\s+(beq|bne|blt|bge|bltu|bgeu|j|jr)' "$asm_file" | wc -l)

    echo "$total_lines|$instr_count|$call_count|$branch_count"
    return 0
}

# 编译并分析单个测试
compile_and_analyze() {
    local test_file="$1"
    local config_name="$2"
    local env_vars="$3"

    local asm_file="$TEMP_DIR/${config_name}_$(basename "$test_file" .c).s"

    # 编译
    local compile_start=$(date +%s%N)
    if [ "$env_vars" = "0" ]; then
        timeout 30 "$MINIC" "$test_file" -O2 -S -o "$asm_file" > /dev/null 2>&1
    else
        timeout 30 env $env_vars "$MINIC" "$test_file" -O2 -S -o "$asm_file" > /dev/null 2>&1
    fi

    if [ $? -ne 0 ] || [ ! -f "$asm_file" ]; then
        echo "0|0|0|0|0|FAIL"
        return 1
    fi

    local compile_end=$(date +%s%N)
    local compile_time=$(( (compile_end - compile_start) / 1000000 ))

    # 分析汇编
    local analysis=$(analyze_asm "$asm_file")
    echo "${analysis}|${compile_time}|OK"
    return 0
}

echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${CYAN}性能测试分析 (2025_performance) - 代码质量指标${NC}"
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""

# 获取性能测试文件列表
mapfile -t perf_tests < <(find "$TEST_DIR/2025_performance" -name "*.c" -type f | head -10)

echo "测试样例数: ${#perf_tests[@]}"
echo ""

# 对每个测试用例
test_count=0
for test_file in "${perf_tests[@]}"; do
    test_count=$((test_count + 1))
    test_name=$(basename "$test_file" .c)

    echo -e "${MAGENTA}[${test_count}/${#perf_tests[@]}] ${test_name}${NC}"

    # 对每个配置
    for config in "${CONFIG_ORDER[@]}"; do
        IFS='|' read -r env_vars desc <<< "${CONFIGS[$config]}"

        printf "  %-15s ... " "$config"

        result=$(compile_and_analyze "$test_file" "$config" "$env_vars")
        IFS='|' read -r lines instrs calls branches ctime status <<< "$result"

        if [ "$status" = "OK" ]; then
            TOTAL_LINES[$config]=$((${TOTAL_LINES[$config]} + lines))
            TOTAL_INSTRS[$config]=$((${TOTAL_INSTRS[$config]} + instrs))
            TOTAL_CALLS[$config]=$((${TOTAL_CALLS[$config]} + calls))
            TOTAL_BRANCHES[$config]=$((${TOTAL_BRANCHES[$config]} + branches))
            COMPILE_TIME[$config]=$((${COMPILE_TIME[$config]} + ctime))
            SUCCESS_COUNT[$config]=$((${SUCCESS_COUNT[$config]} + 1))

            printf "${GREEN}✓${NC} instrs:%-6d calls:%-4d branches:%-4d\n" "$instrs" "$calls" "$branches"
        else
            printf "${RED}✗ FAILED${NC}\n"
        fi
    done

    echo ""
done

# 显示总结
echo -e "${GREEN}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║                  基准测试结果 (性能指标)                       ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""

printf "${CYAN}%-15s %8s %10s %10s %10s %10s %12s${NC}\n" \
    "配置" "成功数" "总指令数" "call数" "分支数" "编译时间" "指令归一化"
echo "──────────────────────────────────────────────────────────────────────────────────────"

# 以 baseline 为基准
baseline_instrs=${TOTAL_INSTRS[baseline]}
if [ $baseline_instrs -eq 0 ]; then
    baseline_instrs=1
fi

best_instrs=999999999
best_config=""
best_calls=999999999
best_call_config=""

for config in "${CONFIG_ORDER[@]}"; do
    success=${SUCCESS_COUNT[$config]}
    instrs=${TOTAL_INSTRS[$config]}
    calls=${TOTAL_CALLS[$config]}
    branches=${TOTAL_BRANCHES[$config]}
    ctime=${COMPILE_TIME[$config]}

    if [ $success -eq 0 ]; then
        printf "%-15s %8d %10s %10s %10s %10s %12s\n" \
            "$config" "$success" "N/A" "N/A" "N/A" "N/A" "N/A"
        continue
    fi

    # 归一化指令数
    normalized=$(awk "BEGIN {printf \"%.2f%%\", ($instrs * 100.0) / $baseline_instrs}")

    # 记录最佳（指令数最少 = 性能最好）
    if [ $instrs -lt $best_instrs ] && [ $instrs -gt 0 ]; then
        best_instrs=$instrs
        best_config=$config
    fi

    # 记录call数最少（间接优化指标）
    if [ $calls -lt $best_calls ] && [ $calls -gt 0 ]; then
        best_calls=$calls
        best_call_config=$config
    fi

    # 高亮最佳结果
    if [ "$config" = "$best_config" ]; then
        printf "${GREEN}%-15s %8d %10d %10d %10d %10dms %12s ★${NC}\n" \
            "$config" "$success" "$instrs" "$calls" "$branches" "$ctime" "$normalized"
    else
        printf "%-15s %8d %10d %10d %10d %10dms %12s\n" \
            "$config" "$success" "$instrs" "$calls" "$branches" "$ctime" "$normalized"
    fi
done

echo ""

# 性能分析
echo -e "${GREEN}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║                  性能分析 (预测运行时性能)                     ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""

echo -e "${CYAN}📊 指令数对比 (相对于 baseline，越少越快):${NC}"
echo ""

for config in "${CONFIG_ORDER[@]}"; do
    if [ "$config" = "baseline" ]; then
        continue
    fi

    success=${SUCCESS_COUNT[$config]}
    if [ $success -eq 0 ]; then
        continue
    fi

    instrs=${TOTAL_INSTRS[$config]}
    diff=$((instrs - baseline_instrs))
    percent=$(awk "BEGIN {printf \"%.2f\", ($diff * 100.0) / $baseline_instrs}")

    if (( $(awk "BEGIN {print ($diff < 0)}") )); then
        abs_diff=${diff#-}
        echo -e "   ${GREEN}$config: -${abs_diff} 指令 ($percent%) - 预测更快 ⚡${NC}"
    elif [ $diff -eq 0 ]; then
        echo "   $config: 相同"
    else
        echo -e "   ${YELLOW}$config: +${diff} 指令 (+$percent%) - 预测更慢${NC}"
    fi
done

echo ""

echo -e "${CYAN}📞 函数调用次数对比 (相对于 baseline，越少越好):${NC}"
echo ""

baseline_calls=${TOTAL_CALLS[baseline]}
for config in "${CONFIG_ORDER[@]}"; do
    if [ "$config" = "baseline" ]; then
        continue
    fi

    success=${SUCCESS_COUNT[$config]}
    if [ $success -eq 0 ]; then
        continue
    fi

    calls=${TOTAL_CALLS[$config]}
    diff=$((calls - baseline_calls))
    percent=$(awk "BEGIN {printf \"%.2f\", ($diff * 100.0) / $baseline_calls}")

    if (( $(awk "BEGIN {print ($diff < 0)}") )); then
        abs_diff=${diff#-}
        echo -e "   ${GREEN}$config: -${abs_diff} calls ($percent%) - 内联效果好${NC}"
    elif [ $diff -eq 0 ]; then
        echo "   $config: 相同"
    else
        echo -e "   ${YELLOW}$config: +${diff} calls (+$percent%)${NC}"
    fi
done

echo ""

# 计算性能得分 (指令数权重70%, call数权重30%)
declare -A PERF_SCORES
for config in "${CONFIG_ORDER[@]}"; do
    if [ "$config" = "baseline" ]; then
        PERF_SCORES[$config]=100
        continue
    fi

    success=${SUCCESS_COUNT[$config]}
    if [ $success -eq 0 ]; then
        PERF_SCORES[$config]=0
        continue
    fi

    instrs=${TOTAL_INSTRS[$config]}
    calls=${TOTAL_CALLS[$config]}

    # 得分：指令越少越好，call越少越好
    instr_score=$(awk "BEGIN {printf \"%.2f\", 100 - ((($instrs - $baseline_instrs) * 100.0) / $baseline_instrs)}")
    call_score=$(awk "BEGIN {printf \"%.2f\", 100 - ((($calls - $baseline_calls) * 100.0) / $baseline_calls)}")
    total_score=$(awk "BEGIN {printf \"%.2f\", ($instr_score * 0.7) + ($call_score * 0.3)}")

    PERF_SCORES[$config]=$total_score
done

# 找出性能得分最高的配置
best_score=-999
best_perf_config=""
for config in "${CONFIG_ORDER[@]}"; do
    if [ "$config" = "baseline" ]; then
        continue
    fi

    score=${PERF_SCORES[$config]}
    if (( $(awk "BEGIN {print ($score > $best_score)}") )); then
        best_score=$score
        best_perf_config=$config
    fi
done

# 推荐配置
echo -e "${GREEN}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║              推荐配置 (基于性能预测指标)                       ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""

if [ -z "$best_perf_config" ]; then
    echo -e "${RED}无法确定最佳配置${NC}"
    exit 1
fi

echo -e "${CYAN}🏆 预测性能最佳: ${GREEN}${best_perf_config}${NC}"
echo "   性能得分: ${best_score}"
echo "   指令数: ${TOTAL_INSTRS[$best_perf_config]} (${best_config} 配置)"
echo "   call数: ${TOTAL_CALLS[$best_perf_config]}"
echo ""

IFS='|' read -r env_vars desc <<< "${CONFIGS[$best_perf_config]}"

case "$best_perf_config" in
    "default")
        echo -e "${GREEN}✓ 推荐: default (默认配置)${NC}"
        echo "  - 阈值: 150"
        echo "  - 热点倍数: 3x"
        echo "  - 使用方式: ./build/minic program.c -O2"
        echo ""
        echo "  优势:"
        echo "    • 指令数优化良好"
        echo "    • 平衡代码大小"
        echo "    • 无需额外配置"
        ;;
    "conservative")
        echo -e "${GREEN}✓ 推荐: conservative (保守配置)${NC}"
        echo "  - 阈值: 70 (类似 GCC)"
        echo "  - 使用方式: MINIC_INLINE_THRESHOLD=70 ./build/minic program.c -O2"
        echo ""
        echo "  优势:"
        echo "    • 代码大小最小"
        echo "    • 编译快速"
        echo "    • 性能仍然优秀"
        ;;
    "aggressive")
        echo -e "${GREEN}✓ 推荐: aggressive (激进配置)${NC}"
        echo "  - 阈值: 225 (类似 LLVM)"
        echo "  - 使用方式: MINIC_INLINE_THRESHOLD=225 ./build/minic program.c -O2"
        echo ""
        echo "  优势:"
        echo "    • 最大化内联"
        echo "    • call 指令最少"
        echo "    • 性能潜力最大"
        ;;
    "balanced")
        echo -e "${GREEN}✓ 推荐: balanced (平衡配置)${NC}"
        echo "  - 阈值: 180"
        echo "  - 使用方式: MINIC_INLINE_THRESHOLD=180 ./build/minic program.c -O2"
        ;;
    "hot5x")
        echo -e "${GREEN}✓ 推荐: hot5x (热点5倍)${NC}"
        echo "  - 热点倍数: 5x"
        echo "  - 使用方式: MINIC_INLINE_HOT_MULTIPLIER=5 ./build/minic program.c -O2"
        echo ""
        echo "  优势:"
        echo "    • 循环内函数大幅优化"
        echo "    • 适合循环密集型代码"
        ;;
esac

echo ""

# 场景化建议
echo -e "${CYAN}📋 不同场景的选择建议:${NC}"
echo ""

echo -e "${BLUE}1. 性能优先场景 (Performance First):${NC}"
echo "   推荐: $best_perf_config"
echo "   指令数最少，预测运行最快"
echo "   使用: $(IFS='|' read -r env_vars desc <<< "${CONFIGS[$best_perf_config]}"; [ "$env_vars" = "0" ] && echo "./build/minic program.c -O2" || echo "$env_vars ./build/minic program.c -O2")"
echo ""

# 找出代码最小的配置
smallest_lines=999999999
smallest_config=""
for config in "${CONFIG_ORDER[@]}"; do
    lines=${TOTAL_LINES[$config]}
    if [ $lines -gt 0 ] && [ $lines -lt $smallest_lines ]; then
        smallest_lines=$lines
        smallest_config=$config
    fi
done

echo -e "${BLUE}2. 代码大小优先场景 (Code Size First):${NC}"
echo "   推荐: $smallest_config"
echo "   代码大小: ${smallest_lines} 行"
echo ""

echo -e "${BLUE}3. 平衡场景 (Balanced):${NC}"
if [ "$best_perf_config" = "default" ] || [ "$best_perf_config" = "balanced" ]; then
    echo "   推荐: $best_perf_config"
else
    echo "   推荐: default"
fi
echo "   性能和大小兼顾"
echo ""

echo -e "${CYAN}📊 性能指标说明:${NC}"
echo ""
echo "  • 指令数: 代码中实际指令数量，越少执行越快"
echo "  • call数: 函数调用指令数，内联后减少"
echo "  • 分支数: 分支指令数量，影响流水线效率"
echo "  • 性能得分 = 指令数得分×70% + call数得分×30%"
echo ""

echo -e "${CYAN}📝 环境变量参考:${NC}"
echo ""
echo "  MINIC_INLINE_THRESHOLD=<value>      # 覆盖默认阈值 (默认: 150)"
echo "  MINIC_INLINE_HOT_MULTIPLIER=<value> # 覆盖热点倍数 (默认: 3)"
echo "  MINIC_DISABLE_PROFITABILITY=1       # 禁用收益检查"
echo "  MINIC_OPT_REMARKS=1                 # 显示优化决策"
echo ""

echo -e "${GREEN}测试完成！${NC}"
