# MassAI Crowd 当前架构

## 1. 文档职责

本文只描述当前 `main` 已经存在的结构、运行链、运行模式和已知迁移债务。

本文不承担：

- 架构演进历史；看 `History/`。
- 最终目标；看 `TargetArchitecture.md`。
- 实施顺序；看 `PhasePlan.md`。
- 功能是否通过；看 `FeatureChecklist.md` / `TestScenarioMatrix.md`。
- 源码从哪里读；看 `SourceReadingMap.md`。
- 哪些旧代码能不能删；看 `LegacyCodeInventory.md`。

---

## 2. 项目定位

MASSAITEST 是一个基于 Unreal Engine 5.7 + Mass 的大规模 Agent Simulation 验证工程。

当前仓库包含两个层次：

```text
MassCrowdSimulation
= 可复用的大规模 Agent Simulation 插件

MassAICrowdDemo
= 插件的验证宿主 / 测试场
```

Demo 使用虫群、目标围攻、异构实体、战斗、Projectile、VAT、网络和视觉等场景验证同一套 Runtime。Demo 不是最终产品本体。

当前核心已经从“多个 Round/Mass Processor 共同推进完整模拟”迁移到：

```text
Persistent Worker 持有迁移后的模拟状态
Mass / Network / Presentation 消费结果并提供代理/适配
```

但旧 Demo RoundSim shell 尚未完全退出资源、事务、诊断和测试链，因此当前仍是明显的迁移态。

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
│   └── 排序 / 量化 / Stable Hash 等纯逻辑
│
├── MassCrowdSpatial
├── MassCrowdCombat
├── MassCrowdRuntime
│   ├── Persistent Worker Runtime
│   ├── WorkRing / TimeWheel / DependencyIndex
│   ├── Entity / Resource / Dirty State Store
│   ├── Domain Registry / Executor
│   ├── Async Task dispatch / deterministic merge
│   ├── Result Apply Proxy
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
    └── Scenario / World Adapter / 验收 / Legacy Round shell
```

`MassCrowdCore.Build.cs` 当前只依赖 UE `Core`。

当前实际 Build.cs 主干以 `Reference/PluginModuleBoundary.md` 为准；不要从模块排列顺序猜依赖方向。

---

## 4. 当前主运行链

主链：

```text
Unreal / Mass / Gameplay / Network
        │
        │ Spawn / Despawn / Command
        │ Resource Revision / Correction
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
        ├── Combat / Reactive / Projectile
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

当前 `UCrowdDemoMassSubsystem` 动态注册的核心 Simulation Processor 已收敛为：

```text
UCrowdDemoWorkerInputSyncProcessor
UCrowdDemoWorkerResultApplyProcessor
```

客户端可额外注册视觉 Processor。

源码日志明确打印：

```text
legacy_round_processors=0
```

这表示旧 Round Stage 不再作为独立动态 Mass Processor 注册；**不表示旧 Stage struct、Pipeline 数据源和 Round transaction 已经物理删除。**

---

## 5. 当前运行模式：Production-capable，但默认仍是 Shadow

这是理解当前代码必须知道的事实。

当前代码已经有完整的 Worker Production Owner 实现和正式 Production runner 路径，但 Demo 普通无参数启动并不等于自动进入 Full Production Authority。

`CrowdDemoWorkerInputSync.cpp` 中：

```text
WorkerV2 Runtime config
Movement Authority
Behavior Authority
```

在没有显式 Production 参数时默认使用 Shadow。

Production 路径由命令行 / 正式 runner 显式开启，例如 Movement mode 的 Production 解析。

因此当前应区分：

```text
Production-capable Worker architecture = 已存在
Production runner/path                  = 已存在
普通 no-flag Demo startup               = Shadow
```

后续排查“Worker 为什么没有真正接管某字段”时，先确认当前 Authority Mode，不要只看 Executor 是否注册。

---

## 6. 当前模拟权威

核心原则：

> 同一个模拟字段在同一运行模式下只能有一个 Production Owner。

