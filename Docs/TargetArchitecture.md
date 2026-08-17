# MassAI Crowd 最终目标架构

## 1. 文档职责

本文定义 MASSAITEST / MassCrowdSimulation 已经确定的最终产品方向与目标架构。

本文只回答三个问题：

1. 这个系统最终要做成什么。
2. 最终由谁拥有模拟权威、各模块如何分工。
3. 当前代码最终要收敛到什么结构才算完成。

本文不描述当前 main 已经实现到哪里；当前事实由 `CurrentArchitecture.md` 负责。

本文不记录开发顺序；开发顺序由 `PhasePlan.md` 负责。

本文不记录功能是否已经通过；完成状态与测试证据分别由 `FeatureChecklist.md` 和 `TestScenarioMatrix.md` 负责。

专项算法的详细数据结构、公式和边界继续由对应 Design 文档负责。

---

## 2. 最终产品定义

最终产品不是一个“虫群 Demo”，而是一套可复用的 Unreal Engine Mass 大规模 Agent Simulation Runtime。

它要支持持续存在的 Agent population，而不是依赖固定 Round、固定 Agent 集合或测试关卡才能运行。

最终应能够长期处理：

```text
Spawn
Despawn
Membership Change
Behavior Change
Target Change
Navigation / Flow Change
Movement
Local Interaction
Combat
Projectile
Hit / Damage / Death
Correction
Late Join
Relevancy Enter / Exit
Presentation
```

敌方追逐、友方搬运、中立游荡、近战、远程、重型、小型等都只是同一套 Agent Runtime 上不同的 Capability、Behavior、Objective、Movement Profile 和业务配置。

最终产品本体是 `MassCrowdSimulation` 及其可选插件模块；`MassAICrowdDemo` 只作为生产架构的验证宿主。

---

## 3. 不可破坏的核心原则

### 3.1 单一模拟权威

同一个模拟字段在任意时刻只能有一个 Production Owner。

最终所有日常模拟状态由每 World 的 Persistent Worker Runtime 持有唯一权威。

Mass Fragment、Actor、Network Cache、ISM/VAT 和其他表现数据只能是已消费 Worker 结果的代理、缓存或适配状态，不能与 Worker 同时成为同一字段的模拟权威。

### 3.2 单向事实流

GT / Network / Scene 到 Worker 只输入外部事实：

```text
Spawn
Despawn
Gameplay Command
Behavior Command
Objective / Resource Revision
Environment Revision
Authority Correction
```

Worker 到外部只输出：

```text
Dirty State Patch
Ordered Gameplay Event
Checkpoint
Digest / Diagnostic
```

GT 不得把刚从 Worker 应用出来的 Position、Velocity、Facing、Combat 等代理状态在下一帧作为普通输入重新回灌 Worker。

只有显式 Authority Correction 才允许覆盖 Worker 状态。

### 3.3 Work 驱动，而不是全量 Entity Tick

Worker 的基本调度单位是 Work，不是“每实体每帧 Tick”。

实体只有在状态、依赖、资源、时间或邻域变化要求重新计算时才产生 Work。

最终系统不以每帧完整遍历 10k Agent 作为正常推进模型。

### 3.4 确定性优先

并行 Task 的完成顺序不得决定模拟结果。

所有跨 Shard、跨 Pair、跨 Event 的合并必须使用稳定 Key、稳定排序和唯一 Owner Merge。

### 3.5 Fail-Closed

缺失、重复、stale lifecycle、错误 revision、错误 hash、容量溢出、非法双 Writer、事件丢失或依赖漏标不能通过静默降级继续运行。

不确定时拒绝整批结果，而不是制造部分提交。

### 3.6 Demo 不保留第二套生产架构

当插件生产路径替代 Demo 旧路径后，旧 Runtime、Barrier、Transaction、rollback 数据源、fallback、alias 和双写入口必须物理删除。

Demo 可以保留测试 fixture、故障注入、Golden、Round 窗口和录像工具，但不能为了旧测试永久维护第二套产品 Runtime。

---

## 4. 最终产品与模块边界

最终依赖方向如下：

