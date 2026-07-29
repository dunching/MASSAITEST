# MassAI Crowd Demo 当前架构

## 0.0.1 2026-07-29 T9覆盖说明

[COMPUTED][HIGH] T9在DP0–DP6之后新增通用Attack Profile/State/Intent与Host Attack Adapter。业务Planner只决定Acquire/Windup/Commit/Recovery/Cooldown及目标，Movement继续由Standard Sources和安全链处理；Melee/MidRange经一次Boundary稳定Spatial Index执行相对Sweep，Ranged生成Mass Projectile，三类Impact统一进入Combat Resolver与Prepared Health/Death提交。

[COMPUTED][HIGH] `FCrowdImpactFact`现使用通用`ImpactId + ImpactTypeId`；Projectile仍以Projectile ID作为ImpactId，Melee/MidRange使用Attack Commit稳定ID。生产`AttackTarget` Source已删除，Behavior Registry黄金值版本化为`11335697795273479593`。

[COMPUTED][HIGH] T9 Mixed可靠Agent Payload为显式v2，包含攻击Profile、Target、Phase、PhaseEnter、CooldownEnd和FireSequence并拒绝旧布局。8517双端20实体门通过死亡、目标重选、TargetRegion/Flow缓存重建、Projectile守恒、双端Hash与`2.910ms` p95。

## 0.0 2026-07-29 PJ0覆盖说明

[COMPUTED][HIGH] 本文件顶部与第PJ节描述当前状态；后续按日期记录的迁移切片是历史执行证据，其中“尚未接入”“下一步”或“未授权100/500”不得覆盖R0–R7、S0–S6关闭结论。

[COMPUTED][HIGH] PJ0–PJ6现已关闭。Projectile跨Boundary持久状态只位于`MassCrowdProjectiles`的Mass Fragment；Boundary使用不可变Snapshot和临时Prepared数组，最终由已验证Patch一次写回。空间查询、Combat事实/Resolver和Projectile运行时分别归属`MassCrowdSpatial`、`MassCrowdCombat`和`MassCrowdProjectiles`，Demo不再拥有第二套Kernel/Fragment/Store。

## 0. 2026-07-28 R基线源码事实

[COMPUTED][HIGH] Runtime当前新增了公共`ICrowdBehaviorSourceProvider`、`FCrowdBehaviorRegistryBuilder`、稳定Provider/Context ID、冻结时Registry Hash、标准位置/速度/Facing Context、最多8项96字节扩展Context、96字节实例状态以及Writer Next State。

[COMPUTED][HIGH] 当前Runtime通过开放Provider注册并冻结Registry，领域Source由Demo Provider拥有；Mixed读取Resolved Movement/Facing，公共Scheduler使用通用Stage/Task/Scope键；Projectile由Mass Fragment唯一持有，旧跨Boundary数组权威与固定镜像路径已删除，Boundary内临时Gather/Prepared数组仍作为事务数据使用；StateTree Adapter已拆为默认禁用兄弟插件。

[COMPUTED][HIGH] 当前已有随`MassCrowdSimulation`加载的`MassCrowdStandardSources` Runtime模块，Provider ID=`100`。模块提供13种稳定TypeId的自主Evaluator、TargetKinematics/FormationAnchor v1 POD Context、MaintainDistance/Wander持久State；`MassCrowdRuntime`和`MassCrowdCore`均不反向依赖该模块或特判其TypeId。

[COMPUTED][HIGH] `MassCrowdSpatial`提供Movement安全索引、稳定Body Snapshot、Uniform Grid、Swept Bounds查询、移动球相对Sweep和环境AABB Sweep；`MassCrowdCombat`提供Impact/Hit、Effect Profile、纯Resolver和Prepared Host Commit；`MassCrowdProjectiles`提供Spawn/Profile/State、Mass Fragment/Trait/Store、生命周期事件、Kernel和Boundary Pipeline。Runtime只单向依赖Spatial，Projectiles依赖Core/Spatial/Combat/Runtime/MassEntity，三个公共模块均不引用Demo。

[COMPUTED][HIGH] Mixed Movement Worker消费Resolved Movement Goal/Velocity/Facing/Constraint，执行`FCrowdMassMovementPipelineWork → Particle Constraint → Facing Finalize`并在Prepared Boundary中一次提交Movement、Business与Slot状态；Business数组非空不再跳过移动。Local Predictive与Particle按InteractionLayer过滤跨层邻居。Presentation Resolver按Property保留最高优先级Override和稳定排序的全部Additive记录。

[COMPUTED][HIGH] 开放Behavior框架、通用Scheduler、行为网络v3、Standard Sources、Projectile三模块、Demo Business与T9均已关闭；T9后完整自动化为MassCrowd 64/64与CrowdDemo 133/133。既有20/100/500同路径并发Projectile门继续作为PJ6基线，T9本身只验收固定20实体。详细数值以`TestScenarioMatrix.md`为准。

## 1. 文档职责

[COMPUTED][HIGH] 本文件只描述当前生产代码、数据所有权和已验证边界。阶段计划查阅`PhasePlan.md`，验收状态查阅`FeatureChecklist.md`与`TestScenarioMatrix.md`，退出实验与旧端口查阅`Docs/History`。

[INFERRED][HIGH] 生产运行、持续生命周期与复制的长期合同以`MassCrowdUnifiedRuntimeAndReplicationContract.md`为事实源；Behavior Source详细合同与现行关闭状态以`EntityBehaviorSourceArchitecture.md`为事实源。本文中的Round、Scenario和testcase均是Demo实现事实，不是插件最终产品API。

## 1.1 当前能力与未来目标

[COMPUTED][HIGH] 当前已实现的是固定 Agent 集合、Round Bootstrap/Plan、双端 fixed-step、分块 correction/checkpoint、RoundResult/hash 验收，以及插件 Core/Runtime 的通用运动 kernel 与 WORK 合同。

[COMPUTED][HIGH] 当前已实现生产Relevant Snapshot、Demo RoundBootstrap adapter、lifecycle batches、真实Mass lifecycle store、组合式Behavior Source Runtime、Runtime静态Recast Graph/Flow资源、owner-only late-join channel、空间RelevantSet、可靠状态/latest-wins correction、公共Presentation slot lifecycle，以及20实体continuous混合Sandbox。行为架构事实源为`EntityBehaviorSourceArchitecture.md`。

[COMPUTED][HIGH] P0–P5公共产品闭环已完成：旧Round接入P1 Orchestrator与P3 channel/Presentation，J已删除O(N)安全检查，`NavFlowProductSmall`与专用`FriendlyLogisticsSmall`场景均通过。

[COMPUTED][HIGH] Behavior Source核心模型和World Store已进入Mixed生产路径：Movement Goal/Facing/Constraint消费Resolver输出并进入完整Movement/Particle/Facing安全链；边界使用完整Prepare/Validate后Final Apply；Source Codec v3进入可靠状态、late join与resync。StandardSources自主Evaluator、Demo五Controller稳定Diff、目标丢失Stop、临时压制精确恢复及同路径20/100/500服务端门均已有当前证据。真实StateTree业务Task不属于现行框架门。

[INFERRED][HIGH] P0–P5历史关闭结论继续成立；同路径20/100/500与第三方Source/并发Projectile同场组合门均已有当前证据。

[INFERRED][HIGH] 未来目标必须复用同一生产 Runtime、Networking 和 Presentation；Demo Round 协议只能成为生产协议的测试适配器，不能先维持一套测试协议、再另行替换成不兼容的生产协议。

## 1.2 历史源码只读审计矩阵（2026-07-22；非当前状态）

[COMPUTED][HIGH] 本节冻结2026-07-22审计快照，表内“缺失/后续归属”不得作为当前状态引用；当前状态以1.1节、`EntityBehaviorSourceArchitecture.md`和文件末尾日期更晚的检查点为准。