已经进入 Worker Production Owner 路径的主要字段包括：

```text
Lifecycle
Behavior
Resource
Target / TargetCohort
Combat / Projectile
MovementPlan
Movement
Particle
Facing
Simulation timeline
```

Async Runtime 当前明确把 Worker Field 映射到 Owner Domain / Execution Rank。

Mass Fragment、Actor、Network Cache、Presentation Slot 等是结果代理、宿主业务状态或表现状态，不应成为同一 Worker 字段的第二推进器。

GT 不应把刚从 Worker Result Apply 消费出来的 Position / Velocity / Facing 再作为普通输入回灌 Worker；显式 Correction / Resnapshot / Resource Revision / Gameplay Command 除外。

---

## 7. 多实体处理：Entity → Work → Shard → Task

当前 Worker 不是“一实体一个线程”，也不是每个 Tick 无条件扫完整 Agent 集合。

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

### 7.1 Entity

稳定身份：

```text
ProviderId + StableEntityId + LifecycleSerial
```

LifecycleSerial 用于拒绝槽位复用后的旧事实。

### 7.2 Work

`FCrowdWorkerWorkItem` 支持：

```text
Entity
Pair
Resource
Cohort
Timer
```

Work 表达“现在什么需要重新计算”，不是线程对象。

### 7.3 WorkRing

WorkRing 维护：

```text
Current Epoch
Next Epoch
```

相同 WorkKey 会合并 ReasonMask / Priority 等，而不是无限重复入队。

当前 10k config：

```text
MaxWorkItems          80000
MaxWakeups            40000
MaxDependencyEdges    320000
MaxDirtyEntities      16000
MaxOrderedEvents      64000
MaxPropagationRounds  8
ShardEntityCount      64
```

其中 `ShardEntityCount` 是历史命名；通用 Shard Planner 当前实际按 **WorkItem 数量**切片。

### 7.4 Dependency / TimeWheel

DependencyIndex 支持：

```text
Entity
Resource
Cohort
```

TimeWheel 管理未来 Simulation Tick 才需要唤醒的工作。

系统目标是变化传播：

```text
Fact changed
→ dependent Work
→ new state
→ next dependent Work
```

而不是任何变化都重跑所有实体。

---

## 8. Shard 与真实异步执行

`FCrowdWorkerDeterministicShardPlanner` 会按稳定 WorkKey 对同 Domain Work 分片。

例如 300 个普通 WorkItem，在 shard size 64 时大约形成 5 个 Shard。

Async Runtime 中存在真实：

```text
UE::Tasks::Launch("CrowdWorkerV2DomainShard", ...)
UE::Tasks::Launch("CrowdWorkerV2OwnerContinuation", ...)
```

Shard Task 读取冻结的 `FCrowdWorkerDomainContext`，只写自己的 `FCrowdWorkerDomainOutput`。

Task 完成顺序不是模拟顺序；Owner 在 merge 时使用稳定的 Domain / Shard / Entity / Pair / Event 规则。

---

## 9. Execution Rank 与 Domain Dependency 的区别

当前稳定 Domain ID 与执行顺序分离。

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

Async Runtime 从 pending work 中选择最早 Execution Rank 的 Domain 推进。

每个 Executor 的 `GetDependencies()` **不是这张完整顺序图的逐边复制**，而是 Registry 冻结时验证的显式 prerequisite。

例如：

```text
Behavior      depends Lifecycle
Target        depends FlowResource
Combat        depends Target
MovementPlan  depends Behavior + FlowResource + Target + Combat
Particle      depends Movement
Facing        depends Particle
```

所以阅读源码时：

```text
Execution Rank = 全局 stage ordering
GetDependencies = executor 声明的 prerequisite subset
```

不能把二者混为一谈。

---

## 10. 当前群体运动链

