# MassAI Crowd 最终目标架构

## 1. 文档职责

本文定义 MASSAITEST / MassCrowdSimulation 已经确定的最终产品方向与终态架构。

本文回答：

```text
最终产品是什么
最终模拟权威是谁
模块怎么分工
多实体怎么调度
运动/战斗/网络/表现怎么分层
什么条件下才算 10k 完成
```

当前 `main` 实现到哪里看 `CurrentArchitecture.md`；当前实施顺序看 `PhasePlan.md`；测试状态看 `FeatureChecklist.md` / `TestScenarioMatrix.md`。

---

## 2. 最终产品定义

最终产品不是“虫群 Demo”，而是一套可复用的 Unreal Engine Mass 大规模 Agent Simulation Runtime。

最终产品本体：

```text
MassCrowdSimulation
+ 可选适配模块
```

`MassAICrowdDemo` 只作为验证宿主。

Runtime 必须支持持续 Agent population：

```text
Spawn / Despawn
Membership Change
Capability / Behavior Change
Objective / Target Change
Navigation / Resource Revision
Movement / Interaction
Combat / Projectile
Hit / Death
Correction
Late Join
Relevancy Enter / Exit
Presentation
```

敌人追逐、友方物流、中立游荡、近战、远程、不同体型，只是同一个 Runtime 上不同的 Capability / Behavior / Objective / Profile / Host Business 配置。

---

## 3. 不可破坏的核心原则

### 3.1 单一模拟权威

同一个模拟字段在任意时刻只能有一个 Production Owner。

最终每 World 的 Persistent Worker Runtime 是日常模拟状态的唯一权威。

Mass Fragment、Actor、Network Cache、ISM/VAT、Presentation Slot 等只能是代理、宿主业务状态或表现状态，不能与 Worker 同时推进同一模拟字段。

### 3.2 单向事实流

外部 → Worker：

```text
Spawn
Despawn
Gameplay / Behavior Command
Objective / Resource Revision
Environment Revision
Authority Correction
```

Worker → 外部：

```text
Dirty State Patch
Ordered Gameplay Event
Checkpoint
Digest / Diagnostic
```

刚由 Worker Result Apply 写到 Mass 的 Position / Velocity / Facing 不得在下一帧作为普通输入 Echo 回 Worker。

### 3.3 Work-driven

正常推进单位是 Work，不是“每实体每帧 Tick”。

只有状态、依赖、资源、时间或邻域变化需要重新计算时才产生 Work。

### 3.4 Deterministic

线程完成顺序不得决定结果。

跨 Shard / Pair / Event 的 merge 必须使用稳定 Key、稳定排序和唯一 Owner。

### 3.5 Fail-Closed

stale lifecycle、错误 revision/hash、容量溢出、非法双 writer、事件序列破坏、依赖漏标等不得静默降级为成功。

### 3.6 Demo 不保留第二套产品 Runtime

Demo 可以保留：

```text
Scenario
固定测试窗口
fault injection
Golden
metrics
录像 / human review
```

但不能长期保留：

```text
第二套 Worker Runtime
第二套 Commit Barrier
第二套 Simulation Clock
第二套生产 rollback source
重复通用 Kernel
永久 compatibility writer
```

---

## 4. 模块边界与依赖方向

这里明确约定：

> **`A → B` 表示 A depends on B。**

当前 Build.cs 主干已经接近最终边界，最终架构不得为了方便把依赖反向拉回 Demo。

```text
MassCrowdSpatial         → MassCrowdCore
MassCrowdCombat          → MassCrowdCore

MassCrowdRuntime         → MassCrowdCore
MassCrowdRuntime         → MassCrowdSpatial

MassCrowdNetworking      → MassCrowdRuntime + MassCrowdCore
MassCrowdPresentation    → MassCrowdRuntime + MassCrowdCore
MassCrowdStandardSources → MassCrowdRuntime + MassCrowdCore

MassCrowdProjectiles     → MassCrowdRuntime
                         + MassCrowdSpatial
                         + MassCrowdCombat
                         + MassCrowdCore

Host / Demo              → plugin public modules
```