| 当前对象 | 当前职责 | 生产可复用 | Demo 专用 | 缺失/跑偏 | 后续归属 |
|---|---|---|---|---|---|
| Demo Round Bootstrap | [COMPUTED][HIGH] Server将排序后的固定Round初态以显式版本adapter写入Relevant Snapshot；复制bounded metadata并可靠multicast chunks，Client组装后构造本地packet进入既有Pipeline。 | [COMPUTED][HIGH] 直接复用`MassCrowdNetworking` Snapshot primitives。 | [COMPUTED][HIGH] Agent字段adapter、ServerTime与Round消费入口是Demo宿主职责。 | [COMPUTED][HIGH] 尚无late join重发及后续lifecycle delta。 | [INFERRED][HIGH] E新增通用Delta；Demo仍只做宿主adapter。 |
| `RoundPlanPacket` | [COMPUTED][HIGH] 描述 RoundId、持续时间、宽限、Scenario 规则与起始时间。 | [COMPUTED][HIGH] fixed-step 生效边界和 revision 思路可参考。 | [COMPUTED][HIGH] 是。 | [COMPUTED][HIGH] 固定 Round/Testcase 语义，不是 Behavior/Task Delta。 | [INFERRED][HIGH] 保留 Demo Director；生产行为进入公共 Behavior/Objective API。 |
| Correction/Checkpoint header 与 chunks | [COMPUTED][HIGH] 以 100-agent chunk 传输 Round correction/checkpoint，支持组装、重复/乱序处理和超时。 | [COMPUTED][HIGH] 分块与 assembly 机制部分可复用。 | [COMPUTED][HIGH] 类型和日志带 Round 语义。 | [COMPUTED][HIGH] 面向固定完整 Agent 集合，尚无 Relevancy/Lifecycle 通用合同。 | [INFERRED][HIGH] 抽取为 `MassCrowdNetworking` Correction primitives。 |
| readiness | [COMPUTED][HIGH] 验证客户端 AgentCount 与可见实例数后延迟 Round 开始，带超时 VIOLATION。 | [COMPUTED][HIGH] 测试宿主同步门可复用。 | [COMPUTED][HIGH] 是。 | [COMPUTED][HIGH] 不是生产网络相关集或 late-join ready 合同。 | [INFERRED][HIGH] Demo 验收层。 |
| `RoundResultHeader` | [COMPUTED][HIGH] 紧凑版本化 Round 验收汇总，最大序列化 2048 字节。 | [COMPUTED][HIGH] 版本化和 bounded header 思路可参考。 | [COMPUTED][HIGH] 是。 | [COMPUTED][HIGH] 含 T1–T8 指标，不是生产状态协议。 | [INFERRED][HIGH] Demo 结果层。 |
| Runtime Identity/Lifecycle fragments | [COMPUTED][HIGH] `FCrowdMassAgentFragment`保存迁移期 AgentId 及 ProviderId/StableEntityId/LifecycleSerial，并映射 Core StableEntityRef；Demo 另有 Id/VisualId/LifecycleSerial。 | [COMPUTED][HIGH] StableEntityRef POD与Runtime映射已实现；Lifecycle 预验证已用于 Movement Commit，F/G 已验证真实销毁、同槽位高 serial 复用与 stale 拒绝。 | [COMPUTED][HIGH] Demo 身份仍并存。 | [COMPUTED][HIGH] Round Movement 排序/提交仍使用 AgentId/Lifecycle，尚未迁移为 StableEntityRef 排序的统一产品 CommitPlan。 | [INFERRED][HIGH] P1 Runtime boundary。 |
| 当前完整 Agent 集合预验证 | [COMPUTED][HIGH] Movement/Finalize 在提交前验证完整 AgentId/Lifecycle/result 集合。 | [COMPUTED][HIGH] 原子整批提交门可复用。 | [COMPUTED][HIGH] 当前 expected set 来自固定 Round 集合。 | [COMPUTED][HIGH] 不支持动态 Relevant set 与增量 membership。 | [INFERRED][HIGH] Runtime boundary set + Networking Relevancy。 |
| T1 OpenSpawn/staging | [COMPUTED][HIGH] 所有 Mass 实体持续存在；testcase 只切换 Particle 参与状态并在 staging/active 布局间重置。 | [COMPUTED][HIGH] 压力传播和布局测试可复用。 | [COMPUTED][HIGH] 是。 | [COMPUTED][HIGH] 不是 spawn、despawn、死亡移除或槽位复用。 | [INFERRED][HIGH] 继续作为 fixture，禁止充当生命周期验收。 |
| Mass 实体真实 spawn/despawn 入口 | [COMPUTED][HIGH] `SpawnAgents`在场景启动时先销毁全部 tracked entities，再一次性创建固定数量；`DestroyTrackedAgents`做全量销毁。 | [COMPUTED][HIGH] 使用 `UMassSpawnerSubsystem` 的实际创建/销毁可作最小参考。 | [COMPUTED][HIGH] 固定场景启动/清理。 | [COMPUTED][HIGH] 无 fixed-step 增量 spawn/despawn、死亡移除、乱序 delta 或槽位复用 API。 | [INFERRED][HIGH] `MassCrowdRuntime` lifecycle。 |
| Target Region membership | [COMPUTED][HIGH] Demand/Plan 使用 Agent 列表与 MembershipHash，支持同一 Round 内计划重建和 claim 迁移。 | [COMPUTED][HIGH] Cohort 求解 kernel 与 membership hash 可复用。 | [COMPUTED][HIGH] 当前 cohort 由 Scenario/Capability profile 准备。 | [COMPUTED][HIGH] 没有网络 Membership Delta 或 fixed-step 通用加入/退出 API。 | [INFERRED][HIGH] Runtime Cohort + Networking Membership Delta。 |
| Combat `HitFact` | [COMPUTED][HIGH] Demo POD 含 EventId、目标 Agent/Lifecycle、伤害和击退/击飞事实，并执行去重与 Lifecycle 校验。 | [COMPUTED][HIGH] 业务事件形状和幂等规则部分可复用，且已有公共 StableEntityRef 可供后续引用。 | [COMPUTED][HIGH] 类型、状态和事务仍在 Demo。 | [COMPUTED][HIGH] 尚无插件 GameplayEvent 公共出口，现有 HitFact 尚未改用 StableEntityRef。 | [INFERRED][HIGH] 宿主 Combat 经公共 Gameplay Event 接口接入。 |
| Projectile visual events | [COMPUTED][HIGH] Demo multicast `Spawn/Impact/Expire`，客户端按 ProjectileId 去重并维护视觉实例。 | [COMPUTED][HIGH] 表现事件分类与幂等键可参考。 | [COMPUTED][HIGH] 是。 | [COMPUTED][HIGH] 未进入 `MassCrowdPresentation`，事件缺少通用 Lifecycle/EventId 合同。 | [INFERRED][HIGH] Presentation Event API。 |
| Client ISM/VAT 实例生命周期 | [COMPUTED][HIGH] 固定 Agent 集合按 `InstanceIndex`更新；数量不符时下一帧全量 rebuild。Projectile 视觉每帧清空并重建 active instances。 | [COMPUTED][HIGH] VAT playback、插值和 correction offset 已有 Demo 证据。 | [COMPUTED][HIGH] 资产与 processor 属于 Demo。 | [COMPUTED][HIGH] 无按生产 Spawn/Despawn Delta 的稳定增量实例创建、回收和槽位复用合同。 | [INFERRED][HIGH] `MassCrowdPresentation`。 |
| `MassCrowdNetworking` | [COMPUTED][HIGH] 已有通用Relevant Snapshot及StableEntityRef驱动的Spawn/Despawn/Membership batches；模块不包含Demo RPC或复制属性。 | [COMPUTED][HIGH] Snapshot primitives已由Demo adapter消费；Delta状态机提供严格序列、bounds、原子apply与membership hash。 | [COMPUTED][HIGH] 实际UFUNCTION/UPROPERTY发送包装仍在Demo Coordinator。 | [COMPUTED][HIGH] Delta网络包装、Correction/Event、Relevancy和late join调度缺失。 | [INFERRED][HIGH] `MassCrowdNetworking`。 |
| Nav Surface Graph / Shared Flow | [COMPUTED][HIGH] Core保存稳定分层节点、多边形、邻接宽度/坡度/整数成本并构建layer-aware integration/direction；Runtime从静态Recast tile/poly/portal提取图。 | [COMPUTED][HIGH] Core图和Flow不含World、Actor、Demo或地图路径；Runtime提取器仅依赖NavigationSystem/Recast运行数据。 | [COMPUTED][HIGH] J Coordinator直接拥有Graph、按目标Flow cache、marker、移动采样和验收指标。 | [COMPUTED][HIGH] J虽已组合G/H/Presentation，但公共Runtime尚无Graph生命周期、provider、revision、cache、预算或handle；动态NavMesh重建不在本轮合同内。 | [INFERRED][HIGH] P2 Runtime Nav资源。 |
| `MassCrowdPresentation` | [COMPUTED][HIGH] 当前只有 `IModuleInterface` 模块壳。 | [COMPUTED][HIGH] 无运行实现可复用。 | [COMPUTED][HIGH] 实际 ISM/VAT/Projectile visual 全部在 Demo。 | [COMPUTED][HIGH] 插件 Presentation API 与实例生命周期缺失。 | [INFERRED][HIGH] `MassCrowdPresentation`。 |
| Plugin Source 的 Enemy/Friendly/Faction 特判 | [COMPUTED][HIGH] 指定插件 Source 中未检出这些阵营分支。 | [COMPUTED][HIGH] 当前运动 kernel 保持 provider-neutral。 | [COMPUTED][HIGH] 否。 | [INFERRED][HIGH] 未来仍必须通过 Faction 只做关系/过滤的合同防止回流。 | [INFERRED][HIGH] Core 公共事实 + 宿主关系策略。 |
| Demo testcase 替代产品生命周期路径 | [COMPUTED][HIGH] T1 active/inactive 与 boundary layout reset、Round 全量重置会改变测试参与状态或位置，但不创建/销毁对应 Agent。 | [COMPUTED][HIGH] 仅测试诊断可复用。 | [COMPUTED][HIGH] 是。 | [COMPUTED][HIGH] 若称为动态生命周期即属错误。 | [INFERRED][HIGH] 保留 fixture，并新增独立 production lifecycle 测试。 |

[COMPUTED][HIGH] 历史P0审计快照：当时Core/Runtime运动链、AgentFacts、Snapshot/lifecycle和20实体Sandbox已实现，而late join、Presentation及J公共消费尚未完成；这些缺口后来已由P3–P5关闭。Hit/VAT和部分Target诊断继续属于Demo宿主语义。

## 1.3 P0 产品化闭环审计（2026-07-23）

| 审计问题 | 当前源码事实 | P0 决定 |
|---|---|---|
| canonical gather 后仍读哪些事实 | [COMPUTED][HIGH] `BoundaryGather`一次读取Runtime/运动/Formation/Facing及Combat业务事实；Combat prepare消费不可变business overlay。Facing prepare只做完整集合只读验证，最终`ApplyPreparedCommit`先复核目标集合再执行唯一运动/视觉写回遍历。FlowSample诊断遍历仍保留。 | [INFERRED][HIGH] P1继续把剩余Target/Particle诊断变成prepared patch；允许独立完整集合验证遍历。 |
| WORK 调度 | [COMPUTED][HIGH] Round即时`Async/Future.Get`为0；SoftPressure每个boundary一次Orchestrator Dispatch和一次completion-event Wait，真实worker链覆盖SharedFlow、TargetTopology、TargetDemand、TargetPlan、TargetGuidance、Movement、Particle与FacingFinalize/MovementFinalize，且Movement直接消费worker SharedFlow/Target结果。GT仍保留SharedFlow/Target同步副本，Business仍是屏障，Obstacle仍是旧同步Movement路径。 | [INFERRED][HIGH] P1继续删除同步副本、迁移Business/Combat与Obstacle，同时维持单次Dispatch/Wait。 |
| Nav所有权 | [COMPUTED][HIGH] Core持有Graph/Flow数据与kernel，Runtime只有静态Recast提取器；Round Pipeline和J Coordinator分别持有资源或cache，插件没有World级生命周期所有者。 | [INFERRED][HIGH] P2由Runtime subsystem统一持有静态cooked Recast派生Graph及Flow cache。 |
| J绕过公共层 | [COMPUTED][HIGH] J直接管理LifecycleWorld、Graph/Flow cache、O(N)安全检查、全量状态Multicast和ISM实例。 | [INFERRED][HIGH] P5前这些只能视为Demo验收组合，不能视为插件产品路径闭环。 |
| 物流事实归属 | [COMPUTED][HIGH] 当前Runtime只有通用Behavior/BusinessCommitRequest，Cargo carrier ledger与业务规则位于Demo Adapter。 | [INFERRED][HIGH] P4采用混合边界：Runtime公开Task/Cargo/Inventory POD与事务合同，库存权威、Planner和仓库策略留在宿主Adapter。 |
| late join与相关集 | [COMPUTED][HIGH] Snapshot和lifecycle batch primitives已存在，但没有per-client baseline调度、空间相关集、恢复序列或插件RPC channel。 | [INFERRED][HIGH] P3最小闭环必须包含owner-only client channel、Snapshot→Delta恢复序列、空间网格provider和关系闭包。 |
| Demo兼容镜像 | [COMPUTED][HIGH] Round专用状态仍被rollback、指标、结果和资产适配消费；J完整状态包、直接Flow cache和直接ISM只有J消费者。 | [INFERRED][HIGH] P1/P5只删除迁移后无消费者的镜像；Round观察/控制事实可保留，但不得继续拥有独立运动或实体状态权威。 |