```text
MassCrowdCore
      ↓
MassCrowdRuntime
   ↙      ↓       ↘        ↘
Spatial  Combat  Networking Presentation
   ↓       ↓
Projectiles
      \
       \→ StandardSources

Optional:
MassCrowdStateTreeAdapter

Host / Demo
      ↓
MassCrowdSimulation public APIs
```

### 4.1 MassCrowdCore

负责：

```text
稳定 POD
Stable ID
Behavior Source 基础数据
Resolver
排序
量化
Hash
Shared Flow kernel
Target Region kernel
Local Predictive kernel
Particle / Safety kernel
Facing kernel
其他纯算法
```

Core 不得依赖：

```text
UWorld
Actor
Component
FMassEntityManager
Replication
Rendering
Round
Scenario
Demo 业务语义
```

### 4.2 MassCrowdRuntime

负责：

```text
Persistent Worker Runtime
Entity State Store
Resource Store
WorkRing
TimeWheel
DependencyIndex
Spatial Runtime integration
Domain Registry
Shard dispatch
Deterministic Merge
Dirty State
Ordered Event
Checkpoint
Result Apply Proxy
Owner Commit Barrier
Runtime metrics
```

Runtime 只认识通用 POD、稳定 ID、Domain 和版本合同，不解释 Demo 的 Boss、虫子、T5、T8、物流、测试地图等语义。

### 4.3 MassCrowdSpatial

负责稳定空间索引、邻域候选、Broadphase 和通用空间安全查询。

空间系统不得通过具体职业、Faction 或测试场景决定碰撞规则。

### 4.4 MassCrowdCombat

负责通用 Combat Fact / Resolver / Hit Fact 机制。

伤害公式、护甲、掉落、任务、库存等具体业务属于宿主。

### 4.5 MassCrowdProjectiles

负责 entity-native Projectile simulation、生命周期、轨迹、Broadphase、Swept hit 和通用 Impact / Hit Fact。

Projectile simulation 属于 Worker 模拟域；宿主只消费命中事实并执行自己的业务结算。

不得重新建立逐 Projectile Actor 作为大规模生产路径。

### 4.6 MassCrowdNetworking

负责：

```text
Relevant Snapshot
Lifecycle Delta
Intent
Correction
Checkpoint transport
Source / Behavior replication
Ordered Event transport
Digest
Resync
Late Join
Relevancy
```

Networking 消费 Worker 的版本化事实，不反向成为模拟 Owner。

### 4.7 MassCrowdPresentation

负责：

```text
StableEntityRef → Instance Slot
Spawn / Update / Despawn
ISM / VAT
Interpolation
Visual State
Cargo / Hit / Projectile visual facts
```

Presentation 永远不是 Simulation Authority。

### 4.8 MassCrowdStandardSources

提供可复用的通用 Behavior Source，例如：

```text
MoveToLocation
ArriveAtLocation
FollowEntity
PursueEntity
FleeFromEntity
MaintainDistance
FaceMovement
FaceEntity
MovementLock
SpeedLimit
TimedImpulse
```

Standard Source 不负责选择敌人、决定攻击合法性、提交伤害或解释具体业务。

### 4.9 Host / Demo Business

宿主负责：

```text
Faction / Relationship 业务解释
目标选择
攻击合法性
Damage / Health
Inventory
Warehouse
Logistics
Loot
Mission
资产映射
Scenario
验收与故障注入
```

宿主通过插件公开接口提供事实和消费结果，不能复制插件算法实现。

---

## 5. 最终统一 Agent 模型

一个 Agent 是真实业务实体，不是纯视觉粒子。

稳定身份固定为：

```text
StableEntityRef
= ProviderId
+ StableEntityId
+ LifecycleSerial
```

`LifecycleSerial` 必须拒绝槽位复用后过期的：

```text
Spawn
Despawn
Command
Correction
Hit
Damage
Cargo
Behavior Source
Projectile reference
```

统一 Agent Facts 至少分离以下概念：

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

其中：

- Faction 表示关系、权限和过滤。
- Capability 表示“能做什么”。
- Behavior Source 表示“当前有哪些行为意图在贡献”。
- Objective / Target 表示业务目标。
- Cohort 表示可共享宏观计算的一组实体。
- Physical Profile 决定半径、HardGap、SoftMargin、Mobility 等物理事实。

Faction 不得直接选择 Movement、Networking、Presentation 或 Safety 实现。

---