这个图是**依赖方向**，不是 Domain execution order。

### MassCrowdCore

负责：

```text
稳定 POD / ID
排序 / 量化 / Hash
Behavior Source 基础模型
Shared Flow kernel
Target Region kernel
Local Predictive kernel
Particle Safety kernel
Facing / Guidance pure logic
```

不得依赖 UWorld / Actor / Mass EntityManager / Replication / Rendering / Demo Scenario。

### MassCrowdSpatial

负责稳定空间索引、候选查询、Broadphase 和通用空间事实。

### MassCrowdCombat

负责 Impact / Hit / Combat fact 与纯 resolver 边界。

项目 Damage/Armor/Loot/Inventory 等属于 Host。

### MassCrowdRuntime

负责：

```text
Persistent Worker state owner
WorkRing / TimeWheel / DependencyIndex
Entity / Resource / Dirty stores
Domain Registry
Task dispatch
Deterministic Merge
Checkpoint / Digest 基础
Result Apply Proxy
Owner Commit Barrier
Runtime metrics
```

Runtime 不解释 Boss、虫子、T5、T8、物流地图等 Demo 语义。

### MassCrowdProjectiles

负责 entity-native projectile simulation、trajectory、broadphase/sweep、lifecycle、Impact/Hit fact，并通过 Worker Domain 接入 Runtime。

### MassCrowdNetworking

负责 snapshot/lifecycle/intent/correction/checkpoint/digest/resync/relevancy/late join。

### MassCrowdPresentation

负责 StableEntityRef → instance slot、视觉生命周期、ISM/VAT/interpolation/visual state。

### MassCrowdStandardSources

提供可复用 Behavior Source，例如：

```text
MoveToLocation
ArriveAtLocation
FollowEntity
PursueEntity
FleeFromEntity
MaintainDistance
FaceMovement / FaceEntity
MovementLock
SpeedLimit
TimedImpulse
```

### Host / Demo

负责：

```text
Faction / Relationship 解释
目标选择
攻击合法性
Damage / Health / Inventory / Logistics
资产映射
Scenario / Acceptance
```

Host 通过 public API 提供外部事实、业务规则和结果消费，不复制插件算法。

---

## 5. 最终统一 Agent 模型

稳定身份：

```text
StableEntityRef
= ProviderId + StableEntityId + LifecycleSerial
```

统一概念必须分离：

```text
Faction / Relationship
Capability
Behavior Source Set
Objective / Target
Cohort
Movement Profile
Physical Profile
Combat Facts
Presentation Profile
```

含义：

- Faction：关系/权限/过滤。
- Capability：能做什么。
- Behavior Source：当前哪些意图在贡献。
- Objective/Target：业务目标。
- Cohort：哪些 Agent 可以共享宏观计算。
- Physical Profile：Radius / HardGap / SoftMargin / Mobility 等。

Faction 不直接选择运动算法、网络实现或 Particle 优先级。

---

## 6. Persistent Full Worker Authority

最终每 World 拥有一个长期存在的 Worker logical owner。

它拥有：

```text
Simulation Clock
Lifecycle
Behavior
Flow / Resource
Target / Cohort
Combat / Reactive
Projectile
Movement Planning
Movement
Particle / Interaction
Facing / Finalize
```

主数据流：

```text
GT / Network / Scene
      │ external facts
      ▼
Worker Input Sync
      ▼
Persistent Worker
      │ dirty patch / event / checkpoint
      ▼
Worker Result Apply
      ├── Mass Proxy
      ├── Network Adapter
      └── Presentation Proxy
```

最终正常 Simulation Mass Processor 只保留：

```text
Worker Input Sync
Worker Result Apply
```

视觉/Test Adapter 可有自己的 Processor，但不能重新形成第二套模拟 DAG。

