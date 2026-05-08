# 常量除法优化流程图

## 优化总览

常量除法优化在指令选择阶段（`InstSelectorRiscV64`）内联执行，采用三层递进策略：

1. **2的幂次除法** → 移位+bias修正（最快）
2. **一般常量除法** → Hacker's Delight magic number乘法+移位序列（较快）
3. **非常量除法** → 回退到 `divw`/`remw` 指令（最慢）

```mermaid
flowchart TD
    Start(["translate_div / translate_mod"]) --> Pow2Check{{"除数是2的小幂次?<br>powerOfTwoDivisorShift()"}}

    Pow2Check -- "Yes" --> Pow2Path["2的幂次路径<br>tryTranslateDivBySmallPowerOfTwo()<br>或 tryTranslateModBySmallPowerOfTwo()"]
    Pow2Check -- "No" --> ConstCheck{{"除数是编译期常量?<br>asConstInteger(rhs)"}}

    ConstCheck -- "Yes" --> MagicPath["Magic Number路径<br>tryTranslateDivByConstant()<br>或 tryTranslateModByConstant()"]
    ConstCheck -- "No" --> Fallback["回退路径<br>translate_binary(inst, 'divw')<br>或 translate_binary(inst, 'remw')"]

    Pow2Path --> End(["优化完成"])
    MagicPath --> End
    Fallback --> End

    %%Node styles
    classDef default fill:#E2EAFE4F,stroke:#5A88F6AF
    classDef endNode fill:#DDF4D84F,stroke:#7DCF62AF
    classDef decisionNode fill:#FCEBD34f,stroke:#F6AA4BAF

    %%Link styles
    linkStyle default stroke:#666666AF,stroke-width:2px
    linkStyle 1 stroke:#339933AF,stroke-width:2px
    linkStyle 3 stroke:#339933AF,stroke-width:2px
    linkStyle 2,4 stroke:#DD3333AF,stroke-width:2px

    %%Node classes
    class Start,End endNode
    class Pow2Check,ConstCheck decisionNode
```

## powerOfTwoDivisorShift 判断流程

```mermaid
flowchart TD
    Start(["powerOfTwoDivisorShift(divisor)"]) --> ExcludeCheck{{"divisor ∈ {0, ±1, INT_MIN}?"}}
    ExcludeCheck -- "Yes" --> Fail(["返回 false<br>排除特例"])
    ExcludeCheck -- "No" --> AbsDiv("计算 absDivisor<br>absDivisor = |divisor|")

    AbsDiv --> Pow2Test{{"absDivisor 是2的幂?<br>(val & (val-1)) == 0"}}
    Pow2Test -- "No" --> Fail2(["返回 false"])
    Pow2Test -- "Yes" --> CalcShift("shift = log2(absDivisor)<br>negative = (divisor < 0)")

    CalcShift --> RangeCheck{{"0 < shift < 31?"}}
    RangeCheck -- "Yes" --> Success(["返回 true, shift, negative"])
    RangeCheck -- "No" --> Fail3(["返回 false"])

    %%Node styles
    classDef default fill:#E2EAFE4F,stroke:#5A88F6AF
    classDef endNode fill:#DDF4D84F,stroke:#7DCF62AF
    classDef decisionNode fill:#FCEBD34f,stroke:#F6AA4BAF

    %%Link styles
    linkStyle default stroke:#666666AF,stroke-width:2px
    linkStyle 1 stroke:#DD3333AF,stroke-width:2px
    linkStyle 3 stroke:#DD3333AF,stroke-width:2px
    linkStyle 4 stroke:#339933AF,stroke-width:2px
    linkStyle 6 stroke:#339933AF,stroke-width:2px
    linkStyle 7 stroke:#DD3333AF,stroke-width:2px

    %%Node classes
    class Success,Fail,Fail2,Fail3 endNode
    class ExcludeCheck,Pow2Test,RangeCheck decisionNode
```

## 2的幂次除法优化 (tryTranslateDivBySmallPowerOfTwo)

将 `x / 2^k` 转换为有符号截断语义的移位序列。C整数除法向零截断，而负数右移向-∞取整，因此需要先加bias修正。

