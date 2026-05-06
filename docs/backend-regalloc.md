# 寄存器分配详细流程图

## GreedyRegAllocator::allocate 总流程

```mermaid
flowchart TD
    Start(["GreedyRegAllocator::allocate(func)"]) --> Clear("清空上次分配状态<br>allocationMap/spilledValues/...")

    Clear --> BuiltinCheck{{"内建函数?"}}
    BuiltinCheck -- "Yes" --> Return(["直接返回"])
    BuiltinCheck -- "No" --> BuildPool["构建可用物理寄存器池<br>buildRegisterPool(func)<br>t0-t2, a0-a7, s1-s11, t5-t6"]

    BuildPool --> DomTree["构建支配树<br>DominatorTree(func)"]
    DomTree --> LoopInfo["循环分析<br>LoopInfo(func, domTree)"]
    LoopInfo --> SetDepth["设置基本块循环深度<br>bb->setLoopDepth()"]

    SetDepth --> LIA["活跃区间分析<br>LiveIntervalAnalysis(func, loopInfo)"]
    LIA --> LIARun["analysis.run()<br>computeLiveIntervals() +<br>buildInterferenceGraph()"]
    LIARun --> RecordCall["记录CallInst指令编号<br>callInstNumbers"]
    RecordCall --> BuildIndex["建立LiveInterval→索引映射<br>intervalToIndex"]

    BuildIndex --> RunGreedy["运行Greedy分配主循环<br>runGreedy(intervals, graph)"]
    RunGreedy --> Rebuild["重建分配映射表<br>rebuildAllocationMap(intervals)"]
    Rebuild --> SaveLive["保存活跃性快照<br>valueLiveRanges"]
    SaveLive --> End(["结束: 寄存器分配完成"])

    %%Node styles
    classDef default fill:#E2EAFE4F,stroke:#5A88F6AF
    classDef endNode fill:#DDF4D84F,stroke:#7DCF62AF
    classDef decisionNode fill:#FCEBD34f,stroke:#F6AA4BAF

    %%Link styles
    linkStyle default stroke:#666666AF,stroke-width:2px
    linkStyle 2 stroke:#339933AF,stroke-width:2px
    linkStyle 3 stroke:#DD3333AF,stroke-width:2px

    %%Node classes
    class Start,End,Return endNode
    class BuiltinCheck decisionNode
```

## 活跃区间分析流程 (LiveIntervalAnalysis::run)

### 总体流程

```mermaid
flowchart TD
    Start(["LiveIntervalAnalysis::run()"]) --> ComputeLI["computeLiveIntervals()<br>计算所有虚拟寄存器的活跃区间"]
    ComputeLI --> BuildIG["buildInterferenceGraph()<br>构建干涉图"]
    BuildIG --> End(["结束"])

    %%Node styles
    classDef default fill:#E2EAFE4F,stroke:#5A88F6AF
    classDef endNode fill:#DDF4D84F,stroke:#7DCF62AF

    %%Link styles
    linkStyle default stroke:#666666AF,stroke-width:2px

    %%Node classes
    class Start,End endNode
```

### computeLiveIntervals 详细流程

```mermaid
flowchart TD
    Start(["computeLiveIntervals()"]) --> A1("按基本块顺序遍历指令")
    A1 --> A2("为每条指令编号<br>instNumbering[inst] = nextInstNum++")
    A2 --> A3{{"指令定义了Value?"}}
    A3 -- "Yes" --> A4["扩展该Value的活跃区间<br>加入[instNum, instNum+1)段"]
    A4 --> A5
    A3 -- "No" --> A5{{"指令使用了Value?"}}
    A5 -- "Yes" --> A6["扩展该Value的活跃区间<br>从定义点到使用点"]
    A6 --> A7
    A5 -- "No" --> A7("继续下一条指令")
    A7 --> A8{{"还有更多指令?"}}
    A8 -- "Yes" --> A2
    A8 -- "No" --> A9("计算溢出权重<br>权重 = 使用频率 × (1 + 循环深度 × 3)")
    A9 --> End(["结束: 活跃区间计算完成"])

    %%Node styles
    classDef default fill:#E2EAFE4F,stroke:#5A88F6AF
    classDef endNode fill:#DDF4D84F,stroke:#7DCF62AF
    classDef decisionNode fill:#FCEBD34f,stroke:#F6AA4BAF

    %%Link styles
    linkStyle default stroke:#666666AF,stroke-width:2px
    linkStyle 2,4 stroke:#339933AF,stroke-width:2px
    linkStyle 3,5 stroke:#DD3333AF,stroke-width:2px

    %%Node classes
    class Start,End endNode
    class A3,A5,A8 decisionNode
```

