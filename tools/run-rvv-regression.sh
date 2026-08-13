#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

MINIC_BIN=${MINIC_BIN:-"${REPO_ROOT}/build/minic"}
RISCV64_GCC_BIN=${RISCV64_GCC_BIN:-"riscv64-linux-gnu-gcc"}
QEMU_RISCV64_BIN=${QEMU_RISCV64_BIN:-""}
RVV_VLENS=${MINIC_RVV_TEST_VLENS:-"128 1024"}
REDUCTION_CASE=custom_139_rvv_reduction
SPILL_CASE=custom_140_rvv_vector_spill
read -r -a rvv_vlens <<< "${RVV_VLENS}"

unset QEMU_VERSION

if [[ -z "${QEMU_RISCV64_BIN}" ]]; then
    if command -v qemu-riscv64-static >/dev/null 2>&1; then
        QEMU_RISCV64_BIN=qemu-riscv64-static
    else
        QEMU_RISCV64_BIN=qemu-riscv64
    fi
fi

TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/minic-rvv-regression.XXXXXX")
trap 'rm -rf "${TMP_DIR}"' EXIT

if [[ ! -x "${MINIC_BIN}" ]]; then
    echo "Compiler not found: ${MINIC_BIN}" >&2
    exit 1
fi

"${MINIC_BIN}" -S --riscv64-rvv=on -O1 -t RISCV64 \
    -o "${TMP_DIR}/${REDUCTION_CASE}.s" \
    "${REPO_ROOT}/tests/custom_function/${REDUCTION_CASE}.c" >/dev/null
"${MINIC_BIN}" -S --riscv64-rvv=on -O1 -t RISCV64 \
    -o "${TMP_DIR}/${SPILL_CASE}.s" \
    "${REPO_ROOT}/tests/custom_function/${SPILL_CASE}.c" >/dev/null

for opcode in vredsum.vs vfredosum.vs vmv1r.v; do
    if ! rg -q "${opcode}" "${TMP_DIR}/${REDUCTION_CASE}.s"; then
        echo "RVV reduction regression did not emit ${opcode}" >&2
        exit 1
    fi
done

for opcode in vs1r.v vl1re32.v; do
    if ! rg -q "${opcode}" "${TMP_DIR}/${SPILL_CASE}.s"; then
        echo "RVV spill regression did not emit ${opcode}" >&2
        exit 1
    fi
done

for testcase in "${REDUCTION_CASE}" "${SPILL_CASE}"; do
    "${RISCV64_GCC_BIN}" -static -march=rv64gcv \
        -o "${TMP_DIR}/${testcase}" \
        "${TMP_DIR}/${testcase}.s" "${REPO_ROOT}/tests/libsysy_riscv.a"
    sed '$d' "${REPO_ROOT}/tests/custom_function/${testcase}.out" \
        > "${TMP_DIR}/${testcase}.expected"
done

for vlen in "${rvv_vlens[@]}"; do
    cpu="rv64,v=true,vlen=${vlen}"
    echo "RVV regression: VLEN=${vlen}"
    for testcase in "${REDUCTION_CASE}" "${SPILL_CASE}"; do
        "${QEMU_RISCV64_BIN}" -cpu "${cpu}" "${TMP_DIR}/${testcase}" \
            > "${TMP_DIR}/${testcase}.${vlen}.actual"
        diff -a --strip-trailing-cr \
            "${TMP_DIR}/${testcase}.${vlen}.actual" \
            "${TMP_DIR}/${testcase}.expected"
        echo "${testcase} OK [VLEN=${vlen}, same binary]"
    done
done