[INFERRED][HIGH] GT/WORK完成定义冻结为“一次canonical gather、不可变基础Snapshot、显式POD overlays、依赖图WORK、稳定merge、完整集合预验证、唯一GT原子最终写回”；独立验证遍历是fail-closed合同的一部分。

[INFERRED][HIGH] Nav V1冻结为仅消费cooked静态Recast topology；允许Objective重新attachment与Flow重建，不实现运行时动态NavMesh topology更新。

[INFERRED][HIGH] Networking/Presentation产品闭环必须包含bounded durable delta、unreliable latest-wins correction、late-join baseline、动态相关集接口、StableEntityRef实例生命周期和Cargo视觉；Demo全量状态包或直接ISM不能替代这些公共能力。

## 2. 正式场景与当前生产链

[COMPUTED][HIGH] 顶层场景只保留`SimRoundObstacle=0`与`SimRoundSoftPressure=1`；T1–T8是SoftPressure下的能力测试，不是八套独立模拟架构。

```text
RoundPlanApply
→ Business / Target Fact Prepare
→ SharedFlowFieldBuild
→ Flow Guidance Candidate
→ [可选] Target Topology / Demand / Plan / Guidance Candidate
→ [可选] Business Guidance Candidate
→ Guidance Compose
→ Local Predictive
→ MovementPredict
→ Particle Safety / SF1 Obstacle Constraint
→ Facing
→ MovementFinalize
→ Authority Commit / Client Prediction Commit
```

[COMPUTED][HIGH] Guidance provider固定为`BusinessOverride > TargetRegion > SharedFlow > Stop`；只有`Guidance Compose`写最终宏观自主速度，不再依靠processor先后覆盖同一Intent。

[COMPUTED][HIGH] Local Predictive只消费Composed Guidance并输出局部可执行速度；Particle只处理预测位置的Soft/Hard/Swept/Obstacle/Bounds约束；Facing在稳定前只消费自主速度，不读取局部避让或Particle修正向量。

[COMPUTED][HIGH] `MovementFinalize`是普通fixed-step中`FCrowdDemoRoundSimStateFragment`的唯一写入点；它先由Runtime WORK构建全局稳定Commit plan并预验证完整AgentId/Lifecycle集合，再在同一GT boundary同步发布Runtime movement/state与Demo checkpoint兼容镜像。PlanApply与correction只在boundary恢复或应用状态。

## 3. GT / WORK与数据所有权

[COMPUTED][HIGH] Coordinator只负责RoundPlan场景控制、公共channel的版本化RoundResultHeader/correction适配、readiness和紧凑汇总，不承载Flow、Transport、Local Predictive或Particle算法。

[COMPUTED][HIGH] Guidance Compose、Local Predictive、MovementPredict、Particle与Facing已经使用不可变POD输入在WORK线程执行；GT等待完成并验证完整AgentId/result集合后统一写回对应Mass fragments。Compose、Local Predictive、Particle与Facing只调用Core纯kernel，MovementPredict是Runtime的确定性POD积分阶段。

[COMPUTED][HIGH] `MassCrowdRuntime`现已建立`UMassCrowdMovementTrait`、Agent identity、simulation state、properties、facing 与 movement output 持久 fragments，以及按 CapabilityProfile 稳定分批的 Gather、全局 AgentId 稳定 Merge、Lifecycle 全量预验证和 Commit 映射。Guidance、local velocity 与 Particle 等中间结果已经改由 prepared POD 传递，不再作为持久 Runtime fragments。

[COMPUTED][HIGH] 每个fixed-step在Plan/correction边界处理完成后先构建Runtime基础snapshot；Shared Flow、Target Region与Business分别发布按AgentId排序的不可变Guidance overlay。`FCrowdMassRuntimeBridge::BuildGuidanceRecords`把基础snapshot与三类overlay稳定合并为Compose WORK记录，要求Shared Flow覆盖全部Agent，并拒绝重复provider/Agent、错误revision或错误provider。WORK完成且全量AgentId/result集合验证通过后，GT一次更新Runtime composed与同源prepared SoA；旧Demo composed与MoveIntent已删除。Runtime结果、overlay与基础snapshot都是可重建派生状态，correction rollback后从恢复的权威事实重新生成，不复制进rollback snapshot。

[COMPUTED][HIGH] Local Predictive不再重新读取Runtime identity/state/properties/composed fragments来构建WORK输入；它直接消费同一boundary基础snapshot与prepared Core composed结果。Runtime WORK调用Core kernel并输出pair、grant、result、summary和可选trace。Mass查询只保留完整结果集合验证与Runtime local-velocity发布；Demo Pipeline继续保存grant、diagnostic和rollback状态，旧Demo local-velocity fragment已删除。

[COMPUTED][HIGH] MovementPredict直接消费boundary snapshot、prepared composed与prepared local-velocity结果；仅通过Mass查询叠加T1 boundary freeze和业务垂直运动事实，随后由`FCrowdMassMovementPredictWork`稳定排序、选择自主/局部速度、限制局部速度并积分预测位置。其prepared预测结果是Particle的正式输入，不再由Particle重新读取Runtime identity/properties镜像拼装基础事实。

[COMPUTED][HIGH] Particle Safety从同一boundary snapshot与prepared MovementPredict结果构造Core agent；`FCrowdMassParticleWork`执行Core Solve和applied-state安全复验，输出candidate/applied summary、hash、pair、result及可选trace。GT保留诊断、路线/稳定指标和T1状态采集，只发布Runtime particle与同源prepared结果；旧Demo particle fragment已删除。

[COMPUTED][HIGH] Facing现由`FCrowdMassFacingWork`在WORK线程调用Core kernel；它消费boundary snapshot、prepared composed与prepared particle结果。GT只从Facing fragment读取上一boundary settled计数，完整校验结果集合后同步发布Runtime Facing与Demo Facing兼容镜像；Movement Finalize只消费Runtime Facing结果。

[COMPUTED][HIGH] Shared Flow生产构建和Preferred生成现由`FCrowdMassSharedFlowWork`在WORK线程调用Core kernel。权威对象是Runtime定义的不可变Flow resource、动态anchor及T3双cohort resource，当前实例仍由Demo Pipeline迁移期托管；Demo `FCrowdDemoSharedFlowField`只作为Transport、末态指标和rollback迁移期结构镜像，不再执行生产Build或运动Guidance采样。GT准备Config、Target位置和每Agent停止/Goal事实，WORK稳定排序后输出Flow sample与SharedFlow candidate，GT在完整AgentId集合校验后发布Runtime candidate和Demo FlowSample阶段事实，不再发布Demo GuidanceCandidates。

[COMPUTED][HIGH] Target Region生产Topology、Demand、短期Plan、quota/claim execution、validation与Guidance现由`FCrowdMassTargetRegionWork`在线程池调用Core kernel。Target Demand已从统一基础snapshot和prepared Flow输出构建Agent输入，不再重复读取Mass；其余Demo processor仍负责场景规则准备、等待WORK、验证结果并发布兼容镜像。Demo Target Region kernel已退出这些生产调用。Plan与quota execution作为同一份状态进出WORK，转换后的Demo镜像继续服务Round指标、生命周期诊断、资源引用rollback及客户端调试绘制，当前尚未把这些宿主职责迁入Runtime subsystem。

[COMPUTED][HIGH] Runtime movement output已经接入正式Finalize与Commit：Particle或SF1 Obstacle阶段发布prepared final kinematics，Facing发布prepared yaw；`FCrowdMassMovementFinalizeWork::BuildInputFromPrepared`将这些结果与boundary snapshot稳定组装，再按CapabilityProfileKey分批生成Movement并全局按AgentId稳定Merge。GT不再为Finalize输入执行第一遍全实体Mass Gather，但仍在任何写入前对完整AgentId/Lifecycle及Runtime Facing/Particle或SF1 Obstacle结果执行原子预验证，随后同步更新Runtime simulation/movement与Demo RoundSim最终checkpoint状态。Authority/Client Commit均从Runtime MovementOutput写Transform、Velocity和Demo Movement，并校验其与RoundSim状态一致。

[COMPUTED][HIGH] Demo仍持有`MovementFinalize` processor外壳和Round指标/rollback采集，因此这不是整个Pipeline移出Demo；但最终运动事实已经只由Runtime Commit plan产生，Demo不再独立重算另一份最终Movement。

[COMPUTED][HIGH] 第十二切片将Compose、Local Predictive与MovementPredict合并为`FCrowdMassMovementPipelineWork`。GT从同一boundary snapshot、三类Guidance overlay、Local公平历史、T1 boundary fact和Reactive垂直运动事实构建一份按AgentId排序的不可变输入；一个ThreadPool任务内部顺序调用原有三个Runtime纯阶段；完整结果集验证通过后，GT在一次Mass查询中同步发布Runtime identity/state/properties/candidates/composed/local与Demo ProposedMovement。三个旧processor实现已物理删除，不存在逐阶段`Async→Get→GT发布→再Gather`链。

[COMPUTED][HIGH] 合并WORK仍保留三个独立阶段hash与诊断语义；性能归类把任务内部Compose耗时记入GuidanceCompose，其余GT准备、Local Predictive与MovementPredict记入LocalPredictive。计时值不进入确定性hash。

[COMPUTED][HIGH] 当前尚未达到“整个boundary只读取Mass一次”：基础运动事实及Compose→Local Predictive→MovementPredict→Particle/Obstacle→Facing→MovementFinalize的WORK输入链已收敛到同一snapshot/prepared链，但Business状态准备、T1/诊断/累计器读取、原子镜像预验证、阶段兼容写回及最终业务采集仍由分阶段GT processors处理；最终Mass archetype也尚未按能力拆分。因此不能宣称完整GT/WORK迁移完成。

[COMPUTED][HIGH] Rollback snapshot已停止复制Target topology、demand、guidance和短期plan大数组；短期plan以资源key引用不可变资源，snapshot只保存quota/claim等可变执行态、累计器和游标。恢复后按Target Fact重建派生结果。

