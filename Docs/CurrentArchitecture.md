# MassAI Crowd 当前架构

## 1. 文档职责

本文只描述当前 `main` 分支已经存在的生产结构、模块边界、运行链和已知迁移债务。

本文不承担以下职责：

- 不记录架构演进流水账；历史阶段进入 `Docs/History/`。
- 不描述尚未实现的最终目标；最终目标由后续 `TargetArchitecture.md` 统一描述。
- 不记录实施顺序；实施顺序由 `PhasePlan.md` 负责。
- 不证明功能是否已经通过；完成状态与测试证据分别由 `FeatureChecklist.md` 和 `TestScenarioMatrix.md` 负责。

---

## 2. 项目定位

MASSAITEST 是一个基于 Unreal Engine 5.7 + Mass 的大规模 Agent Simulation 验证工程。

当前工程包含两个不同层次：

```text
MassCrowdSimulation
= 可复用的群体 Agent Simulation 插件

MassAICrowdDemo
= 插件的验证宿主 / 测试场
```

Demo 使用虫群移动、通道、目标围攻、异构实体、战斗、Projectile、VAT、网络和视觉场景验证插件能力；Demo 本身不是最终可复用产品。

当前代码已经从“由大量 Round/Mass Processor 共同推进完整模拟”的结构，收敛到“Persistent Worker 持有模拟状态，Mass/Network/Presentation 负责输入、代理和结果应用”的结构。

---

## 3. 当前模块结构

```text
Plugins/MassCrowdSimulation
│
├── MassCrowdCore
│   ├── Shared Flow
│   ├── Target Region Transport
│   ├── Local Predictive Interaction
│   ├── Particle Constraint
│   ├── Facing / Guidance
│   ├── Behavior Source 基础数据模型
│   └── 排序、量化、Stable Hash 等纯逻辑
│
├── MassCrowdRuntime
│   ├── Persistent Worker Runtime
│   ├── WorkRing
│   ├── TimeWheel
│   ├── DependencyIndex
│   ├── EntityStateStore
│   ├── ResourceStore
│   ├── Worker Domain Registry / Executor
│   ├── Worker Result Apply Proxy
│   └── Runtime Owner Commit Barrier
│
├── MassCrowdSpatial
├── MassCrowdCombat
├── MassCrowdProjectiles
├── MassCrowdNetworking
├── MassCrowdPresentation
├── MassCrowdStandardSources
└── MassCrowdTests

Source/
│
├── MassCrowdDemoBusiness
│   └── Demo 专用 Planner / Provider / 业务解释
│
└── MassAICrowdDemo
    └── Scenario / World Adapter / 测试与验收宿主
```

`MassCrowdCore` 只依赖 UE `Core`，通用 kernel 不直接依赖 UWorld、Actor、MassEntity 或 Demo 类型。

`MassCrowdDemoBusiness` 独立承载攻击、物流等 Demo 业务规划，通用 Runtime 不应解释敌我、攻击、取货、交付等产品语义。

---

## 4. 当前主运行链

当前生产链可以概括为：

```text
Unreal / Mass / Gameplay
        │
        │ Spawn / Despawn / Command / Resource / Correction
        ▼
Worker Input Sync
        │
        ▼
Persistent Worker Runtime
        │
        ├── Lifecycle / Input
        ├── Behavior
        ├── Flow / Resource
        ├── Target / Cohort
        ├── Combat / Projectile
        ├── Movement Planning
        ├── Movement
        ├── Particle / Interaction
        ├── Facing / Finalize
        └── Publish
        │
        │ Dirty State Patch / Ordered Event / Checkpoint
        ▼
Worker Result Apply
        │
        ├── Mass Proxy
        ├── Network Adapter
        └── Presentation Proxy
```

当前核心 Mass 模拟边界 Processor 已收敛为：

```text
UCrowdDemoWorkerInputSyncProcessor
UCrowdDemoWorkerResultApplyProcessor
```

客户端表现仍有自己的表现 Processor，但旧 Round 子阶段不再各自作为长期生产 Mass Processor 运行。

