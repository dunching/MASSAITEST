# MassCrowdSimulation 插件产品边界

## 1. 文档职责

[COMPUTED][HIGH] 本文件是可复用 MassCrowd 插件的模块边界与依赖方向事实源。生产运行、行为组合和复制合同查阅`MassCrowdUnifiedRuntimeAndReplicationContract.md`，Demo目的查阅`DemoPurposeAndTargetEffect.md`，当前代码事实查阅`CurrentArchitecture.md`，阶段执行查阅`PhasePlan.md`。

## 2. 产品边界

```text
MassCrowdCore
      ↓
MassCrowdRuntime
   ↙       ↘
Networking  Presentation
      \     /
    MassCrowdTests

MassAICrowdDemo → 插件公共接口与可选模块
```

[COMPUTED][HIGH] `MassCrowdCore`只依赖 UE `Core`模块，保存稳定纯数据合同、确定性算法、排序、量化、hash及通用 Movement WORK 输入输出。Core禁止引用 Mass EntityManager、Actor、World、复制、渲染、Round、Scenario 或 Demo 语义。

[COMPUTED][HIGH] `MassCrowdRuntime`依赖 Core 和 MassEntity；当前承载通用运动链、AgentFacts fragments映射及真实Mass LifecycleStore。Store支持snapshot create、spawn、destroy、槽位复用、membership原子更新、correction与完整集合hash；持续调度和Behavior注册/切换服务尚未实现。Runtime不依赖Networking或Presentation。

[COMPUTED][HIGH] `MassCrowdNetworking`当前已实现不含Demo语义的Relevant Snapshot与Spawn/Despawn/Membership batch primitives；Demo已通过宿主adapter实际消费Snapshot。Delta具备严格序列、bounds、原子状态转换和membership hash，但仍无宿主无关RPC包装、真实Mass apply、Correction/Event、late join或Relevancy调度。

[COMPUTED][HIGH] `MassCrowdPresentation`当前只有模块壳，没有 ISM/VAT、视觉插值、Cargo 视觉、correction offset 或实例生命周期实现。其产品职责是承载这些通用表现能力；Demo 客户端视觉代码不是该模块已经完成的证据。

[COMPUTED][HIGH] `MassCrowdTests`是DeveloperTool模块，承载纯fixture、乱序/hash、GT/WORK等价、rollback、最小Mass World和可选模块编译测试，不进入Shipping运行依赖。

## 3. 当前公共接口

[COMPUTED][HIGH] 阶段1已建立以下无`CrowdDemo`命名的最小合同：

- `FCrowdAgentInput`
- `FCrowdEnvironmentObstacle`
- `FCrowdEnvironmentSnapshot`
- `FCrowdTargetInput`
- `FCrowdGuidanceCandidate`
- `FCrowdComposedGuidance`
- `FCrowdSimulationProfile`
- `FCrowdMovementOutput`
- `FCrowdRoundWorkInput`
- `FCrowdRoundWorkOutput`
- `ICrowdEnvironmentProvider`
- `ICrowdBusinessGuidanceProvider`
- `FCrowdSharedFlowObstacleSpec`
- `FCrowdSharedFlowFieldConfig`
- `FCrowdSharedFlowField`
- `FCrowdSharedFlowFieldKernel`
- `FCrowdTargetRegionTransportSettings`
- `FCrowdTargetPolarTopology`
- `FCrowdTargetRegionDemandResult`
- `FCrowdTargetRegionFlowPlan`
- `FCrowdTargetRegionQuotaExecutionState`
- `FCrowdTargetRegionTransportKernel`
- `FCrowdGuidanceComposeKernel`
- `FCrowdVelocityHalfPlaneKernel`
- `FCrowdLocalPredictiveSettings`
- `FCrowdLocalPredictiveAgent`
- `FCrowdLocalPredictiveResult`
- `FCrowdLocalPredictiveInteractionKernel`
- `FCrowdParticleConstraintAgent`
- `FCrowdParticleConstraintEnvironment`
- `FCrowdParticleConstraintSettings`
- `FCrowdParticleConstraintResult`
- `FCrowdParticleConstraintSummary`
- `FCrowdParticleConstraintKernel`
- `FCrowdFacingSettings`
- `FCrowdFacingInput`
- `FCrowdFacingResult`
- `FCrowdFacingSummary`
- `FCrowdFacingKernel`

[INFERRED][HIGH] 这些合同在迁移纯算法前只固定所有权和依赖方向，不宣称字段已经覆盖100/500、战斗或生产项目全部需求。新增字段必须来源于实际迁移用例，不得预埋Demo场景枚举。

## 4. Demo宿主边界