```mermaid
flowchart TD
    Start(["tryTranslateDivBySmallPowerOfTwo(inst)"]) --> GetConst("获取除数常量<br>asConstInteger(rhs)")
    GetConst --> Pow2Check{{"powerOfTwoDivisorShift()?"}}
    Pow2Check -- "No" --> Fail(["返回 false"])

    Pow2Check -- "Yes" --> LoadSrc("加载被除数到lhs寄存器<br>loadOperand(lhs, inst, dstReg)")
    LoadSrc --> BorrowBias("借用bias临时寄存器<br>tempMgr.borrowExcluding()")

    BorrowBias --> GenBias("生成bias: 符号掩码右移<br>sraiw bias, src, 31<br>srliw bias, bias, (32-shift)")
    GenBias --> GenAdd("加bias: 截断修正<br>addw dst, src, bias")
    GenAdd --> GenShift("算术右移: 得到商<br>sraiw dst, dst, shift")

    GenShift --> NegCheck{{"除数为负?"}}
    NegCheck -- "Yes" --> GenNeg("取反: subw dst, zero, dst")
    NegCheck -- "No" --> StoreResult
    GenNeg --> StoreResult("存储结果<br>storeResult(inst, dstReg, inst)")

    StoreResult --> Success(["返回 true"])

    %%Node styles
    classDef default fill:#E2EAFE4F,stroke:#5A88F6AF
    classDef endNode fill:#DDF4D84F,stroke:#7DCF62AF
    classDef decisionNode fill:#FCEBD34f,stroke:#F6AA4BAF

    %%Link styles
    linkStyle default stroke:#666666AF,stroke-width:2px
    linkStyle 2 stroke:#DD3333AF,stroke-width:2px
    linkStyle 1 stroke:#339933AF,stroke-width:2px
    linkStyle 8 stroke:#339933AF,stroke-width:2px
    linkStyle 9 stroke:#DD3333AF,stroke-width:2px

    %%Node classes
    class Start,Success,Fail endNode
    class Pow2Check,NegCheck decisionNode
```

**生成指令序列示例**（`x / 4`，shift=2）：
```asm
sraiw  bias, x, 31        # bias = x的符号位扩展
srliw  bias, bias, 30      # bias = (x<0) ? 3 : 0
addw   dst, x, bias        # dst = x + bias (截断修正)
sraiw  dst, dst, 2         # dst = (x+bias) >> 2 = x/4
```

## 2的幂次取模优化 (tryTranslateModBySmallPowerOfTwo)

余数按 `x - (x / d) * d` 生成，复用除法的移位序列计算商，再乘回除数后相减。

```mermaid
flowchart TD
    Start(["tryTranslateModBySmallPowerOfTwo(inst)"]) --> GetConst("获取除数常量")
    GetConst --> Pow2Check{{"powerOfTwoDivisorShift()?"}}
    Pow2Check -- "No" --> Fail(["返回 false"])

    Pow2Check -- "Yes" --> LoadSrc("加载被除数lhs")
    LoadSrc --> BorrowTmp("借用quotient和bias临时寄存器")

    BorrowTmp --> GenQuotient("生成商: 移位+bias序列<br>(同除法优化逻辑)")
    GenQuotient --> NegDivCheck{{"除数为负?"}}

    NegDivCheck -- "Yes" --> NegQuotient("取反商: subw q, zero, q")
    NegDivCheck -- "No" --> GenProduct
    NegQuotient --> GenProduct("乘回除数: slliw q, q, shift")

    GenProduct --> NegDivCheck2{{"除数为负?"}}
    NegDivCheck2 -- "Yes" --> NegProduct("取反积: subw q, zero, q")
    NegDivCheck2 -- "No" --> GenRem
    NegProduct --> GenRem("求余: subw dst, src, q<br>rem = x - quotient * d")

    GenRem --> StoreResult("存储结果")
    StoreResult --> Success(["返回 true"])

    %%Node styles
    classDef default fill:#E2EAFE4F,stroke:#5A88F6AF
    classDef endNode fill:#DDF4D84F,stroke:#7DCF62AF
    classDef decisionNode fill:#FCEBD34f,stroke:#F6AA4BAF

    %%Link styles
    linkStyle default stroke:#666666AF,stroke-width:2px
    linkStyle 2 stroke:#DD3333AF,stroke-width:2px
    linkStyle 1 stroke:#339933AF,stroke-width:2px
    linkStyle 5 stroke:#339933AF,stroke-width:2px
    linkStyle 6 stroke:#DD3333AF,stroke-width:2px
    linkStyle 8 stroke:#339933AF,stroke-width:2px
    linkStyle 9 stroke:#DD3333AF,stroke-width:2px

    %%Node classes
    class Start,Success,Fail endNode
    class Pow2Check,NegDivCheck,NegDivCheck2 decisionNode
```

