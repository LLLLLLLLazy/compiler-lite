#!/usr/bin/env bash

set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

MINIC_BIN=${MINIC_BIN:-"${REPO_ROOT}/build/minic"}
TEST_ROOT="${REPO_ROOT}/tests"
RUNTIME_SOURCE=${MINIC_RUNTIME_SOURCE:-"${TEST_ROOT}/sylib.c"}
FRONTEND=${MINIC_FRONTEND:-"antlr"}
TEST_MODE=${MINIC_TEST_MODE:-"llvmir"}
CLANG_BIN=${CLANG_BIN:-"clang"}
TEST_TIMEOUT=${MINIC_TEST_TIMEOUT:-30}
ASM_TARGET=${MINIC_ASM_TARGET:-"RISCV64"}
RISCV64_GCC_BIN=${RISCV64_GCC_BIN:-"riscv64-linux-gnu-gcc"}
QEMU_RISCV64_BIN=${QEMU_RISCV64_BIN:-""}

if [[ -z "${QEMU_RISCV64_BIN}" ]]; then
    if command -v qemu-riscv64-static >/dev/null 2>&1; then
        QEMU_RISCV64_BIN="qemu-riscv64-static"
    else
        QEMU_RISCV64_BIN="qemu-riscv64"
    fi
fi

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
  2026              -> tests/2026_function
  2026_perf         -> tests/2026_performance
  2026_performance  -> tests/2026_performance
  all               -> all suites above

Environment:
  MINIC_FRONTEND=antlr      Use ANTLR frontend (default)
  MINIC_FRONTEND=recursive  Use recursive-descent frontend
  MINIC_FRONTEND=default    Use compiler default frontend
    MINIC_RUNTIME_SOURCE=./tests/sylib.c

  MINIC_TEST_MODE=llvmir    Verify generated LLVM IR via clang (default)
  MINIC_TEST_MODE=asm       Verify generated target assembly via cross-compile + qemu
  MINIC_TEST_MODE=ast       Verify AST image generation
  MINIC_TEST_MODE=all       Run llvmir + asm + ast checks together
  MINIC_TEST_TIMEOUT=30     Per-step timeout passed to timeout(1)
  MINIC_ASM_TARGET=RISCV64  Assembly backend target (default and only supported target)
  RISCV64_GCC_BIN=...       RISC-V cross compiler for asm mode
  QEMU_RISCV64_BIN=...      RISC-V user-mode emulator for asm mode

Examples:
  ./tools/run-local-tests.sh
  ./tools/run-local-tests.sh 2023
  ./tools/run-local-tests.sh 2025 2025_func_009_BFS
  ./tools/run-local-tests.sh 2025 2025_func_009_BFS.sy
  MINIC_TEST_MODE=llvmir ./tools/run-local-tests.sh 2023_func_00_main
  MINIC_TEST_MODE=asm MINIC_ASM_TARGET=RISCV64 ./tools/run-local-tests.sh 2026 2026_func_96_matrix_add
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
    llvmir|asm|ast|all)
        ;;
    *)
        fail_with_usage "Unknown MINIC_TEST_MODE: ${TEST_MODE}"
        ;;
esac

if [[ "${TEST_MODE}" != "ast" && ! -f "${RUNTIME_SOURCE}" ]]; then
    fail_with_usage "Runtime source not found: ${RUNTIME_SOURCE}"
fi

case "${ASM_TARGET}" in
    RISCV64)
        ;;
    *)
        fail_with_usage "Unknown MINIC_ASM_TARGET: ${ASM_TARGET}. Only RISCV64 is supported."
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

run_asm_check() {
    local cfile="$1"
    local infile="$2"
    local outfile="$3"
    local testcase="$4"
    local source_name
    local asmfile="${TMP_DIR}/${testcase}.s"
    local exe_file="${TMP_DIR}/${testcase}"
    local output_file="${TMP_DIR}/${testcase}.asm.output"
    local stderr_file="${TMP_DIR}/${testcase}.asm.stderr"
    local result_file="${TMP_DIR}/${testcase}.asm.result"
    local exit_code=0
    source_name=$(basename "${cfile}")

    if ! timeout --foreground "${TEST_TIMEOUT}" \
        "${MINIC_BIN}" -S "${frontend_args[@]}" -O1 -t "${ASM_TARGET}" -o "${asmfile}" "${cfile}" >/dev/null 2>&1; then
        echo "${source_name} compile NG [asm]"
        return 1
    fi

    if [[ ! -s "${asmfile}" ]]; then
        echo "${asmfile} not generated [asm]"
        return 1
    fi

    if ! timeout --foreground "${TEST_TIMEOUT}" \
        "${RISCV64_GCC_BIN}" -g -static -o "${exe_file}" "${asmfile}" "${RUNTIME_SOURCE}" >/dev/null 2>&1; then
        echo "${source_name} link NG [asm]"
        return 1
    fi

    if [[ -f "${infile}" ]]; then
        timeout --foreground "${TEST_TIMEOUT}" "${QEMU_RISCV64_BIN}" "${exe_file}" < "${infile}" > "${output_file}" 2> "${stderr_file}"
        exit_code=$?
    else
        timeout --foreground "${TEST_TIMEOUT}" "${QEMU_RISCV64_BIN}" "${exe_file}" > "${output_file}" 2> "${stderr_file}"
        exit_code=$?
    fi

    write_result_file "${output_file}" "${exit_code}" "${result_file}"

    if ! diff -a --strip-trailing-cr "${result_file}" "${outfile}" >/dev/null 2>&1; then
        echo "${source_name} NG [asm]"
        return 1
    fi

    echo "${source_name} OK [asm]"
    return 0
}