[COMPUTED][HIGH] T1–T8、地图、GameMode、Round测试调度、端口脚本、30秒/宽限验收、测试Lighting、录像和人工审片设施继续属于`MassAICrowdDemo`。

[INFERRED][HIGH] Demo最终只负责把环境、目标、Capability和Business Guidance转换为插件公共输入，并消费Movement/Networking/Presentation输出；不得保留插件算法副本。

[INFERRED][HIGH] Demo 必须直接测试同一套生产 Runtime、Networking 与 Presentation。固定 Round、readiness、全量 hash、fixture、故障注入、VIOLATION 和人工审片属于宿主观察/控制层，不得成为生产 Agent 的运行依赖。

[INFERRED][HIGH] `RoundPlan`、`RoundResult`、测试端口、Scenario 枚举与当前 `RoundBootstrapPacket` 不进入插件公共产品 API；当前 RoundBootstrap 只能在通用 Relevant Snapshot primitive 建立后成为适配器。

[COMPUTED][HIGH] 插件Source禁止引用`CrowdDemo`、`SimRound`、`/Game/Maps/`和端口命令行。Core额外禁止引用MassEntity、World、Actor、MassReplication或Runtime。`MassCrowd.Plugin.Boundary`自动化对这些规则执行源码扫描。

## 5. 模块依赖与可选性

[COMPUTED][HIGH] Core、Runtime、Networking与Presentation均在`Default`阶段加载；依赖方向为Core → Runtime，Networking与Presentation依赖Runtime且互不反向引用。Demo显式声明Networking/Presentation依赖，插件Source不引用Demo。

[INFERRED][HIGH] 当前只验证了依赖DAG、模块编译和边界扫描；“删除Demo后Core/Runtime/Tests独立编译”与“关闭Networking/Presentation的最小宿主运行”仍需后续专用Target或空白宿主验证，不能由LoadingPhase设置直接推断。

## 6. 当前产品化闭环顺序

[COMPUTED][HIGH] 历史 Core/Runtime 运动迁移及A–J能力证据保留在下文；K/L继续冻结。当前实施顺序只以`PhasePlan.md`的P0–P5为准。

[COMPUTED][HIGH] 合同、通用POD、Relevant Snapshot、Demo生产适配、lifecycle batches、最小Mass World真实生命周期、Demo continuous lifecycle、统一Behavior接口、NavMesh Surface Graph/Shared Flow及20实体混合Sandbox已按A–J完成；这些是能力与验收证据，不是公共Runtime/Networking/Presentation闭环证据。

[COMPUTED][HIGH] P0当时确认的J私有Graph/Flow cache、全量状态Multicast、直接ISM实例及O(N)安全检查现已删除。Networking/Presentation已改为`Default`并实现late join、空间相关集和公共实例生命周期。

[COMPUTED][HIGH] 2026-07-23续跑后，Runtime新增公共`FCrowdLogisticsTransactionStore`与`FCrowdSpatialSafetyIndex`。前者实现版本化Claim/Pickup/Deliver/Cancel/Requeue、两阶段patch验证、幂等CommitId、in-transit cargo恢复及fallback sink；后者使用稳定StableEntityRef索引、空间邻格查询与移动后增量更新。两者均不依赖Demo。

[COMPUTED][HIGH] `NavFlowProductSmall`已证明Runtime subsystem的静态Recast Graph、Flow handle/refcount/LRU可与20实体P1 boundary同场运行；专用`FriendlyLogisticsSmall`已通过公共Networking/Presentation恢复业务与Cargo视觉。J已删除O(N)`IsMoveSafe`并消费公共空间索引；旧Round bootstrap/correction/ResultHeader/projectile与实体视觉也已迁移到公共channel/subsystem。

[INFERRED][HIGH] P1–P5依次收敛Runtime boundary、Nav资源、Networking/Presentation、FriendlyLogisticsSmall和J/旧入口；完成P5后停止，不进入100/500或原工程迁移。

[COMPUTED][HIGH] 当前P0–P5公共产品闭环均已关闭；下一停止点固定在K前，不运行正式100/500或原工程迁移。

### 2026-07-28 产品路径复核

[COMPUTED][HIGH] Networking新增有界reliable batch API和ACK缓存分批追赶；J不再逐记录发送20次可靠RPC，且没有提高既有队列上限。Presentation继续在Runtime/Lifecycle Apply之后按StableEntityRef提交。
[COMPUTED][HIGH] 7939 J、7946 Continuous、7953 FriendlyLogistics及7948–7951 Round回归均通过硬错误扫描；插件Source到Demo的反向依赖为0。
[COMPUTED][HIGH] 最新累计门为Development/DebugGame `-DisableUnity`、MassCrowd 43/43与CrowdDemo 115/115。停止点保持在K前。