## 6. Persistent Full Worker Authority

最终每个 World 拥有一个长期存在的：

```text
FCrowdAsyncSimulationRuntime
```

它是模拟状态的逻辑 Owner。

最终 Worker Authority 覆盖：

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

目标主链：

```text
GT / Network / Scene
      │
      │ External Facts
      ▼
Worker Input Sync
      │
      ▼
Persistent Worker Runtime
      │
      │ Dirty State / Event / Checkpoint
      ▼
Worker Result Apply
      │
   ┌──┼───────────────┐
   ▼  ▼               ▼
 Mass Network     Presentation
 Proxy Adapter       Proxy
```

最终正常模拟 Processor 只保留：

```text
Worker Input Sync
Worker Result Apply
```

测试、表现或非模拟 Adapter 可以拥有独立 Processor，但不重新建立第二套模拟 DAG。

---

## 7. Worker 多实体处理模型

最终 Worker 使用四层概念：

```text
Entity
  ↓
Work
  ↓
Shard
  ↓
Task
```

这四层必须保持清晰，不能把“64 Entity Shard”误解为“每 64 个实体固定开一个线程”。

### 7.1 Entity

Entity 是持久模拟对象。

它保存在 Worker Entity State Store 中，通过 StableEntityRef 和 Lifecycle 管理。

### 7.2 Work

Work 表示某项需要重新执行的计算。

支持至少：

```text
Entity Work
Pair Work
Resource Work
Cohort Work
Timer Work
```

例如：

```text
Agent17 Movement 变化
A/B Interaction 需要重算
Target Cohort 3 失效
Environment Resource revision 更新
Projectile cooldown 到期
```

### 7.3 WorkRing

WorkRing 使用 Current / Next Epoch 双队列。

相同 WorkKey 在同一 Epoch 内稳定去重，合并 priority、reason 和 revision。

容量必须有界；不能无界增长。

### 7.4 DependencyIndex

DependencyIndex 记录：

```text
Entity dependency
Resource dependency
Cohort dependency
```

当某个事实改变时，只唤醒闭合依赖集合。

例如：

```text
Target Cohort A changed
        ↓
only Target / Guidance work for Cohort A
```

而不是重新运行整个世界。

### 7.5 TimeWheel

TimeWheel 管理未来 Simulation Tick 才需要运行的 Work：

```text
Cooldown
Movement wakeup
Projectile
Recovery
Sleep / Wake
Timer
```

没有到期的工作不参与普通扫描。

### 7.6 Shard

同 Domain 的 Work 先按稳定 Key 排序，再拆成有界 Shard。

Shard 的容量表达的是 WorkItems 数量，不保证一一等同于实体数量。

一个 Pair Work 涉及两个实体；一个 Cohort / Resource Work 可能代表大量实体。

### 7.7 Task

Shard 通过短生命周期 `UE::Tasks` 并行执行。

Task 只读取冻结 Context，只写自己的 Shard-local Output。

Task 不得直接并发修改全局 Worker State。

### 7.8 Deterministic Merge

多个 Task 完成后，由唯一 Owner 按稳定规则合并：

```text
Domain Rank
Shard Ordinal
StableEntityRef
Pair Key
Field Key
Event order
```

线程完成顺序不得影响结果。

状态字段允许按版本规则 latest-wins；Gameplay Event 不得被状态合并吞掉。

---

## 8. 最终 Domain DAG

稳定 Domain ID 与执行 Rank 必须分离。

最终主执行顺序为：

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

Domain Executor 是无 UObject 的纯 C++ 执行器。

一个 Epoch 内使用冻结的 Entity / Resource 版本。

达到传播轮数上限的 Work 延迟到 Next Epoch，并记录诊断；禁止递归自旋。

---

## 9. Behavior 与 Capability 最终模型

最终不建立一个单一：

```text
ActiveBehavior = Attack
```

作为所有行为的权威中心。

一个实体允许同时存在多个 Behavior Source，例如：

```text
PursueTarget
+ FaceTarget
+ MaintainDistance
+ AvoidDanger
+ HitReaction
```

Source 通过不同 Channel 贡献：

```text
Movement
Facing
Constraint
Interaction
Business
Presentation
```

最终由 Resolver 按稳定规则合并。