## computeSignedMagic 算法流程 (Hacker's Delight Algorithm 10-2)

计算有符号常量除法的magic参数（multiplier和shift），使得 `high32(n * multiplier) >> shift` 等于 `n / divisor`。

```mermaid
flowchart TD
    Start(["computeSignedMagic(divisor)"]) --> Init("初始化<br>two31 = 2^31<br>absDivisor = |divisor|<br>t = two31 + (divisor >> 31)<br>anc = t - 1 - (t % absDivisor)")

    Init --> InitP("p = 31<br>q1 = two31 / anc, r1 = two31 - q1*anc<br>q2 = two31 / absDivisor, r2 = two31 - q2*absDivisor")

    InitP --> LoopStart("迭代循环开始")

    subgraph IterLoop["迭代搜索最小p"]
        LoopStart --> IncP("p++")
        IncP --> UpdateQ1("更新q1, r1<br>q1 <<= 1, r1 <<= 1<br>if r1 >= anc: q1++, r1 -= anc")
        UpdateQ1 --> UpdateQ2("更新q2, r2<br>q2 <<= 1, r2 <<= 1<br>if r2 >= absDivisor: q2++, r2 -= absDivisor")
        UpdateQ2 --> CalcDelta("delta = absDivisor - r2")
        CalcDelta --> Converge{{"q1 < delta<br>|| (q1 == delta && r1 != 0)?"}}
        Converge -- "No" --> IncP
    end

    Converge -- "Yes" --> CalcMult("multiplier = q2 + 1")
    CalcMult --> NegCheck{{"divisor < 0?"}}
    NegCheck -- "Yes" --> NegMult("multiplier = -multiplier")
    NegCheck -- "No" --> Return
    NegMult --> Return(["返回 {multiplier, p - 32}"])

    %%Node styles
    classDef default fill:#E2EAFE4F,stroke:#5A88F6AF
    classDef endNode fill:#DDF4D84F,stroke:#7DCF62AF
    classDef decisionNode fill:#FCEBD34f,stroke:#F6AA4BAF

    %%Link styles
    linkStyle default stroke:#666666AF,stroke-width:2px
    linkStyle 7 stroke:#DD3333AF,stroke-width:2px
    linkStyle 8 stroke:#339933AF,stroke-width:2px
    linkStyle 10 stroke:#339933AF,stroke-width:2px
    linkStyle 11 stroke:#DD3333AF,stroke-width:2px

    %%Node classes
    class Start,Return endNode
    class Converge,NegCheck decisionNode

    %%Subgraph style
    style IterLoop fill:#E2EAFE3F,stroke:#6666669F,stroke-width:1px,stroke-dasharray: 5 5
```

## emitSignedConstDivQuotient 流程

使用magic number生成常量除法商的指令序列，处理特例后执行乘法+移位+符号修正。