## 6.1 P0冻结的模块责任

| 模块 | P0冻结责任 |
|---|---|
| `MassCrowdCore` | [INFERRED][HIGH] 保持World/Mass/网络/渲染无关的POD与确定性kernel；不接收Combat、Warehouse、Round或Demo字段。 |
| `MassCrowdRuntime` | [INFERRED][HIGH] P1承载Boundary Orchestrator、不可变Snapshot/overlay、WORK依赖图、Stable CommitPlan和唯一GT writer；P2承载静态Recast Graph与Flow资源生命周期。 |
| `MassCrowdNetworking` | [INFERRED][HIGH] P3承载per-client baseline/delta/correction/event、relevancy、sequence、resync与owner-only channel；不引用Demo。 |
| `MassCrowdPresentation` | [INFERRED][HIGH] P3承载StableEntityRef实例槽位、插值、Profile、视觉状态和Cargo attachment；不反向决定Server生命周期或业务commit。 |
| Demo/宿主Business | [INFERRED][HIGH] 提供Scenario、环境/业务overlays、库存与Planner策略、资产映射、故障注入和验收；不得复制插件运动、网络状态机或实例生命周期。 |

## 7. 阶段2状态

[COMPUTED][HIGH] `Plugins/MassCrowdSimulation`、五个模块、公共合同和边界自动化已经创建；Development Editor无UBT依赖警告编译通过，`MassCrowd.Plugin.Boundary` 1/1通过。

[COMPUTED][HIGH] Shared Flow已经成为首个提取到`MassCrowdCore`的正式纯算法。Core公开无Demo命名的Flow config、field、V1/V2 topology、动态goal anchor integration、sampling与movement constraint；Core只依赖UE `Core`。

[COMPUTED][HIGH] Target Region Transport已作为第二个纯算法提取到Core，包含Target-relative polar topology、Demand、min-cost Transport plan、validation、quota execution、Guidance、EngagedHold与claim-preserving replacement；Core版本直接依赖Core Shared Flow，不依赖Demo Capability kernel。

[COMPUTED][HIGH] Guidance Compose已作为第三个纯算法提取到Core，原样保持`BusinessOverride > TargetRegion > SharedFlow > Stop`优先级、候选稳定排序、1cm位置/速度量化、0.01度Yaw量化及FNV-1a hash合同。

[COMPUTED][HIGH] Local Predictive已作为第四个纯算法提取到Core，其无场景语义的Velocity Half-Plane数值依赖同时迁移。Core保留稳定pair/component、有限期grant、环境约束、CoherentTranslation、JointPreferredRecovery、量化复验与fixture hash合同。

[COMPUTED][HIGH] Particle Safety已作为第五个纯算法提取到Core，包含Pair/Environment Soft、统一Hard/Swept/Obstacle/Bounds闭环、环境Contact、量化复验、失败fixture、applied几何复验与settling tracker。Demo的Combat RoundSim状态hash仍留在Demo适配层，没有进入Core。

[COMPUTED][HIGH] Facing已作为阶段2最后一个纯算法提取到Core，保持“移动阶段服从自主Guidance、最终落位后才面向Target”的职责边界，并包含固定步转速限制、角度量化、稳定排序和hash。连续落位资格仍由Runtime输入提供，客户端视觉插值仍属于Presentation，二者没有被塞入Core。

[COMPUTED][HIGH] 阶段2结束时Demo模块仅以等价自动化依赖`MassCrowdCore`。进入阶段3后，Shared Flow、Target Region、Guidance Compose、Local Predictive、Particle Safety和Facing已经依次成为由Runtime WORK调用Core的正式生产段；原Round pipeline、指标、诊断和场景配置仍保留在Demo宿主。

[COMPUTED][HIGH] Development与DebugGame Editor、102/102项`CrowdDemo`自动化和12/12项`MassCrowd`插件自动化通过；SF1 build hash保持既有验证值`267519150`，Shared Flow、Transport、Guidance Compose、Local Predictive、Particle与Facing旧/Core/Runtime等价测试通过。

[COMPUTED][HIGH] 阶段2纯算法清单已经完成；阶段3采用逐processor切换，不能因Guidance Compose已接入Runtime就宣称整个Mass Runtime迁移完成。

[INFERRED][HIGH] 下一步进入阶段3，先固定Runtime公共数据所有权和`GT Gather → Core WORK → Stable Merge → GT Commit`接缝，再迁移Mass fragments、traits、Pipeline与processors。

## 8. 阶段3状态