[COMPUTED][HIGH] Server与Client运行同一fixed-step driver和纯C++ kernels；客户端visual只读取client RoundSim/visual state并提交ISM，不计算gameplay movement。

## 4. 已删除的当前兼容面

[COMPUTED][HIGH] TargetApproach、TargetSlotLayout、旧Polar Density及其execution diagnostic已从settings、fragment、kernel、processor、rollback、metrics、CLI与自动化中删除；Target Fact已提取为独立纯kernel。

[COMPUTED][HIGH] RoundResultHeader使用contract v2版本化NetSerialize；仅Server本地消费的Performance汇总不再进入复制payload，AgentState仍只经100-agent checkpoint chunks传输。高熵异构自动化为1566字节，8790真实T6M运行时为1970字节，均低于2048字节硬门且无Native NetSerialize Warning。

## 5. 当前验证事实

[COMPUTED][HIGH] 第十二切片通过Development、DebugGame Editor（均`-DisableUnity`）、`CrowdDemo` 105/105与`MassCrowd` 13/13。新增`MassCrowd.Runtime.MovementPipelineWork`证明合并前后三个阶段hash相同、输入反序不变且重复overlay被拒绝。8723 T2、8724异构T6、8725 T1和8726 T8均通过双端安全、同步与性能门；fixed-step p95分别为`3.849/5.337/1.649/2.131ms`，agents/visible均为`20/20`，correction位置/速度/Yaw误差均为0。

[COMPUTED][HIGH] 2026-07-22：Development、DebugGame Editor（均使用`-DisableUnity`）、`git diff --check`、当前105/105项`CrowdDemo`自动化及13/13项`MassCrowd`插件自动化通过。Runtime测试覆盖Target Region四阶段WORK、统一boundary输入、合并Movement Pipeline、MovementPredict语义、输入反序、重复Agent/overlay拒绝及旧/Core/Runtime等价。

[COMPUTED][HIGH] 8663 T2生产回归通过Runtime Finalize/Commit链：handoff/inside/terminal=`20/20/20`、Region coverage=`16/16`、安全与invalid/fallback为0、双端candidate/applied hash匹配、correction位置/速度/Yaw误差均为0；fixed-step p95=`3.529ms`、Commit p95=`0.021ms`。8664 SF1 Single authority路径运行未出现VIOLATION；该短运行未覆盖完整RoundResult。

[COMPUTED][HIGH] Facing生产迁移后的8665 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`、双端Yaw误差为0；fixed-step/Commit p95=`3.638/0.020ms`。8666 SF1 Single无Particle路径也完成Facing→Finalize→Authority Commit且无VIOLATION。

[COMPUTED][HIGH] Shared Flow生产迁移后的8667 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`、Flow/Transport双端hash一致、Hard/Swept/Obstacle/Bounds与invalid/fallback为0；fixed-step/Flow p95=`3.166/0.264ms`。8668 SF1 Single authority烟雾确认build hash=`267519150`、rebuild=`1`且无Fatal/Assertion/Ensure/`LogWindows: Error`/VIOLATION；该短运行没有完成整轮路线验收。

[COMPUTED][HIGH] Target Region Runtime接管后的8669 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`、plan/guidance unrouted及validation failure为0，五类Target Region hash双端一致；fixed-step/Topology/Demand/Plan/Guidance p95=`4.061/0.012/0.198/1.252/0.221ms`。8671异构T6 Static覆盖7个Capability cohort，inside-band=`20`、feasible coverage=`20`、unrouted/invalid/validation failure=`0`，五类hash双端一致，fixed-step p95=`6.552ms`。两次运行均为20/20可见、安全违规0且性能门通过；correction使用零误差快速路径，未实际触发历史重放。

[COMPUTED][HIGH] Guidance overlay与Local Predictive统一输入第二切片通过Development、DebugGame、`MassCrowd` 12/12及`CrowdDemo` 102/102。8677 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`，Compose/Local Predictive双端hash一致，fixed-step p95=`3.854ms`；8678异构T6保持inside/coverage=`20/20`、unrouted/invalid/validation failure=0、五类Transport及Compose/Local Predictive hash一致，fixed-step p95=`5.265ms`。两次运行安全、同步、可见实例与性能门通过。

[COMPUTED][HIGH] MovementPredict、Particle与Facing统一输入第三切片通过Development、DebugGame、`MassCrowd` 12/12及`CrowdDemo` 102/102。8681 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`，安全与双端hash通过，fixed-step p95=`5.379ms`；8682异构T6保持inside/coverage=`20/20`、安全、同步和五类Transport hash通过，fixed-step p95=`6.598ms`。8683 SF1 smoke保持Flow hash=`267519150`、rebuild=`1`且无硬错误；该短运行未完成整轮。

[COMPUTED][HIGH] MovementFinalize统一输入第四切片通过Development、DebugGame、`MassCrowd` 12/12及`CrowdDemo` 102/102。8684 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`，fixed-step p95=`4.393ms`；8685异构T6保持inside/coverage=`20/20`，fixed-step p95=`5.438ms`。两者安全、同步、prepared/兼容镜像原子门与性能门通过。8686 SF1 smoke保持Flow hash=`267519150`、rebuild=`1`且prepared Obstacle→Finalize路径无硬错误；短运行未完成整轮。

[COMPUTED][HIGH] MovementFinalize查询职责第五切片已将写前原子一致性检查与提交后业务/指标采集拆为`ValidationQuery`和`ApplyMetricsQuery`。前者只读取身份及Demo/Runtime的Particle、Facing、Obstacle镜像；后者不再读取已由prepared链替代的MoveIntent、Runtime properties、Runtime Particle和Runtime Facing。两段仍在同一processor和同一fixed-step boundary内执行，写前完整集合验证与失败时零部分写入语义保持不变。Development、DebugGame、`MassCrowd` 12/12及`CrowdDemo` 102/102通过；8687 T2保持terminal=`20/20`、coverage=`16/16`，fixed-step p95=`4.149ms`；8688异构T6保持inside/coverage=`20/20`，fixed-step p95=`5.683ms`。8689 SF1 smoke保持Flow hash=`267519150`、rebuild=`1`且无硬错误。

[COMPUTED][HIGH] 第六切片已把第五切片的`ApplyMetricsQuery`彻底拆成`MovementFinalize::ApplyQuery`与独立`PostFinalizeMetricsProcessor`。Finalize现在只执行prepared Commit plan的Demo/Runtime状态原子写入；post-finalize只读最终状态并采集Flow路线、T1/T3/T4进度、SoftPressure rollback和Combat snapshot，且仍位于VisualResolve之前，因此采样语义未改变。Pipeline为每个boundary记录Finalize成功step；post-finalize和Authority/Client Commit均以该标记为门，Finalize失败时不得对旧状态生成新snapshot或提交旧Movement。该职责拆分增加一次20实体只读查询，尚不是最终单次Mass读取实现。

[COMPUTED][HIGH] 第六切片通过Development、DebugGame、`MassCrowd` 12/12和`CrowdDemo` 102/102。8693 T2保持terminal=`20/20`、coverage=`16/16`，fixed-step p95=`4.074ms`，client Game/Render/GPU p95=`2.790/4.963/4.593ms`；8694异构T6保持completed/settled/inside-band/coverage=`20/20`，fixed-step p95=`5.073ms`，client Game/Render/GPU p95=`3.451/5.068/4.712ms`。8695 SF1 smoke保持Flow hash=`267519150`、rebuild=`1`且无硬错误。

[COMPUTED][HIGH] 第七切片继续收缩`PostFinalizeMetricsProcessor`查询：FormationIndex与checkpoint `RadiusCm`由boundary formation facts提供，Composed Guidance由prepared Runtime结果转换；post-finalize不再读取Formation、Composed Guidance、Particle Properties或未使用的Particle Constraint fragment。`RadiusCm`与Particle `PhysicalRadiusCm`保持不同语义，禁止互相替代。

[COMPUTED][HIGH] 首次8697异构T6回归因误把Particle `PhysicalRadiusCm`写入rollback checkpoint的`RadiusCm`，从第一帧correction起出现agent mismatch；该实现已修正为保存精确Formation radius。8698随后达到rollback hit/miss/mismatch=`80/0/0`、inside/coverage=`20/20`、fixed-step p95=`6.540ms`且双端无硬错误。8699 T2保持terminal/inside=`20/20`、coverage=`16/16`、rollback=`54/0/0`、fixed-step p95=`4.322ms`；8702 SF1保持Flow hash=`267519150`、rebuild=`1`。

[COMPUTED][HIGH] 第八切片又删除post-finalize对FlowSample与ObstacleConstraint fragment的读取。rollback FlowSample从同一prepared Runtime Shared Flow输出重建；SF1 penetration按原定义从boundary起点与Finalize终点复验膨胀障碍。8703异构T6 rollback=`80/0/0`、inside/coverage=`20/20`、fixed-step p95=`4.595ms`；8704 SF1保持Flow hash=`267519150`、rebuild=`1`且无硬错误。

[COMPUTED][HIGH] 第九切片删除post-finalize对GuidanceCandidates与Facing fragment的读取。完整rollback GuidanceCandidates由boundary snapshot和Flow/Target/Business prepared overlay通过Runtime Bridge重新构建；Facing processor在发布Runtime结果时同步保存包含连续settle计数与最终资格的精确rollback fact，post-finalize只按AgentId消费该prepared事实。Development、DebugGame、`MassCrowd` 12/12与`CrowdDemo` 102/102通过；8705异构T6 rollback hit/miss/mismatch=`80/0/0`、inside/coverage=`20/20`、fixed-step p95=`4.551ms`且双端无硬错误；8706 SF1保持Flow hash=`267519150`、rebuild=`1`且无硬错误。

[COMPUTED][HIGH] 第十切片把T1 OpenSpawn状态收敛为PipelineSubsystem中的唯一运行时权威状态，并为每个boundary生成按AgentId稳定排序的prepared事实；MovementPredict、Particle和客户端视觉只消费该事实或唯一runtime，已物理删除`FCrowdDemoOpenSpawnRelaxationFragment`。pending reset在完整集合验证后原子消费，重复、缺失、错位或过期事实均拒绝。

[COMPUTED][HIGH] Combat/Visual rollback现在采用两阶段完成门：PostFinalize只记录Identity与最终RoundSim movement facts；VisualStateResolve完成最终Health、BusinessState、AttackPhase、Reactive、HitFlash和VisualState事实。snapshot只有在`MovementFactsComplete && CombatFactsComplete`后才进入`SnapshotReadyForReplay`，不完整snapshot不得用于correction replay或checkpoint发布。

