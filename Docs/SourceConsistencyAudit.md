# Documentation ↔ Source Consistency Audit

## 1. 审计范围

本轮审计目标不是证明功能通过，而是回答：

> 当前已经收敛后的文档，与 `main` 源码真实结构是否一致？如果不一致，是文档错、源码债务，还是只是需要更精确地描述？

审计基线：

```text
repository: dunching/MASSAITEST
main SHA: 5690f2f27c85bb811773c3359c1dfe3856f4747d
engine: UE 5.7
```

本轮只做静态源码与 Build.cs / descriptor 对照，**没有由 ChatGPT 独立编译 UE、运行自动化或启动真实场景**。

状态：

```text
EXACT
文档与源码一致。

NEEDS CLARIFICATION
方向正确，但文字容易让读者产生比源码更强的结论。

DOC BUG
文档/元数据明确与源码事实冲突，需要修文档。

SOURCE DEBT
文档描述的是正确目标或当前主链，但源码仍有迁移残留/双实现/旧依赖。

SOURCE COMMENT / NAMING DEBT
行为基本正确，但名称或注释仍停留在旧阶段。
```

---

## 2. 总表

| # | 主题 | 文档当前结论 | 源码事实 | 审计结果 |
|---|---|---|---|---|
| 1 | `MassCrowdCore` 隔离 | Core 是纯通用算法底层 | `MassCrowdCore.Build.cs` 只依赖 `Core` | EXACT |
| 2 | 核心 Mass Processor | Input Sync + Result Apply | `CrowdDemoMassSubsystem.cpp` 只注册这两个模拟 Processor，log=`legacy_round_processors=0` | EXACT |
| 3 | Worker Domain 顺序 | Lifecycle→Behavior→Flow→Target→Combat→MovementPlanning→Movement→Particle→Facing→Publish | `CrowdWorkerRuntimeV2DomainExecutionRank()` 完全一致 | EXACT |
| 4 | Domain Dependency | 文档容易把 DAG 箭头理解成完整 `GetDependencies()` | 实际 scheduler 由 execution rank 选 stage；每个 Executor 的 `GetDependencies()` 只声明部分 prerequisite | NEEDS CLARIFICATION |
| 5 | Worker Production Authority | 主要 Domain 已有 Production Owner | Production-capable executor/authority 均存在，但 Demo 无参数启动会把 WorkerV2 / Movement / Behavior 默认设为 Shadow | NEEDS CLARIFICATION |
| 6 | Entity→Work→Shard→Task | Work 为调度单位，Shard 后跑 UE Task | Work kinds、WorkRing、ShardPlanner、`CrowdWorkerV2DomainShard` 均存在 | EXACT |
| 7 | `ShardEntityCount=64` | 实际含义接近每 Shard 64 WorkItem | Planner 用 `Shard.WorkItems.Append(... Count)` | EXACT + NAMING DEBT |
| 8 | Result Apply 原子提交 | 所有 fallible validate 在首次写前 | Token→ProxyValidate→HostValidate→HostApply→ProxyCommit→SideEffects | EXACT |
| 9 | Target Region | Polar topology + demand + transport quota + guidance | Runtime wrapper 和 Core kernel 完整存在 | EXACT |
| 10 | Particle 多 Island | 多个 Island 可以独立求解 | 可以分解/独立 Solve，但当前在一个 Particle Work 内顺序循环 Island，不是 Island Task 并行 | NEEDS CLARIFICATION |
| 11 | Large single Island | 内部 Cell-Pair 并行仍 OPEN | 单 component 时直接 monolithic solve | EXACT |
| 12 | Projectile Authority | Worker owns projectile simulation | Executor 内持久 `Projectiles` state，并发布 Projectile/Combat dirty state；Mass 是代理 | EXACT |
| 13 | Plugin 模块依赖 | TargetArchitecture 有旧/模糊依赖图 | 实际 Build.cs：Runtime→Core+Spatial；Projectiles→Runtime+Spatial+Combat+Core | DOC BUG |
| 14 | Demo 项目定位 | Demo 是大规模 Runtime 验证宿主 | `.uproject` Description 仍写 “Isolated 500-entity...” | DOC/METADATA BUG |
| 15 | WA8 Legacy / SharedFlow owner | 新 Worker 不应从旧 Round shell 取生产资源 | Primary Shared Flow owner 已迁到 `UMassCrowdRuntimeSubsystem`，WorkerInputSync 已与 Pipeline 断开；Round transaction/rollback 等仍残留 | EXACT + SOURCE DEBT REMAINS |
| 16 | Duplicate Kernel | Plugin 是 canonical，Demo 仍有迁移副本 | Demo Particle/SharedFlow 仍有 diagnostics consumer；其他重复 kernel 仍需引用审计 | SOURCE DEBT |
| 17 | Demo 巨型文件 | 文档说 legacy shell 很大 | RoundSimPipeline ~390KB、Processors ~330KB、Mixed ~204KB | EXACT / SOURCE DEBT |
| 18 | Unity | Demo 关闭 Unity 是技术债信号 | Build.cs 明确因 legacy helper 重名而 `bUseUnity=false` | EXACT / SOURCE DEBT |
| 19 | Projectile dependency comment | 不属于文档事实 | `GetDependencies()` 注释仍描述 WA5/WA6 迁移时期 | SOURCE COMMENT DEBT |
| 20 | `AllowedPw5Mask` | 不影响架构 | Runtime public header 仍保留 PW5 阶段命名 | NAMING DEBT |