[COMPUTED][HIGH] `MassCrowdRuntime`已建立`UMassCrowdMovementTrait`以及 Agent identity、simulation state、properties、facing 与 movement output 持久 fragments；guidance candidates、composed guidance、local velocity 与 particle constraint 等中间 fragments 已在后续收敛中删除并改用 prepared POD。Combat、Projectile、Demo Round 与 Presentation 状态没有进入 Base Movement archetype。

[COMPUTED][HIGH] Runtime Bridge把Mass事实按CapabilityProfileKey稳定分成`FCrowdRoundWorkInput`，把多个WORK输出稳定合并为全局AgentId顺序的Commit计划，并要求完整AgentId/Lifecycle集合预验证通过后才允许写回。Gather和Commit hash覆盖环境、Target、Profile、Agent及完整Movement事实。

[COMPUTED][HIGH] 插件原生Gather/Merge/Commit测试和带真实`FMassEntityManager`的最小Mass World测试2/2通过；Demo适配器旧/Core候选、状态、Composed Guidance与Commit等价测试1/1通过。Development、DebugGame、102/102项`CrowdDemo`及11/11项`MassCrowd`自动化通过。

[COMPUTED][HIGH] Demo生产模板持有plugin fragments作为中间运动权威。每个fixed-step先从当前Demo身份、RoundSim状态、Movement属性和Particle属性构建稳定Runtime基础snapshot；Flow、Target Region和Business随后发布三类不可变Guidance overlay。Runtime Bridge稳定合并snapshot与overlay并拒绝缺失Shared Flow、重复provider/Agent及revision错误。六个曾与Runtime逐阶段重复的Demo运动fragment已删除；overlay与基础snapshot仍属于可重建派生状态，不进入权威rollback snapshot。

[COMPUTED][HIGH] 正式`Guidance Compose`已切换到`FCrowdMassGuidanceWork`：WORK线程只消费稳定POD并调用Core Compose，GT在完整AgentId/result集合验证通过后一次写入Runtime composed与同源prepared SoA，不再写Demo composed或MoveIntent。旧`FCrowdDemoRoundWorkKernel::ComposeGuidance`已无生产调用者，仅保留迁移等价测试。

[COMPUTED][HIGH] 正式`Local Predictive`已切换到`FCrowdMassLocalPredictiveWork`：它直接消费同一boundary snapshot与prepared Core composed结果，调用Core局部预测kernel，并在完整AgentId/result集合校验后一次更新Runtime local-velocity与prepared结果。旧Demo local-velocity fragment已删除；旧Demo kernel无生产调用者，只保留旧/Core等价及历史fixture测试。

[COMPUTED][HIGH] 正式`MovementPredict`已切换到`FCrowdMassMovementPredictWork`：它消费boundary snapshot、prepared composed与local-velocity结果，稳定处理自主/局部速度、T1 boundary freeze、业务垂直运动和Particle active事实，并发布prepared预测位置。

[COMPUTED][HIGH] 正式`Particle Safety`已切换到`FCrowdMassParticleWork`：它消费boundary snapshot与prepared MovementPredict结果，在同一WORK内执行Core Solve及applied-state安全复验，并输出candidate/applied summary与hash。GT完整校验结果集合后发布Runtime particle与prepared结果，旧Demo particle兼容fragment已删除。

[COMPUTED][HIGH] 正式`Facing`已切换到`FCrowdMassFacingWork`：Runtime WORK消费boundary snapshot、prepared composed与particle结果并调用Core Facing。连续settled计数由Runtime Facing持有并进入精确rollback fact；结果集合完整后只发布Runtime facing与prepared事实，旧Demo facing fragment已删除。

[COMPUTED][HIGH] 正式`Shared Flow`已切换到`FCrowdMassSharedFlowWork`：Runtime resource持有Core field、动态anchor与T3双cohort field，构建和每Agent Preferred均在线程池执行。Demo field/sample只由Core结果转换得到，服务尚未迁移的Transport、指标和rollback消费者；生产Build/Preferred不再调用Demo Shared Flow kernel。

[COMPUTED][HIGH] `FCrowdMassMovementFinalizeWork`现从最终Particle/Obstacle位置、速度与Facing结果生成按Capability分批的Movement outputs，调用Bridge稳定Merge为全局Commit plan。GT先验证完整AgentId/Lifecycle集合，再同步发布Runtime simulation/movement与Demo RoundSim兼容镜像；Authority/Client Commit均只从Runtime MovementOutput写Engine Transform/Velocity。

[COMPUTED][HIGH] Runtime properties和公共Agent POD显式包含per-agent HardSafetyGap、SoftMargin与Mobility，Gather验证与hash覆盖全部字段。Demo `MovementFinalize`仍是普通fixed-step中RoundSim兼容镜像的唯一writer，但最终Movement不再由Demo独立计算，因此不存在Demo/plugin双Movement事实。

