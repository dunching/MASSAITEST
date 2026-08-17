# MassAI Crowd 当前架构

## 1. 文档职责

本文只描述当前 `main` 已经存在的结构、运行链、权威边界和已知迁移债务。

本文不承担：

- 架构演进历史；看 `History/` 与 Git 历史。
- 最终目标；看 `TargetArchitecture.md`。
- 下一步实施顺序；看 `PhasePlan.md`。
- 当前测试是否通过；看 `FeatureChecklist.md` / `TestScenarioMatrix.md`。
- 模块依赖细节；看 `Reference/PluginModuleBoundary.md`。
- 字段 Owner 细节；看 `Reference/WorkerOwnershipMatrix.md`。

---

## 2. 项目定位

MASSAITEST 是一个基于 Unreal Engine 5.7 + Mass 的大规模 Agent Simulation 验证工程。

当前仓库分成两个明确层次：

```text
MassCrowdSimulation
= 可复用的大规模 Agent Simulation Runtime 插件

MassAICrowdDemo
= 插件的生产架构验证宿主 / 场景与验收工程
```

Demo 使用虫群、目标围攻、异构实体、Combat、Projectile、VAT、Networking、Presentation 等场景验证同一套 Runtime。Demo 不是最终产品本体。

当前源码最准确的总体描述是：

> **Mass 负责 Entity / Engine bridge，Persistent Worker 负责持续模拟权威，Network / Presentation 消费版本化结果，Demo 负责输入、场景、验证与宿主业务适配。**

第一代跨帧 Demo Round Transaction 已经从 live server simulation path 物理退出；当前仍保留的 RoundSim 代码主要属于场景计划、一次性 bootstrap preparation、指标/验收、checkpoint host 适配和历史命名债务。

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
│   └── Stable Sort / Quantization / Stable Hash 等纯逻辑
│
├── MassCrowdSpatial
├── MassCrowdCombat
├── MassCrowdRuntime
│   ├── Persistent Async Worker Runtime
│   ├── WorkRing / TimeWheel / DependencyIndex
│   ├── Entity / Resource / Dirty State Store
│   ├── Domain Registry / Executor
│   ├── UE Task shard dispatch / deterministic merge
│   ├── Worker Result Apply Proxy
│   └── Runtime Owner Commit Barrier
│
├── MassCrowdProjectiles
├── MassCrowdNetworking
├── MassCrowdPresentation
├── MassCrowdStandardSources
└── MassCrowdTests

Source/
├── MassCrowdDemoBusiness
│   └── Demo Planner / Provider / Host Intent / 业务解释
└── MassAICrowdDemo
    ├── Scenario / Plan / World Adapter
    ├── Worker Input Sync / Result Apply host adapter
    ├── Bootstrap compatibility preparation
    ├── Checkpoint / metrics / acceptance
    └── Remaining legacy naming / duplicate implementation debt
```

`MassCrowdCore.Build.cs` 当前只依赖 UE `Core`。

精确模块依赖以 `Reference/PluginModuleBoundary.md` 为准。

---

## 4. 当前 live server 主运行链

普通 Production fixed step 的主链已经收敛为：

```text
External Facts
Spawn / Despawn / Command / Resource Revision / Correction
        │
        ▼
UCrowdDemoWorkerInputSyncProcessor
        │
        │ SubmitIntentBatch / versioned resource-objective changes
        ▼
Persistent FCrowdAsyncSimulationRuntime
        │
        ├── Lifecycle / Input
        ├── Behavior
        ├── Flow / Resource
        ├── Target / Cohort
        ├── Combat / Reactive / Projectile
        ├── Movement Planning
        ├── Movement
        ├── Particle / Interaction
        ├── Facing / Finalize
        └── Publish
        │
        ▼
Worker Published Result
        │
        ▼
UCrowdDemoWorkerResultApplyProcessor
        │
        ▼
FCrowdWorkerResultOwnerCommitBarrier
        │
        ├── Host FinalValidate
        ├── Dirty Mass Apply
        ├── Proxy Commit
        └── no-fail side effects
        │
        ▼
Checkpoint / Network / Presentation
```

`CrowdDemoRoundSimProcessors.h` 当前只暴露两个 `UMassProcessor`：

```text
UCrowdDemoWorkerInputSyncProcessor
UCrowdDemoWorkerResultApplyProcessor
```

旧的 Round Stage struct、PostFinalize Stage、AuthorityCommit Stage、ClientPredictionCommit Stage 已从该 Processor surface 删除。

---

## 5. 首次 bootstrap：一次性兼容准备，不是持续模拟权威

Persistent Worker 第一次接管某个 Plan / Generation 前，需要初始 Movement / Target / Projectile / Facing control facts。

当前 Demo 仍保留一条**一次性同步 bootstrap preparation**：

```text
Boundary Snapshot
      ↓
