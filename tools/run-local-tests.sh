#!/usr/bin/env bash

set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

MINIC_BIN=${MINIC_BIN:-"${REPO_ROOT}/build/minic"}
ARM_GCC_BIN=${ARM_GCC_BIN:-"arm-linux-gnueabihf-gcc"}
QEMU_ARM_BIN=${QEMU_ARM_BIN:-"qemu-arm-static"}
TEST_ROOT="${REPO_ROOT}/tests"
FRONTEND=${MINIC_FRONTEND:-"antlr"}
TEST_MODE=${MINIC_TEST_MODE:-"asm"}

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

Environment:
  MINIC_FRONTEND=antlr      Use ANTLR frontend (default)
  MINIC_FRONTEND=recursive  Use recursive-descent frontend
  MINIC_FRONTEND=default    Use compiler default frontend

  MINIC_TEST_MODE=asm       Verify generated assembly via cross-compile + qemu (default)
  MINIC_TEST_MODE=ir        Verify generated DragonIR via IRCompiler
  MINIC_TEST_MODE=ast       Verify AST image generation
  MINIC_TEST_MODE=all       Run asm + ir + ast checks together

Examples:
  ./tools/run-local-tests.sh
  ./tools/run-local-tests.sh 2023
  ./tools/run-local-tests.sh 2025 2025_func_009_BFS
  MINIC_TEST_MODE=ir ./tools/run-local-tests.sh 2023_func_00_main
  MINIC_TEST_MODE=all ./tools/run-local-tests.sh 2023 2023_func_00_main
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
    asm|ir|ast|all)
        ;;
    *)
        fail_with_usage "Unknown MINIC_TEST_MODE: ${TEST_MODE}"
        ;;
esac

OS_KIND=$(uname -s)
OS_ARCH=$(uname -m)
LINUX_DISTRO=$(lsb_release -i -s)
LINUX_RELEASE=$(lsb_release -r -s)
IR_RUNNER_DEFAULT="${REPO_ROOT}/tools/IRCompiler/${OS_KIND}-${OS_ARCH}/${LINUX_DISTRO}-${LINUX_RELEASE}/IRCompiler"
IR_RUNNER_BIN=${IR_RUNNER_BIN:-"${IR_RUNNER_DEFAULT}"}

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

run_asm_check() {
    local cfile="$1"
    local infile="$2"
    local outfile="$3"
    local testcase="$4"
    local asmfile="${TMP_DIR}/${testcase}.s"
    local exe_file="${TMP_DIR}/${testcase}"
    local result_file="${TMP_DIR}/${testcase}.asm.result"
    local output=""
    local exit_code=0

    if ! "${MINIC_BIN}" -S "${frontend_args[@]}" -O1 -o "${asmfile}" "${cfile}" >/dev/null 2>&1; then
        echo "${testcase}.c compile NG [asm]"
        return 1
    fi

    if [[ ! -s "${asmfile}" ]]; then
        echo "${asmfile} not generated [asm]"
        return 1
    fi

    if ! "${ARM_GCC_BIN}" -g -static -o "${exe_file}" "${asmfile}" "${TEST_ROOT}/std.c" >/dev/null 2>&1; then
        echo "${testcase}.c link NG [asm]"
        return 1
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
        echo "${testcase}.c NG [asm]"
        return 1
    fi

    echo "${testcase}.c OK [asm]"
    return 0
}

run_ir_check() {
    local cfile="$1"
    local outfile="$2"
    local testcase="$3"
    local irfile="${TMP_DIR}/${testcase}.ir"
    local result_file="${TMP_DIR}/${testcase}.ir.result"
    local output=""
    local exit_code=0

    if ! "${MINIC_BIN}" -S "${frontend_args[@]}" -I -O1 -o "${irfile}" "${cfile}" >/dev/null 2>&1; then
        echo "${testcase}.c compile NG [ir]"
        return 1
    fi

    if [[ ! -s "${irfile}" ]]; then
        echo "${irfile} not generated [ir]"
        return 1
    fi

    output="$(${IR_RUNNER_BIN} -R "${irfile}" 2>&1)"
    exit_code=$?

    write_result_file "${output}" "${exit_code}" "${result_file}"

    if ! diff -a --strip-trailing-cr "${result_file}" "${outfile}" >/dev/null 2>&1; then
        echo "${testcase}.c NG [ir]"
        return 1
    fi

    echo "${testcase}.c OK [ir]"
    return 0
}

