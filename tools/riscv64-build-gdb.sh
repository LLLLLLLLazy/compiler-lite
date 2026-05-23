#!/bin/bash

if [ $# -ne 2 ]; then
	echo "riscv64-build-gdb.sh workspacefolder arm32filename"
	exit 1
fi

# 交叉编译程序成RISCV64程序
"$1/build/minic" -S -A -o "$1/tests/$2.s" "$1/tests/$2.c"

# 交叉编译程序成ARM32程序
riscv64-linux-gnu-gcc -g -static --include "$1/tests/std.h" -o "$1/tests/$2" "tests/$2.s" "$1/tests/std.c"

echo "Now run gdb in another window"
echo "Now gdb start"

# 通过qemu运行，同时开启gdb server功能（支持RVV向量扩展）
QEMU_CPU_ARGS=()
if qemu-riscv64-static -cpu rv64,v=true -version >/dev/null 2>&1; then
	QEMU_CPU_ARGS=(-cpu rv64,v=true)
elif qemu-riscv64-static -cpu rv64gcv -version >/dev/null 2>&1; then
	QEMU_CPU_ARGS=(-cpu rv64gcv)
fi
qemu-riscv64-static "${QEMU_CPU_ARGS[@]}" -g 1234 "$1/tests/$2"

echo
echo "Now gdb end"

exit 0