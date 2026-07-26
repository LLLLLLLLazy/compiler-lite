#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
MINIC_BIN=${MINIC_BIN:-"${REPO_ROOT}/build/minic"}
TEST_ROOT="${REPO_ROOT}/tests/pure_function_regression"

TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/minic-purity.XXXXXX")
trap 'rm -rf "${TMP_DIR}"' EXIT

MINIC_TEST_MODE=asm \
MINIC_TEST_TIMEOUT=${MINIC_TEST_TIMEOUT:-20} \
MINIC_ASM_OPT_LEVEL=1 \
bash "${SCRIPT_DIR}/run-local-tests.sh" pure_function_regression

"${MINIC_BIN}" -S -O 1 -t RISCV64 \
    -o "${TMP_DIR}/self.s" "${TEST_ROOT}/purity_self_recursion.sy" >/dev/null
"${MINIC_BIN}" -S -O 1 -t RISCV64 \
    -o "${TMP_DIR}/mutual.s" "${TEST_ROOT}/purity_mutual_recursion.sy" >/dev/null
"${MINIC_BIN}" -S -O 1 -t RISCV64 \
    -o "${TMP_DIR}/impure.s" "${TEST_ROOT}/purity_impure_mutual_recursion.sy" >/dev/null

if ! grep -q '^\.comm _memo_hash_key0_fibonacci,' "${TMP_DIR}/self.s"; then
    echo "pure self-recursion was not memoized" >&2
    exit 1
fi

mutual_calls=$(
    sed -n '/^main:/,/^\.size main/p' "${TMP_DIR}/mutual.s" |
        grep -Ec '^[[:space:]]*call[[:space:]]+first$' || true
)
if [[ "${mutual_calls}" -ne 1 ]]; then
    echo "pure mutual-recursion calls were not eliminated: ${mutual_calls}" >&2
    exit 1
fi

impure_calls=$(
    sed -n '/^main:/,/^\.size main/p' "${TMP_DIR}/impure.s" |
        grep -Ec '^[[:space:]]*call[[:space:]]+second$' || true
)
if [[ "${impure_calls}" -ne 2 ]]; then
    echo "impure mutual-recursion calls were incorrectly eliminated: ${impure_calls}" >&2
    exit 1
fi

echo "pure-function SCC regression checks passed"