run_ast_check() {
    local cfile="$1"
    local case_root="$2"
    local testcase="$3"
    local astfile="${TMP_DIR}/${testcase}.png"
    local expected_png="${case_root}/${testcase}.png"
    local expected_svg="${case_root}/${testcase}.svg"

    if ! "${MINIC_BIN}" -S "${frontend_args[@]}" -T -o "${astfile}" "${cfile}" >/dev/null 2>&1; then
        echo "${testcase}.c compile NG [ast]"
        return 1
    fi

    if [[ ! -s "${astfile}" ]]; then
        echo "${astfile} not generated [ast]"
        return 1
    fi

    if [[ -f "${expected_png}" ]]; then
        if ! cmp -s "${astfile}" "${expected_png}"; then
            echo "${testcase}.c NG [ast-png]"
            return 1
        fi
    elif [[ -f "${expected_svg}" ]]; then
        echo "${testcase}.c NG [ast] expected SVG reference is not supported yet"
        return 1
    fi

    echo "${testcase}.c OK [ast]"
    return 0
}

run_testcase() {
    local suite_dir="$1"
    local testcase="$2"
    local case_root="${TEST_ROOT}/${suite_dir}"
    local cfile="${case_root}/${testcase}.c"
    local infile="${case_root}/${testcase}.in"
    local outfile="${case_root}/${testcase}.out"
    local failed=0

    if [[ ! -f "${cfile}" ]]; then
        echo "${cfile} not found"
        NG_NUM=$((NG_NUM + 1))
        return
    fi

    if [[ "${TEST_MODE}" != "ast" && ! -f "${outfile}" ]]; then
        echo "${outfile} not found"
        NG_NUM=$((NG_NUM + 1))
        return
    fi

    if [[ "${TEST_MODE}" == "asm" || "${TEST_MODE}" == "all" ]]; then
        if ! run_asm_check "${cfile}" "${infile}" "${outfile}" "${testcase}"; then
            failed=1
        fi
    fi

    if [[ "${TEST_MODE}" == "ir" || "${TEST_MODE}" == "all" ]]; then
        if ! run_ir_check "${cfile}" "${outfile}" "${testcase}"; then
            failed=1
        fi
    fi

    if [[ "${TEST_MODE}" == "ast" || "${TEST_MODE}" == "all" ]]; then
        if ! run_ast_check "${cfile}" "${case_root}" "${testcase}"; then
            failed=1
        fi
    fi

    if [[ ${failed} -ne 0 ]]; then
        NG_NUM=$((NG_NUM + 1))
    else
        OK_NUM=$((OK_NUM + 1))
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

if [[ "${TEST_MODE}" == "asm" || "${TEST_MODE}" == "all" ]]; then
    if ! command -v "${ARM_GCC_BIN}" >/dev/null 2>&1; then
        fail_with_usage "arm-linux-gnueabihf-gcc not found: ${ARM_GCC_BIN}"
    fi

    if ! command -v "${QEMU_ARM_BIN}" >/dev/null 2>&1; then
        fail_with_usage "qemu-arm-static not found: ${QEMU_ARM_BIN}"
    fi
fi

if [[ "${TEST_MODE}" == "ir" || "${TEST_MODE}" == "all" ]]; then
    if [[ ! -x "${IR_RUNNER_BIN}" ]]; then
        fail_with_usage "IR runner not found: ${IR_RUNNER_BIN}"
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