run_llvmir_check() {
    local cfile="$1"
    local infile="$2"
    local outfile="$3"
    local testcase="$4"
    local source_name
    local llfile="${TMP_DIR}/${testcase}.ll"
    local exe_file="${TMP_DIR}/${testcase}_ll"
    local output_file="${TMP_DIR}/${testcase}.llvmir.output"
    local stderr_file="${TMP_DIR}/${testcase}.llvmir.stderr"
    local result_file="${TMP_DIR}/${testcase}.llvmir.result"
    local exit_code=0
    source_name=$(basename "${cfile}")

    if ! timeout --foreground "${TEST_TIMEOUT}" "${MINIC_BIN}" -S "${frontend_args[@]}" -L -o "${llfile}" "${cfile}" >/dev/null 2>&1; then
        echo "${source_name} compile NG [llvmir]"
        return 1
    fi

    if [[ ! -s "${llfile}" ]]; then
        echo "${llfile} not generated [llvmir]"
        return 1
    fi

    if ! timeout --foreground "${TEST_TIMEOUT}" "${CLANG_BIN}" -Wno-override-module -o "${exe_file}" "${llfile}" "${RUNTIME_SOURCE}" >/dev/null 2>&1; then
        echo "${source_name} link NG [llvmir]"
        return 1
    fi

    if [[ -f "${infile}" ]]; then
        timeout --foreground "${TEST_TIMEOUT}" "${exe_file}" < "${infile}" > "${output_file}" 2> "${stderr_file}"
        exit_code=$?
    else
        timeout --foreground "${TEST_TIMEOUT}" "${exe_file}" > "${output_file}" 2> "${stderr_file}"
        exit_code=$?
    fi

    write_result_file "${output_file}" "${exit_code}" "${result_file}"

    if ! diff -a --strip-trailing-cr "${result_file}" "${outfile}" >/dev/null 2>&1; then
        echo "${source_name} NG [llvmir]"
        return 1
    fi

    echo "${source_name} OK [llvmir]"
    return 0
}

run_ast_check() {
    local cfile="$1"
    local case_root="$2"
    local testcase="$3"
    local source_name
    local astfile="${TMP_DIR}/${testcase}.png"
    local expected_png="${case_root}/${testcase}.png"
    local expected_svg="${case_root}/${testcase}.svg"
    source_name=$(basename "${cfile}")

    if ! timeout --foreground "${TEST_TIMEOUT}" "${MINIC_BIN}" -S "${frontend_args[@]}" -T -o "${astfile}" "${cfile}" >/dev/null 2>&1; then
        echo "${source_name} compile NG [ast]"
        return 1
    fi

    if [[ ! -s "${astfile}" ]]; then
        echo "${astfile} not generated [ast]"
        return 1
    fi

    if [[ -f "${expected_png}" ]]; then
        if ! cmp -s "${astfile}" "${expected_png}"; then
            echo "${source_name} NG [ast-png]"
            return 1
        fi
    elif [[ -f "${expected_svg}" ]]; then
        echo "${source_name} NG [ast] expected SVG reference is not supported yet"
        return 1
    fi

    echo "${source_name} OK [ast]"
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
    local failed=0

    if [[ -z "${cfile}" || ! -f "${cfile}" ]]; then
        echo "${case_root}/${testcase}.{c,sy} not found"
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

    if [[ "${TEST_MODE}" == "llvmir" || "${TEST_MODE}" == "all" ]]; then
        if ! run_llvmir_check "${cfile}" "${infile}" "${outfile}" "${testcase}"; then
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

if [[ "${TEST_MODE}" == "asm" || "${TEST_MODE}" == "all" ]]; then
    if ! command -v "${RISCV64_GCC_BIN}" >/dev/null 2>&1; then
        fail_with_usage "riscv64 gcc not found: ${RISCV64_GCC_BIN}"
    fi

    if ! command -v "${QEMU_RISCV64_BIN}" >/dev/null 2>&1; then
        fail_with_usage "qemu riscv64 not found: ${QEMU_RISCV64_BIN}"
    fi
fi

if [[ "${TEST_MODE}" == "llvmir" || "${TEST_MODE}" == "all" ]]; then
    if ! command -v "${CLANG_BIN}" >/dev/null 2>&1; then
        fail_with_usage "clang not found: ${CLANG_BIN}"
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
