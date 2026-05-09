#!/usr/bin/env bash

set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

MINIC_BIN=${MINIC_BIN:-"${REPO_ROOT}/build/minic"}
TEST_DIR=${MINIC_PHI_TEST_DIR:-"${REPO_ROOT}/tests/phi_regression"}
RUNTIME_SOURCE=${MINIC_RUNTIME_SOURCE:-"${REPO_ROOT}/tests/sylib.c"}
RUNTIME_LIB=${MINIC_RUNTIME_LIB:-"${REPO_ROOT}/tests/libsysy_riscv.a"}
FRONTEND=${MINIC_FRONTEND:-"antlr"}
MODE=${MINIC_PHI_TEST_MODE:-"all"}
CLANG_BIN=${CLANG_BIN:-"clang"}
RISCV64_GCC_BIN=${RISCV64_GCC_BIN:-"riscv64-linux-gnu-gcc"}
QEMU_RISCV64_BIN=${QEMU_RISCV64_BIN:-""}
LL_OPT_LEVEL=${MINIC_PHI_LL_OPT_LEVEL:-"1"}
ASM_OPT_LEVEL=${MINIC_PHI_ASM_OPT_LEVEL:-"1"}

if [[ -z "${QEMU_RISCV64_BIN}" ]]; then
	if command -v qemu-riscv64-static >/dev/null 2>&1; then
		QEMU_RISCV64_BIN="qemu-riscv64-static"
	else
		QEMU_RISCV64_BIN="qemu-riscv64"
	fi
fi

OK_NUM=0
NG_NUM=0

TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/minic-phi-regression.XXXXXX")
trap 'rm -rf "${TMP_DIR}"' EXIT

usage() {
	cat <<'USAGE'
Usage:
  ./tools/run-phi-regression.sh
  ./tools/run-phi-regression.sh <mode>
  ./tools/run-phi-regression.sh <testcase>
  ./tools/run-phi-regression.sh <mode> <testcase>

Modes:
  ll     Generate .ll, link with clang, run, and diff against .out
  asm    Generate RISCV64 asm, link with riscv64 gcc, run under qemu, and diff against .out
  all    Run both ll and asm checks (default)

Examples:
  ./tools/run-phi-regression.sh
  ./tools/run-phi-regression.sh ll
  ./tools/run-phi-regression.sh asm phi_loop_rotate3
  MINIC_PHI_LL_OPT_LEVEL=0 ./tools/run-phi-regression.sh ll phi_if_swap2

Environment:
  MINIC_BIN=./build/minic
  MINIC_PHI_TEST_DIR=./tests/phi_regression
	MINIC_RUNTIME_SOURCE=./tests/sylib.c
	MINIC_RUNTIME_LIB=./tests/libsysy_riscv.a
  MINIC_FRONTEND=antlr|recursive|default
  MINIC_PHI_TEST_MODE=ll|asm|all
  MINIC_PHI_LL_OPT_LEVEL=0|1
  MINIC_PHI_ASM_OPT_LEVEL=0|1
  CLANG_BIN=clang
  RISCV64_GCC_BIN=riscv64-linux-gnu-gcc
  QEMU_RISCV64_BIN=qemu-riscv64-static
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

validate_opt_level() {
	case "$1" in
	0|1)
		return 0
		;;
	*)
		return 1
		;;
	esac
}

if ! validate_opt_level "${LL_OPT_LEVEL}"; then
	fail_with_usage "Unsupported MINIC_PHI_LL_OPT_LEVEL: ${LL_OPT_LEVEL}"
fi

if ! validate_opt_level "${ASM_OPT_LEVEL}"; then
	fail_with_usage "Unsupported MINIC_PHI_ASM_OPT_LEVEL: ${ASM_OPT_LEVEL}"
fi

normalize_mode() {
	case "$1" in
	ll|asm|all)
		echo "$1"
		return 0
		;;
	*)
		return 1
		;;
	esac
}

normalize_testcase() {
	local testcase="$1"
	testcase=$(basename "${testcase}")
	echo "${testcase%.c}"
}

