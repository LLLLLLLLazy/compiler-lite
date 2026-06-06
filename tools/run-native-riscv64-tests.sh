#!/usr/bin/env bash

set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

MINIC_BIN=${MINIC_BIN:-"${REPO_ROOT}/build/minic"}
RISCV64_GCC_BIN=${RISCV64_GCC_BIN:-"gcc"}
TEST_ROOT=${MINIC_TEST_ROOT:-"${REPO_ROOT}/tests"}
RUNTIME_LIB=${MINIC_RUNTIME_LIB:-"${REPO_ROOT}/tests/libsysy_riscv.a"}
FRONTEND=${MINIC_FRONTEND:-"antlr"}
TEST_MODE=${MINIC_RISCV64_TEST_MODE:-"asm"}
RISCV64_TIMEOUT=${MINIC_RISCV64_TIMEOUT:-30}
MINIC_RISCV64_RVV=${MINIC_RISCV64_RVV:-"on"}
PARALLEL_JOBS=${MINIC_RISCV64_PARALLEL:-1}
LINK_STATIC=${MINIC_RISCV64_LINK_STATIC:-1}
EXTRA_GCC_ARGS=${MINIC_RISCV64_GCC_ARGS:-""}
SKIP_RVV_PROBE=${MINIC_RISCV64_SKIP_RVV_PROBE:-0}

OK_NUM=0
NG_NUM=0
TOTAL_RUN=0
STATUS_COL_WIDTH=50

TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/minic-rv64-native-tests.XXXXXX")
trap 'rm -rf "${TMP_DIR}"' EXIT

usage() {
	cat <<'USAGE'
Usage:
  ./tools/run-native-riscv64-tests.sh
  ./tools/run-native-riscv64-tests.sh <suite>
  ./tools/run-native-riscv64-tests.sh <suite> <testcase>
  ./tools/run-native-riscv64-tests.sh <testcase>

Run on a riscv64 Linux board directly. No QEMU is used.

Suites:
  2023              -> tests/2023_function
  2025              -> tests/2025_function
  2025_perf         -> tests/2025_performance
  2025_performance  -> tests/2025_performance
  2026              -> tests/2026_function
  2026_perf         -> tests/2026_performance
  2026_performance  -> tests/2026_performance
  all               -> all suites above

Environment:
  MINIC_BIN=./build/minic
  MINIC_FRONTEND=antlr|recursive|default
  MINIC_RISCV64_TEST_MODE=asm       Generate asm, link, run natively, compare .out md5 (default)
  MINIC_RISCV64_TEST_MODE=assemble  Generate asm and assemble only
  MINIC_RISCV64_TIMEOUT=30          Per-step timeout passed to timeout(1)
  MINIC_RISCV64_PARALLEL=1          Number of parallel jobs; keep 1 for stable perf timings
  MINIC_RISCV64_RVV=on|off          Pass --riscv64-rvv to minic (default: on)
  MINIC_RISCV64_LINK_STATIC=1|0     Link with -static by default
  MINIC_RISCV64_GCC_ARGS="..."      Extra gcc args, e.g. "-mabi=lp64d"
  MINIC_RISCV64_SKIP_RVV_PROBE=1    Skip the native RVV availability probe
  RISCV64_GCC_BIN=gcc               Native compiler on the board
  MINIC_TEST_ROOT=./tests
  MINIC_RUNTIME_LIB=./tests/libsysy_riscv.a

Examples:
  ./tools/run-native-riscv64-tests.sh 2025
  ./tools/run-native-riscv64-tests.sh 2025_perf
  ./tools/run-native-riscv64-tests.sh 2025_perf 2025_perf_h-2-01.c
  MINIC_RISCV64_RVV=off ./tools/run-native-riscv64-tests.sh 2025_perf
USAGE
}

fail_with_usage() {
	echo "$1" >&2
	usage >&2
	exit 1
}

frontend_args=()
case "${FRONTEND}" in
	antlr)
		frontend_args=(-A)
		;;
	recursive)
		frontend_args=(-D)
		;;
	default)
		frontend_args=()
		;;
	*)
		fail_with_usage "Unknown MINIC_FRONTEND: ${FRONTEND}"
		;;
esac

case "${TEST_MODE}" in
	asm|assemble)
		;;
	*)
		fail_with_usage "Unknown MINIC_RISCV64_TEST_MODE: ${TEST_MODE}"
		;;
