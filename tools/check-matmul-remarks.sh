#!/usr/bin/env bash
# 校验 MatMulInterchange 的识别行为：
#   tests/matmul_performance 下文件名含 _neg_ 的用例必须不触发交换，其余用例必须恰好触发一次。
# 用法: bash tools/check-matmul-remarks.sh

set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
MINIC_BIN=${MINIC_BIN:-"${REPO_ROOT}/build/minic"}
SUITE_DIR="${REPO_ROOT}/tests/matmul_performance"

if [[ ! -x "${MINIC_BIN}" ]]; then
	echo "Compiler not found: ${MINIC_BIN}" >&2
	exit 1
fi

TMP_DIR=$(mktemp -d)
trap 'rm -rf "${TMP_DIR}"' EXIT

fail=0
for sy in "${SUITE_DIR}"/*.sy; do
	name=$(basename "${sy}" .sy)
	applied=$(MINIC_OPT_REMARKS=1 "${MINIC_BIN}" -S --riscv64-rvv=on -O1 -t RISCV64 \
		-o "${TMP_DIR}/${name}.s" "${sy}" 2>&1 | grep -c "matmul-interchange applied")

	if [[ "${name}" == *_neg_* ]]; then
		expected=0
	else
		expected=1
	fi

	if [[ "${applied}" -eq "${expected}" ]]; then
		printf '%-36s OK  (applied=%s)\n' "${name}" "${applied}"
	else
		printf '%-36s NG  (applied=%s expected=%s)\n' "${name}" "${applied}" "${expected}"
		fail=1
	fi
done

exit "${fail}"
