# MASSAITEST 源码阅读地图

## 1. 文档职责

本文解决一个问题：**当前代码应该从哪里开始读，哪些文件代表生产主链，哪些文件只是迁移壳、诊断或测试。**

如果目标是理解当前架构，不要从体量最大的 Demo 文件开始，也不要根据文件名中的 `Round`、`Processor`、`Kernel` 猜测当前生产权威。

当前架构事实先看：

```text
README.md
→ Docs/README.md
→ CurrentArchitecture.md
→ TargetArchitecture.md
```

然后再按本文进入源码。

当前 Legacy 风险与可删除性看 `LegacyCodeInventory.md`；文档与源码逐项对照结果看 `SourceConsistencyAudit.md`。

---

## 2. 最短主链阅读顺序

如果只想用最短时间理解“现在真正怎么跑”，按下面顺序阅读：

```text
1. MassCrowdWorkerRuntimeV2.h
        ↓
2. MassCrowdAsyncSimulationRuntime.h/.cpp
        ↓
3. Worker Domain Executors
        ↓
4. MassCrowdWorkerResultApply.h/.cpp
        ↓
5. CrowdDemoWorkerInputSync.cpp
        ↓
6. CrowdDemo Worker Result Apply / Host Adapter
        ↓
7. MassCrowdCore pure kernels
        ↓
8. Networking / Presentation
        ↓
9. 最后才读 Demo RoundSim legacy shell
```

核心原因：

> `MassCrowdRuntime` 定义“谁拥有状态、什么是 Work、怎么调度和发布”；Demo RoundSim 目前主要是宿主、迁移壳、诊断和旧事务残留，不再代表系统的核心模拟模型。

---

## 3. 第一层：Worker Runtime

### 3.1 `MassCrowdWorkerRuntimeV2.h`

路径：

```text
Plugins/MassCrowdSimulation/Source/MassCrowdRuntime/Public/
MassCrowdWorkerRuntimeV2.h
```

先找这些符号：

```text
ECrowdWorkerDomainId
CrowdWorkerRuntimeV2DomainExecutionRank
ECrowdWorkerField
ECrowdWorkerWorkKind
FCrowdWorkerRuntimeV2Config
FCrowdWorkerWorkKey
FCrowdWorkerWorkItem
FCrowdWorkerWorkRing
FCrowdWorkerTimeWheel
FCrowdWorkerDependencyIndex
FCrowdWorkerEntityStateStore
FCrowdWorkerResourceStore
FCrowdWorkerDirtyStateStore
ICrowdWorkerDomainExecutor
FCrowdWorkerDomainRegistry
FCrowdWorkerDeterministicShardPlanner
```

这一个头文件先建立四个概念：

```text
Entity
  ↓
Work
  ↓
Shard
  ↓
Task
```

以及 Runtime 的五类 Work：

```text
Entity / Pair / Resource / Cohort / Timer
```

注意：`ShardEntityCount` 是历史命名。当前通用 Shard Planner 实际按 **WorkItem 数量**切分，不等于固定多少个 Agent。

### 3.2 `MassCrowdWorkerRuntimeV2.cpp`

重点看：

```text
FCrowdWorkerWorkRing::Enqueue
FCrowdWorkerWorkRing::PopCurrent
FCrowdWorkerDomainRegistry::Freeze
FCrowdWorkerDeterministicShardPlanner::Build
FCrowdWorkerDeterministicShardPlanner::Merge
```

这里能确认：

- WorkKey 去重；
- Priority × Domain bucket；
- FairDomainCursor；
- 有界容量；
- stable sort；
- Shard 按 WorkItems 切分；
- Merge 不依赖 Task 完成顺序。

---

## 4. 第二层：Persistent Async Runtime

路径：

```text
Plugins/MassCrowdSimulation/Source/MassCrowdRuntime/
Public/MassCrowdAsyncSimulationRuntime.h
Private/MassCrowdAsyncSimulationRuntime.cpp
```

重点搜索：

```text
RegisterDomainExecutor
Start
SubmitResnapshot
SubmitIntentBatch
RestoreNetworkCheckpoint
Poll
TryExchangePublishedBatch
LaunchOwnerPump
CrowdWorkerV2DomainShard
CrowdWorkerV2OwnerContinuation
FieldOwnerDomain
FieldOwnerExecutionRank
```

这里回答：

> Worker 是不是真的异步？

答案从源码上看是肯定的：Domain Shard 通过短生命周期 `UE::Tasks::Launch` 执行，Owner continuation 在任务完成后继续推进 Runtime。

同时要注意：Persistent 指**逻辑 Owner 和状态长期存在**，不是让一个 `while(true)` Task 永久占住线程池。

---

## 5. 第三层：Domain Executors

