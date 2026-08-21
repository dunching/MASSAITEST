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
- Target 边界/容量终态细节；看 `Reference/TargetRegionBoundaryCapacityContract.md`。

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
Spawn / Despawn / Command / Objective / Resource Revision / Correction
        │
        ▼
UCrowdDemoWorkerInputSyncProcessor
        │
        ├── moving Objective absolute-effective-tick fact
        ├── Runtime-owned dynamic SharedFlow refresh when semantics change
        └── SubmitIntentBatch / versioned resource-objective changes
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

### 4.1 Moving objective 时钟域

当前 Moving Target Objective 的 `EffectiveFixedStepIndex` 已使用与 Worker `AbsoluteSimulationTick` 一致的 persistent runtime tick domain。

因此 Worker Target extrapolation 不再把 Round 开始前 uptime 误计入 objective age。

### 4.2 Moving objective dynamic SharedFlow

Full Worker Production fast path 在提交 versioned resources / intent 前，会在 moving objective 造成实际语义变化时刷新 `UMassCrowdRuntimeSubsystem` 唯一持有的 dynamic SharedFlow resource。

这不是恢复旧 Host SharedFlow owner；Environment revision 仍由 Runtime-owned resource 提供。

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
Flow / Resource
        ↓
Target Region（按需）
        ↓
Movement Planning
        ↓
Local Predictive
        ↓
Movement Predict
        ↓
Particle / Interaction
        ↓
Facing / Finalize
        ↓
