# 后端整体流程图

## 编译流水线总览

```mermaid
flowchart TD
    Start(["开始: 源文件输入"]) --> ArgsParse("参数解析<br>ArgsAnalysis")
    ArgsParse --> FrontEnd["前端: 词法/语法分析"]
    FrontEnd --> AST2IR["IR生成: AST → 结构化IR<br>IRGenerator::run()"]
    AST2IR --> Rename1("IR重命名<br>module->renameIR()")

    Rename1 --> OptCheck{{"优化等级 > 0?"}}
    OptCheck -- "Yes" --> Mem2Reg["Mem2Reg优化<br>alloca/load/store → SSA Phi"]
    OptCheck -- "No" --> PhiLower
    Mem2Reg --> OptLoop["优化循环 (最多8轮)<br>ConstProp → UnreachableBlockElim<br>→ DeadInstElim → CFGSimplify"]
    OptLoop --> PhiLower["Phi降级<br>Phi → Copy指令<br>PhiLowering::run()"]

    PhiLower --> Rename2("IR重命名<br>module->renameIR()")
    Rename2 --> Backend["后端: IR → RISC-V64汇编<br>CodeGeneratorRiscV64::run()"]
    Backend --> End(["结束: 输出.s汇编文件"])

    %%Node styles
    classDef default fill:#E2EAFE4F,stroke:#5A88F6AF
    classDef endNode fill:#DDF4D84F,stroke:#7DCF62AF
    classDef decisionNode fill:#FCEBD34f,stroke:#F6AA4BAF

    %%Link styles
    linkStyle default stroke:#666666AF,stroke-width:2px
    linkStyle 4 stroke:#339933AF,stroke-width:2px
    linkStyle 5 stroke:#DD3333AF,stroke-width:2px

    %%Node classes
    class Start,End endNode
    class OptCheck decisionNode
```

## 后端代码生成主流程

```mermaid
flowchart TD
    Start(["CodeGeneratorAsm::run()"]) --> Header["genHeader()<br>输出.arch rv64gc等汇编器指令"]
    Header --> DataSection["genDataSection()<br>输出全局变量(.comm/.data段)"]
    DataSection --> CodeSection["genCodeSection()<br>遍历所有非内建函数"]
    CodeSection --> FuncIter{{"还有下一个函数?"}}
    FuncIter -- "Yes" --> GenFunc["genCodeSection(func)<br>函数级代码生成"]
    GenFunc --> FuncIter
    FuncIter -- "No" --> End(["结束: 汇编文件输出完成"])

    %%Node styles
    classDef default fill:#E2EAFE4F,stroke:#5A88F6AF
    classDef endNode fill:#DDF4D84F,stroke:#7DCF62AF
    classDef decisionNode fill:#FCEBD34f,stroke:#F6AA4BAF

    %%Link styles
    linkStyle default stroke:#666666AF,stroke-width:2px
    linkStyle 4 stroke:#339933AF,stroke-width:2px
    linkStyle 5 stroke:#DD3333AF,stroke-width:2px

    %%Node classes
    class Start,End endNode
    class FuncIter decisionNode
```

## 函数级代码生成流程 (genCodeSection)