esac

case "${MINIC_RISCV64_RVV}" in
	on|off)
		;;
	*)
		fail_with_usage "Unknown MINIC_RISCV64_RVV: ${MINIC_RISCV64_RVV}"
		;;
esac

# 将脚本级 RVV 开关同时传给 minic 和本机 gcc，保证生成、汇编、链接的目标一致。
rvv_args=(--riscv64-rvv="${MINIC_RISCV64_RVV}")
gcc_arch_args=(-march="rv64gc")
if [[ "${MINIC_RISCV64_RVV}" == "on" ]]; then
	gcc_arch_args=(-march="rv64gcv")
fi
if [[ -n "${EXTRA_GCC_ARGS}" ]]; then
	read -r -a extra_gcc_args <<< "${EXTRA_GCC_ARGS}"
	gcc_arch_args+=("${extra_gcc_args[@]}")
fi

link_args=()
if [[ "${LINK_STATIC}" == "1" ]]; then
	link_args=(-static)
elif [[ "${LINK_STATIC}" != "0" ]]; then
	fail_with_usage "Unknown MINIC_RISCV64_LINK_STATIC: ${LINK_STATIC}"
fi

suite_dir_from_key() {
	case "$1" in
		2023|2023_function)
			echo "2023_function"
			;;
		2025|2025_function)
			echo "2025_function"
			;;
		2025_perf|2025_performance)
			echo "2025_performance"
			;;
		2026|2026_function)
			echo "2026_function"
			;;
		2026_perf|2026_performance)
			echo "2026_performance"
			;;
		*)
			return 1
			;;
	esac
}

infer_suite_from_testcase() {
	local testcase="${1%.c}"
	testcase="${testcase%.sy}"

	case "${testcase}" in
		2023_func_*)
			echo "2023_function"
			;;
		2025_func_*)
			echo "2025_function"
			;;
		2025_perf_*)
			echo "2025_performance"
			;;
		2026_func_*)
			echo "2026_function"
			;;
		2026_perf_*)
			echo "2026_performance"
			;;
		*)
			return 1
			;;
	esac
}

strip_source_ext() {
	local testcase="$1"
	testcase="${testcase%.c}"
	testcase="${testcase%.sy}"
	echo "${testcase}"
}

find_source_file() {
	local case_root="$1"
	local testcase_arg="$2"
	local testcase

	case "${testcase_arg}" in
		*.c|*.sy)
			if [[ -f "${case_root}/${testcase_arg}" ]]; then
				echo "${case_root}/${testcase_arg}"
				return 0
			fi
			;;
	esac

	testcase=$(strip_source_ext "${testcase_arg}")
	if [[ -f "${case_root}/${testcase}.c" ]]; then
		echo "${case_root}/${testcase}.c"
		return 0
	fi
	if [[ -f "${case_root}/${testcase}.sy" ]]; then
		echo "${case_root}/${testcase}.sy"
		return 0
	fi
	return 1
}

write_result_file() {
	local output_file="$1"
	local exit_code="$2"
	local result_file="$3"

	if [[ -f "${output_file}" ]]; then
		cp "${output_file}" "${result_file}"
		if [[ -s "${result_file}" ]]; then
			local last_byte
			last_byte=$(tail -c 1 "${result_file}" | od -An -t u1 | tr -d '[:space:]')
			if [[ "${last_byte}" != "10" ]]; then
				printf '\n' >> "${result_file}"
			fi
		fi
	else
		: > "${result_file}"
	fi
	printf '%s\n' "${exit_code}" >> "${result_file}"
}

compute_md5() {
	local file="$1"
	if command -v md5sum >/dev/null 2>&1; then
		md5sum "${file}" | awk '{print $1}'
		return 0
	fi
	if command -v md5 >/dev/null 2>&1; then
		md5 -q "${file}"
		return 0
	fi
	echo ""
	return 1
}

normalized_text_files_equal() {
	local lhs="$1"
	local rhs="$2"
	cmp -s <(sed 's/\r$//' "${lhs}") <(sed 's/\r$//' "${rhs}")
}

file_size_bytes() {
	wc -c < "$1" | tr -d '[:space:]'
}

tail_bytes_hex() {
	local file="$1"
	local bytes
	bytes=$(tail -c 16 "${file}" 2>/dev/null | od -An -tx1 -v | tr '\n' ' ' | sed 's/[[:space:]]\+/ /g; s/^ //; s/ $//')
	if [[ -z "${bytes}" ]]; then
		echo "<empty>"
		return 0
	fi
	echo "${bytes}"
}