Stage immutable bootstrap inputs
      ↓
FCrowdDemoBootstrapSynchronousGraph
      ↓
Build initial control/resource facts
      ↓
SubmitPreparedWorkerBootstrapInput()
      ↓
Persistent Worker owns current fixed step
```

这个 synchronous graph：

- 是 Demo host 的 bootstrap compatibility mechanism。
- 只用于生成 Worker 首次控制输入和确定性 bootstrap facts。
- 不通过旧 Round Transaction 提交结果。
- 不产生第二遍 Prepared Movement / Target Resource / Particle Diagnostic commit。
- 一旦 Worker 接受输入，当 Tick 立即进入统一 Worker Owner Barrier 提交路径。

后续普通 Tick 不重建这套 bootstrap graph，而是直接提交 intent / revision facts。

因此必须区分：

```text
one-shot bootstrap compute != ongoing simulation authority
Persistent Worker          == live simulation authority
```

---

## 6. 当前 Authority Mode 语义

插件 Runtime 仍保留 Shadow / Canary / Production 等能力；这些模式没有被从通用插件中删除。

但当前 Demo live server Round path 已不再保留 Legacy Simulation fallback：

- Full Production 配置下，bootstrap 后由 Worker 持续推进。
- 普通 Production Tick 直接走 Worker intent path。
- 非 Full Production 的 Demo server 若试图进入这条已收口的 Round simulation path，会 fail-closed，而不是退回旧 Round DAG。

因此不能再把旧文档中的：

```text
Demo 默认 Shadow
→ Shadow 继续依赖旧 Round Transaction 正常模拟
```

当成当前事实。

更准确的说法是：

> **插件仍支持 Shadow/Canary；当前 Demo live server 的已收口 simulation path 以 Full Worker Production 为成立条件，旧 Round simulation fallback 已删除。**

---

## 7. 当前模拟权威

核心原则：

> 同一个模拟字段在同一运行模式下只能有一个 Production Owner。

当前 Worker authority 覆盖：

```text
Lifecycle
Behavior
Flow / Resource
Target / Target Cohort
Combat / Reactive
Projectile
Movement Planning
Movement
Particle / Interaction
Facing / Finalize
Simulation Clock
```

Mass Fragment、Actor、Network Cache、Presentation Slot 是实体桥接、消费缓存、宿主业务状态或视觉状态，不是同一 Worker 字段的第二推进器。

GT 允许提交的是外部事实，例如：

```text
Spawn
Despawn
Gameplay Command
Resource Revision
Objective Revision
Correction / Resnapshot
```

Worker Result Apply 刚写出的 Position / Velocity / Facing 不应作为普通输入再回灌 Worker。

---

## 8. 多实体处理：Entity → Work → Shard → Task

当前 Worker 不是“一实体一个线程”，也不是每 Tick 无条件扫完整 Agent 集合。

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
Deterministic Owner Merge
```

### 8.1 Entity

稳定身份：

```text
ProviderId + StableEntityId + LifecycleSerial
```

LifecycleSerial 用于拒绝实体槽复用后的 stale facts。

### 8.2 Work

`FCrowdWorkerWorkItem` 支持：

```text
Entity
Pair
Resource
Cohort
Timer
```

Work 表达“什么变化需要重算”，不是线程对象。

### 8.3 WorkRing

WorkRing 维护 Current / Next Epoch；相同 WorkKey 合并 ReasonMask / Priority / Revision，而不是无限重复入队。

当前 Production10k 配置：

```text
MaxWorkItems          80000
MaxWakeups            40000
MaxDependencyEdges    320000
MaxDirtyEntities      16000
MaxOrderedEvents      64000
MaxPropagationRounds  8
ShardEntityCount      64
```

`ShardEntityCount` 是历史命名；通用 Shard Planner 实际按 **WorkItem 数量**切片。

### 8.4 Dependency / TimeWheel

DependencyIndex 支持 Entity / Resource / Cohort 稀疏依赖传播；TimeWheel 管理未来 Simulation Tick 的 wakeup。

目标模型是：

```text
fact changed
→ dependent Work
→ domain output changed
→ dependent NextWork
```

而不是任何变化都重跑所有实体。

---

## 9. Shard 与异步执行

`FCrowdWorkerDeterministicShardPlanner` 先稳定排序，再按 Domain / ShardOrdinal 形成短任务。

Async Runtime 存在真实：

```text
UE::Tasks::Launch("CrowdWorkerV2DomainShard", ...)
UE::Tasks::Launch("CrowdWorkerV2OwnerContinuation", ...)
```