[COMPUTED][HIGH] `PostFinalizeMetricsProcessor`当前Mass requirements仅为`FCrowdDemoMassIdentityFragment`与只读`FCrowdDemoRoundSimStateFragment`；结构自动化同时禁止OpenSpawn及六个Combat/Visual fragment回流。Development与DebugGame Editor（`-DisableUnity`）、T1 4/4、Combat 15/15、结构1/1、`MassCrowd` 12/12及完整`CrowdDemo` 105/105通过。

[COMPUTED][HIGH] 默认Unity Development仍在未由本切片修改的`MassCrowdSimulation`插件旧`.cpp`中因匿名命名空间辅助函数重名失败；当前验证使用`-DisableUnity`。该构建兼容性债务未被运行回归掩盖，也不应写成第十切片行为回退。

[COMPUTED][HIGH] 第十切片双端运行：8707 T1保持六阶段、传播与settling合同，Particle安全违规和invalid/fallback均为0，rollback hit/miss/mismatch=`53/0/0`，agents/visible=`20/20`，fixed-step p95=`1.851ms`；8709异构T6保持inside/coverage=`20/20`、rollback=`53/0/0`及五类Transport hash一致，fixed-step p95=`6.334ms`；8708 T7与8710 T8均无安全、同步或snapshot完整性错误，T8 attack/projectile/event hash双端一致，fixed-step p95分别为`2.559/2.215ms`。8714 SF1短时smoke保持Flow hash=`267519150`、rebuild=`1`；该smoke未完成整轮。

[COMPUTED][HIGH] 第十一切片物理删除六个已被Runtime权威状态取代的Demo迁移镜像：`RoundMoveIntent`、`RoundGuidanceCandidates`、`RoundComposedGuidance`、`RoundLocalVelocity`、`RoundParticleConstraint`与`RoundFacing` fragments。Mass template、spawn初始化、processor requirements/写入、派生rollback副本及旧`BuildGatherRecord()`适配入口均同步删除；Facing连续settle状态改由`FCrowdMassFacingFragment`持有并由rollback显式恢复。MovementFinalize直接验证Runtime Facing/Particle，不再做Runtime↔Demo派生镜像一致性检查。

[COMPUTED][HIGH] 本切片没有删除`RoundProposedMovement`、`RoundFlowSample`或`RoundObstacleConstraint`：三者分别承载MovementPredict阶段传输/诊断、Transport与路线/rollback事实、SF1正式障碍安全结果，不是Runtime结构的逐字段重复。`RoundSimState`仍是checkpoint、网络结果和Demo指标所需的最终提交状态，也不是待删迁移镜像。

[COMPUTED][HIGH] 第十一切片通过Development与DebugGame Editor（`-DisableUnity`）、结构删除与Runtime适配器等价测试、`CrowdDemo` 105/105及`MassCrowd` 12/12。8722 T2保持terminal/inside=`20/20`、coverage=`16/16`、安全与双端hash通过，fixed-step/client frame p95=`4.028/5.248ms`；8717异构T6保持completed/settled=`20/20`、七类Capability及Transport/T6 hash一致，fixed-step/client frame p95=`4.556/5.481ms`；8718 T1与8719 T8分别为`1.641/2.025ms`，T8三类业务hash双端一致。8721 SF1 smoke保持Flow hash=`267519150`、rebuild=`1`，该短运行未完成整轮。

[COMPUTED][HIGH] 收敛后20实体fixed-step p95：T1=`1.131ms`、T2=`3.073ms`、T3=`2.938ms`、T4=`3.376ms`、T5S=`5.362ms`、T6A=`3.114ms`、T6S=`4.261ms`、T7热复跑=`1.739ms`、T8=`1.598ms`；这些通过运行的realtime均不低于1.000，step-limit hit为0。

[COMPUTED][HIGH] T1的staging/active测试布局切换现已显式归类：普通`non_correction_discontinuity=0`，测试boundary reset jump=21，Round reset jump=20；不得把测试夹具换布局混入普通移动连续性失败。

[COMPUTED][HIGH] 客户端性能窗口现从Round 1实际激活开始，并分别记录Game、Render、GPU、shader compile、async loading、visual asset compiling与PSO precache；启动热身单独统计，不再与Round 1混算。

## P1–P5 产品化执行检查点（2026-07-23）

[COMPUTED][HIGH] `FCrowdMassBoundaryOrchestrator`提供依赖任务键、不可变prepared patch、事务状态、稳定CommitPlan hash及Gather/Queue/Work/Wait/Merge/Validate/Commit计时；Runtime record使用`FCrowdStableEntityRef`与`FCrowdAgentFacts`。旧Round已消费Orchestrator且即时`Async/Future.Get`为0。

[COMPUTED][HIGH] 2026-07-23 P1关闭：canonical gather包含完整Combat业务事实；Business/Combat、SharedFlow、按Cohort拆分的Target四段、Obstacle、Movement、Particle与Facing均进入一次Dispatch/Wait的typed Worker DAG，唯一GT writer在完整CommitEnvelope验证后提交。源码无`Async/TFuture/Future.Get`；8132 T2、8137 T6、8138 T7、8139 T8双端门通过，fixed-step p95分别=`2.581/5.140/1.853/1.525ms`。

[COMPUTED][HIGH] `UMassCrowdRuntimeSubsystem`按World拥有Nav Graph与Flow cache；8122 `NavFlowProductSmall`真实验证98节点、234有向边、4层、2个有引用Flow资源、cache hit=1、9504字节，并与20实体P1 boundary同时运行。P2已关闭。

[COMPUTED][HIGH] `AMassCrowdReplicationActor`为每个PlayerController提供owner-only baseline/state/correction通道；客户端冲突、缺序列、损坏或超限后fail-closed，请求服务端销毁并重建channel以发布新baseline。`UMassCrowdPresentationSubsystem`按StableEntityRef管理Profile slot、swap-remove反向表、视觉状态和Cargo引用。

[COMPUTED][HIGH] J已删除完整MixedState multicast、私有visual maps与直接ISM调用；ContinuousLifecycle已删除可靠lifecycle multicast。两者均通过公共channel与Presentation运行，生命周期唯一同步写者固定到`TG_PostUpdateWork`，避免Mass processing期间调用同步Create/Destroy。

[COMPUTED][HIGH] J的O(N)`IsMoveSafe`已删除并改用公共`FCrowdSpatialSafetyIndex`；8153 step600双端保持active/visible=`20/20`、最小间距=`71.51cm`及既有业务指标。旧Round correction/bootstrap/ResultHeader/projectile已使用公共owner-only channel，实体视觉使用公共Presentation；P5已关闭。

[COMPUTED][HIGH] P4新增`FCrowdLogisticsTransactionStore`和专用`CrowdDemo_FriendlyLogisticsSmall`地图。8154延迟客户端从公共baseline/reliable channel恢复最终状态；20实体、40总量/5交付、竞争、幂等、死亡后cargo恢复、fallback sink、两次不可达退避及取消通过，双端hash=`3180435972084878253`。Cargo attach/detach=`2/2`，携货与交付近景证据已保存。

[COMPUTED][HIGH] P0–P5当时的累计门：Development/DebugGame Editor `-DisableUnity`通过，`MassCrowd`40/40、`CrowdDemo`115/115；8151 Round、8153 J、8154 P4、8156 NavFlow与8157双客户端late join均通过且零硬错误。该历史检查点当时停止在K前；当前B0–B7与K状态以本文1.1节、`EntityBehaviorSourceArchitecture.md`和`PhasePlan.md`为准。

[COMPUTED][HIGH] T7首次冷运行8777出现client frame p95=`112.235ms`、collapsed steps p95=`4`；新增证据后的两次普通运行8781/8783连续通过，frame p95=`6.016/5.820ms`，Round内shader/loading/PSO帧均为0。`-noshaderddc`控制运行8782因shader job超过60秒而未进入场景，只证明冷资源门可以阻塞ready，不能事后归因为8777的唯一根因。

[COMPUTED][HIGH] T5M 8785通过安全、同步、Transport与性能门：fixed-step p95=`6.196ms`，稳定诊断`valid=1`且无merge block/chatter；移动目标窗口`settled_windows=0`、相对位置peak-to-peak p95=`125.431cm`，所以它是稳定追随技术通过，不是静止落位结论。

[COMPUTED][HIGH] 8788把T6M诊断总窗口增加到60秒后，最终inside/coverage仍为`16/20`；因此“只因45秒不足”已被反驳。8789生命周期诊断记录1591次重建，所有634个具备新Plan继承资格的claim均完成迁移，`dropped_still_feasible=0`；主要变化来自真实环境图变化与path/execution invalid，不是claim迁移丢失。

[COMPUTED][HIGH] 8788/8789进一步确认`guidance_mode=3`为显式`EngagedHold`而非Terminal guidance意外清零。旧实现把Hold固定为世界坐标零速，强于“目标靠近时不主动后退”的设计。8790改为通用单向Hold：抑制目标向实体靠近的径向分量，保留切向运动及目标远离时的跟随分量。

[COMPUTED][HIGH] 8790原P0 T6M的Round末inside/coverage恢复为`20/20`，Hard/Swept/Obstacle/Bounds、invalid/fallback、双端hash、correction均通过；fixed-step p95=`12.137ms`，client Game/Render/GPU p95=`10.332/6.852/5.802ms`。最后90步诊断仍记录terminal population最低`18/20`、Region coverage最低`17/20`、particle settled window=0；这些值保留为移动目标过程诊断，不再作为AcquireThenHold实体的持续重排硬门。

[COMPUTED][HIGH] 用户确认的T6M终态合同为：实体取得正确Terminal并进入AcquireThenHold后，只要交互资格仍有效就不主动换Region；目标靠近时不主动后退，切向运动与目标远离跟随仍可执行。当前Demo的资格失效条件为Region变成Supply、策略不再是AcquireThenHold，或距离超过`Maximum + 100cm`释放滞回；目标Actor销毁/业务Target丢失尚未形成Demo运行场景，不能写成已验收。

## 6. 准确停止点

[COMPUTED][HIGH] 本节按实施时间保留历史切片；下文各切片中的“当前/下一步”只描述当时阶段门，不覆盖本文件1.3节和本节最后一条记录的现行P0/P1停止点。

[COMPUTED][HIGH] 可复用产品边界现由`Docs/MassCrowdSimulationPluginArchitecture.md`定义。阶段1插件骨架与阶段2纯Core迁移已完成；阶段3已把Shared Flow、Target Region、Guidance Compose、Local Predictive、Particle Safety、Facing以及最终Movement组装/稳定Merge/Commit切到Runtime WORK。各阶段Demo processor外壳、Round指标、诊断及rollback协调仍属于Demo宿主。