---

## 7. Entity → Work → Shard → Task

### Entity

持久模拟对象，用 StableEntityRef + Lifecycle 管理。

### Work

需要重新执行的计算：

```text
Entity Work
Pair Work
Resource Work
Cohort Work
Timer Work
```

### WorkRing

Current / Next Epoch，有界容量，同 WorkKey 稳定去重/合并。

### DependencyIndex

记录 Entity / Resource / Cohort 变化会唤醒哪些 Work。

### TimeWheel

管理未来 Simulation Tick 的 cooldown / wakeup / projectile / recovery / timer。

### Shard

同 Domain Work 先 stable sort，再拆成有界 WorkItem batch。

Shard 容量不能被解释成固定 Agent 数量。

### Task

短生命周期 UE Task。只读冻结 Context，只写 shard-local output。

### Deterministic Merge

Owner 按稳定 Domain rank / Shard / Entity / Pair / Field / Event 顺序合并。

---

## 8. 最终 Domain Execution Rank

稳定 Domain ID 与 execution rank 分离。

Canonical execution order：

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

这里表示 Runtime stage ordering，不要求每个相邻节点都在 `GetDependencies()` 中形成一条直接 edge。

Executor 的显式 dependency 是 prerequisite contract；stage scheduler 的全局顺序由 execution rank 决定。

一个 Epoch 达到传播轮数上限时，剩余 Work 延迟到 Next Epoch并记录诊断，不无限递归自旋。

---

## 9. Behavior / Capability

最终不使用一个互斥 `ActiveBehavior` 承载所有语义。

同一实体可同时有：

```text
PursueEntity
+ MaintainDistance
+ FaceEntity
+ MovementLock
+ HitReaction / TimedImpulse
```

Source 通过通用 Channel 贡献并由 Resolver 合并。

高层“为什么做”属于 Host Planner；通用 Source 只表达“怎么移动/朝向/约束”。

临时高优先级 Source 到期后，持久任务实例和状态应精确恢复，不通过 Stop-All/Start-All 重建。

---

## 10. 最终群体移动分层

```text
Behavior / Objective
        ↓
Macro Guidance
   ├── Shared Flow
   └── Target Region Transport（可选）
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

解决世界空间大尺度通行和绕障。

### Target Region Transport

解决接近目标后的目标相对宏观分布。

### Local Predictive

解决短时间尺度局部速度冲突和公平让行。

### Particle

最终保证 Hard / Swept / Obstacle / Bounds / Environment Safety。

这四层不能互相吞并职责。

---

## 11. Target-relative Polar Transport Field

Target Region 是可选 Macro Guidance，不是统一导航层。

以目标为原点建立：

```text
Radial Band × Angular Sector
→ Polar Navigation Cells
```

维护：

```text
Current Population
Desired Population
Deficit / Surplus
Transport Plan
Edge Quota
Guidance
```

设计原则：

- 无永久 Agent Slot；
- Navigation Cell 可共享；
- anchor 是引导参考，不是精确站位；
- Cohort 来源于共享 Objective/NavigationLayer/MovementProfile/Capability/macro policy，不等于 Faction；
- Local Predictive / Particle 始终拥有局部可执行性和安全最终裁决。

---

## 12. Particle 最终并行方向

当前多岛分解与最终目标要区分。

终态应支持：

```text
Independent Interaction Islands
        ↓
可独立调度 / 并行

Large Single Island
        ↓
Stable Spatial Cells
        ↓
Stable Cell-Pair Ownership
        ↓
Per-round work
        ↓
Deterministic barrier merge
        ↓