---

## 5. 当前模拟权威

当前架构遵循一个核心原则：

> 同一个模拟字段在任意时刻只能有一个 Production Owner。

已经迁移到 Worker 的字段，以 Persistent Worker 内部状态为模拟权威。

例如：

```text
Worker Movement
      │
      ├── Mass Fragment
      ├── Network State
      └── Presentation State
```

Mass 中已经应用的 Transform、Velocity、Facing、Combat 等数据，是最近一次已消费 Worker 结果的 Engine 代理，不是另一套可以独立继续推进模拟的权威状态。

GT 不应把刚刚从 Worker 应用出来的位置或速度重新作为普通输入回灌 Worker；只有明确的外部事实或 Authority Correction 才能改变 Worker 权威状态。

Worker 使用 `Generation`、`WorkerEpoch`、`InputSequence`、`CorrectionRevision`、`StateRevision`、`PublishSequence` 和 `LifecycleSerial` 等版本事实拒绝 stale 输入、旧生命周期和过期结果。

---

## 6. 多实体处理模型：Entity → Work → Shard → Task

当前 Worker 不是“一实体一个线程”，也不是“每个 Tick 完整遍历全部 Agent”。

真正的调度结构是：

```text
Entity
  ↓
Work
  ↓
Shard
  ↓
UE::Task
  ↓
Shard-local Output
  ↓
Deterministic Merge
```

### 6.1 Entity

Entity 使用：

```text
ProviderId + StableEntityId + LifecycleSerial
```

形成稳定身份。

`FCrowdWorkerEntityStateStore` 保存当前有效实体以及各 Worker Field。Lifecycle 槽位复用后，旧 Lifecycle 的 Spawn、State、Hit、Correction 等事实会被拒绝。

### 6.2 Work

Runtime 实际调度单位是 `FCrowdWorkerWorkItem`。

当前 Work Kind 包括：

```text
Entity
Pair
Resource
Cohort
Timer
```

因此 Work 可以表示：

```text
重新计算 Agent 17 的 Movement
重新处理 Agent A / B 的 Pair Interaction
重新计算 Target Cohort 3
处理 MovementControl Resource
执行某个到期 Timer
```

这使系统不必把所有逻辑都退化成逐实体 Tick。

### 6.3 WorkRing

WorkRing 维护：

```text
Current Epoch Queue
Next Epoch Queue
```

相同 WorkKey 重复入队时，会合并 Priority、ReasonMask 和 Revision，而不是无限生成重复工作。

因此 Runtime 更接近“变化传播系统”：

```text
某个事实改变
    ↓
唤醒相关 Work
    ↓
Work 产生新状态
    ↓
Dependency 传播
    ↓
产生下一批 Work
```

而不是“任何东西发生变化都重新运行全部实体”。

当前 Production 10k 配置中，Work 上限为 80,000。

---

## 7. Dependency 与 TimeWheel

### 7.1 DependencyIndex

Dependency 可以以以下来源为键：

```text
Entity
Resource
Cohort
```

用来描述：

```text
某个实体 / Resource / Cohort 变化
            ↓
哪些 Work 应该被重新执行
```

例如一个 Target Cohort 发生变化时，只需要唤醒该 Cohort 相关 Target / Guidance Work，而不是重新规划所有 Cohort。

### 7.2 TimeWheel

TimeWheel 负责未来 Simulation Tick 到期的工作，例如：

```text
Movement wakeup
Projectile
Cooldown
恢复
Timer
```

未到期的 Bucket 不参与当前 Tick 的普通扫描。

---

## 8. Shard 与并行执行

同一 Domain 中的大量 Work 会先按稳定 Key 排序，然后由 `FCrowdWorkerDeterministicShardPlanner` 切成 Shard。

当前默认：

```text
ShardEntityCount = 64
```

但当前通用 Planner 实际按 WorkItem 数量切片，因此更准确的含义是：

> 每个普通 Shard 大约最多包含 64 个 WorkItem。