```mermaid
flowchart TD
    Start(["emitSignedConstDivQuotient<br>(inst, dividend, divisor, dstReg)"]) --> LoadLhs("加载被除数<br>loadOperand(dividend, inst, dstReg)")

    LoadLhs --> SpecialCheck{{"divisor == 1?"}}
    SpecialCheck -- "Yes" --> DivBy1("mv dst, lhs<br>(x/1 = x)")
    SpecialCheck -- "No" --> NegOneCheck{{"divisor == -1?"}}

    NegOneCheck -- "Yes" --> DivByNeg1("subw dst, zero, lhs<br>(x/-1 = -x)")
    NegOneCheck -- "No" --> IntMinCheck{{"divisor == INT_MIN?"}}

    IntMinCheck -- "Yes" --> DivByIntMin("li tmp, INT_MIN<br>subw dst, lhs, tmp<br>seqz dst, dst<br>(x/INT_MIN = (x==INT_MIN)?1:0)")
    IntMinCheck -- "No" --> MagicPath["Magic Number路径"]

    subgraph MagicPath["Magic Number乘法+移位"]
        MagicPath --> ComputeMagic("computeSignedMagic(divisor)<br>获取 {multiplier, shift}")
        ComputeMagic --> LoadMagic("li magicTmp, multiplier<br>加载magic乘数到临时寄存器")
        LoadMagic --> Mul("mul dst, lhs, magicTmp<br>乘以magic乘数")
        Mul --> High32("srai dst, dst, 32<br>取高32位(算术右移32位)")
        High32 --> SignAdjCheck{{"需要符号调整?"}}

        SignAdjCheck -- "d>0, m<0" --> AddBack("addw dst, dst, lhs<br>加回被除数")
        SignAdjCheck -- "d<0, m>0" --> SubBack("subw dst, dst, lhs<br>减去被除数")
        SignAdjCheck -- "不需要" --> ShiftCheck
        AddBack --> ShiftCheck
        SubBack --> ShiftCheck

        ShiftCheck{{"magic.shift > 0?"}}
        ShiftCheck -- "Yes" --> DoShift("sraiw dst, dst, shift<br>最终算术右移")
        ShiftCheck -- "No" --> SignFix
        DoShift --> SignFix("符号修正: srliw tmp, dst, 31<br>addw dst, dst, tmp<br>(向零截断修正)")
    end

    SignFix --> End(["商已存入dstReg"])
    DivBy1 --> End
    DivByNeg1 --> End
    DivByIntMin --> End

    %%Node styles
    classDef default fill:#E2EAFE4F,stroke:#5A88F6AF
    classDef endNode fill:#DDF4D84F,stroke:#7DCF62AF
    classDef decisionNode fill:#FCEBD34f,stroke:#F6AA4BAF

    %%Link styles
    linkStyle default stroke:#666666AF,stroke-width:2px
    linkStyle 2 stroke:#339933AF,stroke-width:2px
    linkStyle 3 stroke:#DD3333AF,stroke-width:2px
    linkStyle 5 stroke:#339933AF,stroke-width:2px
    linkStyle 6 stroke:#DD3333AF,stroke-width:2px
    linkStyle 8 stroke:#339933AF,stroke-width:2px
    linkStyle 9 stroke:#DD3333AF,stroke-width:2px
    linkStyle 12 stroke:#339933AF,stroke-width:2px
    linkStyle 13 stroke:#DD3333AF,stroke-width:2px
    linkStyle 15 stroke:#339933AF,stroke-width:2px
    linkStyle 16 stroke:#DD3333AF,stroke-width:2px

    %%Node classes
    class Start,End endNode
    class SpecialCheck,NegOneCheck,IntMinCheck,SignAdjCheck,ShiftCheck decisionNode

    %%Subgraph style
    style MagicPath fill:#E2EAFE3F,stroke:#6666669F,stroke-width:1px,stroke-dasharray: 5 5
```

**生成指令序列示例**（`x / 7`，magic = {multiplier=-1840700269, shift=2}）：
```asm
li     magicTmp, -1840700269   # 加载magic乘数
mul    dst, x, magicTmp        # 乘法
srai   dst, dst, 32            # 取高32位
addw   dst, dst, x             # 符号调整 (d>0, m<0)
sraiw  dst, dst, 2             # 右移shift位
srliw  tmp, dst, 31            # 符号修正
addw   dst, dst, tmp           # 向零截断
```

## 常量取模优化 (tryTranslateModByConstant)

基于magic除法计算余数：`rem = dividend - quotient * divisor`。

```mermaid
flowchart TD
    Start(["tryTranslateModByConstant(inst)"]) --> GetConst("获取除数常量<br>asConstInteger(rhs)")
    GetConst --> ConstCheck{{"除数是常量且非0?"}}
    ConstCheck -- "No" --> Fail(["返回 false"])

    ConstCheck -- "Yes" --> TrivialCheck{{"divisor ∈ {1, -1}?"}}
    TrivialCheck -- "Yes" --> ZeroRem("li dst, 0<br>(x%1 = x%-1 = 0)")
    TrivialCheck -- "No" --> LoadLhs("加载被除数lhs")

    LoadLhs --> BorrowQuot("借用quotient寄存器<br>(避开lhs和dst)")
    BorrowQuot --> EmitQuot("emitSignedConstDivQuotient()<br>计算商到quotientReg")

    EmitQuot --> LoadDivisor("li product, divisor<br>加载除数到临时寄存器")
    LoadDivisor --> MulBack("mulw product, quotient, divisor<br>商乘以除数")
    MulBack --> SubRem("subw dst, lhs, product<br>余数 = 被除数 - 商*除数")

    SubRem --> StoreResult("存储结果")
    StoreResult --> Success(["返回 true"])
    ZeroRem --> Success2(["返回 true"])

    %%Node styles
    classDef default fill:#E2EAFE4F,stroke:#5A88F6AF
    classDef endNode fill:#DDF4D84F,stroke:#7DCF62AF
    classDef decisionNode fill:#FCEBD34f,stroke:#F6AA4BAF

    %%Link styles
    linkStyle default stroke:#666666AF,stroke-width:2px
    linkStyle 2 stroke:#DD3333AF,stroke-width:2px
    linkStyle 1 stroke:#339933AF,stroke-width:2px
    linkStyle 4 stroke:#339933AF,stroke-width:2px
    linkStyle 3 stroke:#DD3333AF,stroke-width:2px

    %%Node classes
    class Start,Success,Success2,Fail endNode
    class ConstCheck,TrivialCheck decisionNode
```