按执行 Rank 阅读，不按 Domain ID 数值猜顺序。

当前执行 Rank：

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

### Lifecycle / Behavior

```text
MassCrowdWorkerLifecycleBehaviorDomain.h/.cpp
MassCrowdBehaviorSourceRuntime.h/.cpp
```

搜索：

```text
FCrowdWorkerLifecycleDomainExecutor
FCrowdWorkerBehaviorDomainExecutor
FCrowdWorkerBehaviorState
```

### Flow / Resource

```text
MassCrowdWorkerMovementDomain.cpp
MassCrowdWorkerFlowResource.h/.cpp
MassCrowdSharedFlowWork.h/.cpp
```

搜索：

```text
FCrowdWorkerFlowResourceDomainExecutor
FCrowdWorkerFlowFieldResourceCodec
```

### Target

```text
MassCrowdWorkerTargetDomain.h/.cpp
MassCrowdTargetRegionWork.h/.cpp
CrowdTargetRegionTransportKernel.h/.cpp
```

先读 Runtime wrapper，再读 Core kernel。

搜索：

```text
FCrowdWorkerTargetDomainExecutor
FCrowdWorkerTargetCohortState
BuildTopology
BuildDemand
SolvePlan
BuildGuidanceSharded
```

### Combat / Projectile

```text
MassCrowdWorkerProjectileDomain.h/.cpp
MassCrowdProjectileBoundary.h/.cpp
MassCrowdProjectileKernel.h/.cpp
MassCrowdCombatResolver.h/.cpp
```

搜索：

```text
FCrowdWorkerProjectileDomainExecutor
ICrowdWorkerCombatExtension
FCrowdWorkerProjectileState
FCrowdImpactFact
FCrowdHitFact
```

重要：Projectile Executor 的公开 Domain ID 是 `CombatReactive`，不是一个独立的新 Domain ID。

### Movement Planning / Movement

```text
MassCrowdWorkerMovementDomain.h/.cpp
MassCrowdWorkerMovementControlResource.h/.cpp
MassCrowdLocalPredictiveWork.h/.cpp
MassCrowdMovementPipelineWork.h/.cpp
```

`MovementPlanning` 当前已经包含 Behavior / Target / Combat / Flow 的组合与 Local Predictive 规划逻辑，不要只把它理解成“简单计算 DesiredVelocity”。

### Particle / Interaction

```text
MassCrowdWorkerInteractionDomain.h/.cpp
MassCrowdParticleWork.h/.cpp
CrowdParticleConstraintKernel.h/.cpp
```

先看 Worker Interaction Domain 如何准备完整 Particle 输入，再看 `FCrowdMassParticleWork::Solve()` 的 Interaction Island 分解，最后看 Core Constraint Kernel。

当前一个很重要的源码事实：

> 多 Interaction Island 已经可以拆成独立子问题，但 `FCrowdMassParticleWork::Solve()` 当前是在一个 Particle Resource Work 中顺序循环各 Island；这不是 Island-level UE Task 并行。

### Facing

```text
MassCrowdWorkerInteractionDomain.cpp
MassCrowdFacingFinalizeWork.*
CrowdFacingKernel.*
```

搜索：

```text
FCrowdWorkerFacingFinalizeDomainExecutor
```

---

## 6. 第四层：Result Apply

路径：

```text
Plugins/MassCrowdSimulation/Source/MassCrowdRuntime/
Public/MassCrowdWorkerResultApply.h
Private/MassCrowdWorkerResultApply.cpp
```

重点搜索：

```text
FCrowdWorkerResultCommitToken
FCrowdWorkerResultOwnerCommitBarrier
FCrowdWorkerResultApplyProxy::Prepare
ValidatePreparedState
CommitPreparedValidated
AcknowledgeDirtyBatch
```

当前原子提交顺序：

```text
Token Match
→ Proxy Validate
→ Host FinalValidate
→ HostApplyNoFail
→ Proxy Commit
→ Host SideEffectsNoFail
```

因此普通失败应发生在第一次写入前，而不是依赖“写一半后 rollback”。

---

## 7. 第五层：Demo 与 Worker 的边界

### `CrowdDemoWorkerInputSync.cpp`

这是目前理解 Demo → Worker 数据流最重要的宿主文件。

搜索：

```text
RegisterDomainExecutors
BuildVersionedResources
BuildConfig
ResolveMovementAuthorityMode
ResolveBehaviorAuthorityMode
ConsumeWorkerLateJoinCheckpoint
```

这里有两个必须知道的当前事实：

1. Production-capable Worker Domain 已经注册齐全；
2. **Demo 普通无参数启动时 WorkerV2 / Movement / Behavior Authority 默认仍为 Shadow，正式 Production 路径由命令行/runner 显式开启。**