write_result_file_from_path() {
	local output_file="$1"
	local exit_code="$2"
	local result_file="$3"

	if [[ -s "${output_file}" ]]; then
		cat "${output_file}" > "${result_file}"

		local last_byte_hex
		last_byte_hex=$(tail -c 1 "${output_file}" | od -An -tx1 | tr -d '[:space:]')
		if [[ "${last_byte_hex}" != "0a" ]]; then
			printf '\n' >> "${result_file}"
		fi
	else
		: > "${result_file}"
	fi

	printf '%s\n' "${exit_code}" >> "${result_file}"
}

run_ll_check() {
	local cfile="$1"
	local infile="$2"
	local outfile="$3"
	local testcase="$4"
	local llfile="${TMP_DIR}/${testcase}.ll"
	local exe_file="${TMP_DIR}/${testcase}.ll.out"
	local output_file="${TMP_DIR}/${testcase}.ll.output"
	local stderr_file="${TMP_DIR}/${testcase}.ll.stderr"
	local result_file="${TMP_DIR}/${testcase}.ll.result"
	local exit_code=0

	if ! "${MINIC_BIN}" -S "${frontend_args[@]}" -O"${LL_OPT_LEVEL}" -L -o "${llfile}" "${cfile}" >/dev/null 2>&1; then
		echo "${testcase}.c compile NG [ll]"
		return 1
	fi

	if [[ ! -s "${llfile}" ]]; then
		echo "${llfile} not generated [ll]"
		return 1
	fi

	if ! "${CLANG_BIN}" -Wno-override-module -o "${exe_file}" "${llfile}" "${RUNTIME_SOURCE}" >/dev/null 2>&1; then
		echo "${testcase}.c link NG [ll]"
		return 1
	fi

	if [[ -f "${infile}" ]]; then
		"${exe_file}" < "${infile}" > "${output_file}" 2> "${stderr_file}"
		exit_code=$?
	else
		"${exe_file}" > "${output_file}" 2> "${stderr_file}"
		exit_code=$?
	fi

	write_result_file_from_path "${output_file}" "${exit_code}" "${result_file}"

	if ! diff -a --strip-trailing-cr "${result_file}" "${outfile}" >/dev/null 2>&1; then
		echo "${testcase}.c NG [ll]"
		return 1
	fi

	echo "${testcase}.c OK [ll]"
	return 0
}

run_asm_check() {
	local cfile="$1"
	local infile="$2"
	local outfile="$3"
	local testcase="$4"
	local asmfile="${TMP_DIR}/${testcase}.rv64.s"
	local exe_file="${TMP_DIR}/${testcase}.rv64"
	local output_file="${TMP_DIR}/${testcase}.rv64.output"
	local stderr_file="${TMP_DIR}/${testcase}.rv64.stderr"
	local result_file="${TMP_DIR}/${testcase}.rv64.result"
	local exit_code=0

	if ! "${MINIC_BIN}" -S "${frontend_args[@]}" -O"${ASM_OPT_LEVEL}" -t RISCV64 -o "${asmfile}" "${cfile}" >/dev/null 2>&1; then
		echo "${testcase}.c compile NG [asm]"
		return 1
	fi

	if [[ ! -s "${asmfile}" ]]; then
		echo "${asmfile} not generated [asm]"
		return 1
	fi

	if ! "${RISCV64_GCC_BIN}" -static -o "${exe_file}" "${asmfile}" "${RUNTIME_LIB}" >/dev/null 2>&1; then
		echo "${testcase}.c link NG [asm]"
		return 1
	fi

	if [[ -f "${infile}" ]]; then
		"${QEMU_RISCV64_BIN}" "${exe_file}" < "${infile}" > "${output_file}" 2> "${stderr_file}"
		exit_code=$?
	else
		"${QEMU_RISCV64_BIN}" "${exe_file}" > "${output_file}" 2> "${stderr_file}"
		exit_code=$?
	fi

	write_result_file_from_path "${output_file}" "${exit_code}" "${result_file}"

	if ! diff -a --strip-trailing-cr "${result_file}" "${outfile}" >/dev/null 2>&1; then
		echo "${testcase}.c NG [asm]"
		return 1
	fi

	echo "${testcase}.c OK [asm]"
	return 0
}