[COMPUTED][HIGH] 单boundary Gather前二十一切片已接入：`TryBeginFixedStep()`成功后由专用GT processor一次读取身份、FormationIndex、Formation Radius、RoundSim位置/速度/Yaw、Movement速度上限和Particle半径/间隔/Mobility，构建Runtime `FCrowdMassBoundarySnapshot`及最小Demo formation facts。Flow、Target与Business发布Guidance overlay；Runtime稳定合并后，Compose、Local Predictive、MovementPredict、Particle/Obstacle、Facing与MovementFinalize从snapshot/prepared链构建WORK输入，不再为这些基础事实重复读取Mass fragments。FacingFinalize的原子写回同时提交最终运动与当前boundary最新的Combat/Visual状态并捕获最终记录；PostFinalize与CheckpointPublisher只消费prepared事实。

[COMPUTED][HIGH] 该snapshot只保存boundary开始时的基础运动事实；Target Fact、Combat、Business override、Transport plan/guidance、Local Predictive与Particle等依赖前序阶段的派生事实仍按原processor顺序产生。correction恢复后重新执行Gather，因此snapshot不是rollback权威状态，也不进入rollback副本。

[COMPUTED][HIGH] RoundResultHeader运行时门已关闭；T6M按AcquireThenHold资格保持合同技术放行。最后90步严格Region窗口继续保留为观察指标，不再要求已接战实体追随移动目标持续重排。单进程DebugGame PIE、当前版人工审片、所有业务事实单次Mass读取/统一原子提交和按能力archetype拆分仍未执行。

[COMPUTED][HIGH] 第十三切片将Facing与MovementFinalize合并为Runtime `FCrowdMassFacingFinalizeWork`：一个不可变POD任务严格执行Facing Resolve→Finalize输入组装→CommitPlan构建和目标集合验证。Demo以单一`UCrowdDemoRoundFacingFinalizeProcessor`先验证完整AgentId/Lifecycle、Particle或SF1 Obstacle最终运动事实及Facing结果，再在一次Mass遍历中同步发布Runtime Facing、Runtime Movement和Demo RoundSim；旧Facing与MovementFinalize processor实现已物理删除。

[COMPUTED][HIGH] 第十三切片通过Development、DebugGame、`MassCrowd` 14/14与`CrowdDemo` 105/105。8727 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`、fixed-step p95=`3.847ms`；8728异构T6保持wall/corridor/completed/settled=`20/20/20/20`、inside/coverage=`20/20`、fixed-step p95=`6.121ms`。两次双端运行的Particle硬安全、invalid/fallback、correction误差和明确错误日志均为0。

[COMPUTED][HIGH] 第十四切片新增Runtime `FCrowdMassParticlePipelineWork`：同一个Particle WORK先执行Core Solve，再根据boundary snapshot与prepared MovementPredict生成完整、稳定排序的publish plan。publish plan同时覆盖Particle active实体、T1 inactive实体、invalid安全回退、外部Target粒子保留结果及供FacingFinalize消费的最终kinematics；Demo不再从ProposedMovement和FlowSample Mass fragments重新拼装这些事实。

[COMPUTED][HIGH] Particle processor当前只以Mass查询验证完整AgentId/Lifecycle集合并发布Runtime Particle fragment；路线与稳定性诊断改为消费publish plan、boundary snapshot、prepared prediction和prepared Shared Flow。旧的Mass二次读取/派生块已物理删除，最终kinematics直接由publish plan提供。Identity与最终RoundSim state仍是post-finalize对实际提交状态采样的必要事实，不复制第二份权威状态。

[COMPUTED][HIGH] 第十四切片通过Development、DebugGame、`MassCrowd` 15/15与`CrowdDemo` 105/105。8729 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`、fixed-step p95=`3.496ms`；8730异构T6保持wall/corridor/completed/settled=`20/20/20/20`、inside/coverage=`20/20`、fixed-step p95=`4.599ms`。两次运行均保持Particle硬安全与invalid/fallback为0、agents/visible=`20/20`、双端correction位置/速度/Yaw误差为0。

[COMPUTED][HIGH] 第十五切片物理删除`FCrowdMassParticleConstraintFragment`及其Trait、Demo模板和测试archetype注册。Particle processor现在没有Mass query：它只消费boundary snapshot、prepared prediction/Flow和`FCrowdMassParticlePipelineWork`的完整publish plan来生成Demo诊断、累计器、fixture与rollback事实，不再把同一Particle结果写入临时Mass镜像。

[COMPUTED][HIGH] FacingFinalize直接使用`PreparedRuntimeFinalKinematics`作为Particle/SF1 Obstacle最终运动事实，并在任何写回前把它与Runtime Commit plan及实际Mass身份/Lifecycle进行完整集合预验证。结构自动化禁止临时Particle fragment和Particle query回流；插件Core/Runtime仍不包含Demo路线、指标或fixture语义。

[COMPUTED][HIGH] 第十五切片通过`git diff --check`、Development、DebugGame、`MassCrowd` 15/15与`CrowdDemo` 105/105。8731 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`、fixed-step p95=`3.966ms`；8732异构T6保持wall/corridor/completed/settled=`20/20/20/20`、inside/coverage=`20/20`、fixed-step p95=`5.013ms`。两次运行均保持Particle硬安全与invalid/fallback为0、agents/visible=`20/20`、双端correction位置/速度/Yaw误差为0。

[COMPUTED][HIGH] 第十六切片增加`FCrowdDemoPreparedParticleDiagnosticCommit`。Particle processor只准备当前boundary的候选/应用Summary、route/stability样本、cross-profile计数、failure fixture和OpenSpawn输入，不再直接更新任何持久累计器。prepared commit包含step/revision/agent count原子门，且每个boundary最多发布一次。

[COMPUTED][HIGH] `PostFinalizeMetricsProcessor`只有在FacingFinalize已成功原子写入当前boundary后，才按原顺序一次性提交Particle诊断：stability、cross-profile、route及counterfactual、OpenSpawn、failure fixture、最终Particle summary。FacingFinalize失败时该boundary的候选指标不会冒充已提交状态；rollback snapshot仍在诊断提交之后记录，因此累计器和快照时序保持一致。

[COMPUTED][HIGH] 第十六切片通过`git diff --check`、Development、DebugGame、`MassCrowd` 15/15与`CrowdDemo` 105/105。8733 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`、fixed-step p95=`3.892ms`；8734异构T6保持wall/corridor/completed/settled=`20/20/20/20`、inside/coverage=`20/20`、fixed-step p95=`5.244ms`。两次运行均保持Particle candidate hash、能力、安全、同步和correction零误差门。

[COMPUTED][HIGH] 第十七切片新增按AgentId稳定排序的`FCrowdDemoPreparedPostFinalizeAgentRecord`。FacingFinalize仍先完成全量身份/Lifecycle/Commit目标预验证，再在唯一写回遍历中同步更新Runtime Facing、Runtime Movement与Demo RoundSim，并从实际写入后的RoundSim捕获AgentId、Lifecycle与最终状态记录。

[COMPUTED][HIGH] `PostFinalizeMetricsProcessor`现在无Mass query；它只消费上述最终记录、boundary snapshot及各阶段prepared事实，提交Particle诊断并生成路线、场景进度、rollback和Combat movement snapshot。最终验收样本因此来自实际原子写入值，而非重新计算的候选值；FacingFinalize失败时不会发布记录、推进诊断或允许Authority/Client Commit。

[COMPUTED][HIGH] 首次8735生产运行暴露了新prepared记录只在Plan激活时清空、没有按boundary清空的生命周期错误：step 0后所有后续发布被判为重复并触发VIOLATION。该问题已修正为`PublishBoundarySnapshot`每个fixed-step清空派生记录，并加入结构自动化锁定此合同。