另一个 WA8 关键耦合也在这里：

```text
BuildVersionedResources()
→ UCrowdDemoRoundSimPipelineSubsystem
→ GetRuntimeSharedFlowField()
```

这说明新 Worker Input 仍从旧 RoundSimPipeline 获取部分 Shared Flow 资源，是当前需要迁出的 Legacy 数据源依赖。

### `CrowdDemoMassSubsystem.cpp`

搜索：

```text
RegisterDynamicProcessor
UCrowdDemoWorkerInputSyncProcessor
UCrowdDemoWorkerResultApplyProcessor
legacy_round_processors=0
```

这里确认核心 Simulation Mass Processor 已收敛到 Input Sync + Result Apply；旧 Stage struct 的存在不等于它们仍被注册为独立动态 Processor。

---

## 8. 哪些 Demo 大文件不要先读

不要把下面文件作为理解当前架构的入口：

```text
CrowdDemoRoundSimPipelineSubsystem.cpp
CrowdDemoRoundSimProcessors.cpp
CrowdDemoRoundSimCoordinator.cpp
CrowdDemoMixedSandboxCoordinator.cpp
```

原因不是“它们完全没用”，而是它们同时混合：

```text
旧 Round 事务
迁移适配
场景逻辑
诊断
Golden / Metrics
测试支持
部分仍在使用的数据源
```

如果先读这里，非常容易把迁移期结构误认为最终 Runtime 结构。

只有在以下情况下再进入它们：

- 追 WA8 legacy dependency；
- 查 Demo 场景验收；
- 查旧 rollback / transaction；
- 查某个测试 fixture；
- 查仍未迁出的资源生产或 side effect。

---

## 9. `FCrowd*` 与 `FCrowdDemo*` 的粗略判断

不能仅凭前缀判断代码是否生产，但可以作为导航提示：

```text
FCrowd* / MassCrowd*
通常优先检查是否位于可复用 plugin public/runtime/core 路径

FCrowdDemo*
通常优先怀疑是 Demo 业务、场景、适配、测试或迁移遗留
```

但必须用调用方确认。

例如：

- `FCrowdDemoParticleConstraintKernel` 目前仍有 Failure Fixture / Hash 诊断消费者；不能只因有 Demo 前缀立即删除。
- `FCrowdDemoSharedFlowFieldKernel` 仍被旧 RoundSim metrics/penetration 诊断调用。
- Demo Target / LocalPredictive 重复 kernel 是否可删，需要先做 repo-wide 使用审计。

---

## 10. 调试某个问题时从哪里进

| 问题 | 第一入口 | 第二入口 |
|---|---|---|
| Worker 不推进 | `MassCrowdAsyncSimulationRuntime.cpp` | `CrowdDemoWorkerInputSync.cpp` |
| Work 重复/爆队列 | `MassCrowdWorkerRuntimeV2.cpp` | 对应 Domain Executor |
| 某个实体不更新 | `DependencyIndex / DirtyStateStore` | 对应 Domain State Codec |
| Target 错误 | `MassCrowdWorkerTargetDomain.cpp` | `CrowdTargetRegionTransportKernel.cpp` |
| 局部避让振荡 | `MassCrowdWorkerMovementDomain.cpp` | `CrowdLocalPredictiveInteractionKernel.cpp` |
| 穿人/穿墙 | `MassCrowdWorkerInteractionDomain.cpp` | `CrowdParticleConstraintKernel.cpp` |
| Projectile 重复命中 | `MassCrowdWorkerProjectileDomain.cpp` | `MassCrowdProjectileKernel.cpp` |
| Worker 结果没落到 Mass | `MassCrowdWorkerResultApply.cpp` | Demo Host Result Apply |
| Late Join / Correction | `MassCrowdWorkerNetworkState.*` | `MassCrowdNetworking` |
| VAT/实例异常 | `MassCrowdPresentationSubsystem.*` | Demo presentation adapter |

---

## 11. 阅读时的硬规则

1. 先确认文件属于 **Plugin Production / Demo Adapter / Legacy Migration / Test Fixture / Diagnostic** 哪一类。
2. 看到 `Stage` 不要默认它还是独立 Processor；先查是否实际注册。
3. 看到 `Shard` 不要默认代表线程；先查是否真的 `UE::Tasks::Launch`。
4. 看到 `Mass Fragment` 不要默认它是模拟 Authority；先看 Worker Field Owner。
5. 看到“Production”要区分 **Production-capable implementation** 与 **当前启动模式是否真的启用 Production**。
6. 任何删除建议先在 `LegacyCodeInventory.md` 里确认风险级别。
7. 文档与源码冲突时，把冲突记录到 `SourceConsistencyAudit.md`，不要靠记忆选一边。