---

## 3. 审计发现 1：普通启动不是 Production Mode

### 文档原有风险

`CurrentArchitecture.md` 和 `FeatureChecklist.md` 使用“当前生产链”“Worker Production Owners DONE”等表述，本意是：

> Production Owner 实现和正式 Production runner 路径已经存在。

但这很容易被读成：

> 打开 Demo，不带参数，默认就运行 Full Worker Production Authority。

### 源码事实

`CrowdDemoWorkerInputSync.cpp` 的 Runtime config 先调用：

```text
FCrowdWorkerRuntimeV2Config::MakeProduction10k()
```

但随后根据：

```text
CrowdWorkerMovementMode=Production
```

决定 RuntimeV2 Mode；没有该显式 Production 值时会改为 `Shadow`。

Movement Authority 和 Behavior Authority 的解析函数同样默认返回 Shadow。

### 结论

```text
Production-capable implementation = YES
正式 Production runner/path      = YES
普通 no-flag Demo startup         = Shadow
```

状态：NEEDS CLARIFICATION。

这不是算法 bug，但必须在 CurrentArchitecture / FeatureChecklist 中写清楚，否则开发者排查“为什么 Worker 没接管”时会被文档带偏。

---

## 4. 审计发现 2：执行 Rank ≠ `GetDependencies()` 列表

### 源码事实

`ECrowdWorkerDomainId` 的稳定数值 ID 不是 DAG 排序值。

真正执行顺序来自：

```text
CrowdWorkerRuntimeV2DomainExecutionRank()
```

Async Runtime 从 pending work 中选择最小 Execution Rank 的 Domain 推进。

另一方面，`FCrowdWorkerDomainRegistry::Freeze()` 会验证每个 Executor 声明的 `GetDependencies()`：

- dependency 必须已经注册；
- dependency execution rank 必须早于当前 domain。

但 `GetDependencies()` 并不是把整个顺序重复写一遍。

例如：

```text
Behavior     deps = Lifecycle
Target       deps = FlowResource
Combat       deps = Target
MovementPlan deps = Behavior + FlowResource + Target + Combat
Particle     deps = Movement
Facing       deps = Particle
```

FlowResource 自身目前可以没有显式依赖，但 execution rank 仍把它放在 Behavior 之后。

### 结论

文档中的箭头应该解释成：

> **Runtime 的 canonical execution-rank order。**

而不是：

> 每条箭头都对应一个直接 `GetDependencies()` edge。

状态：NEEDS CLARIFICATION。