cpuinfo_has_full_rvv() {
	if [[ ! -r /proc/cpuinfo ]]; then
		return 2
	fi

	# Linux 暴露的 isa 字段若没有 v 扩展，运行 RVV 指令通常会直接 SIGILL。
	local line isa base saw_isa=0
	while IFS= read -r line; do
		case "${line}" in
			isa[[:space:]]*:*)
				saw_isa=1
				isa="${line#*:}"
				isa="${isa//[[:space:]]/}"
				base="${isa%%_*}"
				if [[ "${base}" == rv64*v* ]]; then
					return 0
				fi
				;;
		esac
	done < /proc/cpuinfo

	if [[ "${saw_isa}" == "1" ]]; then
		return 1
	fi
	return 2
}

check_native_rvv_available() {
	if [[ "${MINIC_RISCV64_RVV}" != "on" || "${TEST_MODE}" == "assemble" || "${SKIP_RVV_PROBE}" == "1" ]]; then
		return 0
	fi

	# 先查 cpuinfo，再执行最小 RVV 探针，区分工具链不支持和硬件/内核不支持。
	if cpuinfo_has_full_rvv; then
		:
	else
		local cpuinfo_status=$?
		if [[ "${cpuinfo_status}" == "1" ]]; then
			echo "RVV requested, but /proc/cpuinfo does not advertise the full RISC-V V extension." >&2
			echo "This board/kernel will raise SIGILL for code compiled with --riscv64-rvv=on." >&2
			echo "Use MINIC_RISCV64_RVV=off, or run on hardware and a kernel with RVV enabled." >&2
			echo "Relevant /proc/cpuinfo isa lines:" >&2
			grep -E '^isa[[:space:]]*:' /proc/cpuinfo >&2 || true
			exit 1
		fi
	fi

	local probe_s="${TMP_DIR}/rvv-probe.s"
	local probe_exe="${TMP_DIR}/rvv-probe"
	local probe_stderr="${TMP_DIR}/rvv-probe.stderr"

	# 探针只执行 vsetvli，足以验证当前机器能否进入 RVV 指令路径。
	cat > "${probe_s}" <<'EOF'
	.text
	.globl _start
_start:
	vsetvli zero, zero, e32, m1, ta, ma
	li a7, 93
	li a0, 0
	ecall
EOF

	if ! "${RISCV64_GCC_BIN}" "${gcc_arch_args[@]}" -nostdlib -static -o "${probe_exe}" "${probe_s}" > /dev/null 2> "${probe_stderr}"; then
		echo "RVV probe compile/link failed. Toolchain may not support ${gcc_arch_args[*]}." >&2
		sed -n '1,8p' "${probe_stderr}" >&2
		exit 1
	fi

	timeout --foreground 5 "${probe_exe}" > /dev/null 2> "${probe_stderr}"
	local probe_status=$?
	if [[ "${probe_status}" -ne 0 ]]; then
		echo "RVV requested, but a native RVV probe failed with exit status ${probe_status}." >&2
		echo "Status 132 normally means SIGILL: CPU or kernel cannot execute RVV instructions." >&2
		echo "Use MINIC_RISCV64_RVV=off, or run on hardware and a kernel with RVV enabled." >&2
		if [[ -s "${probe_stderr}" ]]; then
			sed -n '1,8p' "${probe_stderr}" >&2
		fi
		exit 1
	fi
}

