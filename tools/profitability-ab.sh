#!/usr/bin/env bash
#
# profitability-ab.sh —— 对比“收益门 ON vs OFF”在性能套件上的效果。
#   OFF = MINIC_DISABLE_PROFITABILITY=1(等同未加收益门的历史行为)
#   ON  = 默认(CostModel 收益门全开)
# 对每个用例两套各编译运行 REPEAT 次取最小 SysY 计时器(TOTAL us)，同时校验 md5 正确性。
#
# 用法:
#   bash tools/profitability-ab.sh                 # 全部 2026_perf
#   REPEAT=3 SUITE=2026_performance bash tools/profitability-ab.sh
#   TESTS="2026_perf_matmul1 2026_perf_fft1" bash tools/profitability-ab.sh

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
TIMEOUT=${MINIC_RISCV64_TIMEOUT:-20}
REPEAT=${REPEAT:-3}
FRONTEND_ARG=${FRONTEND_ARG:--A}

unset QEMU_VERSION
QEMU_RISCV64_BIN=${QEMU_RISCV64_BIN:-""}
if [[ -z "${QEMU_RISCV64_BIN}" ]]; then
	command -v qemu-riscv64-static >/dev/null 2>&1 && QEMU_RISCV64_BIN="qemu-riscv64-static" || QEMU_RISCV64_BIN="qemu-riscv64"
fi
QEMU_EXTRA=()
[[ -n "${QEMU_RISCV64_CPU:-}" ]] && QEMU_EXTRA=(-cpu "${QEMU_RISCV64_CPU}")
[[ "${RVV}" == "on" ]] && GCC_ARCH=(-march=rv64gcv) || GCC_ARCH=(-march=rv64gc)

WORK=$(mktemp -d "${TMPDIR:-/tmp}/prof-ab.XXXXXX")
trap 'rm -rf "${WORK}"' EXIT

declare -a TESTCASES=()
if [[ -n "${TESTS:-}" ]]; then
	read -r -a TESTCASES <<<"${TESTS}"
else
	while IFS= read -r f; do b=$(basename "${f}"); TESTCASES+=("${b%.*}"); done \
		< <(find "${SUITE_DIR}" -maxdepth 1 -type f \( -name '*.sy' -o -name '*.c' \) | sort -u)
fi

parse_total_us() {
	local line; line=$(grep -E '^TOTAL:' "$1" 2>/dev/null | tail -1)
	[[ -z "${line}" ]] && { echo ""; return 1; }
	if [[ "${line}" =~ ([0-9]+)H-([0-9]+)M-([0-9]+)S-([0-9]+)us ]]; then
		echo $(( (( ${BASH_REMATCH[1]} * 60 + ${BASH_REMATCH[2]} ) * 60 + ${BASH_REMATCH[3]} ) * 1000000 + ${BASH_REMATCH[4]} )); return 0
	fi
	echo ""; return 1
}

result_ok() {  # $1 output $2 exit_code $3 expected
	local r="${WORK}/__r"
	if [[ -s "$1" ]]; then cp "$1" "${r}"; [[ "$(tail -c1 "${r}" | od -An -tu1 | tr -d '[:space:]')" != "10" ]] && printf '\n' >>"${r}"; else : >"${r}"; fi
	printf '%s\n' "$2" >>"${r}"
	cmp -s <(sed 's/\r$//' "${r}") <(sed 's/\r$//' "$3")
}