---

## 5. 审计发现 3：Particle Island 已分解，但没有 Island Task 并行

### 文档原有风险

`CurrentArchitecture.md` / `ParticleSafetyDesign.md` 使用“Island sharding”“独立 Solve”等术语，容易让人自然联想到：

```text
Island A → Task A
Island B → Task B
Island C → Task C
```

### 源码事实

`FCrowdWorkerParticleInteractionDomainExecutor::Execute()` 当前要求：

```text
WorkItems.Num() == 1
Kind == Resource
Scope == MovementControl
```

即 Particle Domain 当前从一个 Resource Work 进入。

`FCrowdMassParticleWork::Solve()` 会：

1. 构建 conservative closure graph；
2. 找 connected components；
3. 多 component 时设置 `bUsedIslandSharding=true`；
4. **在当前函数中顺序 `for (Component)` 调用每个 Island 的 solver**；
5. 稳定 merge；
6. 全局 Applied-State validation；
7. 失败则 monolithic fallback。

因此当前准确表述是：

```text
Island decomposition / independent sub-solve = DONE
Island-level UE Task parallelism              = NOT IMPLEMENTED
Large single-island internal parallelism      = OPEN
```

状态：NEEDS CLARIFICATION。

这也是后续性能治理非常重要的区别：现在“拆岛”首先减少了错误耦合和单次求解范围，但还没有把多岛 CPU 时间真正并行化。

---

## 6. 审计发现 4：TargetArchitecture 模块图需要修正

### 实际 Build.cs 主干

定义：下面 `A → B` 表示 **A depends on B**。

```text
MassCrowdSpatial        → MassCrowdCore
MassCrowdCombat         → MassCrowdCore

MassCrowdRuntime        → MassCrowdCore
MassCrowdRuntime        → MassCrowdSpatial

MassCrowdNetworking     → MassCrowdRuntime + MassCrowdCore
MassCrowdPresentation   → MassCrowdRuntime + MassCrowdCore
MassCrowdStandardSources→ MassCrowdRuntime + MassCrowdCore

MassCrowdProjectiles    → MassCrowdRuntime
                        + MassCrowdSpatial
                        + MassCrowdCombat
                        + MassCrowdCore

Host / Demo             → plugin public modules
```

原 TargetArchitecture 图使用上下箭头表达层次，但方向容易被理解成 Runtime 是 Spatial/Combat 的底层依赖或相反，不能作为精确依赖图。

状态：DOC BUG。

目标文档应该明确区分：

```text
产品职责层次
vs
Build.cs dependency direction
```

---

## 7. 审计发现 5：Primary Shared Flow Resource Owner 已完成迁移

### 当前源码事实

`FCrowdMassSharedFlowResource` 的 primary world resource 现在由：

```text
UMassCrowdRuntimeSubsystem
```

持有。Pipeline 的 static/dynamic SharedFlow build 通过 RuntimeSubsystem 调用同一个 `FCrowdMassSharedFlowWork::EnsureResource()`，自身只保留 Demo diagnostic view/counters。

`CrowdDemoWorkerInputSync.cpp` 构建 Environment versioned resource 时直接读取：

```text
RuntimeSubsystem.GetSharedFlowResource().Field
```

并且不再 include 或查询 `UCrowdDemoRoundSimPipelineSubsystem`。Target Prepared Resource 的资源身份也使用稳定 `CrowdWorkerResourceIds::Environment`，不再使用成员地址。

状态：该 P0 子切片已关闭。

仍需注意：RoundSimPipeline 继续承担 legacy Round frame、rollback/transaction、Target prepared state、metrics/diagnostics，因此 WA8 整体仍是 SOURCE DEBT / OPEN。

---

## 8. 审计发现 6：重复 Demo Kernel 不能整组盲删

当前 Plugin Core 已有 canonical：

```text
LocalPredictive
Particle
SharedFlow
TargetRegion
Facing
GuidanceCompose
VelocityHalfPlane
```

Demo 目录仍有对应重复实现。

