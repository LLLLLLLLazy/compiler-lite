#!/usr/bin/env bash

set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

MINIC_BIN=${MINIC_BIN:-"${REPO_ROOT}/build/minic"}
RISCV64_GCC_BIN=${RISCV64_GCC_BIN:-"riscv64-linux-gnu-gcc"}
QEMU_RISCV64_BIN=${QEMU_RISCV64_BIN:-""}
TEST_ROOT=${MINIC_TEST_ROOT:-"${REPO_ROOT}/tests"}
RUNTIME_LIB=${MINIC_RUNTIME_LIB:-"${REPO_ROOT}/tests/libsysy_riscv.a"}
RUNTIME_SOURCE=${MINIC_RUNTIME_SOURCE:-"${REPO_ROOT}/tests/sylib.c"}
CLANG_BIN=${CLANG_BIN:-"clang"}
FRONTEND=${MINIC_FRONTEND:-"antlr"}
TEST_MODE=${MINIC_RISCV64_TEST_MODE:-"asm"}
RISCV64_TIMEOUT=${MINIC_RISCV64_TIMEOUT:-30}

if [[ -z "${QEMU_RISCV64_BIN}" ]]; then
	if command -v qemu-riscv64-static >/dev/null 2>&1; then
		QEMU_RISCV64_BIN="qemu-riscv64-static"
	else
		QEMU_RISCV64_BIN="qemu-riscv64"
	fi
fi

OK_NUM=0
NG_NUM=0
TOTAL_RUN=0
TOTAL_MINIC_IR_LLVM_RUN=0
TOTAL_LLVM_ALL_RUN=0
STATUS_COL_WIDTH=50
PARALLEL_JOBS=${MINIC_RISCV64_PARALLEL:-$(echo 8)}

TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/minic-rv64-tests.XXXXXX")
trap 'rm -rf "${TMP_DIR}"' EXIT