这不等价于“一个 Shard 永远包含 64 个 Agent”，因为 Pair、Resource、Cohort Work 的语义都不同。

例如 300 个 Movement Work 会被切成大约 5 个 Shard，再通过短生命周期 `UE::Tasks` 并行执行。

Domain Executor 读取冻结的 `FCrowdWorkerDomainContext`，并把结果写入自己的 `FCrowdWorkerDomainOutput`。并行 Shard 不应直接竞争修改全局 Worker State。

---

## 9. 确定性 Merge

Task 实际完成顺序不能决定模拟结果。

例如线程完成顺序可能是：

```text
Shard 3
Shard 0
Shard 2
Shard 1
```

Owner Merge 仍会按稳定的 Domain 和 ShardOrdinal 重新归并。

Shard Output 可以包含：

```text
DirtyStates
OrderedEvents
NextWork
Wakeups
DeclaredDependencies
ObservedDependencies
ConsumedCommands
```

状态结果按 `StableEntityRef + Field` 合并。

不能被 latest-wins 吞掉的 Gameplay Fact，例如 Damage、Death、Spawn、Despawn、Impact 等，通过 Ordered Event 保留，并在 Owner Merge 时分配连续的全局 EventSequence。

因此并行 Task 的完成先后不应该改变最终 Stable Hash 和最终模拟状态。

---

## 10. 当前 Worker Domain DAG

当前主要 Domain 为：

```text
Lifecycle / Input
        ↓
Behavior
        ↓
Flow / Resource
        ↓
Target
        ↓
Combat / Reactive
        ↓
Movement Planning
        ↓
Movement
        ↓
Particle / Interaction
        ↓
Facing / Finalize
        ↓
Publish
```

Domain 的稳定公开 ID 与执行 Rank 分离，因此未来增加 Domain 时，不需要通过修改已有 Domain ID 来改变执行顺序。

---

## 11. 当前群体移动架构

当前移动链不是一个单独算法，而是分层结构：

```text
Behavior / Objective
        ↓
Macro Guidance
   ├── Shared Flow
   └── Target Region Transport
        ↓
Preferred Movement
        ↓
Local Predictive Interaction
        ↓
Movement Predict
        ↓
Particle / Environment Safety
        ↓
Facing / Finalize
```

### Shared Flow

负责大尺度导航：

```text
怎么从当前位置绕过地图障碍，到达目标区域
```

### Target Region Transport

负责目标附近的大规模区域分布：

```text
大量 Agent 接近目标后，应该从哪些区域进入、哪些区域过密、哪些区域缺人、如何在目标周围重新分布
```

### Local Predictive Interaction

负责短时间尺度的局部可执行速度选择：

```text
附近实体按照当前速度继续走马上会冲突，应该怎样提前协调
```

### Particle

负责最终不可放宽的安全约束：

```text
Hard Separation
Swept Safety
Obstacle
Bounds
Environment Safety
```

Particle 是最终 Safety Layer，不承担高层目标选择和宏观导航职责。

---

## 12. Target Region Transport

Target Region 是可选的目标附近 Macro Guidance Provider，不是所有移动的固定必经层。

当前可以把它理解为：

> 以 Target 为原点建立 Target-relative Polar Transport Field。

目标附近空间按照：

```text
Radial Band
+
Angular Sector
```

组织为 Polar Navigation Cells。

系统进一步维护 Demand Region 的：

```text
Current Population
Desired Population
Deficit
Surplus
```

然后在可行 Cell 图上建立 Transport Plan 和 Edge Quota，把过密区域的人口逐步引导到欠占用区域。

它不是永久 Slot 系统，也不是“一 Cell 一实体”。

它管理的是：

```text
区域人口
宏观流量
Cell 间运输
方向 Guidance
```

实体最终仍然必须经过 Local Predictive 和 Particle Safety。

远离目标时通常由 Shared Flow 负责大范围引导；进入目标附近后，Target Region 才负责 Target-relative 的局部分布。

---

## 13. Particle / Interaction

