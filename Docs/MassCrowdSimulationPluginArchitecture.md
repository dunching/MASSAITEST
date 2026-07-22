# MassCrowdSimulation 插件产品边界

## 1. 文档职责

[COMPUTED][HIGH] 本文件是可复用Mass虫群模拟插件的模块边界、公共合同和迁移顺序事实源。Demo目的查阅`DemoPurposeAndTargetEffect.md`，当前代码事实查阅`CurrentArchitecture.md`，阶段执行查阅`PhasePlan.md`。

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

[COMPUTED][HIGH] `MassCrowdCore`只依赖UE `Core`模块，保存稳定纯数据合同、确定性算法、排序、量化、hash及Round WORK输入输出。Core禁止引用Mass EntityManager、Actor、World、复制、渲染或Demo语义。

[COMPUTED][HIGH] `MassCrowdRuntime`依赖Core和MassEntity，未来承载fragments、traits、Pipeline、GT Gather、WORK调度、稳定合并、GT Commit及与网络无关的可回滚执行态。Runtime不得依赖Networking或Presentation。

[COMPUTED][HIGH] `MassCrowdNetworking`只允许依赖Core/Runtime及通用网络模块，未来承载checkpoint、correction、hash比较和rollback replay调度；Runtime中的确定性执行态不因关闭Networking而消失。

[COMPUTED][HIGH] `MassCrowdPresentation`只允许依赖Core/Runtime及渲染模块，未来承载ISM/VAT提交、视觉插值、correction offset衰减、朝向显示和通用调试绘制；资源必须由宿主配置，不得硬编码Demo资产路径。

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

[COMPUTED][HIGH] 插件Source禁止引用`CrowdDemo`、`SimRound`、`/Game/Maps/`和端口命令行。Core额外禁止引用MassEntity、World、Actor、MassReplication或Runtime。`MassCrowd.Plugin.Boundary`自动化对这些规则执行源码扫描。

## 5. 模块依赖与可选性

[COMPUTED][HIGH] Core与Runtime自动加载；Networking与Presentation的LoadingPhase为`None`，Runtime不链接它们。宿主只有显式声明模块依赖时才加载相应能力。

[INFERRED][HIGH] 当前只验证了依赖DAG、模块编译和边界扫描；“删除Demo后Core/Runtime/Tests独立编译”与“关闭Networking/Presentation的最小宿主运行”仍需后续专用Target或空白宿主验证，不能由LoadingPhase设置直接推断。

## 6. 固定迁移顺序

1. [COMPUTED][HIGH] 阶段1：插件、空模块、公共合同、依赖规则和边界扫描。
2. [INFERRED][HIGH] 阶段2：迁移Shared Flow、Target Region Transport、Guidance Compose、Local Predictive、Particle、Facing；逐fixture比较旧/新结果与hash。
3. [INFERRED][HIGH] 阶段3：迁移Mass fragments、traits、Pipeline和processors；Demo只提供规则、环境和目标。
4. [INFERRED][HIGH] 阶段4：完成单boundary `GT Gather → Core WORK → Stable Merge → GT Commit`。
5. [INFERRED][HIGH] 阶段5：迁移可选Networking和Presentation并验证禁用组合。
6. [INFERRED][HIGH] 阶段6：删除Demo算法副本，使T1–T8只配置插件。
7. [INFERRED][HIGH] 阶段7：在无Demo资源和Coordinator的最小生产宿主执行Development/DebugGame烟雾测试。
8. [INFERRED][HIGH] 上述阶段通过后才进入100/500。

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

[COMPUTED][HIGH] `MassCrowdRuntime`已新增`UMassCrowdMovementTrait`以及Base Movement所需的Agent identity、simulation state、properties、guidance candidates、composed guidance、local velocity、particle constraint和movement output fragments。Combat、Projectile、Demo Round与Presentation状态没有进入Base Movement archetype。

[COMPUTED][HIGH] Runtime Bridge把Mass事实按CapabilityProfileKey稳定分成`FCrowdRoundWorkInput`，把多个WORK输出稳定合并为全局AgentId顺序的Commit计划，并要求完整AgentId/Lifecycle集合预验证通过后才允许写回。Gather和Commit hash覆盖环境、Target、Profile、Agent及完整Movement事实。

[COMPUTED][HIGH] 插件原生Gather/Merge/Commit测试和带真实`FMassEntityManager`的最小Mass World测试2/2通过；Demo适配器旧/Core候选、状态、Composed Guidance与Commit等价测试1/1通过。Development、DebugGame、102/102项`CrowdDemo`及11/11项`MassCrowd`自动化通过。

[COMPUTED][HIGH] Demo生产模板现并行持有上述plugin fragments。每个fixed-step先从当前Demo身份、RoundSim状态、Movement属性和Particle属性构建稳定Runtime基础snapshot；Flow、Target Region和Business随后发布三类不可变Guidance overlay。Runtime Bridge稳定合并snapshot与overlay并拒绝缺失Shared Flow、重复provider/Agent及revision错误。Runtime镜像、overlay与基础snapshot均属于可重建派生状态，不进入权威rollback snapshot。

[COMPUTED][HIGH] 正式`Guidance Compose`已切换到`FCrowdMassGuidanceWork`：WORK线程只消费稳定POD并调用Core Compose，GT在完整AgentId/result集合验证通过后一次写入Runtime composed镜像、Demo composed和现有唯一MoveIntent。旧`FCrowdDemoRoundWorkKernel::ComposeGuidance`已无生产调用者，仅保留迁移等价测试。

[COMPUTED][HIGH] 正式`Local Predictive`已切换到`FCrowdMassLocalPredictiveWork`：它直接消费同一boundary snapshot与prepared Core composed结果，调用Core局部预测kernel，并在完整AgentId/result集合校验后一次更新Runtime local-velocity镜像和既有Demo local-velocity。旧Demo kernel已无生产调用者，只保留旧/Core等价及历史fixture测试。

[COMPUTED][HIGH] 正式`MovementPredict`已切换到`FCrowdMassMovementPredictWork`：它消费boundary snapshot、prepared composed与local-velocity结果，稳定处理自主/局部速度、T1 boundary freeze、业务垂直运动和Particle active事实，并发布prepared预测位置。

[COMPUTED][HIGH] 正式`Particle Safety`已切换到`FCrowdMassParticleWork`：它消费boundary snapshot与prepared MovementPredict结果，在同一WORK内执行Core Solve及applied-state安全复验，并输出candidate/applied summary与hash。GT完整校验结果集合后同步发布Runtime particle镜像和Demo兼容fragment。

[COMPUTED][HIGH] 正式`Facing`已切换到`FCrowdMassFacingWork`：Runtime WORK消费boundary snapshot、prepared composed与particle结果并调用Core Facing。连续settled计数暂由Demo rollback兼容状态在GT准备；结果集合完整后同步发布Runtime/Demo facing，Movement Finalize不再读取Demo facing作为最终Yaw来源。

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

[INFERRED][HIGH] 下一切片审计剩余T1 OpenSpawn和Combat/Visual业务事实，分别归入可恢复业务快照或独立诊断快照；Identity与最终RoundSim state仍是实际提交状态采样的必要输入。只有必要状态读取、全部派生输入和最终写回均被收敛后，才可宣称完整`GT Gather → Core WORK → Stable Merge → GT Commit`成立。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
