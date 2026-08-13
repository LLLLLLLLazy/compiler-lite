# RVV 支持与上板准备

## 目标与启用方式

当前后端面向 RVV 1.0 的完整 `V` 扩展，汇编目标为 `rv64gcv`，向量元素固定为
`e32,m1`。RVV 默认关闭；仅在 RISCV64 汇编路径使用 `-O1
--riscv64-rvv=on` 时注册 `LoopVectorize`。LLVM IR 输出不会插入后端专用向量 IR。

生成代码采用 strip-mining：每轮以剩余迭代数执行 `vsetvli`，不假定实际 VLEN，
因此同一二进制可以在不同 VLEN 的 RVV 1.0 实现上执行。RVV 1.0 规定 VLEN 是不大于
65,536 bit 的 2 的幂；完整 `V` 扩展的最小 VLEN 为 128 bit。规范见
[RISC-V Vector Extension 1.0](https://docs.riscv.org/reference/isa/unpriv/v-st-ext)。

## 当前已支持范围

- `i32` 和 `float` 的单位步长、正固定步长 load/store
- `i32`/`float` 的逐元素加、减、乘及其表达式树
- `i32` 加法归约；按 32 bit 模运算结合，退出时使用 `vredsum.vs`
- 严格顺序的 `float` 加法归约；每个 strip 使用 `vfredosum.vs`，strip 之间由标量
  phi 串联，保持原标量循环的加法顺序
- 动态与常量上界、零迭代、非 VLMAX 整数倍尾部
- 一个函数中的多个独立合法循环，以及嵌套结构中的合法最内层循环
- VR 寄存器压力下的 spill/reload；使用 `vs1r.v`、`vl1re32.v` 和 `vmv1r.v`
  保存完整寄存器，不依赖当时的 `vl/vtype`

向量 spill 槽按 RVV 1.0 的架构最大 VLEN 预留 8,192 字节。这样会让极端 VR 压力
函数的栈帧变大，但不会在未知 VLEN 的板卡上越界覆盖相邻栈对象。

## 合法性边界

当前 pass 会保守拒绝以下形态，拒绝时保留原标量循环：

- 非 `i++ && i < bound` 的规范归纳循环，或包含多个 body/latch 块的复杂 CFG
- 循环体内的调用、body phi、条件执行、比较/select、整数除模或浮点除法
- 负步长指针、数组 decay 递推，以及 `step * 2048` 不能由 i32 GEP 索引表示的步长
- 两个可能别名的 store，或无法证明为逐迭代同地址读后写的 load/store 组合
- 同时包含普通 store 与归约、多个归约变量，或无法完整物化的归约表达式

不同全局对象和不同 alloca 可证明不别名。形式参数指针之间没有 noalias 契约，因此
除同一指针递推的原地读写外会保守拒绝；当前没有生成运行期别名检查和标量回退版本。

## 正确性关键点

- `vsetvli` 即使目标 GPR 已死也必须保留，因为后续 RVV 指令隐式读取 `vl/vtype`
- 整数归约使用 tail-undisturbed 策略；VR copy 和 spill 必须保存整个寄存器的 tail lane
- 浮点归约不能使用跨 lane 的无序累加器，否则会改变 IEEE-754 舍入顺序
- strided 访存的 stride 是字节数，而 IR 指针递推步长是元素数
- 向量访存属于真实内存副作用，机器 peephole 必须把它纳入 load/store 屏障判断

## 本地验证

专项回归同时检查生成的关键 RVV 指令，并把同一份静态链接二进制放到 QEMU 的最小/较大
VLEN 下执行：

```bash
bash tools/run-rvv-regression.sh
```

覆盖用例：

- `custom_139_rvv_reduction`：整数尾部归约、浮点严格顺序、同函数多循环、`vsetvli`
  隐式状态
- `custom_140_rvv_vector_spill`：31 路右结合归约制造 VR 压力，覆盖累加器与临时向量
  的整寄存器 spill/reload

全套功能和性能回归可使用：

```bash
MINIC_RISCV64_RVV=on MINIC_FUNC_OPT_LEVEL=1 \
QEMU_RISCV64_CPU=rv64,v=true,vlen=128 \
bash tools/run-local-riscv64-tests.sh 2026

MINIC_RISCV64_RVV=on MINIC_RISCV64_TIMEOUT=120 \
QEMU_RISCV64_CPU=rv64,v=true,vlen=128 \
bash tools/run-local-riscv64-tests.sh 2026_performance
```

## 决赛板验证清单

1. 确认 CPU、内核和上下文切换均支持完整 `V` 扩展，而不是只看工具链能否汇编
2. 先运行 `MINIC_RISCV64_RVV=on bash tools/run-native-riscv64-tests.sh <case>`；脚本会检查
   `/proc/cpuinfo` 并执行最小 `vsetvli` 探针
3. 在同一板卡上对 RVV on/off 做功能差分，再做性能 A/B；QEMU 的逐 lane 仿真耗时不能
   代表真机收益
4. 至少覆盖归约、非整除尾部、正 stride、VR spill 和一个函数多个循环
5. 记录板卡 VLEN、CPU 型号、内核、GCC/binutils 版本和失败汇编，便于复现

## 尚未由本地环境证明的事项

- 当前 QEMU 只执行验证到 VLEN=1024；8,192 字节最大槽和整寄存器指令对更大 VLEN 的
  正确性来自 RVV 1.0 架构约束与静态检查，仍应在目标板实际 VLEN 上复验
- 尚未经过决赛物理板的 CPU、内核向量上下文和真实访存实现验证
- 收益阈值是通用启发式，必须用决赛板性能数据重新评估；正确性通过不等于默认开启后
  一定提速