```mermaid
flowchart TD
    Start(["genCodeSection(func)"]) --> RegAlloc["1. 寄存器分配<br>registerAllocation(func)"]

    RegAlloc --> CreateILoc["2. 创建ILocRiscV64<br>设置寄存器分配映射/保存寄存器/栈帧大小"]

    CreateILoc --> InstSelect["3. 指令选择<br>InstSelectorRiscV64::run()<br>IR指令 → RISC-V64汇编指令"]

    InstSelect --> ScratchCheck{{"4. 存在ScratchValue?"}}
    ScratchCheck -- "Yes" --> ScratchAlloc["ScratchAllocator::allocate()<br>为ScratchValue分配物理寄存器"]
    ScratchAlloc --> ScratchProcess["处理spilled scratch<br>分配栈槽/更新allocationMap"]
    ScratchProcess --> Patchup["ILocRiscV64::patchScratchRegs()<br>替换机器指令中的scratch寄存器编号"]
    Patchup --> CleanLabel

    ScratchCheck -- "No" --> CleanLabel["5. 删除未引用的基本块标签<br>deleteUnusedLabel()"]

    CleanLabel --> Output["6. 输出函数头部<br>.align/.global/.type/函数名"]
    Output --> DebugCheck{{"调试模式?"}}
    DebugCheck -- "Yes" --> DebugOut["输出IR值→寄存器/栈位置映射"]
    DebugOut --> AsmOut
    DebugCheck -- "No" --> AsmOut["7. 输出汇编指令序列<br>ILocRiscV64::outPut(fp)"]
    AsmOut --> End(["结束: 函数代码生成完成"])

    %%Node styles
    classDef default fill:#E2EAFE4F,stroke:#5A88F6AF
    classDef endNode fill:#DDF4D84F,stroke:#7DCF62AF
    classDef decisionNode fill:#FCEBD34f,stroke:#F6AA4BAF

    %%Link styles
    linkStyle default stroke:#666666AF,stroke-width:2px
    linkStyle 4 stroke:#339933AF,stroke-width:2px
    linkStyle 8 stroke:#339933AF,stroke-width:2px
    linkStyle 9 stroke:#DD3333AF,stroke-width:2px

    %%Node classes
    class Start,End endNode
    class ScratchCheck,DebugCheck decisionNode
```

## 寄存器分配与栈帧布局流程 (registerAllocation)

```mermaid
flowchart TD
    Start(["registerAllocation(func)"]) --> BuiltinCheck{{"内建函数?"}}
    BuiltinCheck -- "Yes" --> Return(["直接返回"])
    BuiltinCheck -- "No" --> Greedy["GreedyRegAllocator::allocate(func)<br>Greedy寄存器分配"]

    Greedy --> AdjustCall["adjustFuncCallInsts(func)<br>调整函数调用指令"]
    AdjustCall --> AdjustParam["adjustFormalParamInsts(func)<br>调整形参指令"]
    AdjustParam --> SavedRegs["computeSavedRegs(func, allocMap)<br>计算callee-saved寄存器列表"]
    SavedRegs --> StackAlloc["stackAlloc(func)<br>栈空间分配"]

    StackAlloc --> ParamLoop["遍历形参<br>按RISC-V ABI分配寄存器(a0-a7/fa0-fa7)<br>超出部分分配栈槽"]
    ParamLoop --> InstLoop["遍历所有指令<br>为AllocaInst和有结果值指令创建分配信息"]
    InstLoop --> SpillLoop["为强制栈分配/未分配/溢出变量<br>分配栈槽(assignStackSlot)"]
    SpillLoop --> FrameCalc["计算栈帧总大小<br>savedFrameBytes + localBytes + outgoingBytes<br>16字节对齐"]
    FrameCalc --> End(["结束: 寄存器分配完成"])

    %%Node styles
    classDef default fill:#E2EAFE4F,stroke:#5A88F6AF
    classDef endNode fill:#DDF4D84F,stroke:#7DCF62AF
    classDef decisionNode fill:#FCEBD34f,stroke:#F6AA4BAF

    %%Link styles
    linkStyle default stroke:#666666AF,stroke-width:2px
    linkStyle 1 stroke:#339933AF,stroke-width:2px
    linkStyle 2 stroke:#DD3333AF,stroke-width:2px

    %%Node classes
    class Start,End,Return endNode
    class BuiltinCheck decisionNode
```

## 栈帧布局

```
高地址
┌──────────────────────────────┐
│       caller的栈帧            │
├──────────────────────────────┤
│       返回地址 (ra)           │  ← 若函数包含调用指令
│       帧指针 (s0/FP)         │  ← 始终保存
│       callee-saved (s1-s11)  │  ← 仅保存实际使用的
├──────────────────────────────┤  ← FP (s0) 指向此处
│       局部变量                │  ← AllocaInst分配
│       溢出变量                │  ← 被spill的虚拟寄存器
│       spilled scratch        │  ← 溢出的scratch寄存器
├──────────────────────────────┤
│       outgoing参数            │  ← 超过8个参数的调用参数
└──────────────────────────────┘  ← SP (sp) 指向此处
低地址
```