[COMPUTED][HIGH] 该接管通过Development、DebugGame、Runtime定向2/2、`MassCrowd` 11/11与`CrowdDemo` 102/102。8663 T2双端回归为20/20 terminal、16/16 Region、安全/同步全通过，Commit p95=`0.021ms`；8664 SF1 Single authority短运行无VIOLATION。

[COMPUTED][HIGH] Shared Flow接管通过Development、DebugGame、Runtime定向2/2、Shared Flow等价1/1、`MassCrowd` 11/11和`CrowdDemo` 102/102。8667 T2为20/20 terminal、16/16 Region，fixed-step/Flow p95=`3.166/0.264ms`；8668 SF1确认golden hash=`267519150`。

[COMPUTED][HIGH] Target Region接管新增`FCrowdMassTargetRegionWork`，将Topology、Demand、Plan replacement/validation、quota/claim execution与Guidance作为四个Runtime WORK合同。Demo processors只Gather和发布兼容镜像，生产路径不再调用Demo Target Region kernel。Development、DebugGame、Runtime定向1/1、旧/Core/Runtime等价1/1、`MassCrowd` 12/12与`CrowdDemo` 102/102通过；8669 T2和8671异构T6 Static均保持20实体能力、安全、双端hash与性能门。

[COMPUTED][HIGH] 阶段4第一切片已建立Runtime `FCrowdMassBoundarySnapshot`：GT在fixed-step开始时对基础身份、simulation state和properties执行一次稳定Gather；Shared Flow与Target Demand复用同一快照，Flow结果以prepared SoA传递。snapshot是可重建boundary输入，不是rollback权威资源。

[COMPUTED][HIGH] 阶段4第二切片已把Flow/Target/Business Guidance变成prepared overlay；Compose通过Runtime Bridge从snapshot+overlay生成完整记录，Local Predictive从snapshot+prepared composed生成WORK输入。插件测试覆盖输入反序、overlay稳定hash、缺失Shared Flow和重复provider/Agent拒绝。8677/8678生产回归保持T2/T6能力、安全、双端hash与性能门。

[COMPUTED][HIGH] 阶段4第三切片已把MovementPredict、Particle与Facing接到同一snapshot/prepared链；插件测试覆盖预测积分、限速、freeze、垂直override、乱序和重复Agent拒绝。8681/8682生产回归保持T2/T6能力、安全、双端hash与性能门，8683 SF1保持golden Flow hash。

[COMPUTED][HIGH] 阶段4第四切片新增`FCrowdMassFinalKinematicState`和`BuildInputFromPrepared`：Particle/SF1 Obstacle与Facing发布prepared最终事实，MovementFinalize直接与boundary snapshot组装Commit输入，删除一次全实体Gather；写前完整身份和Runtime/Demo镜像原子门保持。8684/8685生产回归保持T2/T6能力、安全、同步与性能门，8686 SF1保持golden Flow hash。

[COMPUTED][HIGH] 阶段4第五切片将MovementFinalize的Mass访问拆为`ValidationQuery`与`ApplyMetricsQuery`：写前原子门只消费必要身份和双侧镜像，提交后阶段不再读取MoveIntent、Runtime properties、Runtime Particle/Facing。该拆分没有增加processor、没有改变WORK结果或Commit顺序；8687/8688保持T2/T6能力、安全、同步与性能门，8689 SF1保持golden Flow hash。

[COMPUTED][HIGH] 阶段4第六切片进一步将`ApplyMetricsQuery`拆为Finalize最小写入查询与Demo `PostFinalizeMetricsProcessor`。Runtime Commit plan原子应用不再与Flow路线、场景进度、rollback或Combat snapshot采集混写；每boundary Finalize成功标记同时门控post-finalize和Authority/Client Commit。该切片保留旧VisualResolve前采样时序，但暂时增加一次Demo只读查询。8693/8694保持T2/T6能力、安全、同步与性能门，8695 SF1保持golden Flow hash。

[COMPUTED][HIGH] 阶段4第七切片删除post-finalize对Formation、Composed Guidance、Particle Properties和未使用Particle Constraint fragment的读取；boundary formation facts显式保存FormationIndex与checkpoint Radius，prepared Runtime Composed结果转换回Demo rollback合同。8697证明Particle PhysicalRadius不能替代Formation/checkpoint Radius；修正后8698异构T6、8699 T2及8702 SF1均通过安全、同步、correction与golden hash回归。