Recipe、Controller 或 StateTree 负责维护期望 Source Set，产生 Start / Update / Stop Command；它们不直接拥有 Movement 或 Safety 权威。

临时高优先级 Source 结束后，原持久 Source 和持久状态必须能够恢复，而不是 Stop-All / Start-All。

---

## 10. 最终运动与群体导航分层

整个运动链最终固定为：

```text
Behavior / Objective
        ↓
Macro Guidance
        │
        ├── Shared Flow
        ├── Target Region Transport
        └── Other reusable guidance
        ↓
Preferred Movement
        ↓
Local Predictive Interaction
        ↓
Movement Predict
        ↓
Particle / Obstacle / Bounds Safety
        ↓
Quantize
        ↓
Facing / Finalize
```

这些层不是互斥算法，而是解决不同尺度的问题。

### 10.1 Shared Flow

负责地图级宏观导航：

```text
从当前位置
绕过环境障碍
到达目标附近或下一宏观区域
```

大量同 Cohort Agent 应共享 Flow / Navigation facts，而不是每个 Agent 独立做完整路线规划。

### 10.2 Local Predictive Interaction

负责短时间尺度的局部可执行速度选择。

它根据当前位置、速度、半径、邻居、Preferred 和环境判断短期轨迹冲突。

它不认识 T3、T5、Boss、窄口测试等场景语义。

### 10.3 Particle Safety

负责最后不可放宽的安全边界：

```text
Pair Hard Distance
Swept Safety
Obstacle
Bounds
Environment Hard Safety
```

Particle 不负责选择业务目标，也不负责宏观导航。

---

## 11. Target Region：目标附近的极坐标 Transport / Flow Field

Target Region Transport 是可选 Macro Guidance Provider。

它只在业务需要“大量实体围绕某个目标进行区域分布”时启用，不是普通移动、自由游荡或窄口通行的必经层。

它的本质是：

> 以目标为原点建立 Target-relative Polar Transport Field，用来引导大量实体在接近目标后从不同方向进入、流动、分散并形成合理人口分布。

目标附近空间按：

```text
Radial Band
+
Angular Sector
```

形成 Polar Navigation Cells。

另外维护固定 Demand Regions，用于统计：

```text
Current Population
Desired Population
Deficit
Surplus
```

Transport Solver 在可行 Cell Graph 上产生：

```text
Cell → Cell Flow
Edge Quota
Plan Epoch
```

最终 Guidance 将宏观运输意图转换成 Preferred Movement。

Target Region 不是永久 Slot 系统：

```text
没有 per-agent 永久站位
没有 Region owner
没有“一 Cell 只能站一个实体”
```

Navigation Cell 是共享空间区域，Cell Anchor 只是方向参考。

完整链路：

```text
远离目标
   ↓
Shared Flow
   ↓
接近目标
   ↓
Target-relative Polar Transport Field
   ↓
Preferred Movement
   ↓
Local Predictive
   ↓
Particle Safety
```

不同攻击距离只决定各自合法 Target Distance Band / Terminal Region，不应该产生另一套碰撞或安全算法。

---

## 12. Interaction 与 Particle 并行模型

强交互 Domain 不能简单按连续 Entity ID 硬切 Shard。

如果两个实体之间存在约束，它们必须在同一闭合交互语义中求解。

首先通过 Spatial / Pair 关系建立 Interaction Graph，再形成闭合 Interaction Island。

例如：

```text
A-B-C-D       E-F       G-H-I
```

可以作为三个互不影响的 Island 并行处理。

如果数千实体形成一个巨大连通 Island，则继续通过稳定 Cell / Cell-Pair ownership 和 round barrier 进行内部并行，而不能无视跨 Shard pair。

最终每个并行阶段完成后必须进行稳定 Merge，并由全局 Applied-State Safety Validation 守门。

最终目标不允许“大 Island 时自动退回整世界单线程 Solver”成为常态生产路径；monolithic fallback 只能用于诊断或保守失败路径。

---

## 13. Combat 与 Projectile 边界

Combat 分为机制层和宿主业务层。

插件机制层负责：

```text
Target / Hit reference
Attack / Projectile simulation facts
Spatial candidate
Swept hit
ImpactFact
HitFact
Reactive facts
Stable event ordering
```

宿主业务层负责：

