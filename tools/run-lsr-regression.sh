#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
MINIC_BIN=${MINIC_BIN:-"${REPO_ROOT}/build/minic"}
TEST_SOURCE="${REPO_ROOT}/tests/lsr_regression/lsr_multi_stride_versioning.sy"

TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/minic-lsr-regression.XXXXXX")
trap 'rm -rf "${TMP_DIR}"' EXIT

MINIC_BIN="${MINIC_BIN}" MINIC_TEST_MODE=llvmir \
    bash "${SCRIPT_DIR}/run-local-tests.sh" lsr_regression
MINIC_BIN="${MINIC_BIN}" MINIC_TEST_MODE=asm \
    bash "${SCRIPT_DIR}/run-local-tests.sh" lsr_regression

"${MINIC_BIN}" -S -O1 -L -o "${TMP_DIR}/versioned.ll" "${TEST_SOURCE}" >/dev/null
MINIC_DISABLE_LSR_VERSIONING=1 \
    "${MINIC_BIN}" -S -O1 -L -o "${TMP_DIR}/exact.ll" "${TEST_SOURCE}" >/dev/null

versioned_blocks=$(grep -Ec '^[[:alnum:]_.]+:$' "${TMP_DIR}/versioned.ll")
exact_blocks=$(grep -Ec '^[[:alnum:]_.]+:$' "${TMP_DIR}/exact.ll")
versioned_bytes=$(wc -c < "${TMP_DIR}/versioned.ll")
exact_bytes=$(wc -c < "${TMP_DIR}/exact.ll")

if ((versioned_blocks > exact_blocks * 3)); then
    echo "LSR versioning duplicated too many blocks: ${versioned_blocks} vs ${exact_blocks}" >&2
    exit 1
fi
if ((versioned_bytes > exact_bytes * 4)); then
    echo "LSR versioning caused excessive IR growth: ${versioned_bytes} vs ${exact_bytes}" >&2
    exit 1
fi
if ! grep -q 'sdiv i32' "${TMP_DIR}/versioned.ll"; then
    echo "LSR runtime no-wrap check was not generated" >&2
    exit 1
fi

echo "LSR regression checks passed: blocks ${exact_blocks}->${versioned_blocks}, bytes ${exact_bytes}->${versioned_bytes}"
