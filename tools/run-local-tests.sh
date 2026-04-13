#!/usr/bin/env bash

set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

MINIC_BIN=${MINIC_BIN:-"${REPO_ROOT}/build/minic"}
ARM_GCC_BIN=${ARM_GCC_BIN:-"arm-linux-gnueabihf-gcc"}
QEMU_ARM_BIN=${QEMU_ARM_BIN:-"qemu-arm-static"}
TEST_ROOT="${REPO_ROOT}/tests"

OK_NUM=0
NG_NUM=0

TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/minic-tests.XXXXXX")
trap 'rm -rf "${TMP_DIR}"' EXIT

usage() {
    cat <<'USAGE'
Usage:
  ./tools/run-local-tests.sh
  ./tools/run-local-tests.sh <suite>
  ./tools/run-local-tests.sh <suite> <testcase>
  ./tools/run-local-tests.sh <testcase>

Suites:
  2023              -> tests/2023_function
  2025              -> tests/2025_function
  2025_perf         -> tests/2025_performance
  2025_performance  -> tests/2025_performance
  all               -> all suites above

Examples:
  ./tools/run-local-tests.sh
  ./tools/run-local-tests.sh 2023
  ./tools/run-local-tests.sh 2025 2025_func_009_BFS
  ./tools/run-local-tests.sh 2025_perf_01_mm1
USAGE
}

fail_with_usage() {
    echo "$1" >&2
    usage >&2
    exit 1
}

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

run_testcase() {
    local suite_dir="$1"
    local testcase="$2"
    local case_root="${TEST_ROOT}/${suite_dir}"
    local cfile="${case_root}/${testcase}.c"
    local infile="${case_root}/${testcase}.in"
    local outfile="${case_root}/${testcase}.out"
    local asmfile="${TMP_DIR}/${testcase}.s"
    local exe_file="${TMP_DIR}/${testcase}"
    local result_file="${TMP_DIR}/${testcase}.result"
    local output=""
    local exit_code=0

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

    if ! "${MINIC_BIN}" -S -O1 -o "${asmfile}" "${cfile}" >/dev/null 2>&1; then
        echo "${testcase}.c compile NG"
        NG_NUM=$((NG_NUM + 1))
        return
    fi

    if [[ ! -s "${asmfile}" ]]; then
        echo "${asmfile} not generated"
        NG_NUM=$((NG_NUM + 1))
        return
    fi

    if ! "${ARM_GCC_BIN}" -g -static -o "${exe_file}" "${asmfile}" "${TEST_ROOT}/std.c" >/dev/null 2>&1; then
        echo "${testcase}.c link NG"
        NG_NUM=$((NG_NUM + 1))
        return
    fi

    if [[ -f "${infile}" ]]; then
        output="$(${QEMU_ARM_BIN} "${exe_file}" < "${infile}" 2>&1)"
        exit_code=$?
    else
        output="$(${QEMU_ARM_BIN} "${exe_file}" 2>&1)"
        exit_code=$?
    fi

    write_result_file "${output}" "${exit_code}" "${result_file}"

    if ! diff -a --strip-trailing-cr "${result_file}" "${outfile}" >/dev/null 2>&1; then
        echo "${testcase}.c NG"
        NG_NUM=$((NG_NUM + 1))
        return
    fi

    OK_NUM=$((OK_NUM + 1))
    echo "${testcase}.c OK"
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

if ! command -v "${ARM_GCC_BIN}" >/dev/null 2>&1; then
    fail_with_usage "arm-linux-gnueabihf-gcc not found: ${ARM_GCC_BIN}"
fi

if ! command -v "${QEMU_ARM_BIN}" >/dev/null 2>&1; then
    fail_with_usage "qemu-arm-static not found: ${QEMU_ARM_BIN}"
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