[COMPUTED][HIGH] 修复后的Development、DebugGame、`MassCrowd` 15/15与`CrowdDemo` 105/105通过。8737 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`、fixed-step p95=`4.067ms`；8738异构T6保持wall/corridor/completed/settled=`20/20/20/20`、inside/coverage=`20/20`、fixed-step p95=`4.716ms`。两次运行realtime factor均为1.000、agents/visible=`20/20`，双端correction位置/速度/Yaw误差为0。

[COMPUTED][HIGH] 第十八切片将Engine Transform、Mass Velocity和Demo movement写入并入FacingFinalize已经存在的全量原子写回遍历。该遍历仍在任何写入前完整验证AgentId、Lifecycle、Facing、final kinematics和Runtime Commit plan；验证失败时五类最终状态均不写入。

[COMPUTED][HIGH] Authority/Client Commit processor现在无Mass query，不再重复读取Identity、RoundSim和Runtime movement，也不再执行第二次Transform/Velocity遍历；它们只验证当前boundary已有完整post-finalize records并记录角色相关Commit阶段。最终运动事实、Engine表现状态与checkpoint状态由同一写回遍历产生。

[COMPUTED][HIGH] 第十八切片通过`git diff --check`、Development、DebugGame、`MassCrowd` 15/15与`CrowdDemo` 105/105。8739 T2保持terminal=`20/20`、coverage=`16/16`、fixed-step p95=`3.638ms`；8740异构T6保持completed/settled/inside/coverage=`20/20`、fixed-step p95=`5.003ms`。两次运行realtime factor均为1.000、agents/visible=`20/20`，双端correction位置/速度/Yaw误差为0。

[INFERRED][HIGH] 运动终态的重复Mass遍历已关闭，但整个boundary仍包含Business、Target、T1、Visual/Combat和checkpoint等宿主查询；因此仍不能宣称全pipeline只有一次Mass读取。下一步应先审计CheckpointPublisher是否重复读取已存在的最终记录与prepared业务事实，再决定archetype拆分；当前不得进入100/500。

[COMPUTED][HIGH] 第十九切片新增按AgentId稳定排序的`PreparedCheckpointAgentStates`。VisualStateResolve在同一boundary完成Combat/Visual状态决议后，将最终RoundSim、Formation Radius和Combat NetState整批组装并与FacingFinalize捕获的最终记录、boundary formation facts逐项校验；数组每个boundary显式清空，禁止复用旧checkpoint事实。

[COMPUTED][HIGH] CheckpointPublisher已删除Identity、RoundSim、Formation、Stats、Business、Attack、Reactive、HitFlash和Visual九类fragment查询，只在需要构建correction或RoundResult时消费prepared checkpoint states。SoftPressure rollback ready门、checkpoint chunk与RoundResult构建顺序未改变；Publisher不再是额外Mass遍历。

[COMPUTED][HIGH] 第十九切片通过`git diff --check`、Development、DebugGame、`MassCrowd` 15/15与`CrowdDemo` 105/105。8741 T2保持terminal=`20/20`、coverage=`16/16`、fixed-step p95=`3.598ms`；8742异构T6保持completed/settled/inside/coverage=`20/20`、fixed-step p95=`4.780ms`。两次运行realtime factor均为1.000，agents/visible=`20/20`，客户端correction revision gap与位置/速度/Yaw误差均为0。

[INFERRED][HIGH] Checkpoint重复读取已关闭；当前剩余的明显宿主重复读取位于VisualStateResolve本身：它仍为最终速度、Formation Radius和身份读取RoundSim/Formation/Identity，同时FacingFinalize最终记录与boundary formation facts已经持有其中大部分事实。下一切片只应收缩VisualStateResolve输入，不得改变Combat写入时序、checkpoint内容或rollback双完成门。

[COMPUTED][HIGH] 第二十切片删除VisualStateResolve对Formation和RoundSim fragments的读取。该阶段按Identity.AgentId在FacingFinalize最终记录与boundary formation facts中执行稳定二分查找，使用prepared最终速度解析Visual状态，并从同一最终记录组装checkpoint与Particle applied hash；缺失Agent、Lifecycle错位或集合数量不一致会拒绝当前boundary。

[COMPUTED][HIGH] Identity读取仍被保留：Stats、Business、Attack、Reactive、HitFlash和Visual fragments本身不携带稳定AgentId，当前需要Identity把可变业务状态与prepared终态安全关联。直接删除Identity会依赖Mass chunk顺序，违反稳定映射合同。

[COMPUTED][HIGH] 第二十切片通过`git diff --check`、Development、DebugGame、`MassCrowd` 15/15与`CrowdDemo` 105/105。8743 T2保持terminal=`20/20`、coverage=`16/16`、fixed-step p95=`3.137ms`；8744异构T6保持completed/settled/inside/coverage=`20/20`、fixed-step p95=`4.023ms`。两次双端运行的correction revision gap及位置/速度/Yaw误差为0，realtime factor分别为1.000/1.001。

[COMPUTED][HIGH] 第二十一切片物理删除独立`VisualStateResolveProcessor`。`FacingFinalize`在原有全量身份/Lifecycle/Commit目标预验证通过后，于同一次Mass写回遍历中提交最终运动、Transform/Velocity以及宿主Combat/Visual状态；Visual决议使用最终自主运动速度，未读取Local Predictive或Particle修正向量作为朝向意图。该合并保留Identity映射，不依赖chunk顺序，也没有把Demo Combat字段加入MassCrowdCore公共POD。

[COMPUTED][HIGH] 同一次写回还生成稳定的post-finalize records和checkpoint states。`PostFinalize`先记录movement rollback与路线指标，再从prepared checkpoint/post-finalize/formation facts完成Combat rollback及Particle applied hash；movement/combat双完成门、correction boundary和checkpoint chunk内容保持原顺序。CheckpointPublisher继续无Mass query。

[COMPUTED][HIGH] 第二十一切片通过`git diff --check`、Development与DebugGame Editor（`-DisableUnity`）、`MassCrowd` 15/15和`CrowdDemo` 105/105。8745 T2为terminal/inside=`20/20`、coverage=`16/16`、fixed-step p95=`3.924ms`；8746异构T6为completed/settled/inside/coverage=`20/20`、fixed-step p95=`4.994ms`；8747 T7客户端实际覆盖idle/move/attack/hit/death五态；8748 T8 spawn/impact/damage=`50/50/50`、duplicate fire/hit=`0/0`且attack/projectile/event hash双端一致。四次运行agents/visible=`20/20`，correction位置/速度/Yaw误差为0，无明确硬错误。

[INFERRED][HIGH] 最终运动与Combat/Visual现在共享一次GT终态写回，但整个boundary仍不是严格“一次Mass读取、一次Mass写回”：BusinessPrepare、Ranged/Hit业务处理以及Movement WORK结果的中间Mass镜像仍有独立查询或发布。下一切片应先盘点这些剩余接缝并优先删除无消费者的中间镜像；当前不能直接进入archetype拆分或100/500。

[COMPUTED][HIGH] 第二十二切片确认Runtime GuidanceCandidates、ComposedGuidance与LocalVelocity三个Mass fragments只有写入者而没有fragment读取者；正式消费者已全部读取prepared SoA。三者及其Trait/模板注册、processor发布和无调用适配入口已经物理删除，Gather记录改用普通`FCrowdMassGuidanceCandidates` POD。`FCrowdDemoRoundProposedMovementFragment`仍被SF1 ObstacleConstraint消费，因此保留，不把不同语义的桥接一并删除。

[COMPUTED][HIGH] Runtime `Agent/SimulationState/Properties`不是中间镜像，而是插件Runtime持久合同。身份在authority spawn以及双端Plan激活时由稳定Demo identity初始化；状态在bootstrap/Plan激活时同步，能力属性在Plan应用完异构profile后同步；普通fixed step只由FacingFinalize更新Runtime state。8751首次回归证明旧MovementWork写入曾暗中承担客户端Runtime identity初始化，修复后8753/8754从Round 1到Round 2均保持correction位置、速度和Yaw误差为0。

[COMPUTED][HIGH] RangedCombat、HitResponse与ReactiveMotion当前不是无消费者镜像：Stats、Business、Attack、Reactive、HitFlash和Visual是T7/T8连续业务状态，三个阶段按严格顺序读写，FacingFinalize消费最终版本。现存债务是每个阶段内部均先gather再apply、跨阶段重复查询，而不是这些fragments本身应被删除。

[COMPUTED][HIGH] 第二十三切片将上述三个阶段替换为唯一`UCrowdDemoRoundCombatBoundaryProcessor`。它第一次Mass遍历只采集按AgentId稳定排序的宿主Combat事实，随后在POD数组上严格执行Attack/Projectile、Hit resolve和Reactive motion，第二次Mass遍历在完整AgentId集合校验后原子提交最终业务状态与ReactiveStep。T8从每boundary 5次Combat遍历降为2次，T7从3次降为2次；失败时不发布半套业务状态。

[COMPUTED][HIGH] 跨processor的`PendingProjectileHitFacts`、setter/consumer和旧三个processor实现已删除。Projectile权威数组、T7测试HitFacts以及Stats/Business/Attack/Reactive/HitFlash/Visual仍属于Demo宿主；MassCrowdCore、Runtime通用运动POD和最终FacingFinalize写回合同没有增加Combat字段。

[COMPUTED][HIGH] 第二十三切片通过Development与DebugGame Editor（`-DisableUnity`）、`MassCrowd` 15/15和`CrowdDemo` 105/105。8755 T7为agents/visible=`20/20`、fixed-step p95=`2.452ms`；8756 T8为spawn/impact/damage=`50/50/50`、duplicate fire/hit=`0/0`，attack/projectile/event hash双端一致，fixed-step p95=`2.247ms`，agents/visible=`20/20`。两次双端运行的Particle硬安全、invalid/fallback、correction位置/速度/Yaw误差及明确硬错误均为0。

[COMPUTED][HIGH] 非Combat路径回归同样通过：8757 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`、fixed-step p95=`4.263ms`；8758异构T6保持wall/corridor/completed/settled=`20/20/20/20`、inside/coverage=`20/20`、fixed-step p95=`5.230ms`。两次运行agents/visible=`20/20`，双端correction位置/速度/Yaw误差为0，无明确硬错误。

[INFERRED][HIGH] 这是第二十三切片的历史停止点：当时仍不能宣称整个fixed-step只有一次Mass读取，且100/500尚未授权；后续切片及S6已经完成接缝收敛和同路径100/500门，本句不得作为PJ0当前状态。

[COMPUTED][HIGH] 第二十四切片已形成[RoundSim Mass查询与数据所有权矩阵](MassQueryOwnershipMatrix.md)。矩阵区分了Round激活事务、canonical boundary gather、Runtime WORK、Demo Combat、SF1桥接、Networking和Presentation，不再用“遍历次数越少越好”替代真实数据所有权。

[COMPUTED][HIGH] `FCrowdDemoReactiveMotionStepFragment`只在同一boundary由Combat写、Movement读，现已替换为按AgentId排序的`FCrowdDemoPreparedReactiveMotionStep`并物理删除；`FCrowdDemoTargetCapabilityFragment`没有读取者，实际Capability由Particle properties、cohort snapshot和规则驱动，现已物理删除。SoftPressure MovementWork因此保持零Mass遍历，T7/T8垂直Reactive运动仍由prepared事实输入统一Movement WORK。

[COMPUTED][HIGH] `TargetRegionGuidance`不再重复查询Identity/Formation/RoundSim，而是直接消费当前canonical boundary snapshot，Mass遍历由1次降为0。`FlowPreferredVelocity`也用该snapshot验证Runtime结果AgentId集合，删除一遍只读身份扫描，只保留一次需要支持诊断/correction rollback的`RoundFlowSample`持久写回。

[COMPUTED][HIGH] 第二十四切片通过Development与DebugGame Editor（`-DisableUnity`）、MassCrowd 15/15和CrowdDemo 105/105。8759 T7 fixed-step p95=`2.058ms`；8760 T8 fixed-step p95=`1.782ms`且spawn/impact/damage=`50/50/50`、duplicate fire/hit=`0/0`；8761 T2 terminal=`20/20`、coverage=`16/16`、fixed-step p95=`4.378ms`；8762异构T6 completed/settled/inside/coverage=`20/20`、fixed-step p95=`6.221ms`。四次运行agents/visible=`20/20`，Particle硬安全、correction误差和明确硬错误均为0。

[INFERRED][HIGH] 这是第二十四切片的历史停止点：当时下一接缝是SF1 `RoundProposedMovement → RoundObstacleConstraint`，且100/500尚未获准；该接缝已由第二十五切片关闭，同路径100/500已由S6关闭。

[COMPUTED][HIGH] 第二十五切片已关闭该SF1中间桥。MovementWork对两个正式场景均只发布`PreparedRuntimePredictedMovements`；SF1 ObstacleConstraint按canonical boundary snapshot验证AgentId全集，调用既有`FCrowdDemoSharedFlowFieldKernel::ConstrainMovement`并发布`PreparedRuntimeFinalKinematics`。`FCrowdDemoRoundProposedMovementFragment`和`FCrowdDemoRoundObstacleConstraintFragment`连同模板注册、spawn初始化和query读写已物理删除。