Particle 不能简单按 Agent ID 每 64 个实体硬切，因为跨 Shard 的两个实体可能正在形成同一个约束。

当前 Particle Work 会根据潜在约束关系建立闭合 Interaction Island。

例如：

```text
A-B-C-D

E-F

G-H-I
```

如果三组之间互不可能在当前 fixed-step 交换约束，它们可以形成三个独立 Island，各自 Solve 后再按稳定 Agent / Pair 顺序归并。

归并后系统还会进行全局 Applied-State Safety Validation。

如果 Island 分解后的最终状态无法通过全局验证，则允许 fail-closed 的 monolithic fallback。

当前已经解决“多个互不相关 Island 不必整世界共同求解”的问题。

---

## 14. 当前 Particle 规模边界

当前仍有一个明确未关闭的问题：

> 单个超大型 Interaction Island 还没有完成真正的 Cell-Pair Owner / per-round Barrier 并行分片。

因此很多独立小群可以自然拆分，但数千实体全部高密度连成一个巨大 Interaction Island 时，仍可能成为 Particle 的主要扩展性瓶颈。

---

## 15. Result Apply 与原子提交

Worker 不直接写 Mass。

Published Result 在 GT Result Apply 中先完成全部可失败验证，再进入 no-fail commit 区。

当前逻辑可以概括为：

```text
Prepared Worker Result
        ↓
Commit Token Match
        ↓
Proxy Final Validate
        ↓
Generation / Publish / Input / Event Watermark
        ↓
Stable Entity View
        ↓
Lifecycle / Mass Handle / Fragment Collection
        ↓
Host Target / Resource / Behavior / Event Validate
        ↓
──────── 首次写入边界 ────────
        ↓
Mass Apply
        ↓
Proxy Commit
        ↓
Target / Resource / Behavior / Event
Presentation / Network Side Effects
```

通用 Commit Token 和 `FCrowdWorkerResultOwnerCommitBarrier` 已经位于 `MassCrowdRuntime`。

Demo 只保留 Host-specific Prepared Commit Plan 和 Host FinalValidate / Apply adapter。

核心原则是：

> 所有正常可失败检查必须发生在第一次状态写入之前。

当前结构不再依赖“写了一半后再尝试补偿 rollback”来保证正常提交原子性。

---

## 16. 当前网络结构

当前网络方向不是简单高频同步所有 Agent Transform。

Worker 网络链已经区分：

```text
Checkpoint
Intent
Correction
```

并通过 Generation、Sequence、StableHash、Chunk 等协议事实进行组装和验证。

客户端可以从 Worker Checkpoint 建立本地模拟状态，然后继续消费后续 Delta / Intent；普通误差恢复开始更多依赖 Digest 和稀疏 Authority Correction，而不是反复发送完整世界状态。

---

## 17. Presentation

`MassCrowdPresentation` 已经从模拟权威中分离。

表现层负责：

```text
StableEntityRef
Instance Slot
Transform
Presentation Profile
Visual State
VAT
Custom Data
Cargo Visual
Spawn / Update / Despawn
```

因此下面三者是不同职责：

```text
Simulation State
Network State
Presentation State
```

Presentation 不能反向决定 Worker Simulation Authority。

---

## 18. Demo 当前职责

`MassAICrowdDemo` 当前主要承担：

```text
Scenario
测试地图
Round / Test Director
Demo Business Adapter
Target Actor
Combat 验收
VAT 验收
网络验收
Golden Hash
故障注入
性能日志
录像 / FFmpeg
人工审片
```

Demo 应使用插件 Runtime、Networking 和 Presentation 的真实生产路径，再叠加测试设施，而不是长期维护第二套通用 Runtime。

---

## 19. 当前仍存在的迁移态 Legacy

虽然 Worker Authority 主体已经成立，Demo 层仍保留较重的旧 RoundSim 结构。

当前仍可见的大型迁移壳包括：

```text
CrowdDemoRoundSimPipelineSubsystem
CrowdDemoRoundSimProcessors
CrowdDemoRoundSimCoordinator
CrowdDemoMixedSandboxCoordinator
```

