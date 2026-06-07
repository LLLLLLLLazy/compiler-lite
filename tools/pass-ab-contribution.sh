#!/usr/bin/env bash
#
# pass-ab-contribution.sh —— 逐个 pass 的 A/B（leave-one-out）消融，量化每个优化对
# 性能套件的净贡献。思路对标 LLVM 内部 bisect 性能回退：
#   baseline   = 全部优化打开
#   variant(P) = 仅关闭 pass P（其余不变），重新编译并运行整套用例
#   贡献(P)    = 关闭 P 后整套变慢/变快的幅度
#
# 依赖编译器侧的开关：minic 读取环境变量
#   MINIC_DISABLE_PASSES="GVN,LoopVectorize"  —— 跳过注册这些具名 pass
#   MINIC_DUMP_PASSES=1                       —— 把可关闭的 pass 名打到 stderr（本脚本用它自动发现 pass 列表）
#
# 指标：运行时程序往 stderr 打印的 SysY 计时器 "TOTAL: %dH-%dM-%dS-%dus"
#       （仅统计 starttime()/stoptime() 包住的热点区，排除 I/O 与启动开销）；
#       若某用例没有计时器输出则回退到 QEMU 墙钟时间。每个用例取 REPEAT 次运行的最小值。
#
# 用法:
#   ./tools/pass-ab-contribution.sh                 # 全部 62 个 2026_perf 用例 × 全部 pass
#   REPEAT=1 ./tools/pass-ab-contribution.sh        # 快速跑（每用例只跑 1 次，噪声大）
#   TESTS="2026_perf_03_sort1 2026_perf_crc1" ./tools/pass-ab-contribution.sh   # 只跑指定用例
#   PASSES="LoopVectorize SimpleLoopUnroll LoopTiling" ./tools/pass-ab-contribution.sh  # 只消融指定 pass
#
# 主要环境变量（默认与 run-local-riscv64-tests.sh 一致）:
#   MINIC_BIN, RISCV64_GCC_BIN, QEMU_RISCV64_BIN, QEMU_RISCV64_CPU/ARGS,
#   MINIC_RUNTIME_LIB, MINIC_TEST_ROOT, MINIC_RISCV64_RVV(on|off), MINIC_RISCV64_TIMEOUT
#   REPEAT(默认3), SUITE(默认2026_performance), OUT_DIR(默认 tools/ab-results)

set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

MINIC_BIN=${MINIC_BIN:-"${REPO_ROOT}/build/minic"}
RISCV64_GCC_BIN=${RISCV64_GCC_BIN:-"riscv64-linux-gnu-gcc"}
RUNTIME_LIB=${MINIC_RUNTIME_LIB:-"${REPO_ROOT}/tests/libsysy_riscv.a"}
TEST_ROOT=${MINIC_TEST_ROOT:-"${REPO_ROOT}/tests"}
SUITE=${SUITE:-"2026_performance"}
SUITE_DIR="${TEST_ROOT}/${SUITE}"
RVV=${MINIC_RISCV64_RVV:-"on"}
TIMEOUT=${MINIC_RISCV64_TIMEOUT:-30}
REPEAT=${REPEAT:-3}
FRONTEND_ARG=${FRONTEND_ARG:--A}
OUT_DIR=${OUT_DIR:-"${REPO_ROOT}/tools/ab-results"}

unset QEMU_VERSION

QEMU_RISCV64_BIN=${QEMU_RISCV64_BIN:-""}
if [[ -z "${QEMU_RISCV64_BIN}" ]]; then
	if command -v qemu-riscv64-static >/dev/null 2>&1; then
		QEMU_RISCV64_BIN="qemu-riscv64-static"
	else
		QEMU_RISCV64_BIN="qemu-riscv64"
	fi
fi

QEMU_EXTRA=()
if [[ -n "${QEMU_RISCV64_CPU:-}" ]]; then
	QEMU_EXTRA=(-cpu "${QEMU_RISCV64_CPU}")
elif [[ -n "${QEMU_RISCV64_ARGS:-}" ]]; then
	read -r -a QEMU_EXTRA <<<"${QEMU_RISCV64_ARGS}"
fi

if [[ "${RVV}" == "on" ]]; then
	GCC_ARCH=(-march=rv64gcv)
else
	GCC_ARCH=(-march=rv64gc)
fi

# ---- 前置检查 ----------------------------------------------------------------
die() { echo "ERROR: $*" >&2; exit 1; }
[[ -x "${MINIC_BIN}" ]] || die "找不到编译器: ${MINIC_BIN}（请先构建）"
command -v "${RISCV64_GCC_BIN}" >/dev/null 2>&1 || die "找不到 riscv64 gcc: ${RISCV64_GCC_BIN}"
command -v "${QEMU_RISCV64_BIN}" >/dev/null 2>&1 || die "找不到 qemu: ${QEMU_RISCV64_BIN}"
[[ -f "${RUNTIME_LIB}" ]] || die "找不到运行时库: ${RUNTIME_LIB}"
[[ -d "${SUITE_DIR}" ]] || die "找不到测试套件目录: ${SUITE_DIR}"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/pass-ab.XXXXXX")
trap 'rm -rf "${WORK}"' EXIT
mkdir -p "${OUT_DIR}"