### buildInterferenceGraph 详细流程

```mermaid
flowchart TD
    Start(["buildInterferenceGraph()"]) --> B1("遍历所有活跃区间对")
    B1 --> B2{{"区间i与区间j重叠?<br>i.overlaps(j)"}}
    B2 -- "Yes" --> B3["添加干涉边<br>graph->addEdge(i, j)"]
    B3 --> B4
    B2 -- "No" --> B4{{"还有更多区间对?"}}
    B4 -- "Yes" --> B1
    B4 -- "No" --> End(["结束: 干涉图构建完成"])

    %%Node styles
    classDef default fill:#E2EAFE4F,stroke:#5A88F6AF
    classDef endNode fill:#DDF4D84F,stroke:#7DCF62AF
    classDef decisionNode fill:#FCEBD34f,stroke:#F6AA4BAF

    %%Link styles
    linkStyle default stroke:#666666AF,stroke-width:2px
    linkStyle 1 stroke:#339933AF,stroke-width:2px
    linkStyle 2 stroke:#DD3333AF,stroke-width:2px

    %%Node classes
    class Start,End endNode
    class B2,B4 decisionNode
```

## Greedy分配主循环 (runGreedy)

```mermaid
flowchart TD
    Start(["runGreedy(intervals, graph)"]) --> Sort["按溢出权重降序排列活跃区间<br>权重相同则按起点升序"]
    Sort --> NextIter["取下一个interval"]
    NextIter --> ValidCheck{{"interval有效<br>且vreg非空?"}}
    ValidCheck -- "No" --> NextIter
    ValidCheck -- "Yes" --> ForcedCheck{{"强制栈分配?<br>isForcedStackValue()"}}

    ForcedCheck -- "Yes" --> Spill["markSpilled(interval)<br>标记为溢出"]
    Spill --> MoreCheck

    ForcedCheck -- "No" --> TryFree["tryAssignFreeReg()<br>尝试分配空闲寄存器"]
    TryFree --> FreeOk{{"分配成功?"}}

    FreeOk -- "Yes" --> MoreCheck{{"还有更多interval?"}}
    FreeOk -- "No" --> TryEvict["tryEvictAndAssign()<br>尝试驱逐已有分配"]
    TryEvict --> EvictOk{{"驱逐成功?"}}

    EvictOk -- "Yes" --> MoreCheck
    EvictOk -- "No" --> Spill2["markSpilled(interval)<br>标记为溢出"]
    Spill2 --> MoreCheck

    MoreCheck -- "Yes" --> NextIter
    MoreCheck -- "No" --> End(["结束: Greedy分配完成"])

    %%Node styles
    classDef default fill:#E2EAFE4F,stroke:#5A88F6AF
    classDef endNode fill:#DDF4D84F,stroke:#7DCF62AF
    classDef decisionNode fill:#FCEBD34f,stroke:#F6AA4BAF

    %%Link styles
    linkStyle default stroke:#666666AF,stroke-width:2px
    linkStyle 4,8 stroke:#339933AF,stroke-width:2px
    linkStyle 5,9,11 stroke:#DD3333AF,stroke-width:2px

    %%Node classes
    class Start,End endNode
    class ValidCheck,ForcedCheck,FreeOk,EvictOk,MoreCheck decisionNode
```

## tryAssignFreeReg 详细流程

