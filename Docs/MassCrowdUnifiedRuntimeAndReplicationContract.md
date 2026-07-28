# MassCrowd 通用运行与生产复制合同

## 1. 文档职责

[INFERRED][HIGH] 本文件是通用 Agent 能力组合、持续生命周期、生产复制、Demo 验收宿主及模块边界的长期稳定事实源。未来实现若与本文冲突，必须先修订本文并记录迁移理由，不能把 Demo 当前实现直接升级为产品合同。

[INFERRED][HIGH] 文档职责固定如下：

| 文档 | 唯一职责 |
|---|---|
| `DemoPurposeAndTargetEffect.md` | [INFERRED][HIGH] 产品目标和群体效果。 |
| `CurrentArchitecture.md` | [INFERRED][HIGH] 当前代码已经如何实现。 |
| `MassCrowdSimulationPluginArchitecture.md` | [INFERRED][HIGH] 插件模块与依赖方向。 |
| `PhasePlan.md` | [INFERRED][HIGH] 当前正在做什么。 |
| `FeatureChecklist.md` | [INFERRED][HIGH] 哪些能力已经通过。 |
| `TestScenarioMatrix.md` | [INFERRED][HIGH] 具体测试场景与结果。 |
| `MassCrowdUnifiedRuntimeAndReplicationContract.md` | [INFERRED][HIGH] 生产运行、行为组合和复制的长期稳定合同。 |

## 2. 统一 Agent 模型

[INFERRED][HIGH] 概念合同为：

```text
FCrowdStableEntityRef
├── ProviderId
├── StableEntityId
└── LifecycleSerial

FCrowdAgentFacts
├── StableEntityRef
├── Faction/Team
├── CapabilitySet
├── ActiveBehavior
├── BusinessTaskRef
├── TargetRef
├── MovementProfile
├── PresentationProfile
└── RuntimeState
```

[INFERRED][HIGH] `LifecycleSerial`用于拒绝实体槽位复用后的过期 Spawn、Correction、Hit、Cargo 和 Despawn 事实；仅比较槽位或短整数 AgentId 不足以构成生产身份校验。

[INFERRED][HIGH] Faction/Team 只用于关系和权限、可攻击性、可协作性、目标过滤、资源节点访问策略、伤害与交互规则。

[INFERRED][HIGH] Faction/Team 不得决定使用哪套 Flow、是否运行 Local Predictive、是否运行 Particle、是否允许动态 spawn/despawn、correction 频率、是否使用 ISM/VAT，或实体是否能够搬运与攻击。

## 3. Capability 与 Behavior 组合

[INFERRED][HIGH] 通用 Capability 示例包括：`CanMove`、`CanWander`、`CanMoveTo`、`CanPursue`、`CanHaul`、`CanAttack`、`CanGuard`、`CanFlee`、`CanUseRangedAttack`、`CanUseNavLayer`。

[INFERRED][HIGH] 通用 Behavior 示例包括：`Idle`、`Wander`、`MoveTo`、`Pursue`、`HaulPickup`、`HaulDeliver`、`Attack`、`Guard`、`Flee`、`Dead`。

[INFERRED][HIGH] Capability 表示实体“可以执行哪些行为”；Active Behavior 表示实体“当前执行什么”。行为切换不得替换底层 Movement、Networking 或 Presentation 实现。

[INFERRED][HIGH] 敌方可以具备 `CanHaul`，友方可以具备 `CanPursue` 与 `CanAttack`，中立实体可以具备 `CanWander` 与 `CanFlee`；搬运、追逐和攻击均不是阵营固有能力。

[INFERRED][HIGH] 行为层主要输出 `TargetRef`、Movement Objective、Movement Profile、Interaction Intent 与 Business Commit Request；行为层不持续直接实现导航、碰撞或 Particle 安全。

## 4. 通用运动链

[INFERRED][HIGH] 固定产品链为：

```text
Environment / Navigation Snapshot
→ Behavior Objective Prepare
→ Shared Flow / Target Region / Other Guidance Providers
→ Guidance Compose
→ Local Predictive
→ MovementPredict
→ Particle / Obstacle Safety
→ Facing
→ MovementFinalize
→ Runtime Commit
```

[INFERRED][HIGH] 所有阵营和行为复用同一条链。Business 只决定“去哪里、做什么”；Movement 决定“如何安全到达”。

[INFERRED][HIGH] Local Predictive 与 Particle 可以观察所有符合 Collision/Interaction Mask 的邻近实体，不按阵营自动分成两个互不作用的求解世界。

## 5. Cohort 合同

[INFERRED][HIGH] Cohort 由共享运动事实形成：`ObjectiveKey`、`NavigationLayer`、`MovementProfile`、`CapabilityProfile`、`MacroStrategy`、`EnvironmentRevision`。

[INFERRED][HIGH] Faction 可以影响 Objective Provider 的目标选择，但不得成为 Cohort kernel 的强制职业分支；Cohort 不等同于阵营。