# ---- 收集用例 ----------------------------------------------------------------
declare -a TESTCASES=()
if [[ -n "${TESTS:-}" ]]; then
	read -r -a TESTCASES <<<"${TESTS}"
else
	while IFS= read -r f; do
		b=$(basename "${f}")
		TESTCASES+=("${b%.*}")
	done < <(find "${SUITE_DIR}" -maxdepth 1 -type f \( -name '*.sy' -o -name '*.c' \) | sort -u)
fi
[[ ${#TESTCASES[@]} -gt 0 ]] || die "套件 ${SUITE} 下没有用例"

# ---- 自动发现可关闭的 pass 列表 ---------------------------------------------
discover_passes() {
	local probe="${WORK}/__probe.sy"
	printf 'int main(){ return 0; }\n' >"${probe}"
	MINIC_DUMP_PASSES=1 MINIC_DISABLE_PASSES="" \
		"${MINIC_BIN}" -S "${FRONTEND_ARG}" --riscv64-rvv="${RVV}" -O1 -t RISCV64 \
		-o "${WORK}/__probe.s" "${probe}" 2>"${WORK}/__probe.passes" >/dev/null
	grep -E '^PASS:' "${WORK}/__probe.passes" 2>/dev/null |
		sed -E 's/^PASS:([^:]+):.*/\1/' | awk '!seen[$0]++'
}

declare -a PASSLIST=()
if [[ -n "${PASSES:-}" ]]; then
	read -r -a PASSLIST <<<"${PASSES}"
else
	while IFS= read -r p; do
		[[ -n "${p}" ]] && PASSLIST+=("${p}")
	done < <(discover_passes)
fi
if [[ ${#PASSLIST[@]} -eq 0 ]]; then
	die "未能发现任何可关闭的 pass。请确认 minic 已重新构建并支持 MINIC_DISABLE_PASSES/MINIC_DUMP_PASSES。"
fi

# ---- 工具函数 ----------------------------------------------------------------
# 解析 stderr 里的 SysY 计时器总时间（微秒）；无则返回空
parse_total_us() {
	local line
	line=$(grep -E '^TOTAL:' "$1" 2>/dev/null | tail -1)
	[[ -z "${line}" ]] && { echo ""; return 1; }
	if [[ "${line}" =~ ([0-9]+)H-([0-9]+)M-([0-9]+)S-([0-9]+)us ]]; then
		local h=${BASH_REMATCH[1]} m=${BASH_REMATCH[2]} s=${BASH_REMATCH[3]} us=${BASH_REMATCH[4]}
		echo $(( (( h * 60 + m ) * 60 + s ) * 1000000 + us ))
		return 0
	fi
	echo ""; return 1
}

# 复刻 run-local-riscv64-tests.sh 的判分语义：stdout(补尾换行) + 退出码 行，与 .out 比对
result_matches_expected() {
	local output="$1" exit_code="$2" expected="$3" result="${WORK}/__result"
	if [[ -s "${output}" ]]; then
		cp "${output}" "${result}"
		local last
		last=$(tail -c 1 "${result}" | od -An -t u1 | tr -d '[:space:]')
		[[ "${last}" != "10" ]] && printf '\n' >>"${result}"
	else
		: >"${result}"
	fi
	printf '%s\n' "${exit_code}" >>"${result}"
	cmp -s <(sed 's/\r$//' "${result}") <(sed 's/\r$//' "${expected}")
}

# 测一个配置（$1=要关闭的 pass，空串表示 baseline）下整套用例，结果写入 $2: "test\tok\tus"
measure_config() {
	local disable="$1" outfile="$2"
	: >"${outfile}"
	local idx=0 total=${#TESTCASES[@]}
	for tc in "${TESTCASES[@]}"; do
		idx=$((idx + 1))
		local src="" e
		for e in sy c; do
			[[ -f "${SUITE_DIR}/${tc}.${e}" ]] && src="${SUITE_DIR}/${tc}.${e}" && break
		done
		local infile="${SUITE_DIR}/${tc}.in"
		local outexp="${SUITE_DIR}/${tc}.out"
		local asm="${WORK}/${tc}.s" exe="${WORK}/${tc}.exe"
		local prog_out="${WORK}/${tc}.out" prog_err="${WORK}/${tc}.err"

		printf '\r  [%s] %3d/%3d %-32s' "${disable:-baseline}" "${idx}" "${total}" "${tc}" >&2

		if [[ -z "${src}" || ! -f "${outexp}" ]]; then
			printf '%s\t%s\t%s\n' "${tc}" "missing" "0" >>"${outfile}"
			continue
		fi

		# 编译（关闭指定 pass）；不开 DUMP，避免污染
		if ! MINIC_DISABLE_PASSES="${disable}" timeout --foreground "${TIMEOUT}" \
			"${MINIC_BIN}" -S "${FRONTEND_ARG}" --riscv64-rvv="${RVV}" -O1 -t RISCV64 \
			-o "${asm}" "${src}" >/dev/null 2>&1 || [[ ! -s "${asm}" ]]; then
			printf '%s\t%s\t%s\n' "${tc}" "compile_ng" "0" >>"${outfile}"
			continue
		fi
		# 汇编+链接
		if ! timeout --foreground "${TIMEOUT}" "${RISCV64_GCC_BIN}" "${GCC_ARCH[@]}" \
			-static -o "${exe}" "${asm}" "${RUNTIME_LIB}" >/dev/null 2>&1; then
			printf '%s\t%s\t%s\n' "${tc}" "link_ng" "0" >>"${outfile}"
			continue
		fi

		# 运行 REPEAT 次，取最小指标；首次同时判正确性
		local best="" ok="ng" r metric
		for ((r = 1; r <= REPEAT; r++)); do
			local t0 t1 ec
			t0=$(date +%s%N)
			if [[ -f "${infile}" ]]; then
				timeout --foreground "${TIMEOUT}" "${QEMU_RISCV64_BIN}" "${QEMU_EXTRA[@]}" "${exe}" \
					<"${infile}" >"${prog_out}" 2>"${prog_err}"
				ec=$?
			else
				timeout --foreground "${TIMEOUT}" "${QEMU_RISCV64_BIN}" "${QEMU_EXTRA[@]}" "${exe}" \
					>"${prog_out}" 2>"${prog_err}"
				ec=$?
			fi
			t1=$(date +%s%N)
			# 超时/无法执行视为失败
			if [[ ${ec} -eq 124 || ${ec} -eq 125 || ${ec} -eq 126 || ${ec} -eq 127 ]]; then
				ok="timeout"; best=""; break
			fi
			if [[ ${r} -eq 1 ]]; then
				if result_matches_expected "${prog_out}" "${ec}" "${outexp}"; then ok="ok"; else ok="wrong"; best=""; break; fi
			fi
			# 指标：优先 SysY 计时器，否则墙钟(ms→us)
			metric=$(parse_total_us "${prog_err}")
			if [[ -z "${metric}" || "${metric}" -le 0 ]]; then
				metric=$(( (t1 - t0) / 1000 ))
			fi
			if [[ -z "${best}" || "${metric}" -lt "${best}" ]]; then best="${metric}"; fi
		done

		if [[ "${ok}" == "ok" && -n "${best}" ]]; then
			printf '%s\t%s\t%s\n' "${tc}" "ok" "${best}" >>"${outfile}"
		else
			printf '%s\t%s\t%s\n' "${tc}" "${ok}" "0" >>"${outfile}"
		fi
	done
	printf '\r%*s\r' 60 '' >&2
}

# ---- 开跑 --------------------------------------------------------------------
echo "==============================================================" >&2
echo " Pass A/B 贡献分析 (leave-one-out)" >&2
echo "   套件      : ${SUITE}  (${#TESTCASES[@]} 个用例)" >&2
echo "   可关闭pass: ${#PASSLIST[@]} 个 -> ${PASSLIST[*]}" >&2
echo "   每用例重复: ${REPEAT} 次取最小   RVV=${RVV}" >&2
echo "   QEMU      : ${QEMU_RISCV64_BIN} ${QEMU_EXTRA[*]:-}" >&2
echo "==============================================================" >&2

echo "[*] 测 baseline（全部优化打开）..." >&2
BASE_TSV="${WORK}/baseline.tsv"
measure_config "" "${BASE_TSV}"

base_ok=0; base_total=0; base_sum=0
while IFS=$'\t' read -r t ok us; do
	base_total=$((base_total + 1))
	[[ "${ok}" == "ok" ]] && { base_ok=$((base_ok + 1)); base_sum=$((base_sum + us)); }
done <"${BASE_TSV}"
echo "    baseline: ${base_ok}/${base_total} 用例通过, 计时合计=${base_sum}us" >&2
[[ ${base_ok} -gt 0 ]] || die "baseline 没有任何用例通过，请先确认套件本身能跑通。"

# 逐个 pass 消融，生成表行
ROWS="${WORK}/rows.tsv"
: >"${ROWS}"
i=0
for p in "${PASSLIST[@]}"; do
	i=$((i + 1))
	echo "[*] (${i}/${#PASSLIST[@]}) 关闭 ${p} ..." >&2
	VAR_TSV="${WORK}/var_${p}.tsv"
	measure_config "${p}" "${VAR_TSV}"

	# 仅在 baseline 与 variant 都通过的用例上对比（apples-to-apples）；统计被关掉后变挂的用例数
	awk -F'\t' -v pass="${p}" '
		NR==FNR { bok[$1]=$2; bus[$1]=$3; next }
		{
			vok[$1]=$2; vus[$1]=$3
		}
		END {
			sumb=0; sumv=0; common=0; broke=0; sumln=0; gcnt=0
			for (t in bok) {
				if (bok[t]=="ok" && vok[t]=="ok") {
					common++; sumb+=bus[t]; sumv+=vus[t]
					if (bus[t]>0 && vus[t]>0) { sumln += log(vus[t]/bus[t]); gcnt++ }
				} else if (bok[t]=="ok" && vok[t]!="ok") {
					broke++
				}
			}
			gm = (gcnt>0) ? exp(sumln/gcnt) : 1.0       # 几何平均(variant/baseline)
			gmpct = (gm-1.0)*100.0                       # >0: 关掉它整套变慢 => 该pass有正贡献
			delta = sumv - sumb                          # 绝对时间差(us)
			dpct = (sumb>0) ? (delta*100.0/sumb) : 0.0
			# verdict
			v="~neutral"
			if (broke>0) v="UNSAFE(breaks)"
			else if (gmpct >= 1.0) v="HELPS"
			else if (gmpct <= -1.0) v="HURTS(regressor)"
			printf "%s\t%.2f\t%d\t%.2f\t%d\t%d\t%s\n", pass, gmpct, delta, dpct, broke, common, v
		}
	' "${BASE_TSV}" "${VAR_TSV}" >>"${ROWS}"
done

# ---- 产出表格 ----------------------------------------------------------------
# 排序：先把会 break 的排前面（重要），再按 |几何平均贡献%| 降序
SORTED="${WORK}/sorted.tsv"
awk -F'\t' '{ a=$2; if(a<0)a=-a; printf "%d\t%.4f\t%s\n",($5>0?1:0),a,$0 }' "${ROWS}" \
	| sort -t$'\t' -k1,1nr -k2,2nr | cut -f3- >"${SORTED}"

MD="${OUT_DIR}/contribution.md"
CSV="${OUT_DIR}/contribution.csv"

{
	echo "# Pass 贡献表 (leave-one-out, 套件=${SUITE})"
	echo
	echo "- baseline: ${base_ok}/${base_total} 用例通过, 计时合计 = ${base_sum} us"
	echo "- 指标: SysY 计时器总微秒(无则墙钟), 每用例取 ${REPEAT} 次最小; 仅在双方都通过的用例上对比"
	echo "- **贡献%(geomean)**: 关闭该 pass 后整套相对 baseline 的几何平均变化。**正=该 pass 在帮忙(关掉变慢); 负=该 pass 在拖后腿(关掉反而变快, 是回退元凶)**"
	echo "- Δus: 关闭后双方共同通过用例的绝对时间差(正=变慢); breaks: 关闭后从通过变为失败的用例数"
	echo
	printf '| %-22s | %12s | %12s | %8s | %7s | %7s | %-16s |\n' "Pass" "贡献%(gm)" "Δus" "Δ%" "breaks" "common" "verdict"
	printf '| %s | %s | %s | %s | %s | %s | %s |\n' "----------------------" "------------" "------------" "--------" "-------" "-------" "----------------"
	while IFS=$'\t' read -r pass gmpct delta dpct broke common verdict; do
		printf '| %-22s | %+12.2f | %+12d | %+7.2f%% | %7d | %7d | %-16s |\n' \
			"${pass}" "${gmpct}" "${delta}" "${dpct}" "${broke}" "${common}" "${verdict}"
	done <"${SORTED}"
} | tee "${MD}"

{
	echo "pass,contrib_geomean_pct,delta_us,delta_pct,breaks,common,verdict"
	while IFS=$'\t' read -r pass gmpct delta dpct broke common verdict; do
		echo "${pass},${gmpct},${delta},${dpct},${broke},${common},${verdict}"
	done <"${SORTED}"
} >"${CSV}"

echo >&2
echo "[OK] 贡献表已写入:" >&2
echo "       ${MD}" >&2
echo "       ${CSV}" >&2
echo "解读: verdict=HURTS(regressor) 的 pass 就是需要加收益门/相位调整的对象;" >&2
echo "      verdict=~neutral 多为 canonicalization/enabler, 应按组而非单独评估;" >&2
echo "      verdict=UNSAFE(breaks) 说明后续阶段对它有隐式依赖。" >&2