```mermaid
flowchart TD
    Start(["tryAssignFreeReg(interval)"]) --> GetNode["获取interval在干涉图中的节点编号"]
    GetNode --> GetUsed["获取干涉邻居已占用的寄存器集合<br>graph->getInterferingRegs()"]
    GetUsed --> NextReg["遍历可用寄存器池availableRegs"]
    NextReg --> CanAssign{{"canAssignReg()<br>caller-saved且跨越调用?"}}

    CanAssign -- "不可分配" --> NextReg
    CanAssign -- "可分配" --> NotUsed{{"该寄存器不在usedRegs中?"}}

    NotUsed -- "Yes" --> Assign["assignPhysicalReg()<br>分配该寄存器给interval"]
    Assign --> Success(["返回 true"])

    NotUsed -- "No" --> MoreReg{{"还有更多寄存器?"}}
    MoreReg -- "Yes" --> NextReg
    MoreReg -- "No" --> Fail(["返回 false"])

    %%Node styles
    classDef default fill:#E2EAFE4F,stroke:#5A88F6AF
    classDef endNode fill:#DDF4D84F,stroke:#7DCF62AF
    classDef decisionNode fill:#FCEBD34f,stroke:#F6AA4BAF

    %%Link styles
    linkStyle default stroke:#666666AF,stroke-width:2px
    linkStyle 4 stroke:#339933AF,stroke-width:2px
    linkStyle 5 stroke:#DD3333AF,stroke-width:2px

    %%Node classes
    class Success,Fail endNode
    class CanAssign,NotUsed,MoreReg decisionNode
```

## tryEvictAndAssign 详细流程

```mermaid
flowchart TD
    Start(["tryEvictAndAssign(interval)"]) --> NextReg["遍历可用寄存器池availableRegs"]
    NextReg --> CanAssign{{"canAssignReg()?"}}
    CanAssign -- "不可分配" --> NextReg
    CanAssign -- "可分配" --> CheckNeighbors["检查占用该寄存器的干涉邻居"]

    CheckNeighbors --> NextNeighbor["取下一个邻居neighbor"]
    NextNeighbor --> SameReg{{"neighbor占用了当前寄存器?"}}
    SameReg -- "No" --> NextNeighbor
    SameReg -- "Yes" --> WeightCmp{{"neighbor权重 >=<br>当前interval权重?"}}

    WeightCmp -- "Yes" --> CannotUse["该寄存器不可驱逐<br>canUseReg = false"]
    CannotUse --> MoreReg2

    WeightCmp -- "No" --> AddCandidate["加入驱逐候选列表<br>evictionCandidates"]
    AddCandidate --> MoreNeighbor{{"还有更多邻居?"}}
    MoreNeighbor -- "Yes" --> NextNeighbor
    MoreNeighbor -- "No" --> CanUse{{"canUseReg且<br>候选列表非空?"}}

    CanUse -- "No" --> MoreReg2{{"还有更多寄存器?"}}
    MoreReg2 -- "Yes" --> NextReg
    MoreReg2 -- "No" --> Fail(["返回 false"])

    CanUse -- "Yes" --> Evict["驱逐所有候选邻居<br>markSpilled(victim)"]
    Evict --> Assign["assignPhysicalReg()<br>分配该寄存器给interval"]
    Assign --> Success(["返回 true"])

    %%Node styles
    classDef default fill:#E2EAFE4F,stroke:#5A88F6AF
    classDef endNode fill:#DDF4D84F,stroke:#7DCF62AF
    classDef decisionNode fill:#FCEBD34f,stroke:#F6AA4BAF

    %%Link styles
    linkStyle default stroke:#666666AF,stroke-width:2px
    linkStyle 11 stroke:#339933AF,stroke-width:2px
    linkStyle 9 stroke:#DD3333AF,stroke-width:2px

    %%Node classes
    class Success,Fail endNode
    class CanAssign,SameReg,WeightCmp,MoreNeighbor,CanUse,MoreReg2 decisionNode
```

## 可用寄存器池

| 类别 | 寄存器 | 编号 | 说明 |
|------|--------|------|------|
| caller-saved | t0-t2 | 5,6,7 | 临时寄存器 |
| caller-saved | a0-a7 | 10-17 | 参数/返回值寄存器 |
| callee-saved | s1-s11 | 9,18-27 | 保存寄存器 |
| caller-saved | t5-t6 | 30,31 | 临时寄存器 |
| **保留** | zero,ra,sp,gp,tp,s0/fp | - | 不参与分配 |
| **保留** | t3-t4 | 28,29 | 保留为scratch寄存器 |