run_testcase() {
	local testcase="$1"
	local cfile="${TEST_DIR}/${testcase}.c"
	local infile="${TEST_DIR}/${testcase}.in"
	local outfile="${TEST_DIR}/${testcase}.out"
	local failed=0

	if [[ ! -f "${cfile}" ]]; then
		echo "${cfile} not found"
		NG_NUM=$((NG_NUM + 1))
		return
	fi

	if [[ ! -f "${outfile}" ]]; then
		echo "${outfile} not found"
		NG_NUM=$((NG_NUM + 1))
		return
	fi

	if [[ "${MODE}" == "ll" || "${MODE}" == "all" ]]; then
		if ! run_ll_check "${cfile}" "${infile}" "${outfile}" "${testcase}"; then
			failed=1
		fi
	fi

	if [[ "${MODE}" == "asm" || "${MODE}" == "all" ]]; then
		if ! run_asm_check "${cfile}" "${infile}" "${outfile}" "${testcase}"; then
			failed=1
		fi
	fi

	if [[ ${failed} -ne 0 ]]; then
		NG_NUM=$((NG_NUM + 1))
	else
		OK_NUM=$((OK_NUM + 1))
	fi
}

run_all_tests() {
	local cfile=""
	local testcase=""

	while IFS= read -r cfile; do
		testcase=$(basename "${cfile}" .c)
		run_testcase "${testcase}"
	done < <(find "${TEST_DIR}" -maxdepth 1 -type f -name '*.c' | sort)
}

if [[ ! -x "${MINIC_BIN}" ]]; then
	fail_with_usage "Compiler not found: ${MINIC_BIN}. Please build the project first."
fi

if [[ ! -d "${TEST_DIR}" ]]; then
	fail_with_usage "Phi regression directory not found: ${TEST_DIR}"
fi

single_testcase=""

if [[ $# -gt 2 ]]; then
	fail_with_usage "Too many arguments."
elif [[ $# -eq 2 ]]; then
	MODE=$(normalize_mode "$1") || fail_with_usage "Unknown mode: $1"
	single_testcase=$(normalize_testcase "$2")
elif [[ $# -eq 1 ]]; then
	if normalize_mode "$1" >/dev/null 2>&1; then
		MODE=$(normalize_mode "$1")
	else
		single_testcase=$(normalize_testcase "$1")
	fi
fi

MODE=$(normalize_mode "${MODE}") || fail_with_usage "Unknown MINIC_PHI_TEST_MODE: ${MODE}"

if [[ "${MODE}" == "ll" || "${MODE}" == "all" ]]; then
	if ! command -v "${CLANG_BIN}" >/dev/null 2>&1; then
		fail_with_usage "clang not found: ${CLANG_BIN}"
	fi

	if [[ ! -f "${RUNTIME_SOURCE}" ]]; then
		fail_with_usage "Runtime source not found: ${RUNTIME_SOURCE}"
	fi
fi

if [[ "${MODE}" == "asm" || "${MODE}" == "all" ]]; then
	if ! command -v "${RISCV64_GCC_BIN}" >/dev/null 2>&1; then
		fail_with_usage "riscv64 gcc not found: ${RISCV64_GCC_BIN}"
	fi

	if ! command -v "${QEMU_RISCV64_BIN}" >/dev/null 2>&1; then
		fail_with_usage "qemu riscv64 not found: ${QEMU_RISCV64_BIN}"
	fi

	if [[ ! -f "${RUNTIME_LIB}" ]]; then
		fail_with_usage "Runtime archive not found: ${RUNTIME_LIB}"
	fi
fi

if [[ -n "${single_testcase}" ]]; then
	run_testcase "${single_testcase}"
else
	run_all_tests
fi

echo "OK number=${OK_NUM}, NG number=${NG_NUM}"

if [[ ${NG_NUM} -ne 0 ]]; then
	exit 1
fi