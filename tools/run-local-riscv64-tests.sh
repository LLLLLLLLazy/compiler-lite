#!/usr/bin/env bash

set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

MINIC_BIN=${MINIC_BIN:-"${REPO_ROOT}/build/minic"}
RISCV64_GCC_BIN=${RISCV64_GCC_BIN:-"riscv64-linux-gnu-gcc"}
QEMU_RISCV64_BIN=${QEMU_RISCV64_BIN:-""}
TEST_ROOT=${MINIC_TEST_ROOT:-"${REPO_ROOT}/tests"}
STD_C=${MINIC_STD_C:-"${TEST_ROOT}/std.c"}
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
  all               -> all suites above

Environment:
  MINIC_BIN=./build/minic
  MINIC_FRONTEND=antlr|recursive|default
  MINIC_RISCV64_TEST_MODE=asm       Generate RISCV64 asm, link, run, diff .out (default)
  MINIC_RISCV64_TEST_MODE=assemble  Generate RISCV64 asm and assemble only
  MINIC_RISCV64_TIMEOUT=30          Per-step timeout passed to timeout(1)
  RISCV64_GCC_BIN=riscv64-linux-gnu-gcc
  QEMU_RISCV64_BIN=qemu-riscv64-static
  MINIC_TEST_ROOT=./tests
  MINIC_STD_C=./tests/std.c

Examples:
  ./tools/run-local-riscv64-tests.sh 2023 2023_func_00_main
  ./tools/run-local-riscv64-tests.sh 2023
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
		*)
			return 1
			;;
	esac
}

infer_suite_from_testcase() {
	case "$1" in
		2023_func_*)
			echo "2023_function"
			;;
		2025_func_*)
			echo "2025_function"
			;;
		2025_perf_*)
			echo "2025_performance"
			;;
		*)
			return 1
			;;
	esac
}

write_result_file() {
	local output="$1"
	local exit_code="$2"
	local result_file="$3"

	if [[ -n "${output}" ]]; then
		printf '%s' "${output}" > "${result_file}"
		if [[ "${output}" != *$'\n' ]]; then
			printf '\n' >> "${result_file}"
		fi
	else
		: > "${result_file}"
	fi

	printf '%s\n' "${exit_code}" >> "${result_file}"
}

run_riscv64_check() {
	local cfile="$1"
	local infile="$2"
	local outfile="$3"
	local testcase="$4"
	local asmfile="${TMP_DIR}/${testcase}.rv64.s"
	local objfile="${TMP_DIR}/${testcase}.rv64.o"
	local exe_file="${TMP_DIR}/${testcase}.rv64"
	local result_file="${TMP_DIR}/${testcase}.rv64.result"
	local output=""
	local exit_code=0

	if ! timeout --foreground "${RISCV64_TIMEOUT}" "${MINIC_BIN}" -S "${frontend_args[@]}" -O1 -t RISCV64 -o "${asmfile}" "${cfile}" >/dev/null 2>&1; then
		echo "${testcase}.c compile NG [riscv64]"
		return 1
	fi

	if [[ ! -s "${asmfile}" ]]; then
		echo "${asmfile} not generated [riscv64]"
		return 1
	fi

	if [[ "${TEST_MODE}" == "assemble" ]]; then
		if ! timeout --foreground "${RISCV64_TIMEOUT}" "${RISCV64_GCC_BIN}" -c -o "${objfile}" "${asmfile}" >/dev/null 2>&1; then
			echo "${testcase}.c assemble NG [riscv64]"
			return 1
		fi

		echo "${testcase}.c OK [riscv64-assemble]"
		return 0
	fi

	if ! timeout --foreground "${RISCV64_TIMEOUT}" "${RISCV64_GCC_BIN}" -static -o "${exe_file}" "${asmfile}" "${STD_C}" >/dev/null 2>&1; then
		echo "${testcase}.c link NG [riscv64]"
		return 1
	fi

	if [[ -f "${infile}" ]]; then
		output="$(timeout --foreground "${RISCV64_TIMEOUT}" "${QEMU_RISCV64_BIN}" "${exe_file}" < "${infile}" 2>&1)"
		exit_code=$?
	else
		output="$(timeout --foreground "${RISCV64_TIMEOUT}" "${QEMU_RISCV64_BIN}" "${exe_file}" 2>&1)"
		exit_code=$?
	fi

	write_result_file "${output}" "${exit_code}" "${result_file}"

	if ! diff -a --strip-trailing-cr "${result_file}" "${outfile}" >/dev/null 2>&1; then
		echo "${testcase}.c NG [riscv64]"
		return 1
	fi

	echo "${testcase}.c OK [riscv64]"
	return 0
}

run_testcase() {
	local suite_dir="$1"
	local testcase="$2"
	local case_root="${TEST_ROOT}/${suite_dir}"
	local cfile="${case_root}/${testcase}.c"
	local infile="${case_root}/${testcase}.in"
	local outfile="${case_root}/${testcase}.out"

	if [[ ! -f "${cfile}" ]]; then
		echo "${cfile} not found"
		NG_NUM=$((NG_NUM + 1))
		return
	fi

	if [[ "${TEST_MODE}" == "asm" && ! -f "${outfile}" ]]; then
		echo "${outfile} not found"
		NG_NUM=$((NG_NUM + 1))
		return
	fi

	if run_riscv64_check "${cfile}" "${infile}" "${outfile}" "${testcase}"; then
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

	while IFS= read -r cfile; do
		testcase=$(basename "${cfile}" .c)
		run_testcase "${suite_dir}" "${testcase}"
	done < <(find "${case_root}" -maxdepth 1 -type f -name '*.c' | sort)
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

	if [[ ! -f "${STD_C}" ]]; then
		fail_with_usage "Runtime std.c not found: ${STD_C}"
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