Publish
```

职责：

- SharedFlow：macro route / world-space navigation guidance。
- TargetRegion：near-target finite-capacity spatial distribution，不是 formation slots。
- Local Predictive：short-horizon local yielding / conflict resolution。
- Movement：provisional kinematic progression。
- Particle / Interaction：最终 Hard / Swept / Obstacle / Bounds / Environment Safety。
- Facing / Finalize：final committed motion/facing state。

所有 `Objective`、`Profile`、`Environment`、`Capability`、`Behavior Source` 必须进入这条统一 Worker Domain path。测试名可以决定配置与断言，不得决定模拟算法或调度语义。完整责任合同见 `TargetArchitecture.md` §3.7–§3.11。

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

### 12.1 当前已成立的 Moving Target 输入编排

当前已修复：

```text
Objective effective tick clock-domain alignment
Runtime-owned dynamic SharedFlow refresh
```

因此旧 Moving `source_attachment_failures=20/20` 的 step ~398 failure 不再是当前 active blocker。

### 12.2 边缘裁剪与有限容量

历史 canonical Moving 暴露过：

```text
feasible_regions = 3 / 16
desired = 19
source_attachment_failures = 0
```

当前 main 已实现 `Reference/TargetRegionBoundaryCapacityContract.md`：

```text
immutable Environment/SharedFlow feasibility
→ clipped feasible Polar Cells
→ geometry/profile-derived finite capacity
→ Desired / Assignable / Overflow
→ reachability-aware deterministic admission
→ transient Plan/Execution claims
→ CapacityHold with zero inward Target pressure
```

Plan replacement 会验证 occupied/active claim 不超过 Cell capacity；Moving Cell 失效时释放/迁移 claim，新容量按稳定实体顺序 refill。这是 macro population admission，不是永久 Agent→Cell Slot。

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

`MassCrowdSimulation` Plugin production Core/Runtime 当前没有 Demo scenario/test/map-name simulation branch；已确认的 scenario-driven simulation coupling 位于 Demo host/bootstrap。Slice B/B.5 已在 main 关闭通用 Objective/Cohort/FlowBinding foundation，Slice C 已迁移 T3，表中其余项仍是后续代码迁移债：

Worker Runtime 现已具备并通过 headless/server correctness 回归验证的、独立于 `MovementProfile` 的 entity-level `FlowBinding`：稳定 `ObjectiveRef`、显式非零 `CohortKey` 与 generic `FlowResourceId` 共同选择独立 versioned SharedFlow。`CohortKey` 是调用方显式提供的稳定宏观 Objective/navigation 分组元数据，不从 `AgentId` 或 `FormationIndex` 派生，也不拥有 scheduler、Flow resource 或 capacity。MovementPlanning 对显式绑定从当前 Worker kinematic position 采样对应 Flow；一次 planning execution 按 `(FlowResourceId, Revision)` 只解码/结构验证一次，验证后的每 Agent sample 不再扫描完整 Flow 数组。显式 clear/unbind 删除 `FlowBinding` authoritative field，并恢复既有 `Environment` fallback。

DependencyIndex 以 Source→Dependent forward map 与 Dependent→Source reverse map 保持双向不变量。merged output 使用原子容量预检后的批量差分替换：只删除 stale edges、只添加 new edges，未变化关系不重建；`RemoveDependent` 与 replacement 的成本与受影响旧/新边相关，不再为每个 dependent 扫描完整索引。任意已提交 Resource revision 都通过 DependencyIndex 泛型唤醒已注册 work；Flow 与 Objective 使用同一传播机制，和显式 domain work 重合时由 WorkRing 合并。

Slice B.5 synthetic 100/1k/10k 回归覆盖三边/Agent dependency graph、1%/100% rebind、clear、lifecycle removal、Flow/Objective scoped revision 与 typed Flow reuse。Slice C final regression 的 10k 证据为 30,000 edges/high-watermark、2 个 decoded Flow resources、初始批量替换 21.063 ms、Flow/Objective revision propagation 0.300/0.259 ms、1% rebind 0.233 ms、100% rebind 21.649 ms。T3 现由 fixture 一次性发布 2 个显式 CohortKey、2 个 ObjectiveRef、2 个 generic Flow resources 与 20 个 `FlowBindingRevision`；Worker MovementPlanning 从当前 Worker position 采样绑定 Flow，T3 不再以 `FormationIndex` 连续选 Flow，也不再使用 scenario-owned authoritative preferred velocity。UE 5.8 rendered Editor/client Mass ProcessingQueue assertion 继续延期到 Phase 3，不属于 Slice C Runtime correctness blocker。

Lifecycle、Behavior Source、Movement Constraint、Interaction Participation、Correction 与 Acceptance 的目标所有权边界见 [`Reference/CrowdLifecycleBehaviorContract.md`](Reference/CrowdLifecycleBehaviorContract.md)；该 Reference 不改变下表所列当前实现状态。

Slice D1-A 已补齐 scenario-neutral Worker foundation：显式 versioned `SpawnPending / Active / Suspended / Removed` lifecycle transition、独立的 Particle / Combat / Presentation participation field，以及通用 movement/interaction consumer。Behavior 与 Movement Constraint 继续复用既有 Behavior Source resolved channels；Correction 继续复用 versioned authoritative dirty-state/correction barrier，不引入平行状态存储。T1 `OpenSpawnRelaxation` 尚未迁移，现有 T1 runtime branch 与 authoritative preferred-velocity compatibility path 仍属下表迁移债。

| 范围 | 已确认迁移债 | 目标合同 |
|---|---|---|
| T3（Slice C 已迁移） | 旧 `FormationIndex` 连续选 Flow 与 authoritative preferred-velocity bypass 已移除 | 显式 `ObjectiveRef` / `CohortKey` / `FlowResourceId` / `FCrowdWorkerFlowBinding`，进入通用 current-position MovementPlanning |
| T1 | scenario-name Flow bypass；zero authoritative velocity path | 通用 lifecycle / capability / Behavior Source / movement-lock 输入 |
| Moving Flow | SharedFlow refresh 由 scenario enum 驱动 | Objective / Environment / resolved Flow anchor 语义变化 |
| T6-A | TargetRegion activation 由 scenario progress 驱动 | 显式 Behavior / Objective / Capability activation input |
| T4 | `group_exit_hold` 位于 runtime host | 移至 Acceptance Harness / Runner |
| T7 | `FormationIndex` 驱动 continuous movement | 显式 Objective / Cohort / Profile / Behavior Source |

`FormationIndex` 只允许用于 initial spawn layout、fixture identity、acceptance grouping 和 debug/presentation label；不得持续决定 Flow、destination、preferred velocity、Target Cell/Claim、movement slot 或 rigid translation。

### 19.1 Demo generic duplicate implementations

Plugin Core 与 Demo 仍存在 LocalPredictive / Particle / SharedFlow / Target 等重复或兼容实现。

删除必须以 repo-wide consumer audit 为前提；不能因为 Round Transaction 已删除就整组盲删。

### 19.2 Bootstrap scratch 命名与遗留字段

`FCrowdDemoBoundaryFacingWorkState` 等类型仍带历史 Round/Shadow/Consumed 命名，其中部分字段已经只服务 bootstrap compatibility 或可能失去消费者。

这是下一轮 source cleanup 候选，但不是当前 live authority 的第二套实现。

### 19.3 巨型 Demo host 文件

`CrowdDemoRoundSimPipelineSubsystem.cpp`、`CrowdDemoRoundSimProcessors.cpp`、Coordinator/Mixed host 文件仍承担较多场景、指标和兼容职责。

结构上应继续拆分，但拆分必须保持 Worker authority 和 deterministic contracts 不变。

### 19.4 剩余诊断/验收链

Worker Target observability 已从 Worker retained `Target` / `TargetCohort` 恢复为只读 checkpoint，并进入 runner hard-failure gate。

Particle 与其它 special diagnostics 仍需逐项判断：

```text
Worker result / retained state
checkpoint-derived
explicit test-only observer
obsolete / retire
```

禁止仅为恢复日志重新建立第二套 simulation commit path。

---

## 20. 当前验证状态边界

当前已经有正式/当前链证据：

```text
Default Unity build                         PASS
DisableUnity build                          PASS
PersistentWorkerProductionStructure         PASS
Runtime WorkerResultApply / OwnerBarrier    PASS
first-step bootstrap                        PASS
ordinary direct-intent                      PASS
minimal T8 server-only                      PASS
Worker Target observability                 PASS
Static T5 fixed_step=1199 repeat            PASS ON MAIN
Moving objective clock                      PASS
Runtime-owned dynamic SharedFlow            PASS
Target boundary/corner capacity automation  PASS ON MAIN
Moving T5 fixed_step=1199 repeat             PASS ON MAIN
T6-A heterogeneous transit                   CLOSED
T6-B heterogeneous static target             CLOSED
T6-C heterogeneous moving target             CLOSED / PR #23 MERGED
```

当前尚未关闭：

```text
Networking / Late Join regression
双端 T8 formal runner
remaining diagnostics
Particle scaling
WA9
```

所以不能再把“post-cut runtime 全部未跑”当当前事实；但也不能因为核心链已 PASS 就把所有场景自动登记为 PASS。

---

## 21. 当前主要 OPEN 项

```text
1. Post-cut Regression Remainder
   - network / checkpoint / late join
   - 双端 T8
   - remaining diagnostics

2. Unified Behavior Migration Debt
   - T1/T3/T4/T6-A/T7 与 Moving Flow 的已确认 scenario coupling
   - 删除确认失去消费者的 Demo generic implementation
   - 拆 RoundSimPipeline host responsibilities

3. Large Particle Island Scaling
   - Island-level task parallelism
   - Single large-island Cell-Pair Owner / per-round barrier

4. WA9 Full-Scale Acceptance
   - 1k → 2k → 5k → 10k
   - Simulation + Network + Presentation + Performance
```

当前已有 10k-aware scheduler / spatial / target scoped 专项基础，不等于完整 10k production acceptance。

---

## 22. 当前总体结论

当前 `main@7f0f42478b731b6f4d9147163d9a8f61d1ae39aa` 的架构结论：

> **Persistent Worker 已经成为 Demo live server 的持续模拟权威；Plugin production Core/Runtime 当前无 Demo scenario/test/map-name simulation branch，但 Demo host/bootstrap 仍有已列出的 scenario-driven simulation coupling。T6-A、T6-B、T6-C correctness 均已关闭，其中 T6-C 经 PR #23 合并。LateJoin、完整双端 T8、Performance、Automated Behavior / Visual Acceptance 与 Human Visual Acceptance 仍为 OPEN。**