run_native_check() {
	local cfile="$1"
	local infile="$2"
	local outfile="$3"
	local testcase="$4"
	local is_perf_test="$5"
	local result_dir="$6"
	local source_name
	local asmfile="${result_dir}/${testcase}.rv64.s"
	local objfile="${result_dir}/${testcase}.rv64.o"
	local exe_file="${result_dir}/${testcase}.rv64"
	local output_file="${result_dir}/${testcase}.rv64.output"
	local stderr_file="${result_dir}/${testcase}.rv64.stderr"
	local result_file="${result_dir}/${testcase}.rv64.result"
	local exit_code=0
	local t0 t1 t_compile=0 t_assemble=0 t_link=0 t_run=0
	source_name=$(basename "${cfile}")

	local opt_level="0"
	if [[ "${is_perf_test}" == "1" ]]; then
		# 性能测试默认开启 O1，和 QEMU 版回归脚本保持一致。
		opt_level="1"
	fi

	t0=$(date +%s%N)
	if ! timeout --foreground "${RISCV64_TIMEOUT}" "${MINIC_BIN}" -S "${frontend_args[@]}" "${rvv_args[@]}" -O"${opt_level}" -t RISCV64 -o "${asmfile}" "${cfile}" >/dev/null 2>&1; then
		t1=$(date +%s%N)
		t_compile=$(( (t1 - t0) / 1000000 ))
		echo "${source_name} compile NG [riscv64-native]  compile=${t_compile}ms"
		return 1
	fi
	t1=$(date +%s%N)
	t_compile=$(( (t1 - t0) / 1000000 ))

	if [[ ! -s "${asmfile}" ]]; then
		echo "${asmfile} not generated [riscv64-native]  compile=${t_compile}ms"
		return 1
	fi

	if [[ "${TEST_MODE}" == "assemble" ]]; then
		t0=$(date +%s%N)
		if ! timeout --foreground "${RISCV64_TIMEOUT}" "${RISCV64_GCC_BIN}" "${gcc_arch_args[@]}" -c -o "${objfile}" "${asmfile}" >/dev/null 2>&1; then
			t1=$(date +%s%N)
			t_assemble=$(( (t1 - t0) / 1000000 ))
			echo "${source_name} assemble NG [riscv64-native]  compile=${t_compile}ms assemble=${t_assemble}ms"
			return 1
		fi
		t1=$(date +%s%N)
		t_assemble=$(( (t1 - t0) / 1000000 ))
		printf "%-${STATUS_COL_WIDTH}s %s\n" "${source_name} OK [riscv64-native-assemble]" "compile=${t_compile}ms assemble=${t_assemble}ms"
		return 0
	fi

	t0=$(date +%s%N)
	if ! timeout --foreground "${RISCV64_TIMEOUT}" "${RISCV64_GCC_BIN}" "${gcc_arch_args[@]}" "${link_args[@]}" -o "${exe_file}" "${asmfile}" "${RUNTIME_LIB}" >/dev/null 2>&1; then
		t1=$(date +%s%N)
		t_link=$(( (t1 - t0) / 1000000 ))
		echo "${source_name} link NG [riscv64-native]  compile=${t_compile}ms link=${t_link}ms"
		return 1
	fi
	t1=$(date +%s%N)
	t_link=$(( (t1 - t0) / 1000000 ))

	t0=$(date +%s%N)
	if [[ -f "${infile}" ]]; then
		timeout --foreground "${RISCV64_TIMEOUT}" "${exe_file}" < "${infile}" > "${output_file}" 2> "${stderr_file}"
		exit_code=$?
	else
		timeout --foreground "${RISCV64_TIMEOUT}" "${exe_file}" > "${output_file}" 2> "${stderr_file}"
		exit_code=$?
	fi
	t1=$(date +%s%N)
	t_run=$(( (t1 - t0) / 1000000 ))

	if [[ "${exit_code}" -eq 132 ]]; then
		echo "${source_name} SIGILL [riscv64-native]  compile=${t_compile}ms link=${t_link}ms run=${t_run}ms"
		echo "  Illegal instruction while running native binary."
		echo "  If MINIC_RISCV64_RVV=on, this usually means the board/kernel cannot execute RVV instructions."
		echo "  Retry with MINIC_RISCV64_RVV=off, or use an RVV-capable board and kernel."
		return 1
	fi

	write_result_file "${output_file}" "${exit_code}" "${result_file}"

	local actual_md5 expected_md5 actual_size expected_size
	actual_md5=$(compute_md5 "${result_file}") || {
		echo "md5 tool not found: need md5sum or md5"
		return 1
	}
	expected_md5=$(compute_md5 "${outfile}") || {
		echo "md5 tool not found: need md5sum or md5"
		return 1
	}

	if [[ "${actual_md5}" != "${expected_md5}" ]]; then
		if ! normalized_text_files_equal "${result_file}" "${outfile}"; then
			actual_size=$(file_size_bytes "${result_file}")
			expected_size=$(file_size_bytes "${outfile}")
			echo "${source_name} NG [riscv64-native]  compile=${t_compile}ms link=${t_link}ms run=${t_run}ms"
			echo "  expected md5=${expected_md5} size=${expected_size} tail16=$(tail_bytes_hex "${outfile}")"
			echo "  actual   md5=${actual_md5} size=${actual_size} tail16=$(tail_bytes_hex "${result_file}")"
			return 1
		fi
	fi

	printf '%s\n' "${t_run}" > "${result_dir}/${testcase}.summary"
	printf "%-${STATUS_COL_WIDTH}s %s\n" "${source_name} OK [riscv64-native]" \
		"compile=${t_compile}ms link=${t_link}ms run=${t_run}ms"
	return 0
}

