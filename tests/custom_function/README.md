# custom_function 测试套件

基于 2023_function / 2025_function / 2025_performance / 2026_function /
2026_performance 的用例形态设计，目标是在当前 minic 前端支持下，以
**单个源文件、确定性输出、QEMU 毫秒级运行**为约束，尽可能广泛地覆盖
IR 与 RISC-V64 后端的正确性。O0 与 O1 均须通过：

```bash
./tools/run-local-riscv64-tests.sh custom_function                       # O0
MINIC_RISCV64_OPT_LEVEL=1 ./tools/run-local-riscv64-tests.sh custom_function  # O1
```

## 编写约束(默认文法, 无 -e)

1. 比较/逻辑表达式只能出现在 if/while/for 条件位置；**括号内也是
   AddExp 级**(`(a||b)`、`(x<y)` 都会报错)，需要布尔值时用辅助变量
   或 int 辅助函数。
2. 赋值是语句不是表达式(不能 `0 && (a=1)`)；无位运算、三目、switch、
   函数原型声明(相互递归只能通过调用图已定义的函数实现，或改写成
   迭代/显式栈)。
3. 无字符字面量(用 ASCII 数字 + putch)；字符串字面量只能作为
   putf 格式串等固定位置实参，`%s` 变参位置不可用。
4. 输入用例用 `.in` 文件驱动 getint/getch/getarray/getfarray/getfloat；
   getfloat/getfarray 的 scanf 格式是 `%a`(十六进制浮点)。
5. 浮点输出用 putfloat(`%a` 格式)完全确定。**算术密集的浮点用例只用
   可精确表示的二进制浮点数(二进有理数)**：后端 peephole 会把
   fmul+fadd 融合为 fmadd，与 g++ -O0 参考的舍入不同，精确值可规避
   该差异(见 custom_102 注释)。
6. 避开 C 未定义行为：无 INT_MIN/-1、无除零；有意的 32 位回绕仅沿用
   custom_09/custom_98 的既有模式。

## .out 基线生成

`tools/gen-custom-out.sh` 复刻测试脚本的 g++ 参考路径(注入 sylib
声明 + g++ -O0 + 静态链接 libsysy_riscv.a + qemu 运行 + 追加退出码)：

```bash
./tools/gen-custom-out.sh custom_101_stats_input   # 单个
./tools/gen-custom-out.sh                          # 全部
```

生成后请分别在 O0/O1 下跑一遍套件确认(两次都应与基线一致)。

## 覆盖映射(按编号)

| 编号 | 用例 | 主要压力点 |
|---|---|---|
| 01–19 | 前端语法 | 运算符优先级/负除模/短路/自增减/字面量/回绕/dangling-else/循环控制 |
| 20–35 | 数组与递归基础 | 多维数组、数组形参、全局数组、递归 |
| 36–50 | 函数与 I/O | void/嵌套调用/out 参数/putf/getch |
| 51–60 | 排序与查找 | 各类排序、二分、计数排序、逆序对 |
| 61–80 | 数论与 DP | 筛法、快速幂、组合、背包、LIS/LCS、编辑距离 |
| 81–99 | 图与矩阵 | BFS/DFS、Floyd、矩阵乘幂、并查集、拓扑、生命游戏 |
| 100 | 综合 | 成绩统计 |
| 101–103 | 输入驱动 | getarray/getfarray/getint/getfloat/putarray/putfarray 全 API |
| 104 | static 局部量 | 函数内 static 标量/数组缓存(Mem2Reg 逃逸判定) |
| 105 | 调用图 | 死函数/死全局写/过程间常量传播/内联候选 |
| 106 | 长参数列表 | 12 参走栈传参、混合标量/浮点/数组形参 |
| 107 | 尾递归 | TailRecursionElim |
| 108–110 | 回溯 | N 皇后/数独/组合总和(110 为 foldUnitStepIncrements 跨 call 回归形态) |
| 111–115 | 图算法 | Dijkstra/Bellman-Ford/Kruskal/Prim/Kahn 拓扑 |
| 116–118 | 数据结构 | 二叉堆、线段树、循环队列 |
| 119–121 | 序列算法 | KMP、Manacher、滚动哈希 |
| 122–124 | 数论 | 扩展欧几里得/模逆元、phi 线性筛、大整数加减乘 |
| 125–128 | 循环变换 | 矩形矩阵乘(MatMulInterchange 候选)、条件归约矩阵乘(孤儿 xTrueLoad 回归形态)、box blur 边界守卫、转置守卫尾部(GuardedTailCollapse 形态) |
| 129–130 | DP 重构 | 背包方案回溯、编辑距离对齐回溯 |
| 131 | 高精度 | 100! 进位链 |
| 132 | 求值器 | 调度场算法(无函数原型下实现表达式求值) |
| 133 | 模拟 | 2048 行合并(原地读写混叠) |
| 134 | 浮点综合 | Kahan 补偿求和/浮点排序/浮点二分 |
| 135 | 确定性随机 | LCG 洗牌/随机快排/蒙特卡洛 |
| 136 | 寄存器压力 | 14 个跨调用存活局部量 + 长表达式 + 12 参调用(RA 溢出压力) |
| 137 | 地址折叠回归 | 跨分支零存储地址活跃性 |
| 138 | 边界立即数回归 | INT_MIN/INT_MAX 的 RV64 常量物化 |

## 已捕获的缺陷

- e4ed292 peephole foldUnitStepIncrements 跨 call 丢定义(custom_66 形态)
- reduceAffineAddressRecurrences 以循环体内定义的寄存器作指针递推基址
  (custom_110 触发, 已修复)
- foldConsecutiveZeroStores 链外使用检查在控制边界处截断, 跨块使用被漏掉
  (custom_115 触发, 已修复; 同 pass 的模式 2 一并加固)
- foldConsecutiveZeroStores 删除后继块仍使用的零存储地址定义
  (custom_137 触发, 已修复)
- RV64 常量物化在 INT_MIN 边界发生宿主有符号整数溢出
  (custom_138 触发, 已修复)