usage() {
	cat <<'USAGE'
Usage:
  ./tools/run-local-riscv64-tests.sh
  ./tools/run-local-riscv64-tests.sh <suite>
  ./tools/run-local-riscv64-tests.sh <suite> <testcase>
  ./tools/run-local-riscv64-tests.sh <testcase>

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
  MINIC_RISCV64_TEST_MODE=asm       Generate RISCV64 asm, link, run, compare .out md5 (default)
  MINIC_RISCV64_TEST_MODE=assemble  Generate RISCV64 asm and assemble only
  MINIC_RISCV64_TIMEOUT=30          Per-step timeout passed to timeout(1)
  MINIC_RISCV64_PARALLEL=N          Number of parallel jobs (default: nproc or 4)
  RISCV64_GCC_BIN=riscv64-linux-gnu-gcc
  QEMU_RISCV64_BIN=qemu-riscv64-static
  MINIC_TEST_ROOT=./tests
  MINIC_RUNTIME_LIB=./tests/libsysy_riscv.a
  MINIC_RUNTIME_SOURCE=./tests/sylib.c
  CLANG_BIN=clang

Examples:
  ./tools/run-local-riscv64-tests.sh 2023 2023_func_00_main
  ./tools/run-local-riscv64-tests.sh 2023
  ./tools/run-local-riscv64-tests.sh 2025 2025_func_009_BFS.sy
  MINIC_RISCV64_TEST_MODE=assemble ./tools/run-local-riscv64-tests.sh 2023_func_00_main
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

## @brief Compare two text files after normalizing CRLF line endings to LF
# @param $1 First file path
# @param $2 Second file path
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

run_riscv64_check() {
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
	local t_minic_ir_llvm_run="N/A"
	local t_llvm_all_run="N/A"
	source_name=$(basename "${cfile}")

	# 根据测试类型选择优化级别：性能测试开启优化，功能测试关闭优化
	local opt_level="0"
	if [[ "${is_perf_test}" == "1" ]]; then
		opt_level="1"
	fi

	# compile
	t0=$(date +%s%N)
	if ! timeout --foreground "${RISCV64_TIMEOUT}" "${MINIC_BIN}" -S "${frontend_args[@]}" -O"${opt_level}" -t RISCV64 -o "${asmfile}" "${cfile}" >/dev/null 2>&1; then
		t1=$(date +%s%N)
		t_compile=$(( (t1 - t0) / 1000000 ))
		echo "${source_name} compile NG [riscv64]  compile=${t_compile}ms"
		return 1
	fi
	t1=$(date +%s%N)
	t_compile=$(( (t1 - t0) / 1000000 ))

	if [[ ! -s "${asmfile}" ]]; then
		echo "${asmfile} not generated [riscv64]  compile=${t_compile}ms"
		return 1
	fi

	# assemble
	if [[ "${TEST_MODE}" == "assemble" ]]; then
		t0=$(date +%s%N)
		if ! timeout --foreground "${RISCV64_TIMEOUT}" "${RISCV64_GCC_BIN}" -c -o "${objfile}" "${asmfile}" >/dev/null 2>&1; then
			t1=$(date +%s%N)
			t_assemble=$(( (t1 - t0) / 1000000 ))
			echo "${source_name} assemble NG [riscv64]  compile=${t_compile}ms assemble=${t_assemble}ms"
			return 1
		fi
		t1=$(date +%s%N)
		t_assemble=$(( (t1 - t0) / 1000000 ))

		printf "%-${STATUS_COL_WIDTH}s %s\n" "${source_name} OK [riscv64-assemble]" "compile=${t_compile}ms assemble=${t_assemble}ms"
		return 0
	fi

	# link
	t0=$(date +%s%N)
	if ! timeout --foreground "${RISCV64_TIMEOUT}" "${RISCV64_GCC_BIN}" -static -o "${exe_file}" "${asmfile}" "${RUNTIME_LIB}" >/dev/null 2>&1; then
		t1=$(date +%s%N)
		t_link=$(( (t1 - t0) / 1000000 ))
		echo "${source_name} link NG [riscv64]  compile=${t_compile}ms link=${t_link}ms"
		return 1
	fi
	t1=$(date +%s%N)
	t_link=$(( (t1 - t0) / 1000000 ))

	# run
	t0=$(date +%s%N)
	if [[ -f "${infile}" ]]; then
		timeout --foreground "${RISCV64_TIMEOUT}" "${QEMU_RISCV64_BIN}" "${exe_file}" < "${infile}" > "${output_file}" 2> "${stderr_file}"
		exit_code=$?
	else
		timeout --foreground "${RISCV64_TIMEOUT}" "${QEMU_RISCV64_BIN}" "${exe_file}" > "${output_file}" 2> "${stderr_file}"
		exit_code=$?
	fi
	t1=$(date +%s%N)
	t_run=$(( (t1 - t0) / 1000000 ))

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
			echo "${source_name} NG [riscv64]  compile=${t_compile}ms link=${t_link}ms run=${t_run}ms"
			echo "  expected md5=${expected_md5} size=${expected_size} tail16=$(tail_bytes_hex "${outfile}")"
			echo "  actual   md5=${actual_md5} size=${actual_size} tail16=$(tail_bytes_hex "${result_file}")"
			return 1
		fi
	fi

	# llvm baselines:
	# - minic_ir_llvm: minic generates LLVM IR, clang compiles to riscv64, link, run under qemu
	# - llvm_all: clang compiles source directly to riscv64, link, run under qemu
	local llfile="${result_dir}/${testcase}.rv64.ll"
	local minic_ir_llvm_obj="${result_dir}/${testcase}.minic_ir_llvm.rv64.o"
	local minic_ir_llvm_exe="${result_dir}/${testcase}.minic_ir_llvm.rv64"
	local minic_ir_llvm_output="${result_dir}/${testcase}.minic_ir_llvm.rv64.output"
	local minic_ir_llvm_stderr="${result_dir}/${testcase}.minic_ir_llvm.rv64.stderr"
	local llvm_all_obj="${result_dir}/${testcase}.llvm_all.rv64.o"
	local llvm_all_exe="${result_dir}/${testcase}.llvm_all.rv64"
	local llvm_all_output="${result_dir}/${testcase}.llvm_all.rv64.output"
	local llvm_all_stderr="${result_dir}/${testcase}.llvm_all.rv64.stderr"
	local llvm_opt_level="0"
	if [[ "${is_perf_test}" == "1" ]]; then
		llvm_opt_level="2"
	fi

	# minic IR -> llvm compile & link -> qemu run
	if timeout --foreground "${RISCV64_TIMEOUT}" "${MINIC_BIN}" -S "${frontend_args[@]}" -O"${opt_level}" -L -o "${llfile}" "${cfile}" >/dev/null 2>&1 && \
	   [[ -s "${llfile}" ]] && \
	   timeout --foreground "${RISCV64_TIMEOUT}" "${CLANG_BIN}" --target=riscv64-linux-gnu -O"${llvm_opt_level}" -Wno-override-module -c -o "${minic_ir_llvm_obj}" "${llfile}" >/dev/null 2>&1 && \
	   timeout --foreground "${RISCV64_TIMEOUT}" "${RISCV64_GCC_BIN}" -static -o "${minic_ir_llvm_exe}" "${minic_ir_llvm_obj}" "${RUNTIME_LIB}" >/dev/null 2>&1; then
		t0=$(date +%s%N)
		if [[ -f "${infile}" ]]; then
			timeout --foreground "${RISCV64_TIMEOUT}" "${QEMU_RISCV64_BIN}" "${minic_ir_llvm_exe}" < "${infile}" > "${minic_ir_llvm_output}" 2> "${minic_ir_llvm_stderr}"
		else
			timeout --foreground "${RISCV64_TIMEOUT}" "${QEMU_RISCV64_BIN}" "${minic_ir_llvm_exe}" > "${minic_ir_llvm_output}" 2> "${minic_ir_llvm_stderr}"
		fi
		local minic_ir_llvm_status=$?
		if [[ "${minic_ir_llvm_status}" -ne 124 && "${minic_ir_llvm_status}" -ne 125 && "${minic_ir_llvm_status}" -ne 126 && "${minic_ir_llvm_status}" -ne 127 ]]; then
			t1=$(date +%s%N)
			t_minic_ir_llvm_run=$(( (t1 - t0) / 1000000 ))
		fi
	fi

	# clang compiles source directly to riscv64 -> link -> qemu run
	# Preprocess .sy: replace starttime/stoptime with _sysy_starttime/_sysy_stoptime
	# to match the runtime library symbols, then compile with clang and link with sylib.c
	local llvm_all_cfile="${result_dir}/${testcase}.llvm_all.c"
	if sed 's/\bstarttime\b/_sysy_starttime/g; s/\bstoptime\b/_sysy_stoptime/g' "${cfile}" > "${llvm_all_cfile}" 2>/dev/null && \
	   timeout --foreground "${RISCV64_TIMEOUT}" "${CLANG_BIN}" --target=riscv64-linux-gnu -O"${llvm_opt_level}" -Wno-override-module -c -o "${llvm_all_obj}" "${llvm_all_cfile}" >/dev/null 2>&1 && \
	   timeout --foreground "${RISCV64_TIMEOUT}" "${RISCV64_GCC_BIN}" -static -o "${llvm_all_exe}" "${llvm_all_obj}" "${RUNTIME_LIB}" >/dev/null 2>&1; then
		t0=$(date +%s%N)
		if [[ -f "${infile}" ]]; then
			timeout --foreground "${RISCV64_TIMEOUT}" "${QEMU_RISCV64_BIN}" "${llvm_all_exe}" < "${infile}" > "${llvm_all_output}" 2> "${llvm_all_stderr}"
		else
			timeout --foreground "${RISCV64_TIMEOUT}" "${QEMU_RISCV64_BIN}" "${llvm_all_exe}" > "${llvm_all_output}" 2> "${llvm_all_stderr}"
		fi
		local llvm_all_status=$?
		if [[ "${llvm_all_status}" -ne 124 && "${llvm_all_status}" -ne 125 && "${llvm_all_status}" -ne 126 && "${llvm_all_status}" -ne 127 ]]; then
			t1=$(date +%s%N)
			t_llvm_all_run=$(( (t1 - t0) / 1000000 ))
		fi
	fi

	local summary_file="${result_dir}/${testcase}.summary"
	printf '%s\n' "${t_run}" "${t_minic_ir_llvm_run}" "${t_llvm_all_run}" > "${summary_file}"

	local minic_ir_llvm_run_text="${t_minic_ir_llvm_run}"
	local llvm_all_run_text="${t_llvm_all_run}"
	if [[ "${minic_ir_llvm_run_text}" != "N/A" ]]; then
		minic_ir_llvm_run_text="${minic_ir_llvm_run_text}ms"
	fi
	if [[ "${llvm_all_run_text}" != "N/A" ]]; then
		llvm_all_run_text="${llvm_all_run_text}ms"
	fi

	printf "%-${STATUS_COL_WIDTH}s %s\n" "${source_name} OK [riscv64]" \
		"compile=${t_compile}ms link=${t_link}ms run=${t_run}ms minic_ir_llvm_run=${minic_ir_llvm_run_text} llvm_all_run=${llvm_all_run_text}"

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

	# 判断是否是性能测试（suite_dir 包含 "performance"）
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

	if run_riscv64_check "${cfile}" "${infile}" "${outfile}" "${testcase}" "${is_perf_test}" "${result_dir}"; then
		return 0
	else
		return 1
	fi
}

# Worker function: runs a single testcase in a subshell, writes result to a status file
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

	if run_riscv64_check "${cfile}" "${infile}" "${outfile}" "${testcase}" "${is_perf_test}" "${result_dir}" > "${status_file}.out" 2>&1; then
		printf 'OK\n' > "${status_file}"
	else
		printf 'NG\n' > "${status_file}"
	fi
	return 0
}