Shard Task 读取冻结 `FCrowdWorkerDomainContext`，只写自己的 `FCrowdWorkerDomainOutput`。

Task 完成顺序不是模拟顺序。Owner merge 稳定归并：

```text
NextWork
Wakeups
DirtyStates
OrderedEvents
Declared / Observed Dependencies
Consumed Command Input Sequences
```

Ordered Event 的全局连续 sequence 在 Owner merge 后分配。

---

## 10. Domain Execution Order

Canonical execution rank：

```text
Lifecycle / Input
→ Behavior
→ Flow / Resource
→ Target
→ Combat / Reactive
→ Movement Planning
→ Movement
→ Particle / Interaction
→ Facing / Finalize
→ Publish
```

Execution Rank 是全局推进顺序；Executor `GetDependencies()` 是显式 prerequisite subset，不是整张 DAG 的逐边复制。

---

## 11. 当前运动链

```text
Behavior / Objective
        ↓
Macro Guidance
   ├── Shared Flow
   └── Target Region Transport（按需）
        ↓
Movement Planning
        ↓
Local Predictive Interaction
        ↓
Movement Predict
        ↓
Particle / Environment Safety
        ↓
Facing / Finalize
```

职责：

- Shared Flow：世界空间宏观路线与障碍方向。
- Target Region：目标附近宏观人口分布和运输。
- Local Predictive：短时间尺度可执行速度修正。
- Particle：最终 Hard / Swept / Environment / Bounds 安全。

---

## 12. Target Region Transport

Target Region 是可选 Macro Guidance Provider，不是通用 movement owner。

它以 Target 为原点建立：

```text
Radial Band + Angular Sector
```

并维护：

```text
Current Population
Desired Population
Deficit
Surplus
Transport Plan
Edge Quota
```

目的不是给每个 Agent 永久分配 Slot，而是让大量实体在目标附近按容量与供需自然展开，避免所有实体持续冲向 Target center。

Cohort 的 Plan / Execution 属于 Worker simulation state；Topology 可作为由版本化资源重建的 deterministic cache。

---

## 13. Particle / Interaction 当前并行边界

Particle 不能直接按 AgentId 每 64 个实体硬切，因为跨 shard pair 共享约束。

当前 `FCrowdMassParticleWork::Solve()` 已实现：

```text
Stable Agent Sort
→ Conservative Interaction Closure
→ Connected Components / Islands
→ Independent Island Sub-solve
→ Stable Merge
→ Global Exact Applied-State Validation
→ fail-closed fallback
```

但当前多个 Island 的“独立 Solve”主要是算法问题分解，仍处在一个 Particle Resource Work 的内部处理；不能把 `bUsedIslandSharding` 解释成每个 Island 已各自 UE Task 并行。

当前状态：

```text
Independent island decomposition    = implemented
Island-level UE Task parallelism    = open
Large single-island internal shard  = open
```

单个巨大 Interaction Island 仍是明确的规模热点候选。

---

## 14. Combat / Projectile

Projectile 属于 Worker `CombatReactive` Domain。

Worker-side Projectile executor 持有跨 Tick active Projectile state，并产生：

```text
Projectile dirty state
Combat dirty state
Lifecycle ordered events
Hit ordered events
Wakeups
```

Demo Host Combat Extension 是纯 C++ adapter，不应依赖 UWorld / Mass Fragment / UObject 隐式状态。

Mass Projectile Entity 可以作为 Engine integration proxy，但不是第二套持续 projectile simulator。

---

## 15. Result Apply 与原子提交

当前 live server 的唯一正常提交边界是 Runtime Owner Barrier：

```text
Prepared Worker Result
→ CommitToken Match
→ Proxy ValidatePreparedState
→ Host FinalValidate
──────── first write boundary ────────
→ HostApplyNoFail / Dirty Mass Apply
→ Proxy.CommitPreparedValidated
→ HostCommitSideEffectsNoFail
→ Dirty ACK / later consumption
```

以下旧 Demo 第二遍机制已从 Production source 删除：

```text
Prepared Movement Boundary Commit
Prepared Target/Resource Commit Plan
Prepared Particle Diagnostic Commit
Round Work Batch
TryPrepareRoundApply
BeginBoundaryTransaction
BoundaryOrchestrator
```

Result Apply 的正常原子性不再依赖“写后 rollback”。

---

## 16. Networking

当前网络合同围绕版本化 simulation facts：

```text
Checkpoint
Intent
Correction
Digest
Lifecycle / Relevant Snapshot
Late Join
Resync
```

Worker 状态通过 Generation / Input Sequence / Publish Sequence / Revision / StableHash 管理。

网络是 Simulation Authority 的传输与消费层，不成为普通模拟 Owner。

---

## 17. Presentation