Global exact validation
```

任何并行方案都不能以跨 Shard pair 漏约束为代价。

---

## 13. Combat / Projectile

最终：

```text
Host Attack Intent
→ Worker Combat/Projectile
→ trajectory / broadphase / sweep
→ ImpactFact
→ HitFact
→ Host business rule
→ Worker Combat/Reactive state + Ordered Event
→ Network / Presentation
```

大规模 gameplay projectile 不以逐 Actor 作为主路径。

Worker-side Host extension 必须是纯 C++/POD，不访问 UWorld/Mass Fragment/UObject 隐式状态。

Damage formula、armor、loot、mission 等仍属于产品 Host。

---

## 14. Result Apply 终态

所有 fallible validation 在首次外部写入前完成。

```text
Prepared Result
→ Runtime Commit Token Validate
→ Proxy Validate
→ Host FinalValidate
──────── no-fail boundary ────────
→ Host/Mass Apply
→ Proxy Commit
→ Host Side Effects
→ Network / Presentation publish
→ ACK
```

禁止把“写一半后 rollback”当正常原子提交方案。

Demo 不保留第二套 Barrier / Transaction。

---

## 15. Networking 终态

网络同步的是版本化模拟事实，不是默认高频复制全部 Transform。

主要合同：

```text
Lifecycle
Relevant Snapshot
Intent
Correction
Checkpoint
Ordered Event
Digest
Resync
Late Join
```

Late Join 顺序：

```text
Checkpoint
→ Resource Revisions
→ Event Baseline
→ later Delta
```

Correction 增加 CorrectionRevision；真正 Full Resnapshot / world switch / teardown 才改变 Generation。

---

## 16. Presentation 终态

Presentation 独立拥有：

```text
StableEntityRef → Slot
Visual Profile
ISM / VAT
Interpolation
Spawn / Update / Despawn
```

Simulation 只发布已解析的表现事实。

客户端不能通过隐藏实体、视觉偏移或修改碰撞 footprint 伪装模拟正确性。

---

## 17. Demo 最终职责

Demo 保留：

```text
T1–T8 / 后续 Scenario
测试地图
固定 acceptance window
readiness / hash / fixture
fault injection
performance metrics
录像 / human review
Demo-specific business adapter
```

Demo 不保留：

```text
产品第二 Runtime
产品第二 Commit Barrier
产品第二 rollback owner
重复 generic kernel
Round-specific production replication API
```

Round 是测试工具，不是最终产品生命周期模型。

---

## 18. 10k 最终验收定义

“能 Spawn 10000 个实体”不算完成。

完整规模路径必须覆盖：

```text
Behavior
Flow / Resource
Target
Combat / Projectile
Movement
Local Predictive
Particle
Facing
Networking
Presentation
Lifecycle / Late Join / Correction
```

规模梯度：

```text
1k → 2k → 5k → 10k
```

终态性能门包括：

```text
Worker simulation lag p95 ≤ 66.667 ms
Client frame p95         ≤ 33.333 ms
Visual p95               ≤ 16.667 ms
Realtime                 ≥ 0.95
Propagation limit hit    = 0
Ordered Event loss       = 0
GT Result Apply p95      不相对同规模 baseline 回退
```

还必须满足：

```text
determinism
server/client consistency
lifecycle correctness
no duplicate gameplay event
hard safety
late join correctness
correction convergence
```

---

## 19. 最终结构完成定义

结构上只有以下条件同时满足，才算 Full Worker Authority 收口：

```text
旧 four-node / Round Boundary production path = 0
Frame/Round Transaction production authority  = 0
普通帧 full Mass Gather                        = 0
blocking Wait / CallExecute simulation path    = 0
同字段 dual writer                             = 0
Demo generic duplicate production kernel       = 0
核心 simulation processors                     = InputSync + ResultApply
```

这也是为什么当前 WA8 legacy cleanup 不能只看“测试已经通过”。

---

## 20. 最终一句话

> **MassCrowdSimulation 的最终形态是一套 Work-driven、deterministic、Persistent Full Worker Authority 的大规模 Agent Simulation Runtime：Host 提供外部事实和业务规则，Worker 持有模拟权威，Mass/Network/Presentation 只做集成和结果消费；Demo 只负责验证这套生产架构。**