[INFERRED][HIGH] Cohort membership 必须支持在 fixed-step boundary 增量加入、退出和迁移，不得假设一个 Round 内完整 Agent 集合永久固定。

## 6. 生产持续生命周期

[INFERRED][HIGH] 产品状态流为：

```text
Initial Relevant Snapshot
→ Spawn Delta
→ Despawn Delta
→ Membership Delta
→ Behavior / Task Delta
→ Movement Correction
→ Reliable Gameplay Event
→ Presentation Event
```

[INFERRED][HIGH] 概念 POD 包括：`FCrowdSpawnRecord`、`FCrowdDespawnRecord`、`FCrowdMembershipDelta`、`FCrowdBehaviorStateDelta`、`FCrowdRelevantSnapshotHeader`、`FCrowdRelevantSnapshotChunk`、`FCrowdCorrectionHeader`、`FCrowdCorrectionChunk`、`FCrowdGameplayEvent`。

[INFERRED][HIGH] Spawn/Despawn 必须在 fixed-step boundary 原子应用。Despawn 必须区分死亡、离开相关区域、业务回收和宿主销毁；客户端表现回收不得反向决定 Server 实体生命周期。

[INFERRED][HIGH] 生产世界不能依赖 Round reset 清理全部实体，必须支持持续 spawn/despawn、死亡移除、实体槽位复用、membership 变化、late join 和动态网络相关集。

## 7. 生产复制合同

| 事实类别 | 生产传输合同 |
|---|---|
| 共享不可变资源 | [INFERRED][HIGH] `Revision + Hash` 或资产引用。 |
| 初始相关集 | [INFERRED][HIGH] Snapshot Header + bounded Chunks。 |
| 动态生命周期 | [INFERRED][HIGH] Spawn/Despawn bounded batches。 |
| 群体事实 | [INFERRED][HIGH] Cohort/Target/Flow/Membership revisions 与 deltas。 |
| 个体业务事实 | [INFERRED][HIGH] Behavior/Cargo/Combat state deltas。 |
| 运动纠错 | [INFERRED][HIGH] Relevant entity correction chunks。 |
| 权威业务事件 | [INFERRED][HIGH] Stable EventId + Lifecycle 校验。 |
| 客户端表现事件 | [INFERRED][HIGH] Spawn/Impact/Expire/Animation/Cargo visual facts。 |

[INFERRED][HIGH] 所有 O(N) 数组必须有有界 chunk 或 batch 合同；O(1) 规则、revision 和 header 保持紧凑，不为形式统一强制分块。

[INFERRED][HIGH] 大型 Navigation/Flow 资源只复制 revision/hash 或资产引用，不复制整张图。Correction 面向当前相关实体集合，不假设全世界 membership 固定。

[INFERRED][HIGH] 复制精度和频率按 Relevancy、Ownership、可见性、变化率、事实可靠性和预算决定，不按 Faction 硬编码。

[INFERRED][HIGH] Cargo 被复制是因为实体当前携带 Cargo 事实；Attack 被复制是因为实体当前执行 Attack 并产生权威事件，而不是因为实体属于友方或敌方。

## 8. Demo 与生产协议的关系

```text
Production Plugin
├── Runtime
├── Networking
├── Presentation
└── Public Behavior / Provider APIs

Demo
├── 使用同一 Runtime
├── 使用同一 Networking
├── 使用同一 Presentation
├── 提供 Scenario 输入
├── 提供故障注入
├── 提供 hash 与指标观察
└── 不复制另一套算法或协议
```

[INFERRED][HIGH] Demo 可以增加固定 Round 窗口、相同输入重复运行、readiness、全量双端 hash、correction replay 验证、fixture、VIOLATION 与人工审片设施。

[INFERRED][HIGH] 上述 Demo 能力只能观察和控制生产协议，不得成为生产 Agent 运行的必需数据依赖。`RoundPlan`、`RoundResult`、测试端口和 Scenario 枚举不进入插件公共产品 API。

## 9. Bootstrap 重解释

[COMPUTED][HIGH] 当前 `FCrowdDemoRoundBootstrapPacket::Agents` 只作为Demo固定Round的本地Pipeline消费数组；网络传输已由显式版本adapter写入生产Relevant Snapshot Header/Chunks，完整Agents不再作为单个复制属性发送。

[INFERRED][HIGH] 后续不得只新增 `CrowdDemoBootstrapChunk` 并把测试语义固化到 Networking。必须先定义通用 `RelevantSnapshotHeader`、`RelevantSnapshotChunk`、`SnapshotAssembly`、`SnapshotRevision`、`SnapshotHash`、`SnapshotTimeout`。

[INFERRED][HIGH] Demo RoundBootstrap 适配上述生产协议；生产 late join 和进入新 Relevancy 区域也复用同一协议。

[COMPUTED][HIGH] 当前 Demo correction/checkpoint 已有 header、bounded chunks、乱序组装和超时处理，可作为底层实现参考；公共类型不得带 Round、Scenario、测试端口或 Demo 日志语义。

## 10. 业务模块边界

