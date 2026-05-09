#!/usr/bin/env bash

set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

MINIC_BIN=${MINIC_BIN:-"${REPO_ROOT}/build/minic"}
RISCV64_GCC_BIN=${RISCV64_GCC_BIN:-"riscv64-linux-gnu-gcc"}
QEMU_RISCV64_BIN=${QEMU_RISCV64_BIN:-""}
TEST_ROOT=${MINIC_TEST_ROOT:-"${REPO_ROOT}/tests"}
RUNTIME_LIB=${MINIC_RUNTIME_LIB:-"${REPO_ROOT}/tests/libsysy_riscv.a"}
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
STATUS_COL_WIDTH=50

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
  RISCV64_GCC_BIN=riscv64-linux-gnu-gcc
  QEMU_RISCV64_BIN=qemu-riscv64-static
  MINIC_TEST_ROOT=./tests
  MINIC_RUNTIME_LIB=./tests/libsysy_riscv.a

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
	local source_name
	local asmfile="${TMP_DIR}/${testcase}.rv64.s"
	local objfile="${TMP_DIR}/${testcase}.rv64.o"
	local exe_file="${TMP_DIR}/${testcase}.rv64"
	local output_file="${TMP_DIR}/${testcase}.rv64.output"
	local stderr_file="${TMP_DIR}/${testcase}.rv64.stderr"
	local result_file="${TMP_DIR}/${testcase}.rv64.result"
	local exit_code=0
	local t0 t1 t_compile=0 t_assemble=0 t_link=0 t_run=0
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
		actual_size=$(file_size_bytes "${result_file}")
		expected_size=$(file_size_bytes "${outfile}")
		echo "${source_name} NG [riscv64]  compile=${t_compile}ms link=${t_link}ms run=${t_run}ms"
		echo "  expected md5=${expected_md5} size=${expected_size} tail16=$(tail_bytes_hex "${outfile}")"
		echo "  actual   md5=${actual_md5} size=${actual_size} tail16=$(tail_bytes_hex "${result_file}")"
		return 1
	fi

	printf "%-${STATUS_COL_WIDTH}s %s\n" "${source_name} OK [riscv64]" "compile=${t_compile}ms link=${t_link}ms run=${t_run}ms"
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
		NG_NUM=$((NG_NUM + 1))
		return
	fi

	if [[ "${TEST_MODE}" == "asm" && ! -f "${outfile}" ]]; then
		echo "${outfile} not found"
		NG_NUM=$((NG_NUM + 1))
		return
	fi

	if run_riscv64_check "${cfile}" "${infile}" "${outfile}" "${testcase}" "${is_perf_test}"; then
		OK_NUM=$((OK_NUM + 1))
	else
		NG_NUM=$((NG_NUM + 1))
	fi
}

run_suite() {
	local suite_dir="$1"
	local case_root="${TEST_ROOT}/${suite_dir}"
	local cfile=""
	local testcase=""

	while IFS= read -r testcase; do
		run_testcase "${suite_dir}" "${testcase}"
	done < <(
		find "${case_root}" -maxdepth 1 -type f \( -name '*.c' -o -name '*.sy' \) |
			while IFS= read -r cfile; do
				testcase=$(basename "${cfile}")
				echo "${testcase%.*}"
			done |
			sort -u
	)
}

if [[ ! -x "${MINIC_BIN}" ]]; then
	fail_with_usage "Compiler not found: ${MINIC_BIN}. Please build the project first."
fi

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
	else
		run_suite "${suite_dir}"
	fi
fi

echo "OK number=${OK_NUM}, NG number=${NG_NUM}"

if [[ ${NG_NUM} -ne 0 ]]; then
	exit 1
fi