```text
Behavior / Objective
        ↓
Macro Guidance
   ├── Shared Flow
   └── Target Region Transport（需要时）
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

Shared Flow 解决世界空间大尺度路线。

Target Region 解决目标附近宏观人口分布。

Local Predictive 解决短时间尺度的速度可执行性。

Particle 是最终 Hard / Swept / Obstacle / Bounds 安全层。

---

## 11. Target Region Transport

Target Region 是可选的目标附近 Macro Guidance Provider。

最简理解：

> 以 Target 为原点建立 Target-relative Polar Transport Field。

空间通过：

```text
Radial Band + Angular Sector
```

形成 Polar Navigation Cells。

系统维护：

```text
Current Population
Desired Population
Deficit
Surplus
```

并建立 Transport Plan / Edge Quota，引导过密区域向欠占用区域运输。

它不是永久 Slot，不是一 Cell 一 Agent，也不拥有局部碰撞特权。

当前 Runtime wrapper 已存在：

```text
BuildTopology
BuildDemand / static population update
SolvePlan
ValidateExecution
BuildGuidance
BuildGuidanceSharded
```

Cohort 的 plan/execution 是 Worker simulation state；Topology 可作为由版本化资源重建的 deterministic cache。

---

## 12. Particle / Interaction 当前真实并行边界

Particle 不能按 AgentId 每 64 个实体硬切，因为跨 Shard pair 可能共享约束。

当前 `FCrowdMassParticleWork::Solve()` 会：

```text
排序 Agent
→ 构建 conservative closure graph
→ connected components / Interaction Islands
→ 每个 Island 独立求解
→ stable merge
→ Global Applied-State Validation
→ 失败时 monolithic fallback
```

但必须明确：

> **当前多个 Island 虽然被分解成独立子问题，实际是在一个 Particle Resource Work 内顺序循环求解；还没有做到 Island A/B/C 各自并行 UE Task。**

当前源码中的 `bUsedIslandSharding` 表达“Island decomposition / sub-solve”，不能直接理解成线程并行。

因此当前状态是：

```text
Independent island decomposition   = 已实现
Independent island task parallelism= 未实现
Large single-island internal shard = 未实现
```

单个大 Island 仍会走 monolithic solve，是当前明确的规模瓶颈候选。

---

## 13. Combat / Projectile

Projectile 当前属于 `CombatReactive` Worker Domain。

`FCrowdWorkerProjectileDomainExecutor` 内维护跨 tick 的 active Projectile simulation state，并产生：

```text
Projectile dirty state
Combat dirty state
Lifecycle ordered events
Hit ordered events
Wakeups
```

Worker-side Host Combat Extension 是纯 C++ adapter，不允许访问 UWorld / Mass Fragment / UObject 隐式状态。

因此当前权威关系是：

```text
Worker Projectile Simulation Authority
        ↓
Mass / Network / Presentation proxy/events
        ↓