run_testcase() {
	local suite_dir="$1"
	local testcase_arg="$2"
	local case_root="${TEST_ROOT}/${suite_dir}"
	local testcase
	local cfile
	testcase=$(strip_source_ext "${testcase_arg}")
	cfile=$(find_source_file "${case_root}" "${testcase_arg}")
	local infile="${case_root}/${testcase}.in"
	local outfile="${case_root}/${testcase}.out"

	local is_perf_test="0"
	if [[ "${suite_dir}" == *"performance"* ]]; then
		is_perf_test="1"
	fi

	if [[ -z "${cfile}" || ! -f "${cfile}" ]]; then
		echo "${case_root}/${testcase}.{c,sy} not found"
		return 1
	fi

	if [[ "${TEST_MODE}" == "asm" && ! -f "${outfile}" ]]; then
		echo "${outfile} not found"
		return 1
	fi

	local result_dir="${TMP_DIR}/${testcase}"
	mkdir -p "${result_dir}"
	run_native_check "${cfile}" "${infile}" "${outfile}" "${testcase}" "${is_perf_test}" "${result_dir}"
}

run_testcase_worker() {
	local suite_dir="$1"
	local testcase_arg="$2"
	local case_root="${TEST_ROOT}/${suite_dir}"
	local testcase
	local cfile
	testcase=$(strip_source_ext "${testcase_arg}")
	cfile=$(find_source_file "${case_root}" "${testcase_arg}")
	local infile="${case_root}/${testcase}.in"
	local outfile="${case_root}/${testcase}.out"

	local is_perf_test="0"
	if [[ "${suite_dir}" == *"performance"* ]]; then
		is_perf_test="1"
	fi

	local result_dir="${TMP_DIR}/${testcase}"
	mkdir -p "${result_dir}"
	local status_file="${result_dir}/status"

	if [[ -z "${cfile}" || ! -f "${cfile}" ]]; then
		echo "${case_root}/${testcase}.{c,sy} not found" > "${status_file}.out"
		printf 'NG\n' > "${status_file}"
		return 0
	fi
	if [[ "${TEST_MODE}" == "asm" && ! -f "${outfile}" ]]; then
		echo "${outfile} not found" > "${status_file}.out"
		printf 'NG\n' > "${status_file}"
		return 0
	fi

	if run_native_check "${cfile}" "${infile}" "${outfile}" "${testcase}" "${is_perf_test}" "${result_dir}" > "${status_file}.out" 2>&1; then
		printf 'OK\n' > "${status_file}"
	else
		printf 'NG\n' > "${status_file}"
	fi
	return 0
}