```text
是否允许攻击
伤害公式
护甲
Health
Loot
Inventory
Mission
具体业务状态
```

Projectile simulation 必须保持 entity-native 和批量化。

大规模远程单位不得退化成一 Projectile 一个 Actor 的主生产路径。

命中后只输出通用 Hit / Impact Fact，宿主不重新执行 projectile trajectory 或碰撞查询。

---

## 14. Result Apply 与最终原子提交

Worker 不能直接写 Mass、Actor、Presentation 或 Networking UObject。

Worker 输出 Prepared / Published Result 后，由 GT Result Apply 负责最终投影。

最终提交协议：

```text
Prepare immutable candidate
        ↓
Build Commit Token
        ↓
Runtime Final Validate
        ↓
Host Final Validate
        ↓
--------- first write boundary ---------
        ↓
Host State Apply NoFail
        ↓
Proxy Commit NoFail
        ↓
Ordered Event / Behavior / Target / Resource
Presentation / Network Side Effects NoFail
        ↓
ACK
```

所有正常可能失败的检查必须发生在第一次写入之前。

禁止：

```text
先写 Mass
→ 后面验证失败
→ 再依靠 rollback 补偿
```

来伪装原子提交。

Runtime Commit Barrier 只拥有通用 Generation / Sequence / Stable View 合同；Host-specific Target、Business、Mass Handle、Lifecycle、Revision 等通过 Host Prepared Plan 接入。

Runtime 不引用 Demo 类型。

---

## 15. 最终网络模型

生产网络同步的是模拟事实，不是简单高频广播全部 Transform。

主结构：

```text
Server Worker Authority
        ↓
Relevant Set
        ↓
Checkpoint / Intent / Event / Digest
        ↓
Client Worker
        ↓
Local Prediction
        ↓
Sparse Correction / Resync
        ↓
Presentation
```

### 15.1 Late Join

固定顺序：

```text
Checkpoint
→ Resource Revisions
→ Event Baseline
→ Subsequent Delta
```

Baseline 未完成前拒绝增量。

### 15.2 Relevancy

客户端只维护自己的 Relevant Set。

Relevancy enter / exit 不等于 Server Spawn / Despawn。

客户端表现退出不能反向决定 Server Entity 生命周期。

### 15.3 Correction

普通 Correction 是显式权威事实，并增加 CorrectionRevision。

只有全量 Resnapshot、World 切换、teardown 等真正身份失效事件才需要新的 Generation。

### 15.4 Behavior 网络

Source replication 必须保留稳定 Registry / Schema / SourceSet / Command 序列和 Hash。

Predictable Source 要求客户端与服务端 Registry 兼容。

StateTree 本身不复制，只复制其产生的稳定 Source / Command / Result facts。

---

## 16. Presentation 最终边界

Presentation 只负责“怎么显示”，不负责“世界真实发生了什么”。

输入来自已提交模拟事实：

```text
Transform
Visual State
Animation / VAT State
Hit Reaction
Cargo
Projectile Visual Fact
Spawn / Despawn Visual Fact
```

表现层允许插值、LOD、ISM/VAT、实例复用和视觉缓存，但这些变化不能反向改变 Worker simulation。

客户端不得通过隐藏 Agent、视觉偏移或假位置来伪造模拟通过。

---

## 17. 持续生命周期与 Cohort

最终生产世界不依赖 Round Reset。

必须支持：

```text
Continuous Spawn / Despawn
Death removal
Lifecycle slot reuse
Membership migration
Capability change
Objective change
Cohort change
Late Join
Relevancy enter / exit
```

Cohort 是共享宏观计算的分组，不等于 Faction。

Cohort 可以由这些稳定事实组成：

```text
ObjectiveKey
NavigationLayer
MovementProfile
CapabilityProfile
MacroStrategy
EnvironmentRevision
```

变化只失效相关 Cohort，不应无条件重建所有 Agent 的宏观 Guidance。

---

## 18. Demo 最终角色

`MassAICrowdDemo` 最终是 Production Architecture Verification Host。

它保留：

```text
T1-T8 场景
测试地图
固定 Round 验收窗口
Scenario input
readiness
Golden Hash
fixture
fault injection
VIOLATION
performance logging
录像
FFmpeg
人工审片
Demo-specific Business Adapter
```