Host business resolve / visual consumption
```

Mass Projectile Entity 可以是 Engine proxy / integration target，但不是与 Worker 并行推进的第二套 projectile simulator。

---

## 14. Result Apply 与原子提交

通用 Runtime 已拥有：

```text
FCrowdWorkerResultCommitToken
FCrowdWorkerResultOwnerCommitBarrier
FCrowdWorkerResultApplyProxy
Dirty Batch / ACK
```

提交顺序：

```text
Prepared Result
→ CommitToken Match
→ Proxy.ValidatePreparedState
→ HostFinalValidate
──────── 首次写入边界 ────────
→ HostApplyNoFail
→ Proxy.CommitPreparedValidated
→ HostCommitSideEffectsNoFail
→ 后续 Dirty ACK
```

正常可失败验证必须发生在首次写入之前。

旧 Demo Round rollback 数据源仍存在，是 WA8 legacy；但它不应再被解释为当前 Result Apply 的正常原子性机制。

---

## 15. Networking

当前网络不是“普通帧同步所有 Transform”。

已有主要合同：

```text
Checkpoint
Intent
Correction
Digest
Lifecycle / Relevant Snapshot
Late Join
Resync
```

Worker 状态使用 Generation / Sequence / Revision / StableHash 等版本事实。

网络是 Simulation Authority 的消费者和跨端事实传输层，不反向成为普通模拟 Owner。

---

## 16. Presentation

`MassCrowdPresentation` 独立维护：

```text
StableEntityRef → Instance Slot
Spawn / Update / Despawn
Transform / interpolation
Visual State
ISM / VAT
Custom Data
```

Presentation State 与 Simulation State 是不同职责；视觉系统不能反向决定 Worker 的业务/运动权威。

---

## 17. 当前最重要的 Legacy 耦合

当前最大的误读风险不是“还有旧文件”，而是**部分新主链仍真实依赖旧壳**。

### 17.1 WorkerInputSync 仍读取 RoundSimPipeline Shared Flow

当前 `CrowdDemoWorkerInputSync.cpp` 构建 versioned resources 时仍访问：

```text
UCrowdDemoRoundSimPipelineSubsystem
→ GetRuntimeSharedFlowField()
```

所以 `RoundSimPipelineSubsystem` 还不能被当成纯 Test Harness 删除。

这属于：

```text
Authority 已迁移
但部分 Input Resource Source 尚未迁移
```

是 WA8 需要优先断开的依赖。

### 17.2 RoundSim Stage struct 仍大量存在

`CrowdDemoRoundSimProcessors.h` 仍声明很多 Stage，但只有 InputSync / ResultApply 是当前注册的 UMassProcessor。

这些 Stage 需要逐个按：

```text
production consumer
host adapter
metric/diagnostic
test fixture
no consumer
```

分类后才能安全删除。

### 17.3 Demo generic kernel 重复实现

Plugin Core 与 Demo 仍存在多套 LocalPredictive / Particle / SharedFlow / Target / Facing 等实现。

当前已经确认 Demo Particle / SharedFlow 仍有诊断消费者，因此不能整组直接删除。

详细分类看 `LegacyCodeInventory.md`。

---

## 18. 当前源码阅读入口

理解当前代码不要先从：

```text
CrowdDemoRoundSimProcessors.cpp
CrowdDemoRoundSimPipelineSubsystem.cpp
CrowdDemoMixedSandboxCoordinator.cpp
```

开始。

推荐：

```text
SourceReadingMap.md
→ MassCrowdWorkerRuntimeV2.h/.cpp
→ MassCrowdAsyncSimulationRuntime.h/.cpp
→ Worker Domain Executors
→ MassCrowdWorkerResultApply.h/.cpp
→ CrowdDemoWorkerInputSync.cpp
→ Host Result Apply
→ Core kernels
→ 最后才读 RoundSim legacy shell
```

---

## 19. 当前主要 OPEN 项

当前主要未关闭项：

```text
WA8 Legacy Removal
  - RoundSim resource / transaction / rollback 依赖
  - TryPrepareRoundApply
  - Demo-local Round Transaction
  - 失去消费者的旧 Stage / duplicate implementation

T5 Long-Window Correctness
  - step ~886 feasible-region-insufficient
  - Static / Moving 1000+ Tick

Large Particle Island Scaling
  - 单大 Island Cell-Pair Owner / per-round barrier
  - Island-level parallelism 也尚未实现

WA9 Full-Scale Acceptance
  - 1k → 2k → 5k → 10k
  - Behavior / Target / Movement / Particle / Combat
  - Projectile / Networking / Presentation
```

当前已有 10k-aware scheduler / timer / spatial / target scoped 验证，不等于完整 10k gameplay production-ready。

---

## 20. 当前总体结论

[INFERRED][HIGH] 当前架构最准确的描述是：

> **Persistent Worker 已经成为第二代模拟架构的核心，Production Owner 实现和正式 Production 路径已存在；但 Demo 默认仍以 Shadow 方式启动，且旧 RoundSim shell 仍承担部分资源数据源、事务、诊断和测试职责。**

因此现在的主要工程问题不再是“重新发明一套架构”，而是：

```text
断旧壳生产依赖
→ 删除重复实现
→ 拆巨型宿主文件
→ 清理迁移命名/注释
→ 再做完整规模验收
```

文档 ↔ 源码详细审计见 `SourceConsistencyAudit.md`。