`MassCrowdPresentation` 独立维护：

```text
StableEntityRef → Instance Slot
Spawn / Update / Despawn
Transform / interpolation
Visual State
ISM / VAT
Custom Data
```

Presentation State 与 Simulation State 分层；视觉系统不能反向决定 Worker 的业务/运动权威。

---

## 18. RoundSimPipeline 当前还负责什么

第一代 Round simulation scheduler 已经删除，但 `UCrowdDemoRoundSimPipelineSubsystem` 本身还没有退出。

当前它主要仍承担：

```text
Demo Round / Scenario plan state
Boundary host facts
one-shot bootstrap scratch/state
Target / scenario diagnostics state
checkpoint / RoundResult host assembly inputs
performance / acceptance metrics
Demo-specific test and visualization support
```

这意味着：

> **RoundSimPipeline 仍是大型 Demo host subsystem，但已经不是 live server 的跨帧 simulation scheduler。**

后续重构重点应是把它按 Host Plan / Bootstrap Adapter / Metrics / Checkpoint 等职责继续拆小，而不是重新给它增加模拟权威。

---

## 19. 当前剩余 Legacy / Migration Debt

### 19.1 Demo generic duplicate implementations

Plugin Core 与 Demo 仍存在 LocalPredictive / Particle / SharedFlow / Target 等重复或兼容实现。

删除必须以 repo-wide consumer audit 为前提；不能因为 Round Transaction 已删除就整组盲删。

### 19.2 Bootstrap scratch 命名与遗留字段

`FCrowdDemoBoundaryFacingWorkState` 等类型仍带历史 Round/Shadow/Consumed 命名，其中部分字段已经只服务 bootstrap compatibility 或可能失去消费者。

这是下一轮 source cleanup 候选，但不是当前 live authority 的第二套实现。

### 19.3 巨型 Demo host 文件

`CrowdDemoRoundSimPipelineSubsystem.cpp`、`CrowdDemoRoundSimProcessors.cpp`、Coordinator/Mixed host 文件仍承担较多场景、指标和兼容职责。

结构上应继续拆分，但拆分必须保持 Worker authority 和 deterministic contracts 不变。

### 19.4 诊断/验收链需要 post-cut 重建证据

PR12 删除旧 PostFinalize/Particle 第二遍提交时，也删除了依附在该路径上的一批 Demo per-step diagnostics side effects。

因此当前不能假设旧的：

```text
T1–T8 PASS
Particle diagnostic counters
Target stability metrics
T8 Golden/perf runner evidence
```

自动适用于新主链。

哪些指标需要改由 Worker result / retained state / host checkpoint 重新生成，必须通过 post-cut UE regression 确认。

---

## 20. 当前验证状态边界

当前已成立的是：

```text
Source architecture cut = complete
Static structure review = passed
```

当前尚未完成的是：

```text
UE build after source cut
Architecture automation after source cut
PIE / T1–T8 regression after source cut
Networking / Late Join regression after source cut
Scale / performance regression after source cut
```

所以：

> **“WA8 source structure closed” 不等于 “WA8 runtime regression passed”。**

旧运行结果仍可作为 baseline / 历史证据，但凡执行路径被本轮 source cut 改写，都必须重新验证后才能恢复为当前 PASS。

---

## 21. 当前主要 OPEN 项

当前优先级已经从“继续删 Round Transaction”切换为：

```text
1. Post-cut Regression Gate
   - UE build
   - Architecture automation
   - T1–T8 / networking / checkpoint / diagnostics

2. T5 Long-Window Correctness
   - step ~886 feasible-region-insufficient
   - Static / Moving 1000+ Tick

3. Duplicate Kernel / Host Shell Cleanup
   - 删除确认失去消费者的 Demo generic implementation
   - 拆 RoundSimPipeline host responsibilities

4. Large Particle Island Scaling
   - Island-level task parallelism
   - Single large-island Cell-Pair Owner / per-round barrier

5. WA9 Full-Scale Acceptance
   - 1k → 2k → 5k → 10k
   - Simulation + Network + Presentation + Performance
```

当前已有 10k-aware scheduler / spatial / target scoped 专项基础，不等于完整 10k production acceptance。

---

## 22. 当前总体结论

当前 `main` 的架构结论：

> **Persistent Worker 已经成为 Demo live server 的持续模拟权威；第一代跨帧 Round Transaction、旧 Stage surface 和 Prepared second-pass commit channels 已从 Production source 物理删除。Demo 仍保留一次性 bootstrap preparation 与较大的 scenario/metrics/checkpoint host shell。下一阶段首先不是再改 Authority，而是把新主链重新跑实，再继续删 duplicate/host debt，并关闭 T5、Particle scaling 与 WA9。**