[COMPUTED][HIGH] 阶段4第八切片删除post-finalize对FlowSample和ObstacleConstraint fragment的读取：rollback FlowSample由prepared Runtime输出重建，SF1 penetration由snapshot起点和Finalize终点按同一障碍合同复验。8703异构T6与8704 SF1通过生产回归。

[COMPUTED][HIGH] 阶段4第九切片删除post-finalize对GuidanceCandidates和Facing fragment的读取：Runtime Bridge从boundary snapshot与三类prepared overlay重建完整Guidance rollback记录；Facing阶段在结果发布时同时生成包含连续settle计数与最终资格的精确rollback fact。8705异构T6保持rollback=`80/0/0`、inside/coverage=`20/20`及安全/同步门，8706 SF1保持golden Flow hash。

[COMPUTED][HIGH] 阶段4第十切片将T1 OpenSpawn收敛为Demo Pipeline唯一runtime与稳定prepared boundary facts，删除per-agent OpenSpawn fragment；Combat/Visual最终rollback事实由VisualStateResolve补齐，movement/combat双完成门控制replay/checkpoint。PostFinalize当前只读取Identity与最终RoundSim，未把T1、Combat或Visual合同下沉到MassCrowdCore通用运动POD。Development、DebugGame（`-DisableUnity`）、`MassCrowd` 12/12、`CrowdDemo` 105/105及T1/T6/T7/T8双端回归通过；默认Unity Development仍有插件旧`.cpp`辅助函数重名债务。

[COMPUTED][HIGH] 阶段4第十一切片物理删除Demo的MoveIntent、GuidanceCandidates、ComposedGuidance、LocalVelocity、ParticleConstraint与Facing六个迁移fragment及其Mass模板、processor、rollback和旧Gather适配路径。Runtime fragments现在是这些中间阶段的唯一Mass权威；Demo仅保留RoundSim最终checkpoint状态及具有独立阶段/诊断语义的ProposedMovement、FlowSample、ObstacleConstraint。Development、DebugGame、105/105 CrowdDemo、12/12 MassCrowd及T1/T2/T6/T8/SF1回归通过。

[COMPUTED][HIGH] 阶段4第十二切片新增Runtime `FCrowdMassMovementPipelineWork`，把Guidance Compose、Local Predictive和MovementPredict收敛到一个不可变POD任务。Demo GT只在任务前准备场景overlay与T1/Reactive边界事实，任务后对完整结果集执行一次原子发布；旧三个动态processor实现已删除。插件等价测试逐项比较合并前后三个stage hash，并覆盖输入反序及重复overlay拒绝。

[COMPUTED][HIGH] 阶段4第十三切片新增Runtime `FCrowdMassFacingFinalizeWork`，将Facing Resolve、Finalize输入组装、稳定CommitPlan构建和Commit target验证放入同一不可变POD任务。Demo单一processor在任务后执行完整Mass身份、Lifecycle和最终运动事实预验证，再原子写入Facing、Runtime movement与Demo RoundSim；旧Facing/Finalize processor实现删除。组合链测试与原两段stage hash等价，并覆盖输入反序与revision mismatch整批拒绝。

[COMPUTED][HIGH] 阶段4第十四切片新增Runtime `FCrowdMassParticlePipelineWork`。该任务在Core Particle Solve后生成通用publish plan：稳定排序的per-agent结果、inactive/fallback语义、外部粒子保留结果、最终kinematics及publish hash。Demo只负责把通用结果转换为路线/稳定性/rollback指标，并通过一次最小Mass查询校验身份后发布Runtime Particle fragment；不再读取ProposedMovement与FlowSample fragment来重建求解后事实。

[COMPUTED][HIGH] 阶段4第十五切片删除`FCrowdMassParticleConstraintFragment`。Particle publish plan与prepared final kinematics成为Particle→FacingFinalize的唯一通用传递合同；Demo Particle processor无Mass query，仅组装路线、稳定性、fixture与rollback等宿主验收事实。FacingFinalize在原子写回前以prepared kinematics、Commit plan和实际Mass身份/Lifecycle执行完整预验证。

[COMPUTED][HIGH] 插件结构自动化明确禁止该临时fragment及Particle query回流；Development、DebugGame、15/15 MassCrowd、105/105 CrowdDemo和8731/8732生产回归通过。该切片减少了一个per-agent中间fragment和一次Particle阶段Mass读写接缝，但没有把Demo诊断迁入插件。

[COMPUTED][HIGH] 阶段4第十六切片将Particle求解结果与Demo持久验收副作用分离。Runtime/Core合同未增加Demo字段；Demo Pipeline仅保存带boundary版本门的`FCrowdDemoPreparedParticleDiagnosticCommit`，并在FacingFinalize原子写入成功后的post-finalize入口一次性提交route、stability、OpenSpawn、failure fixture和Particle summary。