同时部分 Demo 目录仍保留与插件 Core 中通用实现对应的历史 Kernel / Adapter 结构。

这些内容当前不能一次性全部删除，因为部分 rollback、Round Transaction、metrics、测试和 side effect 仍然依赖它们。

当前 WA8 尚未关闭的主要结构债包括：

```text
完整 rollback 旧数据源
TryPrepareRoundApply
Demo-local Round Transaction
部分旧 Round Prepared facts / side effects
```

---

## 20. 当前已知 Blocker

### WA8 Legacy 收敛

Worker Authority 主体已经成立，但旧 Round Transaction、完整 rollback 数据源和部分 Demo-local side effect 仍未完全退出生产路径。

### Target 长窗口

T5 短窗口已经有稳定通过记录，但仍存在大约 step 886 的 Target Demand 长窗口失败。600 Tick 成功不能覆盖这个长期稳定性问题。

### Large Particle Island

多个闭合 Island 已经可以独立求解，但超大型单 Island 的内部 Cell-Pair / Barrier 并行仍未完成。

### WA9

当前已经有 1k/2k/5k/10k WorkRing、TimeWheel、Spatial 和 Target Cohort 等专项规模验证，但完整 10k Agent 的 Behavior + Target + Movement + Particle + Combat + Projectile + Networking + Presentation 端到端验收仍未关闭。

---

## 21. 当前验证程度

当前代码已经具备真实 Worker Task Shard、确定性 Merge、Persistent Entity State、增量 Spatial、Target Cohort 增量失效和 Runtime Owner Commit Barrier。

当前记录中已有全 Production T8 server-only 运行：

```text
900 batches
90940 patches
150 Ordered Events

Attack / Spawn / Impact / Damage
50 / 50 / 50 / 50

duplicate = 0
fixed-step p95 = 18.579ms
commit p95 = 0.281ms
realtime = 0.999
```

同时已经存在 10k 级 Work / Timer / Spatial 微基准，以及 10k 双 Cohort Target 增量失效验证。

这些结果证明当前架构已经具备明显的 10k 设计基础，但不等价于“完整 10k Production Ready”。

---

## 22. 当前架构核心总结

当前 MASSAITEST 最重要的结构不是某一个 Crowd 算法，而是：

```text
Persistent Worker Authority
        +
Work-driven Incremental Simulation
        +
Deterministic Sharding / Merge
        +
分层群体运动
        +
Atomic Result Apply
```

整体可以压缩成：

```text
                    External Facts
                         │
                         ▼
                    Input Sync
                         │
                         ▼
              Persistent Worker
                         │
      ┌──────────────────┼──────────────────┐
      │                  │                  │
   Behavior            Target             Combat
      │                  │                  │
      └────────────┬─────┴─────┬────────────┘
                   ▼           ▼
                Movement    Projectile
                   │
            Local Predictive
                   │
              Interaction
                   │
                Particle
                   │
                 Facing
                   │
                 Publish
                   │
                   ▼
              Result Apply
                   │
         ┌─────────┼─────────┐
         ▼         ▼         ▼
       Mass     Network  Presentation
```

---

## 23. 当前代码阅读顺序

推荐按以下顺序理解当前代码：

```text
CurrentArchitecture.md

↓
MassCrowdWorkerRuntimeV2.h

↓
MassCrowdAsyncSimulationRuntime.h/.cpp

↓
MassCrowdWorkerResultApply.h/.cpp

↓
Worker Domains
  Behavior
  Target
  Movement
  Particle
  Combat

↓
MassCrowdCore Kernels

↓
CrowdDemoWorkerInputSync.cpp

↓
MassCrowdNetworking / Presentation

↓
最后再阅读旧 Demo RoundSim
```

不要再从巨大的 `CrowdDemoRoundSimProcessors.cpp` 开始理解项目；该区域仍包含较多迁移期 Legacy，容易把过渡壳误认为当前核心架构。