# 测一个配置: $1=DISABLE_PROFIT(0/1) 回显 "ok|fail us"
measure() {  # $1 disableProfit $2 src $3 in $4 expected $5 tag
	local disable="$1" src="$2" infile="$3" exp="$4" tag="$5"
	local asm="${WORK}/${tag}.s" exe="${WORK}/${tag}.exe" o="${WORK}/${tag}.o" e="${WORK}/${tag}.e"
	local envp=()
	[[ "${disable}" == "1" ]] && envp=(MINIC_DISABLE_PROFITABILITY=1)
	if ! env "${envp[@]}" timeout --foreground "${TIMEOUT}" "${MINIC_BIN}" -S "${FRONTEND_ARG}" --riscv64-rvv="${RVV}" -O1 -t RISCV64 -o "${asm}" "${src}" >/dev/null 2>&1 || [[ ! -s "${asm}" ]]; then
		echo "compile_ng 0"; return; fi
	if ! timeout --foreground "${TIMEOUT}" "${RISCV64_GCC_BIN}" "${GCC_ARCH[@]}" -static -o "${exe}" "${asm}" "${RUNTIME_LIB}" >/dev/null 2>&1; then
		echo "link_ng 0"; return; fi
	local best="" ok="ng" r ec metric
	for ((r=1;r<=REPEAT;r++)); do
		if [[ -f "${infile}" ]]; then timeout --foreground "${TIMEOUT}" "${QEMU_RISCV64_BIN}" "${QEMU_EXTRA[@]}" "${exe}" <"${infile}" >"${o}" 2>"${e}"; ec=$?
		else timeout --foreground "${TIMEOUT}" "${QEMU_RISCV64_BIN}" "${QEMU_EXTRA[@]}" "${exe}" >"${o}" 2>"${e}"; ec=$?; fi
		[[ ${ec} -eq 124 || ${ec} -ge 125 ]] && { ok="timeout"; best=""; break; }
		if [[ ${r} -eq 1 ]]; then result_ok "${o}" "${ec}" "${exp}" && ok="ok" || { ok="wrong"; best=""; break; }; fi
		metric=$(parse_total_us "${e}"); [[ -z "${metric}" || "${metric}" -le 0 ]] && metric=1
		[[ -z "${best}" || "${metric}" -lt "${best}" ]] && best="${metric}"
	done
	[[ "${ok}" == "ok" && -n "${best}" ]] && echo "ok ${best}" || echo "${ok} 0"
}

echo "== 收益门 ON vs OFF @ ${SUITE} (${#TESTCASES[@]} 用例, REPEAT=${REPEAT}) ==" >&2
printf '%-34s %12s %12s %10s\n' "test" "OFF(us)" "ON(us)" "Δ%"
TSV="${WORK}/r.tsv"; : >"${TSV}"
idx=0
for tc in "${TESTCASES[@]}"; do
	idx=$((idx+1))
	src=""; for ext in sy c; do [[ -f "${SUITE_DIR}/${tc}.${ext}" ]] && src="${SUITE_DIR}/${tc}.${ext}" && break; done
	exp="${SUITE_DIR}/${tc}.out"; infile="${SUITE_DIR}/${tc}.in"
	[[ -z "${src}" || ! -f "${exp}" ]] && continue
	printf '\r  [%d/%d] %-30s' "${idx}" "${#TESTCASES[@]}" "${tc}" >&2
	read -r offok offus < <(measure 1 "${src}" "${infile}" "${exp}" "${tc}_off")
	read -r onok  onus  < <(measure 0 "${src}" "${infile}" "${exp}" "${tc}_on")
	printf '%s\t%s\t%s\t%s\t%s\n' "${tc}" "${offok}" "${offus}" "${onok}" "${onus}" >>"${TSV}"
	if [[ "${offok}" == "ok" && "${onok}" == "ok" ]]; then
		pct=$(awk -v a="${offus}" -v b="${onus}" 'BEGIN{ if(a>0) printf "%+.2f", (b-a)*100.0/a; else print "n/a" }')
		printf '%-34s %12s %12s %9s%%\n' "${tc}" "${offus}" "${onus}" "${pct}"
	else
		printf '%-34s %12s %12s   [off=%s on=%s]\n' "${tc}" "${offus}" "${onus}" "${offok}" "${onok}"
	fi
done
printf '\r%*s\r' 50 '' >&2

echo ""
awk -F'\t' '
	{ offok=$2; offus=$3; onok=$4; onus=$5
	  if(offok=="ok") offpass++
	  if(onok=="ok") onpass++
	  if(offok=="ok" && onok!="ok") regress++
	  if(offok=="ok" && onok=="ok"){ sumoff+=offus; sumon+=onus; if(offus>0&&onus>0){sumln+=log(onus/offus); n++} }
	}
	END{
	  gm=(n>0)?exp(sumln/n):1
	  printf "OFF 通过=%d  ON 通过=%d  (ON 因门变挂的用例=%d, 应为0)\n", offpass, onpass, regress+0
	  printf "共同通过 %d 用例: OFF 合计=%d us, ON 合计=%d us, Δ=%+d us (%+.2f%%)\n", n, sumoff, sumon, sumon-sumoff, (sumoff>0?(sumon-sumoff)*100.0/sumoff:0)
	  printf "几何平均 ON/OFF = %.4f  => 收益门使整套%s %.2f%%\n", gm, (gm<1?"提速":"变慢"), (gm<1?(1-gm)*100:(gm-1)*100)
	}' "${TSV}"