[COMPUTED][HIGH] Development、DebugGame（均`-DisableUnity`）、MassCrowd 15/15与CrowdDemo 105/105通过。8763 SF1 Single保持Flow hash=`267519150`、rebuild=1、unreachable=0、goal/wall/corridor/turn=`1/1/1/1`、双端obstacle penetration=0、checkpoint位置误差p95=0；但其correction interval位置误差p95=`26.745cm`，因此不满足`<1cm`严格同步门。8765 T2为terminal=`20/20`、coverage=`16/16`、fixed-step p95=`4.227ms`；8766异构T6为completed/settled/inside/coverage=`20/20`、fixed-step p95=`5.826ms`，两者双端hash与correction均通过。

[COMPUTED][HIGH] 8764 SF1 Cohort 500未进入Round：server在初始复制阶段触发`Ensure !IsBunchTooLarge`，客户端未完成readiness。该结果不证明prepared障碍链在500实体上失败，也不允许宣称500回归通过；当前唯一准确结论是网络启动/复制门存在独立阻塞。

[COMPUTED][HIGH] 第二十六切片把上一boundary的`FCrowdMassFacingFragment::ConsecutiveFinalSettleSteps`纳入唯一`BoundaryGather`，形成按AgentId排序的`FCrowdDemoRoundBoundaryFacingFact`。Facing WORK继续从该prepared事实计算下一状态，FacingFinalize不再为历史settle状态单独读取Mass，因此其遍历由3次降为2次。

[COMPUTED][HIGH] 剩余两次遍历不是中间镜像：第一次只验证实际Mass中的AgentId/Lifecycle、Runtime/Demo身份与完整CommitPlan一一对应；第二次才原子写回Facing、Runtime Movement、Demo RoundSim、Transform/Velocity和宿主Combat/Visual。当前保留二者，避免在遍历中途发现集合错误后留下部分更新。

[COMPUTED][HIGH] 8767曾得到SF1 correction interval位置误差p95=`26.745cm`，而checkpoint误差为0。该问题已在第二十七切片修复：SF1与SoftPressure现在共用128-boundary fixed-step correction历史、AgentId/Lifecycle校验、零误差快速路径及必要时的确定性恢复/重放。8770原参数复测把interval p95降至`0.064cm`、checkpoint/sim p95降至`0.008cm`，且rollback hit/miss/mismatch=`36/0/0`。

[COMPUTED][HIGH] 8764的500实体启动bunch已定位到直接复制的`FCrowdDemoRoundBootstrapPacket`，其`Agents`包含全部完整AgentState且未分块；500状态在Round前作为单一replicated property发送并触发`Ensure !IsBunchTooLarge`。现有correction/checkpoint的100-agent chunk机制尚未用于bootstrap。

[COMPUTED][HIGH] 第二十七切片已移除correction历史的SoftPressure场景门。两个正式Flow场景现在都在PostFinalize成功后记录同一128-boundary历史，等待Combat/Visual最终事实完成后才标记replay-ready；correction按frame fixed-step读取历史状态执行AgentId/Lifecycle/Radius完整校验、真实误差比较、零误差快速路径或恢复后重放。新Round会统一清理history与hit/miss/mismatch/replayed计数。

[COMPUTED][HIGH] 8770 SF1 Single的Round 1 correction snapshot hit/miss/mismatch=`36/0/0`，correction interval位置误差p95由`26.745cm`降至`0.064cm`，checkpoint p95=`0.008cm`；Flow hash、路线和障碍安全保持不变。8771 T2与8772异构T6分别保持原能力与rollback=`54/0/0`、`80/0/0`。

[COMPUTED][HIGH] 阶段 D 已完成Demo Bootstrap生产适配：完整Agents复制属性与旧OnRep路径已删除；8773客户端实际组装revision 1的20 agents、1 chunk、3720 bytes并进入现有Pipeline。Development/DebugGame `-DisableUnity`、定向3/3、MassCrowd 20/20、CrowdDemo 109/109通过，双端fixed-step p95=`3.885ms`、realtime=`1.001`、client frame p95=`3.136ms`且无bunch-too-large、Ensure或VIOLATION。[INFERRED][HIGH] 当前进入E，只实现通用Spawn/Despawn/Membership batches，不提前创建真实Mass生命周期场景。

[COMPUTED][HIGH] 阶段 E 已完成trivially-copyable lifecycle/membership batch合同、四类Despawn原因、严格sequence/revision/fixed-step、稳定hash、重复幂等、缺序列与stale拒绝、despawn后高LifecycleSerial槽位复用及路径无关membership hash。Development/DebugGame `-DisableUnity`、定向3/3、MassCrowd 23/23与CrowdDemo 109/109通过；Networking新增Source未检出Demo/Scenario/Combat反向依赖。[INFERRED][HIGH] 当前进入F，在插件最小Mass World做真实entity boundary apply；E本身不是生命周期运行证据。

[COMPUTED][HIGH] 阶段 F 已把真实Mass entity mutation放入`MassCrowdRuntime`的通用LifecycleStore，Networking adapter仅在协议状态副本通过后调用Runtime并提交。最小World真实验证snapshot create、不同fixed-step spawn、destroy后handle失效、Mass handle serial变化、StableEntityRef高serial槽位复用、membership原子迁移、stale correction/despawn拒绝和完整entity-set hash；Development/DebugGame、定向1/1、MassCrowd 24/24及CrowdDemo 109/109通过。[INFERRED][HIGH] 当前进入G，将该生产路径接入独立Demo continuous lifecycle场景，不使用T1 active标志冒充销毁。

[COMPUTED][HIGH] 阶段 G 已新增独立`ACrowdDemoContinuousLifecycleCoordinator`与CLI入口；该入口在GameMode固定agent spawn之前分支，Round pipeline只空载运行且不拥有生命周期实体。Server以30Hz fixed-step和15-step操作间隔驱动E batches/F Runtime store，population硬上限20，交替执行Membership、Death/BusinessRecycle Despawn与同槽位高LifecycleSerial Respawn；Client用可靠operation wrapper应用同一batch并按StableEntityRef增量维护普通/HitFlash ISM。8777序列18双端entity-set hash=`14341810777549134372`一致，client active/visible检查点一致、max population=20、stale reject=0且无VIOLATION；Development/DebugGame、定向1/1、MassCrowd 24/24及CrowdDemo 110/110通过。[INFERRED][HIGH] 当前进入H，G没有提前实现统一Behavior、Cargo或混合Sandbox。

[COMPUTED][HIGH] 阶段 H 新增`MassCrowdRuntimeBehavior`公共合同：transition先验证AgentFacts/Capability/provider，再输出显式Target、Objective、MovementProfile、InteractionIntent和可选BusinessCommitRequest，Commit只更新通用AgentFacts；Runtime不引用Demo。Demo的`CrowdDemoBehaviorAdapters`把基础行为、Cargo pickup/deliver和Attack路由到同一接口，业务ledger以CommitId幂等，且`FCrowdDemoHitFact::HitEventId`作为外部commit id接入既有damage kernel。定向2/2、MassCrowd 25/25、CrowdDemo 111/111与Development/DebugGame通过。[INFERRED][HIGH] 该阶段没有把H接口与G continuous lifecycle组合；组合运行属于J。

[COMPUTED][HIGH] 阶段 I 新增`CrowdNavSurfaceGraph`与`MassCrowdNavSurfaceGraphExtractor`：Core以量化几何生成稳定节点/拓扑hash，layer-specific与closest-polygon attachment避免大多边形质心误判，Shared Flow以预构建反向邻接执行稳定Dijkstra；Runtime提取静态Recast tile/poly/portal并拒绝缺失、过窄、过陡或跨越落差的连接。8800真实地图运行通过98 nodes、234 directed edges、38 tiles、4 extracted/graph layers、13 overlap、76 reachable sloped edges、8/8 reachable markers、drop unreachable，topology hash=`9799951363989120452`；Development/DebugGame、定向3/3、MassCrowd 27/27与CrowdDemo 112/112通过。[INFERRED][HIGH] I本身没有把continuous lifecycle、Behavior、Combat或Logistics组合进probe；该组合由J完成。

[COMPUTED][HIGH] 阶段 J 新增`ACrowdDemoMixedSandboxCoordinator`：GameMode在固定Round spawn前分支，20个真实Mass实体按30Hz boundary运行；行为由距离、Cargo carrier、Health与当前目标事实驱动，在HaulPickup/Deliver、Pursue/Attack、Guard/Flee及Wander/MoveTo间切换。所有移动目标attachment消费I的Recast图与Shared Flow，业务提交消费H provider/ledger并对同一CommitId即时重放验证幂等，死亡/业务回收与行为cohort变化消费E/F lifecycle batches。客户端用bounded状态包校正完整AgentFacts并增量维护普通/HitFlash ISM。8804 step600的Server/Client entity hash=`13154923896226232907`、membership hash=`13094526216572312548`一致；active/visible=`20/20`，最小同层间距=`71.51cm`且stale reject=0。[INFERRED][HIGH] 这是P0前的历史检查点；当前状态以本文1.1节、`EntityBehaviorSourceArchitecture.md`和`PhasePlan.md`为准。

## 2026-07-28 产品路径复核增量

[COMPUTED][HIGH] `AMassCrowdReplicationActor`现在支持有界reliable batch；J按帧批量发布状态与correction，ACK后的缓存追赶也按上限分批，未提高原队列或网络预算。`DrainApplyFrames()`返回空仅表示当前无帧，只有客户端状态进入`RequiresResync()`才算stale。
[COMPUTED][HIGH] P4 Coordinator驱动Planner、故障策略、指标和截图，但实体位置由公共`FCrowdMassBoundaryRunner`与Nav Runtime提交；Cargo实例仅由复制后的ownership驱动Presentation attach/detach。7953双端状态hash一致，实例数与相关实体数均为20。
[COMPUTED][HIGH] J 7939、Continuous 7946及Round 7948–7951复测均无Fatal、Assertion、Ensure、`LogWindows: Error`、VIOLATION或resync。J客户端隐藏窗口记录的Actor Tick p95=`400ms`，不能解释为渲染帧p95；服务端fixed-step p95=`1.972ms`。
[COMPUTED][HIGH] 当前累计自动化为MassCrowd 43/43、CrowdDemo 115/115，Development/DebugGame `-DisableUnity`通过，插件Source到Demo的反向依赖为0。现行停止点仍为K前。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