它不保留：

```text
第二套 Runtime
第二套 Target / Particle / Flow kernel
第二套 Networking
第二套 Presentation lifecycle
Demo-local 通用 Commit Barrier
旧 Round Transaction 生产路径
插件算法 fallback
```

Demo 必须直接验证与真实产品相同的 Runtime、Networking、Presentation 和 Worker path。

---

## 19. 最终规模目标

第一阶段产品规模目标为：

```text
1k
2k
5k
10k
```

10k 验收不是只证明容器可以装下 10k Entity，也不是只跑一个 WorkRing 微基准。

最终 10k 场景必须让同一生产路径同时覆盖：

```text
Lifecycle
Behavior
Flow
Target
Movement
Local Predictive
Particle
Combat
Projectile
Networking
Presentation
```

并覆盖至少：

```text
开放移动
目标围攻 / Target Region
高密度 Interaction
异构 Capability
持续 Spawn / Despawn
Server / Client
Late Join
Correction / Digest
```

---

## 20. 最终性能与正确性门

最终性能门至少要求：

```text
Worker simulation lag p95 <= 66.667 ms
Client frame p95 <= 33.333 ms
Visual p95 <= 16.667 ms
Realtime >= 0.95
Propagation limit hit = 0
Ordered Event loss = 0
GT Result Apply 不相对同规模基线明显回退
```

同时必须满足：

```text
No stale lifecycle apply
No field double writer
No silent queue drop
No invalid hash acceptance
No partial commit on failure
No hidden visual workaround
Deterministic replay / hash contract holds
```

算法专项、真实地图、网络、视觉和人工审片必须分别证明，不能用一个综合场景替代所有归因测试。

---

## 21. 最终结构关闭门

当项目达到目标架构时，以下生产结构必须为 0：

```text
旧四节点 Boundary 架构
Frame Transaction
Demo-local Round Transaction
生产完整 Mass Gather 作为普通模拟输入
Boundary Request / Result / Commit 主模拟链
阻塞 Wait / WaitAndDrain / Future.Get
同字段 GT + Worker 双 Writer
Demo 内通用算法副本
Demo-local 通用 Commit Barrier
普通帧完整 rollback CPU 世界副本
```

正常模拟 Processor 应收敛为：

```text
Worker Input Sync
Worker Result Apply
```

其他 Processor 只能属于表现、测试或明确非模拟 Adapter。

---

## 22. 最终代码阅读和依赖方向

最终代码理解顺序应是：

```text
TargetArchitecture.md
        ↓
CurrentArchitecture.md
        ↓
MassCrowdCore
        ↓
MassCrowdRuntime / WorkerRuntimeV2
        ↓
Worker Domains
        ↓
Networking / Presentation / Combat / Projectiles
        ↓
Host Adapter
        ↓
Demo scenarios and tests
```

开发依赖方向始终保持：

```text
Host / Demo
    ↓
Plugin Public API
    ↓
Runtime
    ↓
Core / Pure Kernels
```

任何插件反向依赖 Demo、Scenario、测试地图、端口、Round 类型或 Saved 路径都属于架构错误。

---

## 23. 文档权责关系

最终文档事实源关系固定为：

```text
README.md
    项目入口

CurrentArchitecture.md
    当前 main 实际结构

TargetArchitecture.md
    最终已经决定的架构

PhasePlan.md
    Current → Target 的实施顺序

FeatureChecklist.md
    能力是否已经完成

TestScenarioMatrix.md
    完成结论的测试证据
```

详细专项继续由 Design / Reference 文档承载，但不得覆盖上述六份核心事实源的职责。

---

## 24. 一句话定义

MASSAITEST 最终要验证并交付的是：

> 一套基于 Unreal Mass 的、可复用的、确定性的、持续运行的大规模 Agent Simulation Runtime；每 World 由 Persistent Worker 统一拥有 Lifecycle、Behavior、Target、Combat、Movement、Interaction 等模拟权威，通过 Work-driven 增量调度、Shard 并行和确定性 Merge 推进状态，再将版本化结果原子投影到 Mass、Network 和 Presentation；目标附近的群体分布通过 Target-relative Polar Transport Field 等共享宏观 Guidance 完成，而不是让每个 Agent 独立重复完整导航和决策。