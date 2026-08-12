#!/usr/bin/env bash
# 为 tests/custom_function 生成 .out 基线文件。
# 流程与 tools/run-local-riscv64-tests.sh 中的 g++ 参考路径完全一致：
#   注入 sylib 声明 + g++ -O0 编译为 riscv64 + 静态链接 libsysy_riscv.a
#   + qemu 运行(有 .in 则喂入) + 追加退出码行。
# 用法: ./tools/gen-custom-out.sh [testcase...]   # 不带参数时为目录下所有 *.c
set -eu

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
CASE_DIR="${REPO_ROOT}/tests/custom_function"
RUNTIME_LIB="${MINIC_RUNTIME_LIB:-${REPO_ROOT}/tests/libsysy_riscv.a}"
GXX_BIN="${RISCV64_GXX_BIN:-riscv64-linux-gnu-g++}"
QEMU_BIN="${QEMU_RISCV64_BIN:-qemu-riscv64-static}"

TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/gen-custom-out.XXXXXX")
trap 'rm -rf "${TMP_DIR}"' EXIT

gen_one() {
	local testcase="$1"
	local cfile="${CASE_DIR}/${testcase}.c"
	local infile="${CASE_DIR}/${testcase}.in"
	local outfile="${CASE_DIR}/${testcase}.out"
	local gcc_cfile="${TMP_DIR}/${testcase}.cpp"

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
		sed -E 's/\<starttime\>[[:space:]]*\([[:space:]]*\)/_sysy_starttime(__LINE__)/g; s/\<stoptime\>[[:space:]]*\([[:space:]]*\)/_sysy_stoptime(__LINE__)/g' "${cfile}"
	} > "${gcc_cfile}"

	"${GXX_BIN}" -march=rv64gc -O0 -c -o "${TMP_DIR}/${testcase}.o" "${gcc_cfile}"
	"${GXX_BIN}" -march=rv64gc -static -o "${TMP_DIR}/${testcase}" "${TMP_DIR}/${testcase}.o" "${RUNTIME_LIB}"

	local exit_code=0
	if [[ -f "${infile}" ]]; then
		"${QEMU_BIN}" "${TMP_DIR}/${testcase}" < "${infile}" > "${TMP_DIR}/${testcase}.output" || exit_code=$?
	else
		"${QEMU_BIN}" "${TMP_DIR}/${testcase}" > "${TMP_DIR}/${testcase}.output" || exit_code=$?
	fi

	if [[ -s "${TMP_DIR}/${testcase}.output" ]]; then
		local last_byte
		last_byte=$(tail -c 1 "${TMP_DIR}/${testcase}.output" | od -An -t u1 | tr -d '[:space:]')
		if [[ "${last_byte}" != "10" ]]; then
			printf '\n' >> "${TMP_DIR}/${testcase}.output"
		fi
	fi
	printf '%s\n' "${exit_code}" >> "${TMP_DIR}/${testcase}.output"
	cp "${TMP_DIR}/${testcase}.output" "${outfile}"
	echo "generated ${testcase}.out ($(wc -c < "${outfile}") bytes)"
}

if [[ $# -gt 0 ]]; then
	for arg in "$@"; do
		gen_one "${arg%.*}"
	done
else
	while IFS= read -r cfile; do
		gen_one "$(basename "${cfile}" .c)"
	done < <(find "${CASE_DIR}" -maxdepth 1 -type f -name '*.c' | sort)
fi