[COMPUTED][HIGH] 该时序保证未提交的Particle候选不会推进Demo累计器，且rollback snapshot继续捕获已经提交的同boundary指标。结构自动化禁止Particle solve块直接调用持久记录API；8733/8734证明candidate hash、能力、安全与同步语义不变。

[COMPUTED][HIGH] 阶段4第十七切片让FacingFinalize在原子写回过程中生成稳定的post-finalize最终记录；Demo PostFinalize不再执行Mass查询，只消费最终记录与prepared派生事实。插件Core/Runtime没有加入Demo指标或rollback字段，宿主验收边界保持在Demo。

[COMPUTED][HIGH] 最终记录的生命周期属于单个boundary：`PublishBoundarySnapshot`必须清空上一boundary记录。首次8735因遗漏该清理触发连续整批拒绝；修复后结构测试、全自动化及8737/8738生产回归通过。

[COMPUTED][HIGH] 阶段4第十八切片把Transform、Velocity和宿主movement镜像并入FacingFinalize的原子写回遍历；Authority/Client Commit不再访问Mass，只保留角色阶段与完整最终记录门。旧两遍`CommitRoundState`路径已删除，写前全量身份/Lifecycle/结果验证保持不变。

[INFERRED][HIGH] 通用运动终态已经形成`Boundary Snapshot → Runtime WORK → Stable CommitPlan → 单次GT终态写回`；但Demo Business、Visual/Combat、checkpoint和部分场景事实仍有独立宿主查询，所以不能把“运动终态收敛”外推为整个Demo boundary单次读取。下一步审计CheckpointPublisher；当前不得进入archetype拆分或100/500。

[COMPUTED][HIGH] 阶段4第十九切片将checkpoint采集改为VisualStateResolve后的稳定prepared发布。CheckpointPublisher不再读取任何Mass fragment；它消费已校验的最终RoundAgentState数组并保持原有correction/result/chunk边界。该变化只收敛Demo宿主数据流，没有把Combat或网络结构下沉到MassCrowdCore。

[INFERRED][HIGH] 插件Runtime运动终态与Demo checkpoint之间已没有额外Mass遍历；VisualStateResolve仍是宿主业务写入阶段，并仍读取部分已存在于prepared最终记录中的基础运动事实。下一步只收缩该业务查询，不改变模块边界或进入archetype/规模扩展。

[COMPUTED][HIGH] 阶段4第二十切片让VisualStateResolve直接消费Runtime/Demo终态链已经验证的最终记录与boundary formation facts，不再查询Formation或RoundSim。该阶段只剩Identity映射与六类Demo可变Combat/Visual fragments；插件Core和Runtime公共POD没有增加Combat字段。

[COMPUTED][HIGH] 阶段4第二十一切片删除独立VisualStateResolve processor。Demo Combat processors仍先按业务顺序更新宿主fragments；FacingFinalize随后在原有全量预验证后的同一次Mass写回中读取这些最新业务事实，以最终运动速度解析Visual状态，并同步提交运动、Transform/Velocity、Combat/Visual、post-finalize records与checkpoint states。这样避免了在boundary起点过早冻结Combat输入，也没有把宿主Combat合同下沉到Core。

[COMPUTED][HIGH] PostFinalize继续先完成movement rollback事实，再从prepared checkpoint records完成Combat事实和Particle applied hash；CheckpointPublisher无Mass query。Development、DebugGame、15/15 MassCrowd、105/105 CrowdDemo及8745–8748 T2/T6/T7/T8回归通过。

[INFERRED][HIGH] 当前可准确宣称“最终运动与宿主Combat/Visual一次原子写回”，不能宣称整个boundary只有一次Mass读写。BusinessPrepare、Ranged/Hit业务和Movement中间镜像仍需逐项审计；下一阶段先删除无真实消费者的接缝，再评估archetype拆分。

[COMPUTED][HIGH] 阶段4第二十二切片物理删除Runtime GuidanceCandidates、ComposedGuidance和LocalVelocity三个无fragment消费者的中间Mass fragments；正式阶段间数据只经prepared POD传递。Runtime Agent、SimulationState和Properties保留为插件持久接入合同，并在spawn/bootstrap/Plan边界显式初始化；MovementWork不再逐boundary把Demo事实镜像回这些fragments。

[INFERRED][HIGH] 插件通用运动链的中间Mass镜像已经关闭；剩余主要接缝位于Demo宿主Combat链。Stats/Business/Attack/Reactive/HitFlash/Visual是有真实消费者的业务状态，下一阶段应在Demo侧形成顺序Combat transaction，而不是把它们迁入Core或直接删掉。