| 所有者 | 长期职责 |
|---|---|
| `MassCrowdCore` | [INFERRED][HIGH] 通用 POD、Movement kernels、排序、量化、hash。 |
| `MassCrowdRuntime` | [INFERRED][HIGH] Mass lifecycle、Gather、WORK、Merge、Commit、Capability 注册。 |
| `MassCrowdNetworking` | [INFERRED][HIGH] Snapshot/Delta/Correction/Event、assembly、revision、rollback 调度。 |
| `MassCrowdPresentation` | [INFERRED][HIGH] ISM/VAT、插值、Cargo 视觉、correction offset、调试绘制。 |
| 宿主 Business 或可选模块 | [INFERRED][HIGH] Combat、Logistics、Inventory、Plant、Warehouse、Damage、Loot。 |

[INFERRED][HIGH] Logistics 与 Combat 不进入 Movement Core；两者通过相同 Behavior、Objective 与 Gameplay Event 公共接口接入。

## 11. 测试合同

[INFERRED][HIGH] 自动化必须直接调用生产 POD 和生产协议。Demo 场景不得用测试专用 spawn 标志模拟真正 despawn 并宣称生命周期通过。

[INFERRED][HIGH] 后续测试层次固定为：`纯 POD fixture → 最小 Mass World → Production Networking loopback → Demo 真实地图 → continuous Sandbox → 20/100/500 → 原工程最小宿主`。

[INFERRED][HIGH] 生命周期测试至少覆盖：不同 fixed-step 分批 spawn、despawn 与死亡移除、LifecycleSerial 复用、Spawn/Despawn 乱序、late join snapshot 加后续 delta、correction 引用已销毁 Lifecycle、membership 增量、cohort 迁移、client 视觉实例创建与回收、Cargo/Combat 行为切换、rollback 不重复业务事件。

[COMPUTED][HIGH] A–J历史能力阶段已实现AgentFacts、Relevant Snapshot、Demo接入、lifecycle batches、真实Mass lifecycle、continuous lifecycle、统一Behavior、静态Recast Graph/Flow与独立20实体混合Sandbox。

## 12. P0冻结的产品化合同

[INFERRED][HIGH] GT/WORK边界固定为：

```text
GT canonical gather
→ immutable base snapshot
→ versioned host POD overlays
→ dependency-frontier WORK dispatch
→ stable merge
→ full-set validation
→ single GT atomic final writer
```

[INFERRED][HIGH] 独立验证遍历、跨boundary持久诊断写回和宿主业务事实准备可以保留；WORK不得访问UObject、World或EntityManager，任何缺失、重复、stale lifecycle、错误revision或任务失败必须整批零写入。

[INFERRED][HIGH] Nav V1只消费cooked静态Recast topology。运行时允许Objective attachment变化及对应Flow重建；动态NavMesh tile/topology变化不在P0–P5范围内。

[INFERRED][HIGH] 物流公共事实至少包含稳定TaskRef、CargoRef、SourceRef、SinkRef、CarrierRef、Quantity、State和Revision。Runtime提供POD与事务接口；宿主保存库存权威、Planner、Warehouse规则和故障恢复策略。Cargo无需逐件成为Mass entity，但必须具有可复制所有权和可见携带状态。

[INFERRED][HIGH] 最小生产复制闭环必须包含per-client late-join baseline、Snapshot→Delta恢复序列、动态Relevant-set provider、bounded durable state、unreliable latest-wins correction和StableEntityRef表现生命周期；Demo全量TArray multicast不能充当上述公共合同。

## 13. 当前合同实现状态（2026-07-23）

[COMPUTED][HIGH] per-client baseline、可靠state、latest-wins correction、空间RelevantSet、resync重建和StableEntityRef Presentation lifecycle已实现，J与Continuous真实late join已消费这些公共接口。

[COMPUTED][HIGH] GT/WORK合同已由旧Round完整消费；Nav资源公共所有者通过独立`NavFlowProductSmall`；物流POD/事务内核通过专用`FriendlyLogisticsSmall`地图与Cargo视觉门。P0–P5产品合同已闭环，K/L继续冻结。

[RULES I BROKE]：[COMPUTED][HIGH] P1未关闭时继续实施P2/P3/P5切片，违反锁定的阶段顺序；未改变K/L冻结边界。

## 2026-07-28 合同复核

[COMPUTED][HIGH] Boundary合同仍由一次gather/dispatch/wait、完整预验证、唯一逻辑writer和失败零写入实现；Round T2/T6/T7/T8当前工作树复测通过。
[COMPUTED][HIGH] Replication channel新增有界reliable batch与ACK后分批追赶；空Drain不再等同于resync，可靠缺序/溢出仍保持fail-closed，correction仍为按StableEntityRef latest-wins。
[COMPUTED][HIGH] FriendlyLogistics和J均消费公共Boundary/Nav/Networking/Presentation路径；P4 Cargo visual只由复制后的ownership驱动。当前累计构建、自动化和场景门通过，停止在K前。