run_suite() {
	local suite_dir="$1"
	local case_root="${TEST_ROOT}/${suite_dir}"
	local testcase
	local -a testcases=()

	while IFS= read -r testcase; do
		testcases+=("${testcase}")
	done < <(
		find "${case_root}" -maxdepth 1 -type f \( -name '*.c' -o -name '*.sy' \) |
			while IFS= read -r cfile; do
				testcase=$(basename "${cfile}")
				echo "${testcase%.*}"
			done |
			sort -u
	)

	local -a active_pids=()
	local -a active_tcs=()

	collect_one() {
		local tc="$1"
		local result_dir="${TMP_DIR}/${tc}"
		local status_file="${result_dir}/status"
		if [[ -f "${status_file}.out" ]]; then
			cat "${status_file}.out"
		fi
		if [[ -f "${status_file}" && "$(cat "${status_file}")" == "OK" ]]; then
			OK_NUM=$((OK_NUM + 1))
			local summary_file="${result_dir}/${tc}.summary"
			if [[ -f "${summary_file}" ]]; then
				local t_run_val
				read -r t_run_val < "${summary_file}"
				TOTAL_RUN=$((TOTAL_RUN + t_run_val))
			fi
		else
			NG_NUM=$((NG_NUM + 1))
		fi
	}

	drain_completed() {
		local -a remaining_pids=()
		local -a remaining_tcs=()
		local i=0
		while [[ ${i} -lt ${#active_pids[@]} ]]; do
			local pid="${active_pids[${i}]}"
			local tc="${active_tcs[${i}]}"
			if ! kill -0 "${pid}" 2>/dev/null; then
				wait "${pid}" 2>/dev/null || true
				collect_one "${tc}"
			else
				remaining_pids+=("${pid}")
				remaining_tcs+=("${tc}")
			fi
			i=$((i + 1))
		done
		active_pids=("${remaining_pids[@]+"${remaining_pids[@]}"}")
		active_tcs=("${remaining_tcs[@]+"${remaining_tcs[@]}"}")
	}

	for tc in "${testcases[@]}"; do
		run_testcase_worker "${suite_dir}" "${tc}" &
		active_pids+=($!)
		active_tcs+=("${tc}")
		while [[ ${#active_pids[@]} -ge ${PARALLEL_JOBS} ]]; do
			drain_completed
			if [[ ${#active_pids[@]} -ge ${PARALLEL_JOBS} ]]; then
				sleep 0.1
			fi
		done
	done

	while [[ ${#active_pids[@]} -gt 0 ]]; do
		drain_completed
		if [[ ${#active_pids[@]} -gt 0 ]]; then
			sleep 0.1
		fi
	done
}

if [[ ! -x "${MINIC_BIN}" ]]; then
	fail_with_usage "Compiler not found: ${MINIC_BIN}. Please build the project first."
fi
if ! command -v "${RISCV64_GCC_BIN}" >/dev/null 2>&1; then
	fail_with_usage "native gcc not found: ${RISCV64_GCC_BIN}"
fi
if [[ "${TEST_MODE}" == "asm" ]]; then
	if ! command -v md5sum >/dev/null 2>&1 && ! command -v md5 >/dev/null 2>&1; then
		fail_with_usage "md5 tool not found: need md5sum or md5"
	fi
	if [[ ! -f "${RUNTIME_LIB}" ]]; then
		fail_with_usage "Runtime archive not found: ${RUNTIME_LIB}"
	fi
fi

machine=$(uname -m 2>/dev/null || true)
if [[ "${machine}" != "riscv64" && "${MINIC_RISCV64_NATIVE_ALLOW_NON_RISCV:-0}" != "1" ]]; then
	fail_with_usage "This native runner must be executed on riscv64; uname -m=${machine}"
fi

check_native_rvv_available

echo "Native riscv64 runner, no QEMU"
echo "Parallel jobs: ${PARALLEL_JOBS}"

suite_key="all"
single_testcase=""
if [[ $# -gt 2 ]]; then
	fail_with_usage "Too many arguments."
elif [[ $# -eq 2 ]]; then
	suite_key="$1"
	single_testcase="$2"
elif [[ $# -eq 1 ]]; then
	if suite_dir_from_key "$1" >/dev/null 2>&1 || [[ "$1" == "all" ]]; then
		suite_key="$1"
	else
		single_testcase="$1"
	fi
fi

if [[ -n "${single_testcase}" && "${suite_key}" == "all" ]]; then
	suite_dir=$(infer_suite_from_testcase "${single_testcase}") || \
		fail_with_usage "Cannot infer suite from testcase: ${single_testcase}"
	if run_testcase "${suite_dir}" "${single_testcase}"; then
		OK_NUM=$((OK_NUM + 1))
	else
		NG_NUM=$((NG_NUM + 1))
	fi
elif [[ "${suite_key}" == "all" ]]; then
	run_suite "2023_function"
	run_suite "2025_function"
	run_suite "2025_performance"
	run_suite "2026_function"
	run_suite "2026_performance"
else
	suite_dir=$(suite_dir_from_key "${suite_key}") || \
		fail_with_usage "Unknown suite: ${suite_key}"
	if [[ -n "${single_testcase}" ]]; then
		if run_testcase "${suite_dir}" "${single_testcase}"; then
			OK_NUM=$((OK_NUM + 1))
		else
			NG_NUM=$((NG_NUM + 1))
		fi
	else
		run_suite "${suite_dir}"
	fi
fi

echo "OK number=${OK_NUM}, NG number=${NG_NUM}"
echo "total_run=${TOTAL_RUN}ms"

if [[ ${NG_NUM} -ne 0 ]]; then
	exit 1
fi
