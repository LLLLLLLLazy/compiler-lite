#!/usr/bin/env bash
# 生成 tests/matmul_performance 的 .in（固定种子随机数据）与 .out（riscv64 g++ -O2 参考实现执行结果）。
# .out 格式与 run-local-riscv64-tests.sh 的 write_result_file 约定一致：程序 stdout（保证末尾换行）+ 退出码行。

set -eu

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
SUITE_DIR="${REPO_ROOT}/tests/matmul_performance"
RUNTIME_LIB="${REPO_ROOT}/tests/libsysy_riscv.a"
GCC_BIN=${RISCV64_GCC_BIN:-riscv64-linux-gnu-g++}
QEMU_BIN=${QEMU_RISCV64_BIN:-qemu-riscv64-static}
command -v "${QEMU_BIN}" >/dev/null 2>&1 || QEMU_BIN=qemu-riscv64

TMP_DIR=$(mktemp -d)
trap 'rm -rf "${TMP_DIR}"' EXIT

python3 - "${SUITE_DIR}" <<'PYEOF'
import random
import sys

suite = sys.argv[1]

def ints(rng, count, lo=-100, hi=100):
    return " ".join(str(rng.randint(lo, hi)) for _ in range(count))

def floats(rng, count):
    # 1/16 步长的十进制小数，scanf %f 解析无舍入歧义
    return " ".join("{:.4f}".format(rng.randint(-1600, 1600) / 16.0) for _ in range(count))

def write(name, lines):
    with open(f"{suite}/{name}", "w") as f:
        f.write("\n".join(lines) + "\n")

rng = random.Random(20260610)
write("matmul_val_01_basic.in", ["100"] + [ints(rng, 100) for _ in range(200)])

rng = random.Random(20260611)
write("matmul_val_02_inplace.in", ["120"] + [ints(rng, 120) for _ in range(240)])

rng = random.Random(20260612)
write("matmul_val_03_rect.in",
      ["60 80 100"] + [ints(rng, 80) for _ in range(60)] + [ints(rng, 100) for _ in range(80)])

rng = random.Random(20260613)
write("matmul_val_04_float.in", ["64"] + [floats(rng, 64) for _ in range(128)])

rng = random.Random(20260614)
write("matmul_val_05_mulswap.in", ["64"] + [ints(rng, 64) for _ in range(128)])

rng = random.Random(20260615)
write("matmul_val_06_constbound.in", [ints(rng, 96) for _ in range(192)])

rng = random.Random(20260616)
write("matmul_val_07_matvec.in",
      ["200 300", ints(rng, 200)] + [ints(rng, 300) for _ in range(200)])

rng = random.Random(20260617)
write("matmul_val_08_neg_inplace_x.in", ["80"] + [ints(rng, 80) for _ in range(160)])

rng = random.Random(20260618)
write("matmul_val_09_neg_cond.in", ["64"] + [ints(rng, 64) for _ in range(128)])

print("inputs generated")
PYEOF

for sy in "${SUITE_DIR}"/*.sy; do
	name=$(basename "${sy}" .sy)
	ref_c="${TMP_DIR}/${name}.ref.c"
	ref_exe="${TMP_DIR}/${name}.ref"
	{
		cat <<'EOF'
#ifdef __cplusplus
extern "C" {
#endif
int getint(void);
int getch(void);
int getarray(int a[]);
float getfloat(void);
int getfarray(float a[]);
void putint(int a);
void putch(int a);
void putarray(int n, int a[]);
void putfloat(float a);
void putfarray(int n, float a[]);
void putf(char a[], ...);
void _sysy_starttime(int lineno);
void _sysy_stoptime(int lineno);
#ifdef __cplusplus
}
#endif
EOF
		sed -E 's/\<starttime\>[[:space:]]*\([[:space:]]*\)/_sysy_starttime(__LINE__)/g; s/\<stoptime\>[[:space:]]*\([[:space:]]*\)/_sysy_stoptime(__LINE__)/g' "${sy}"
	} > "${ref_c}"

	"${GCC_BIN}" -march=rv64gcv -O2 -c -o "${TMP_DIR}/${name}.ref.o" "${ref_c}"
	"${GCC_BIN}" -march=rv64gcv -static -o "${ref_exe}" "${TMP_DIR}/${name}.ref.o" "${RUNTIME_LIB}"

	out_file="${SUITE_DIR}/${name}.out"
	set +e
	"${QEMU_BIN}" "${ref_exe}" < "${SUITE_DIR}/${name}.in" > "${TMP_DIR}/${name}.stdout" 2> /dev/null
	exit_code=$?
	set -e

	cp "${TMP_DIR}/${name}.stdout" "${out_file}"
	if [[ -s "${out_file}" ]]; then
		last_byte=$(tail -c 1 "${out_file}" | od -An -t u1 | tr -d '[:space:]')
		if [[ "${last_byte}" != "10" ]]; then
			printf '\n' >> "${out_file}"
		fi
	fi
	printf '%s\n' "${exit_code}" >> "${out_file}"
	echo "${name}: exit=${exit_code} out=$(head -1 "${out_file}")"
done