[COMPUTED][HIGH] 阶段4第二十三切片在Demo宿主侧建立唯一Combat boundary transaction：一次稳定Mass gather后依次运行Attack/Projectile、Hit resolve与Reactive motion，再一次原子apply。旧三个业务processor和跨processor Pending HitFact桥已删除；插件Core/Runtime接口、Movement WORK与最终CommitPlan均未增加Combat字段。

[COMPUTED][HIGH] 结构自动化要求新事务恰有两次Mass traversal、业务顺序固定、旧类名与Pending桥引用为0。Development、DebugGame、MassCrowd 15/15、CrowdDemo 105/105及8755–8758 T7/T8/T2/异构T6生产回归通过。

[INFERRED][HIGH] 该切片证明Demo Combat可在宿主事务内收敛，不证明所有宿主系统都应进入插件Runtime。下一步应以剩余Mass接缝矩阵区分通用Runtime、Demo验收、Networking与Presentation边界，再决定archetype拆分；不能仅为追求“一次读写”把宿主业务塞入Core。

[COMPUTED][HIGH] 阶段4第二十四切片已建立`Docs/MassQueryOwnershipMatrix.md`。矩阵确认Runtime persistent fragments、Demo Combat状态、Networking/Presentation状态都有真实跨boundary所有权；ReactiveMotionStep和TargetCapability没有该所有权，已分别改为prepared POD和物理删除。

[COMPUTED][HIGH] TargetRegionGuidance现在直接消费Runtime canonical boundary snapshot，FlowPreferred也用同一snapshot验证结果集合；前者零Mass遍历，后者只保留FlowSample持久写回。该变化没有把Demo Target诊断、Combat或网络字段加入Core/Runtime公共POD。

[INFERRED][HIGH] 插件边界的下一项不是archetype拆分，而是关闭SF1 ProposedMovement/ObstacleConstraint阶段桥，并在golden Flow与障碍安全保持后重新审计FacingFinalize。只有中间所有权继续收敛后，按能力拆archetype才不会复制当前兼容债务。

[COMPUTED][HIGH] 阶段4第二十五切片已关闭SF1阶段桥：Demo MovementWork与ObstacleConstraint均通过Pipeline prepared POD连接，两个Demo中间Mass fragment已物理删除；该变化没有向MassCrowdCore/Runtime公共合同加入SF1专用状态。SF1障碍求解仍调用原Shared Flow纯kernel，8763保持golden hash与路线/障碍结果。

[COMPUTED][HIGH] 阶段4第二十六切片把Facing跨boundary的settle历史加入canonical `BoundaryGather` prepared facts，FacingFinalize从3次Mass遍历降为2次。Runtime/Core公共合同没有增加Demo字段；剩余的全量预验证与原子写回分别承担整批失败门和唯一提交，当前不合并。

[COMPUTED][HIGH] 历史Networking归因已分离：SF1 correction当时没有按frame时间恢复历史snapshot并重放，故8767保持约一个30Hz fixed-step的`26.745cm`时间错位；历史500 bootstrap当时把完整`RoundBootstrapPacket.Agents`作为单一复制属性发送，8764因此在Round前触发bunch过大。前者已由8770修复，后者已由阶段D的Snapshot chunks替代；两者都不是Core运动kernel问题。

[COMPUTED][HIGH] 阶段4第二十七切片先在Demo/Networking宿主边界闭合通用fixed-step correction history/replay：SF1与SoftPressure共享历史边界、集合校验、零误差快速路径和必要时的确定性重放。该变更没有向MassCrowdCore或Runtime运动POD加入网络字段；8770将SF1 correction interval p95收敛到`0.064cm`。

[COMPUTED][HIGH] StableEntityRef/Capability/Behavior POD、Relevant Snapshot、Demo Bootstrap适配与lifecycle/membership delta已按B–E完成；Core没有网络载荷。E通过Development/DebugGame、定向3/3、MassCrowd 23/23与CrowdDemo 109/109。[INFERRED][HIGH] 下一步是F的最小Mass World原子apply，不是修改Demo行为或地图。

[COMPUTED][HIGH] 阶段F的Runtime LifecycleStore及Networking boundary adapter通过真实World定向1/1、MassCrowd 24/24、CrowdDemo 109/109与Development/DebugGame；Runtime新增Source对Networking/Demo/Scenario反向依赖为0。Editor启动阶段仍输出两条不属于任何测试的`LogAutomationTest: Error: Condition failed`既有噪声，自动化Controller结果无失败。[INFERRED][HIGH] G必须新增独立持续场景并验证真实视觉实例回收。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