run_suite() {
	local suite_dir="$1"
	local case_root="${TEST_ROOT}/${suite_dir}"
	local cfile=""
	local testcase=""

	# Collect all testcases
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

	if [[ ${#testcases[@]} -eq 0 ]]; then
		return
	fi

	local total=${#testcases[@]}
	local done_count=0
	local launched=0

	# Associative array: pid -> testcase
	declare -A pid_map

	# Process a completed testcase: print output, accumulate counters
	collect_one() {
		local tc="$1"
		local result_dir="${TMP_DIR}/${tc}"
		local status_file="${result_dir}/status"

		# Print captured output
		if [[ -f "${status_file}.out" ]]; then
			cat "${status_file}.out"
		fi

		if [[ -f "${status_file}" ]]; then
			local status
			status=$(cat "${status_file}")
			if [[ "${status}" == "OK" ]]; then
				OK_NUM=$((OK_NUM + 1))
				# Accumulate timing from summary file
				local summary_file="${result_dir}/${tc}.summary"
				if [[ -f "${summary_file}" ]]; then
					local t_run_val t_minic_ir_llvm_val t_llvm_all_val
					{
						read -r t_run_val
						read -r t_minic_ir_llvm_val
						read -r t_llvm_all_val
					} < "${summary_file}"
					TOTAL_RUN=$((TOTAL_RUN + t_run_val))
					if [[ "${t_minic_ir_llvm_val}" != "N/A" ]]; then
						TOTAL_MINIC_IR_LLVM_RUN=$((TOTAL_MINIC_IR_LLVM_RUN + t_minic_ir_llvm_val))
					fi
					if [[ "${t_llvm_all_val}" != "N/A" ]]; then
						TOTAL_LLVM_ALL_RUN=$((TOTAL_LLVM_ALL_RUN + t_llvm_all_val))
					fi
				fi
			else
				NG_NUM=$((NG_NUM + 1))
			fi
		else
			NG_NUM=$((NG_NUM + 1))
		fi
		done_count=$((done_count + 1))
	}

	# Check for any completed workers and collect their results
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

	local -a active_pids=()
	local -a active_tcs=()

	for tc in "${testcases[@]}"; do
		# Launch worker
		run_testcase_worker "${suite_dir}" "${tc}" &
		active_pids+=($!)
		active_tcs+=("${tc}")
		launched=$((launched + 1))

		# If at capacity, wait for at least one to finish
		if [[ ${#active_pids[@]} -ge ${PARALLEL_JOBS} ]]; then
			# Poll until a slot frees up
			while [[ ${#active_pids[@]} -ge ${PARALLEL_JOBS} ]]; do
				drain_completed
				if [[ ${#active_pids[@]} -ge ${PARALLEL_JOBS} ]]; then
					sleep 0.1
				fi
			done
		fi
	done

	# Wait for all remaining workers, collecting results as they finish
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

echo "Parallel jobs: ${PARALLEL_JOBS}"

if ! command -v "${RISCV64_GCC_BIN}" >/dev/null 2>&1; then
	fail_with_usage "riscv64 gcc not found: ${RISCV64_GCC_BIN}"
fi

if [[ "${TEST_MODE}" == "asm" ]]; then
	if ! command -v "${QEMU_RISCV64_BIN}" >/dev/null 2>&1; then
		fail_with_usage "qemu riscv64 not found: ${QEMU_RISCV64_BIN}"
	fi

	if ! command -v md5sum >/dev/null 2>&1 && ! command -v md5 >/dev/null 2>&1; then
		fail_with_usage "md5 tool not found: need md5sum or md5"
	fi

	if [[ ! -f "${RUNTIME_LIB}" ]]; then
		fail_with_usage "Runtime archive not found: ${RUNTIME_LIB}"
	fi
fi

if ! command -v "${CLANG_BIN}" >/dev/null 2>&1; then
	fail_with_usage "clang not found: ${CLANG_BIN}"
fi

if [[ ! -f "${RUNTIME_SOURCE}" ]]; then
	fail_with_usage "Runtime source not found: ${RUNTIME_SOURCE}"
fi

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
	run_testcase "${suite_dir}" "${single_testcase}"
	if [[ $? -eq 0 ]]; then
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
		run_testcase "${suite_dir}" "${single_testcase}"
		if [[ $? -eq 0 ]]; then
			OK_NUM=$((OK_NUM + 1))
		else
			NG_NUM=$((NG_NUM + 1))
		fi
	else
		run_suite "${suite_dir}"
	fi
fi

echo "OK number=${OK_NUM}, NG number=${NG_NUM}"
echo "total_run=${TOTAL_RUN}ms, total_minic_ir_llvm_run=${TOTAL_MINIC_IR_LLVM_RUN}ms, total_llvm_all_run=${TOTAL_LLVM_ALL_RUN}ms"

if [[ ${NG_NUM} -ne 0 ]]; then
	exit 1
fi