## 乘法优化 (tryTranslateMulByPowerOfTwo)

将乘以2的幂转换为左移指令，作为除法优化的对照。

```mermaid
flowchart TD
    Start(["tryTranslateMulByPowerOfTwo(inst)"]) --> FindConst("查找常量操作数<br>检查rhs或lhs是否为ConstInteger")
    FindConst --> HasConst{{"找到常量操作数?"}}
    HasConst -- "No" --> Fail(["返回 false"])

    HasConst -- "Yes" --> ZeroCheck{{"multiplier == 0?"}}
    ZeroCheck -- "Yes" --> MulByZero("li dst, 0")
    ZeroCheck -- "No" --> Pow2Check{{"|multiplier| 是2的幂?"}}

    Pow2Check -- "No" --> Fail2(["返回 false"])
    Pow2Check -- "Yes" --> Shift1Check{{"shift == 0?<br>(即乘以±1)"}}

    Shift1Check -- "Yes" --> GenMv("mv dst, src<br>(乘以1)")
    Shift1Check -- "No" --> GenShift("slliw dst, src, shift<br>(左移替代乘法)")

    GenMv --> NegCheck
    GenShift --> NegCheck{{"multiplier < 0?"}}
    NegCheck -- "Yes" --> GenNeg("subw dst, zero, dst<br>(取反)")
    NegCheck -- "No" --> StoreResult

    GenNeg --> StoreResult("存储结果")
    MulByZero --> StoreResult2("存储结果")
    StoreResult --> Success(["返回 true"])
    StoreResult2 --> Success2(["返回 true"])

    %%Node styles
    classDef default fill:#E2EAFE4F,stroke:#5A88F6AF
    classDef endNode fill:#DDF4D84F,stroke:#7DCF62AF
    classDef decisionNode fill:#FCEBD34f,stroke:#F6AA4BAF

    %%Link styles
    linkStyle default stroke:#666666AF,stroke-width:2px
    linkStyle 2 stroke:#DD3333AF,stroke-width:2px
    linkStyle 1 stroke:#339933AF,stroke-width:2px
    linkStyle 4 stroke:#339933AF,stroke-width:2px
    linkStyle 3,5 stroke:#DD3333AF,stroke-width:2px
    linkStyle 8 stroke:#339933AF,stroke-width:2px
    linkStyle 9 stroke:#DD3333AF,stroke-width:2px

    %%Node classes
    class Start,Success,Success2,Fail,Fail2 endNode
    class HasConst,ZeroCheck,Pow2Check,Shift1Check,NegCheck decisionNode
```

## 优化策略总结

| 除数类型 | 优化方法 | 生成指令 | 性能 |
|----------|----------|----------|------|
| 2的幂次 (2,4,8,16,...) | 移位+bias修正 | sraiw+srliw+addw+sraiw (4条) | 最快 |
| 一般常量 (3,5,7,...) | Magic number乘法+移位 | li+mul+srai+addw/subw+sraiw+srliw+addw (7-8条) | 较快 |
| 非常量 | 回退到硬件除法 | divw (1条，但周期长) | 最慢 |
| ±1 | 特例处理 | mv 或 subw (1条) | 最快 |
| INT_MIN | 特例处理 | li+subw+seqz (3条) | 快 |

> **注意**：IR层的 `InstCombine` 和 `ConstProp` Pass 也会在指令选择之前折叠部分常量除法（如 `x/1 → x`，`x%1 → 0`），后端的常量除法优化处理的是IR层未能折叠的运行时常量除法场景。