但当前已确认：

### Demo Particle

仍被旧 RoundSim 用于：

```text
BuildFailureFixture
HashAppliedRoundSimState
```

求解本身已经大量转到 Plugin `FCrowdMassParticleWork::Solve()`。

### Demo SharedFlow

仍被旧 RoundSim metrics / penetration 检查用于：

```text
Sample
IsInsideInflatedObstacle
```

### Demo LocalPredictive / TargetRegion

在本轮对巨型 RoundSimProcessors 的直接调用检查中没有找到对应 Demo Kernel 调用，但仓库仍有 equivalence / legacy tests，因此必须完成 repo-wide consumer audit 后才能删除。

状态：SOURCE DEBT。

正确处理方式：把残留 diagnostics/test consumer 迁走，再删重复实现；不是一次性删一半然后靠编译错误找调用点。

---

## 9. 审计发现 7：项目 descriptor 已严重过期

`MassAICrowdDemo.uproject` 仍写：

```text
Description = "Isolated 500-entity MassAI crowd replication and movement demo."
```

当前项目已经包含：

```text
Persistent Worker
Behavior
Target
Combat / Projectile
Networking
Presentation
10k scheduler/spatial/target validation
```

因此 500-entity isolated demo 的描述已经不再准确。

状态：DOC/METADATA BUG。

---

## 10. 审计发现 8：代码阅读债务有源码自证

`MassAICrowdDemo.Build.cs` 明确写道：

> 由于许多 legacy translation-unit-local helper 同名，Unity amalgamation 会产生冲突，因此整个 Demo module `bUseUnity=false`。

这说明“读代码困难”不只是主观感受，而是代码组织已经实际影响构建策略。

状态：SOURCE DEBT。

这项不应马上通过强行开启 Unity 解决；应该等：

```text
WA8 legacy 删除
→ duplicate kernel 删除
→ 巨型文件拆分
→ helper 命名/utility 收敛
```

之后再恢复 Unity 构建验证。

---

## 11. 已确认与文档高度一致的部分

以下关键方向本轮没有发现根本冲突：

```text
Persistent Worker state owner
InputSync + ResultApply processor boundary
Entity → Work → Shard → Task
bounded WorkRing / TimeWheel / Dependency
real UE Task shard execution
stable execution rank
Result Apply owner barrier
Target-relative Polar Transport
Projectile Worker authority
network/presentation 非 simulation authority
single huge Particle island 尚未完成并行
```

因此当前治理不需要推翻架构重新设计。

[INFERRED][HIGH] 更准确的判断是：

> **目标架构大体已经选对并落下骨架；现在的主要困难来自实验迭代留下的旧宿主结构、重复实现、默认 Shadow 迁移开关、过时注释和缺少源码导航。**

---

## 12. Audit V1 后的代码治理顺序

建议按以下顺序进入真正源码清理：

```text
A. 断开 WorkerInputSync → RoundSimPipeline 的资源依赖
        ↓
B. 完成 WA8 Round transaction / rollback removal
        ↓
C. repo-wide duplicate kernel consumer audit
        ↓
D. 迁出 diagnostics/test consumer，删除 Demo generic duplicates
        ↓
E. 拆分 RoundSimProcessors / Pipeline / MixedCoordinator
        ↓
F. 清理迁移时代命名、注释、Public API 残留
        ↓
G. 重新评估 Demo Unity build
        ↓
H. T5 long-window / large island / WA9
```

其中 A/B 是架构正确性和可读性共同收益最高的部分。

---

## 13. 审计维护规则

以后修改核心架构时，本表只记录**当前仍然存在的冲突**。

修复后：

- DOC BUG → 修文档后改为 EXACT；
- SOURCE DEBT → 源码迁移完成后删除该行或改为 EXACT；
- 历史过程进入 Git/History，不在本文件累积阶段流水账。

任何“测试通过”都不能自动把 SOURCE DEBT 改成 DONE；必须确认旧 writer / old data source / duplicate implementation 已经物理退出。
