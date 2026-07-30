# MassAI Crowd Demo 当前阶段计划

## 2026-07-30 PW0–PW8 持久Worker Simulation Runtime

[INFERRED][HIGH] 本阶段在现有非阻塞Boundary之上建立每World持久Worker权威镜像、连续调度和可变Result Batch交换；它不把Mass Work Processor误当成常驻线程，也不允许GT与Worker双写同一模拟字段。详细设计以`PersistentWorkerSimulationArchitecture.md`为准。

PW0. [x] [INFERRED][HIGH] 文档已冻结Worker权威所有权、GT输入/结果Adapter、持久Runtime、短生命周期UE Task Shard、三缓冲Published Batch、模拟时间、混合Consistency Domain、teardown和规模门；未修改生产代码。

PW1. [x] [COMPUTED][HIGH] `MassCrowdRuntime`已实现通用Schema Input Batch、State Patch、Published Batch、Generation/Sequence门、显式Limits及Building/Published/Consuming三缓冲Exchange；覆盖0/1/10/9999结果、State latest-wins、有界有序Event、Deferred发布、单Consumer Frame一次交换和256轮跨线程单生产者/单消费者。未接入Demo生产；Development/DebugGame Editor `-DisableUnity`、定向7/7、MassCrowd 72/72与CrowdDemo 134/134通过。

PW2. [x] [COMPUTED][HIGH] `MassCrowdRuntime`已实现每World唯一`FCrowdAsyncSimulationRuntime`宿主、Worker权威SoA Mirror、固定Quantum Simulation Clock、单Owner短生命周期Pump、显式Input容量、Generation Invalidate、Stop/Drain和全量Resnapshot；Task只捕获线程安全纯数据状态，不访问Mass、World或UObject，也不写生产Mass。Development/DebugGame Editor `-DisableUnity`、定向10/10、MassCrowd 75/75与CrowdDemo 134/134通过。

PW3. [x] [COMPUTED][HIGH] Demo Round/Mixed/Friendly已接入Worker Input Sync：首次Boundary Snapshot提交全量Resnapshot，随后只提交Lifecycle、Dirty State、快照Resource和已成功Commit的Behavior Command Journal；失败Prepared命令不进入Worker，成功Batch后才ACK Journal。Worker按已应用Input Sequence核对Entity/Lifecycle、State Hash和源Snapshot元数据Hash，不写生产Mass。Mixed实际运行300批时累计165条Command、`pending=0`、`superseded=0`；Mixed/Friendly各连续比较600批无Violation。Development/DebugGame Editor `-DisableUnity`、WorkerShadow定向2/2、MassCrowd 77/77与CrowdDemo 134/134通过。

PW4. [x] [COMPUTED][HIGH] SharedFlow sampling、Facing和独立Business已增加稳定Shard归并并接入每World Runtime短Task Shadow Scheduler；Business Schema Payload padding已按字段规范化。Runtime按Kernel拒绝重复/倒退Work Sequence，限制显式in-flight容量，允许Task乱序完成但跨Poll只按全局提交序交付，Invalidate/Stop排空已提交短Task。Round生产只提交自包含不可变输入且不写Mass，Shard大小1–64轮换、正反派发交替；9111在step 300累计`submitted=900 completed=900 in_flight=0 mismatches=0`且无硬错误。Development/DebugGame Editor `-DisableUnity`、MassCrowd 78/78与CrowdDemo 134/134通过。

PW5. [x] [COMPUTED][HIGH] Worker Owner已发布可变State Patch/空Batch，GT `UCrowdDemoWorkerResultApplyProcessor`每帧只执行一次有限Exchange，只更新Presentation/诊断代理。Adapter在应用前验证Generation、Publish Sequence、Hash、PW5 Owner Mask、GT当前Lifecycle和跨Batch Event Sequence；旧Lifecycle不落代理。Exchange既有0/1/10/9999、latest-wins、有序Event、单帧门、三槽不可变与并发压力门继续通过，新增ResultApply 2/2通过。9112生产首批20 Patch应用为`batches=1 patches=20 stale_lifecycle=0 events=0 proxies=20`；Development/DebugGame `-DisableUnity`、MassCrowd 80/80与CrowdDemo 134/134通过。

PW6. [x] [COMPUTED][HIGH] Movement已具备字段级Owner、Shadow/Canary/Production显式模式、Runtime双样本插值、递增Correction覆盖和普通输入Echo拒绝。Runtime短任务生成完整Movement尾链结果；Shadow 20/20比较、Canary 5个实体替换、Obstacle与SoftPressure Production各20个实体整批替换均零Violation，Production最终Mass Writer只消费Runtime尾链。Development/DebugGame `-DisableUnity`、Movement 2/2、MassCrowd 82/82与CrowdDemo 134/134通过。

PW7. [x] [COMPUTED][HIGH] 已将Particle Interaction Island、Target Cohort与Combat Event Boundary的迁移条件实现为公共fail-closed评估器和定向自动化；9121真实Round检查点分别因开放Island、网络语义未冻结、缺少Rollback证明判定为`KeepBoundary`且无Violation。未通过专项证明的Particle、Target和Combat继续使用现行Boundary。

PW8. [x] [COMPUTED][HIGH] Production已关闭Movement双权威：最终Mass代理只消费`PersistentRuntimeAuthority`接受的Domain Tail，旧Boundary Movement结果不再作为第二生产Writer；Shadow/Canary仅保留为显式验证模式。9174/9175/9177/9179分别以1k/2k/5k/10k持续到step 300，接受`301000/602000/1505000/3010000`个状态且Input Queue均为0；10k采样为simulation lag=`9.677ms`、scan=`1.549ms`、owner pump=`2.988ms`、GT apply=`0.403ms`。9154–9166覆盖T1–T9，9168/9169/9170覆盖Mixed/Friendly/Continuous；单进程双PIE在`PW8DualPIE_Final.log`验证每World独立Runtime及完整teardown。9180 T7 Production录屏为20实体、58状态事件、0 mismatch、0 freeze并生成三段事件切片。Development/DebugGame Editor `-DisableUnity`、MassCrowd 83/83、CrowdDemo 135/135通过。10k完整Demo Boundary约145ms/step，不宣称整条强一致流水线达到实时；PW8关闭的是Persistent Runtime/Exchange的持续守恒和迁移门。

## 2026-07-30 AB0–AB6 异步Fixed-Step Boundary

[INFERRED][HIGH] 本阶段替换旧P1“一次Dispatch、一次Wait”的最终合同，但不推翻其不可变Snapshot、Worker纯计算、完整验证和唯一GT writer成果。详细设计以`AsyncFixedStepBoundaryArchitecture.md`为准。

AB0. [x] [INFERRED][HIGH] 文档设计已冻结GT Processor、可选非GT Mass Work Processor、Boundary Work Stage、UE::Tasks DAG、单槽Mailbox、Request/Result、跨帧Fixed Step、失效/teardown及性能门；未修改代码。

AB1. [x] [COMPUTED][HIGH] 已增加Task级queue/run/critical-path遥测、Orchestrator/Runner非阻塞`PollAndDrain()`，并删除Runtime阻塞等待接口；定向自动化7/7通过。

AB2. [x] [COMPUTED][HIGH] Runner已作为每World深度1 Mailbox，携带Generation/PlanRevision/FixedStep/SnapshotHash；Round/Friendly/Mixed持久任务仅捕获ThreadSafe数据。

AB3. [x] [COMPUTED][HIGH] Round跨帧消费/生产与T5S首图已经关闭；8822为inside20、coverage16/16、稳定诊断valid=1、fixed-step/backlog p95=`17.980/31.284ms`，阻塞/stale/catch-up均为0。

AB4. [x] [COMPUTED][HIGH] T5M/T6A/T6S/T6M推广、Worker-side Topology cache和Request级Flow lookup已完成；8824/8825/8826/8823功能与性能门通过，当前不合并Cohort Task。

AB5. [ ] [COMPUTED][HIGH] 全生产路径阻塞Wait已删除；Round动态Stage Adapter的`CallExecute()`收敛尚未完成。

AB6. [ ] [COMPUTED][HIGH] 四构建、MassCrowd 65/65、CrowdDemo 134/134及T5/T6五图独立双端门已通过；单进程双PIE、确定性完成顺序、Correction、teardown、全场景与FFmpeg连续性仍待关闭。

## 2026-07-29 T9 Mixed Combat Integration

[COMPUTED][HIGH] T9 Small已实现并通过最新双端生产门。场景固定20实体、10对10、无重生；每方4 Melee、2 MidRange、4 Ranged。通用攻击状态机、Attack Intent、Melee/MidRange Spatial Sweep、Ranged Mass Projectile、Combat Resolver、Prepared Health/Death提交、目标失效重选、TargetRegion/Flow缓存重建和v2可靠攻击快照均已接入。

T9.1 [x] [COMPUTED][HIGH] `MassCrowdDemoBusiness`提供`MixedCombat=20006`、Planner=`21008`、Action=`23006`以及三类Payload/Profile稳定ID；T8与T9共用Acquire→Windup→Commit→Recovery→Cooldown状态机。

T9.2 [x] [COMPUTED][HIGH] 生产`AttackTarget` Source已删除；攻击由Planner产生Intent，Commit步只叠加一帧`MovementLock`。Behavior Codec保持v3，Registry黄金值版本化为`11335697795273479593`，旧Registry拒绝/resync。

T9.3 [x] [COMPUTED][HIGH] `ImpactFact`已泛化为`ImpactId + ImpactTypeId`；Melee/MidRange与Projectile稳定合并后统一进入Combat Resolver。死亡实体从即时Spatial目标集合排除，下一Boundary清理目标Context并重选，目标变化会清空旧Goal缓存以重建TargetRegion/Flow计划。

T9.4 [x] [COMPUTED][HIGH] 8517双端门在fixed step 600通过：alive=`11`、三类intent=`61/31/21`、impact/damage/death=`62/61/9`、target switch/region rebuild=`227/227`、referenced dead=`0`、Projectile spawned/impacted/expired/active=`21/5/15/1`且duplicate=`0`、双端entity/membership Hash一致，服务端p95=`2.910ms`。

T9.5 [x] [COMPUTED][HIGH] 完整自动化为MassCrowd `64/64`、CrowdDemo `133/133`；T8双端版本化黄金门通过，Development/DebugGame × ForceUnity/DisableUnity四构建均成功。

[INFERRED][HIGH] T10玩家Pawn、`GameplayCommand`、玩家线形/圆形技能、持续生成和完整游戏循环继续延期，不属于T9完成结论。

## 2026-07-29 pre-T9检查点

[COMPUTED][HIGH] 本节是提交`5b947389`的pre-T9历史检查点：DP0–DP6已完成而T9尚未开始。它固定保留MassCrowd 64/64、CrowdDemo 131/131、四构建、旧T8黄金门以及Mixed 20/100/500证据；当前状态以上方T9章节为准。

## 2026-07-29 现行 DP0–DP6 Demo业务规划模块化

[COMPUTED][HIGH] DP0–DP6 已完成。Demo 产品 Source、业务 Planner、业务纯状态和 Prepared Adapter 已收敛到独立项目模块 `MassCrowdDemoBusiness`；Mixed、Friendly、Round T7/T8 已迁移，Round T1–T6 与 Continuous 使用 NoBusiness 保持专项边界。详细合同以 `DemoBusinessPlanningArchitecture.md` 为准。

DP0. [x] [COMPUTED][HIGH] 已冻结提交 `07359ed`、Mixed 每20实体角色比例、现有Provider/Source/Adapter ID、T7/T8业务Hash、65/65、125/125、四构建及Mixed 20/100/500性能基线。

DP1. [x] [COMPUTED][HIGH] 独立模块、Planner Registry/Snapshot/Decision/Writer/Runner、冻结排序、稳定Hash和结构门已实现。

DP2. [x] [COMPUTED][HIGH] Mixed五类Planner、Reaction叠加、稳定角色表、Context Request和反序等价已实现。

DP3. [x] [COMPUTED][HIGH] Demo Provider、Source Diff、Ledger、Combat/RangedAttack规划和Prepared Business Adapter已迁移；Runtime领域Commit API已删除。

DP4. [x] [COMPUTED][HIGH] Mixed、Friendly、T7/T8已使用业务Planner；T1–T6与Continuous显式NoBusiness；旧EvaluateSlotBehavior/AdvanceAttackPhases双路径已删除。

DP5. [x] [COMPUTED][HIGH] 共享Planning Host、Source状态发布与Prepared接线已收口；公共Runtime无Cargo/Attack领域合同，业务模块无Engine/MassEntity/Networking/Projectile依赖。

DP6. [x] [COMPUTED][HIGH] 最终自动化为MassCrowd 64/64、CrowdDemo 131/131；Development/DebugGame × ForceUnity/DisableUnity四构建通过。Continuous、Friendly、NavFlow、T1–T8和Mixed 20/100/500同路径门通过；T8恢复spawn/impact/damage=50/50/50、duplicate=0并双端一致。

## 2026-07-29 已关闭 PJ0–PJ6 Projectile模块化

[COMPUTED][HIGH] R0–R7、S0–S6和PJ0–PJ6均已关闭。Projectile公共所有权现已收敛到`MassCrowdSpatial`、`MassCrowdCombat`和`MassCrowdProjectiles`，Demo只保留攻击业务与宿主Adapter；当前没有未关闭的本仓库Projectile模块化阶段。

PJ0. [x] [COMPUTED][HIGH] PJ0检查点已收口当时的61/61、125/125和20/100/500结果，准确记录Boundary临时Projectile数组，并消除“并发门未运行”等过期断言；进入PJ1前工作区差异只有文档。PJ6最终结果已更新为65/65与新的并发门。

PJ1. [x] [COMPUTED][HIGH] 已新增`MassCrowdSpatial`、`MassCrowdCombat`和`MassCrowdProjectiles`模块；Runtime只依赖Spatial，Projectiles单向依赖Core/Spatial/Combat/Runtime/MassEntity，公共模块不引用Demo。

PJ2. [x] [COMPUTED][HIGH] `MassCrowdSpatial`已接管Movement SpatialSafety，并提供稳定Body Snapshot、Uniform Grid、移动球相对Sweep、环境AABB Sweep、NavLayer/Mask过滤和稳定TOI决胜；候选NavLayer与安全Hold原子提交缺口亦已由专项和规模门关闭。

PJ3. [x] [COMPUTED][HIGH] `MassCrowdCombat`已接管Impact/Hit、Effect Profile、纯Resolver和Prepared Host Commit合同；Acquire/Windup/Fire/Recovery仍属于宿主业务。

PJ4. [x] [COMPUTED][HIGH] `MassCrowdProjectiles`已接管Spawn Request、Profile、Mass Fragment/Trait、动态Mass实体池、Boundary Pipeline和Prepared Patch；Mass Fragment是唯一跨Boundary持久权威，Gather/Prepared数组只存在于单次Boundary事务。

PJ5. [x] [COMPUTED][HIGH] Demo仅保留攻击相位、伤害、VAT/视觉映射和明确的Environment/HostHit Adapter；`FCrowdDemoProjectileKernel`、Demo Projectile Fragment/Store与Round重复算法入口已删除，未保留运行时双路径。

PJ6. [x] [COMPUTED][HIGH] Spatial/Combat/Projectiles/Public API与结构专项、T8/R7回归、`MassCrowd 65/65`、`CrowdDemo 125/125`及Development/DebugGame × ForceUnity/DisableUnity四构建均通过。最终同一路径8402/8403/8401分别以20/100/500实体并发4/20/100发Projectile通过：spawn/impact/damage完全守恒、duplicate=`0`、双端Hash一致、最小同层间距=`70.11/70.03/70.00cm`，服务端fixed-step p95=`2.152/9.675/30.016ms`。

## 2026-07-28 现行 S0–S6 Standard Sources与生产消费

[COMPUTED][HIGH] 本节是Standard Sources当前事实表。R0–R7保留为已经完成的开放框架、Scheduler、网络与Projectile基线；S0–S6现已全部关闭。

S0. [x] [COMPUTED][HIGH] 已在`MassCrowdStandardSourcesDesign.md`锁定扩展类比、模块边界、通用/产品Source归属、Context/State、组合原则、当前偏差和验收边界。

S1. [x] [COMPUTED][HIGH] Mixed已把Resolved Movement/Facing/Constraint接入`FCrowdMassMovementPipelineWork → Particle Constraint → Facing Finalize`，Business请求与Movement独立，所有Movement/Business/Slot结果在完整Prepare/Validate后一次Final Apply。Local Predictive与Particle新增InteractionLayer过滤以避免桥上桥下实体互相约束。Development Editor `-DisableUnity`、`MassCrowd.Core` 13/13、`CrowdDemo.MixedSandbox.J` 3/3通过；8010服务端在step841以pickup/delivery=`2/2`、combat=`50`、spawn/despawn=`4/4`、最小同层间距=`72.72cm`、零违规通过，8011双端门也通过。

S2. [x] [COMPUTED][HIGH] 已新增随插件交付且单向依赖Core/Runtime的`MassCrowdStandardSources`模块；Provider=`100`、公共ID范围、13个TypeId、`TargetKinematics`/`FormationAnchor` v1 Context和Registry Hash均已注册并冻结。Runtime/Core源码无Standard模块反向依赖或TypeId特判。

S3. [x] [COMPUTED][HIGH] 已实现`MoveToLocation`、`ArriveAtLocation`、`FollowEntity`、`PursueEntity`、`FleeFromEntity`、`MaintainDistance`、`FaceMovement/FaceEntity`和`MovementLock/SpeedLimit`自主Evaluator；目标Context校验、预测上限、迟滞State、Goal Hash、Facing独立输出和Constraint均有定向自动化。

S4. [x] [COMPUTED][HIGH] Demo Provider只保留SharedFlow桥、CarryCargo及Pickup/Deliver/Attack产品Source；Mixed按Navigation/Facing/Interaction/Presentation/Reaction五Controller维护稳定期望集合Diff，无变化不发命令。Movement/Facing只消费Resolved Channels，Attack不再隐式锁移。

S5. [x] [COMPUTED][HIGH] 已实现确定性方向表/PRNG的`WanderSteering`、锚点局部槽位修正`FormationOffset`和线性衰减`TimedImpulse`。Escort与Pursue+Attack组合进入Mixed；专项证明HitReaction及一帧Attack Lock结束后持久Handle/Payload/State逐字保持。

S6. [x] [COMPUTED][HIGH] StandardSources定向8/8、Mixed组合5/5、第三方三复制策略Fixture及同路径20/100/500双端门继续保持关闭；PJ6最终回归已将完整插件自动化更新为`MassCrowd 65/65`、`CrowdDemo 125/125`，并在相同Source/Resolver/Boundary/Networking路径加入4/20/100发公共Projectiles模块并发门，结果见上方PJ6。

## 2026-07-28 现行 R0–R7 重构

[COMPUTED][HIGH] 本节取代旧B0–B7并记录已经关闭的基础框架阶段；PJ0–PJ6随后也已关闭。下文A–J、P0–P5和B0–B7均为历史能力或迁移基线。

R0. [x] [COMPUTED][HIGH] 已重写架构事实源、阶段计划、检查表和恢复入口，明确旧B0–B7不再是完成口径；当前未提交文档修改被保留。

R1. [x] [COMPUTED][HIGH] Provider/Registry Builder、标准运动Context、8×96字节扩展Context、96字节实例状态、Next State Writer和Registry Hash已落地；独立`MassCrowdBehaviorFixture`只依赖Public API，覆盖仓库核心未知TypeId、注册/冻结、状态、六通道和网络回放。

R2. [x] [COMPUTED][HIGH] 领域Capability、Spec与Evaluator已迁到Demo Provider；Demo控制器使用稳定期望集合Diff，Mixed生产移动消费Resolver的`DesiredVelocity`，生产路径不再按具体SourceTypeId恢复业务语义。该框架门不证明完整Movement Pipeline或StandardSources自主Evaluator，后者由S1–S6关闭。

R3. [x] [COMPUTED][HIGH] 公共Scheduler使用稳定Stage/Task/Scope与资源Schema描述DAG；Patch Adapter按稳定键排序，Round与Mixed在完整Prepare/Validate后执行返回`void`的Final Apply，失败零写入由自动化覆盖。

R4. [x] [COMPUTED][HIGH] Behavior Source Command/Set Codec v3已携带Registry Hash、Context/State Schema和实例状态，并接入Mixed生产可靠状态、late join与resync；超过单batch容量的可靠记录按既有总上限稳定分批。

R5. [x] [COMPUTED][HIGH] Projectile状态已由动态容量的Mass Fragment唯一保存；旧`PreparedProjectiles`、`MirrorProjectileStates()`和固定32槽路径已删除。T8使用网格Broadphase、移动目标相对Sweep、环境Sweep、通用Impact/Hit事实与宿主唯一提交，并覆盖最早命中、墙体优先、阵营/NavLayer过滤和Pierce。

R6. [x] [COMPUTED][HIGH] `MassCrowdStateTreeAdapter`已移到默认禁用的兄弟插件；主`MassCrowdSimulation.uplugin`及其模块不再声明StateTree/GameplayStateTree/Adapter依赖，独立启用Smoke已通过。

R7. [x] [COMPUTED][HIGH] 同一Mixed Source/Resolver/Boundary/Networking路径已依次通过20、100、500双端门；`CrowdDemo.Integration.R7.ThirdPartySourceMassProjectile20`又在同一组合门中让20个第三方Fixture Source、持久Movement/Cargo/Business Source、临时HitReaction压制与恢复、20实体移动安全阶段和10发并发Mass Projectile共同运行。Projectile通过生产Mass Fragment Store、网格Broadphase和Sweep完成10/10精确命中；压制结束后持久Source保持20/20。最终Development/DebugGame、Unity/`-DisableUnity`、`MassCrowd 51/51`与`CrowdDemo 123/123`全部通过。

## 历史能力阶段与当前产品化顺序

[COMPUTED][HIGH] A–J是已经完成并保留证据的历史能力阶段；8210/8215仅是S6之前旧Round路径的100/500基线。同一Behavior Source生产路径的20/100/500后来已由S6关闭。L原工程迁移仍冻结。

A. [x] [COMPUTED][HIGH] 通用 Runtime/Replication 合同文档冻结。
B. [x] [COMPUTED][HIGH] StableEntityRef + Capability + Behavior POD。
C. [x] [COMPUTED][HIGH] Relevant Snapshot Header/Chunks。
D. [x] [COMPUTED][HIGH] Demo RoundBootstrap 适配生产 Snapshot。
E. [x] [COMPUTED][HIGH] Spawn/Despawn/Membership 增量协议。
F. [x] [COMPUTED][HIGH] 最小 Mass World 真实生命周期。
G. [x] [COMPUTED][HIGH] Demo continuous lifecycle 场景。
H. [x] [COMPUTED][HIGH] Logistics/Combat 通过同一 Behavior 接口接入。
I. [x] [COMPUTED][HIGH] NavMesh Surface Graph + Shared Flow。
J. [x] [COMPUTED][HIGH] 混合行为 Sandbox。
K. [x] [COMPUTED][HIGH] 同一Behavior Source生产路径的20/100/500双端技术门已依次通过；20实体第三方Fixture Source与10发并发Mass Projectile组合门也已通过。
L. [ ] [INFERRED][HIGH] 原工程最小宿主与生产迁移。

[COMPUTED][HIGH] 当前已完成A–K的同路径规模技术门；L原工程迁移未执行，也不在本轮范围。

[COMPUTED][HIGH] 下列P0–P5是2026-07-23已经完成的历史产品化闭环，不再是当前唯一实施顺序；当前新增实施顺序以上方AB1–AB6为准。

P0. [x] [COMPUTED][HIGH] 合同与事实收口：修复J状态冲突，冻结GT/WORK、Nav V1、物流边界、late join/relevancy和公共模块方向。
P1. [x] [COMPUTED][HIGH] 历史P1完成了一次gather、一次Dispatch、一次Wait、typed Worker DAG和唯一writer；8132/8137/8138/8139分别完成T2/T6/T7/T8双端门。该关闭只证明Snapshot/Worker/原子提交合同，不再证明非阻塞调度；同帧Wait已由AB阶段重新打开为待修缺陷。
P2. [x] [COMPUTED][HIGH] Nav provider、Recast adapter、Graph resource、Topology revision/hash、Flow handle/refcount与有界LRU已实现；独立`NavFlowProductSmall`在真实垂直Recast地图同时运行20实体P1 Movement链，98节点/234有向边/4层、2个Flow资源、cache hit=1、9504字节及双端无硬错误通过。
P3. [x] [COMPUTED][HIGH] owner-only replication actor、late-join baseline、可靠状态序列、latest-wins correction、空间RelevantSet、fail-closed resync重建与Presentation slot subsystem已实现；真实J与Continuous late join通过。
P4. [x] [COMPUTED][HIGH] 插件新增可复用事务Store，覆盖Claim/Pickup/Deliver/Cancel/Requeue、死亡后无Carrier cargo恢复和fallback sink；专用`CrowdDemo_FriendlyLogisticsSmall`地图以20个真实Mass实体通过稳定竞争、数量守恒、幂等、退避、取消及公共channel late join。8154双端hash=`3180435972084878253`，客户端Cargo attach/detach=`2/2`、最终实例=`20`，携货与交付近景证据已保存。
P5. [x] [COMPUTED][HIGH] J、Continuous与旧Round的实体状态均统一到公共Networking/Presentation。旧Round bootstrap、correction、ResultHeader和projectile event使用owner-only公共channel，20条latest-wins correction按帧有界聚合；客户端实体ISM只由Presentation subsystem/sink管理。8151 T2、8153 J和8157双客户端late join通过，J step600双端hash一致且无硬错误。

[COMPUTED][HIGH] 下节B0–B7仅是2026-07-28较早历史快照；其“未关闭”描述不得覆盖本文顶部R0–R7现状。P0–P5保持关闭，K的同路径规模门已补齐；L和动态NavMesh topology更新仍未关闭。

## B0–B7 Behavior Source 重构（2026-07-28）

B0. [x] [COMPUTED][HIGH] 脏工作区与P0–P5成果已保存为提交`ddb4740`并推送。
B1. [x] [COMPUTED][HIGH] Capability Profile/Modifier、稳定Source POD、容量与Hash已实现。
B2. [ ] [COMPUTED][HIGH] 冻结Registry、内置Evaluator、六通道结构和确定性排序已实现；Presentation Additive及完整Blend/冲突/溢出专项测试未关闭。
B3. [ ] [COMPUTED][HIGH] Legacy Recipe与Provider权威接口删除已完成；Mixed生产Movement/Facing仍未消费Resolver结果，Recipe仍逐步Stop/Start整组Source。
B4. [ ] [COMPUTED][HIGH] Source staging、Prepared Hash与Envelope v3已实现；Mixed仍在Movement校验前写Business/AgentFacts并在失败后回滚，不满足Final Apply不可失败。
B5. [ ] [COMPUTED][HIGH] 20实体Mixed已组合Runtime World Store、物流、攻击、受击约束、Nav和Presentation；基础移动、Formation、追击/攻击、物流、HitReaction/Death尚未全部统一到Resolved Channels生产入口。
B6. [ ] [COMPUTED][HIGH] 可选StateTree Adapter与物流中断恢复数据链已实现；专项测试未执行真实StateTree Task中断、重入和重复Event。
B7. [ ] [COMPUTED][HIGH] 权威ActiveBehavior、中心CanActivate、Provider API和网络Behavior字节已删除，v1拒绝/v2 Codec已实现；行为Codec生产接线、late join Source baseline、Predictable/ResolvedOnly、Hash resync及最终人工/PIE/规模门未关闭。

[COMPUTED][HIGH] 当前证据基线：DebugGame `-DisableUnity`、Development `-ForceUnity`、`MassCrowd 50/50`与`CrowdDemo 115/115`成功；8216 T8攻击/投射/伤害=`50/50/50`且双端Hash一致；8210的100实体SoftPressure和8215的500实体Obstacle分别是旧Round性能/网络/安全基线，不是Behavior Source 100/500关闭证据。

## 已完成（历史阶段记录；不覆盖上述现行B0–B7状态）

- [x] [COMPUTED][HIGH] 建立`MassCrowdSimulation`插件、Core/Runtime/Networking/Presentation/Tests五模块、公共合同和边界扫描；Development无依赖警告编译及插件边界1/1自动化通过。
- [x] [COMPUTED][HIGH] Shared Flow已提取为`MassCrowdCore`原生纯内核并接入Runtime生产WORK；权威Flow资源和动态anchor使用Runtime合同、暂由Demo Pipeline托管，Demo算法结构只保留迁移期field/sample镜像。SF1 golden hash、旧/Core/Runtime等价fixture、输入乱序和动态anchor均通过。
- [x] [COMPUTED][HIGH] Target Region Transport已提取为`MassCrowdCore`原生纯内核并接入Runtime生产WORK；Topology、Demand、Plan、validation、quota execution、Guidance、EngagedHold及claim replacement的旧/Core/Runtime结果与hash一致。
- [x] [COMPUTED][HIGH] Guidance Compose已提取为`MassCrowdCore`原生纯内核；provider优先级、稳定候选排序、量化、fallback和全部hash与Demo旧实现一致。
- [x] [COMPUTED][HIGH] Local Predictive及Velocity Half-Plane已提取为`MassCrowdCore`原生纯内核；8518六实体结果、pair、grant、component fixture hash及输入乱序合同与Demo旧实现一致。
- [x] [COMPUTED][HIGH] Particle Safety已提取为`MassCrowdCore`原生纯内核；8372完整20实体的各安全阶段、最终结果、candidate hash及applied几何hash与Demo旧实现一致，Combat RoundSim hash未迁入Core。
- [x] [COMPUTED][HIGH] Facing已提取为`MassCrowdCore`原生纯内核；稳定排序、转速限制、移动自主朝向、最终落位后朝目标、保持当前Yaw和角度跨界均与Demo旧实现一致。
- [x] [COMPUTED][HIGH] `MassCrowdRuntime`第一段已建立：Base Movement trait/fragments、Capability分批Gather、稳定Merge、全量Lifecycle预验证和Commit适配，并通过最小Mass World测试。
- [x] [COMPUTED][HIGH] 阶段 B 已在`MassCrowdCore`建立 trivially-copyable `FCrowdStableEntityRef`、`FCrowdCapabilitySet`、`ECrowdActiveBehavior`与`FCrowdAgentFacts`；Runtime identity/behavior fragments 提供双向映射，Demo 仅在现有 spawn/Plan 边界初始化兼容事实，未替换现行 AgentId 排序与 Movement 提交合同。
- [x] [COMPUTED][HIGH] 阶段 C 已在`MassCrowdNetworking`建立版本化 Relevant Snapshot Header、bounded Chunk、调用方显式 limits、FNV-1a 64-bit hash及支持header/chunk任意顺序的fail-closed assembly；未引入Demo语义或复制入口。Development `-DisableUnity`、定向3/3、MassCrowd 20/20与CrowdDemo 106/106通过。
- [x] [COMPUTED][HIGH] 阶段 D 已以显式版本化适配器将`FCrowdDemoRoundAgentState`写入阶段 C payload，并以复制的bounded metadata加可靠multicast chunks替换完整`RoundBootstrapPacket.Agents`复制；本地packet只保留为现有Pipeline消费对象。Development/DebugGame `-DisableUnity`、定向3/3、MassCrowd 20/20、CrowdDemo 109/109及8773 T2双端通过；客户端实际组装20 agents、1 chunk、3720 bytes，未出现bunch-too-large、Ensure或VIOLATION。
- [x] [COMPUTED][HIGH] 阶段 E 已在`MassCrowdNetworking`建立StableEntityRef驱动的Spawn、Despawn与Membership batches，共用base snapshot revision、fixed-step、RelevantSet revision和sequence；整批原子预验证，相同重复幂等，缺序列、冲突重复、stale lifecycle、非法槽位复用、错误membership前态与bounds均确定性拒绝。四类Despawn原因进入稳定hash；membership hash只描述排序后的当前活跃集合，与合法到达历史无关。Development/DebugGame `-DisableUnity`、定向3/3、MassCrowd 23/23与CrowdDemo 109/109通过；未创建真实Mass entity。
- [x] [COMPUTED][HIGH] 阶段 F 已在`MassCrowdRuntime`建立不依赖Networking的通用LifecycleStore，真实创建/销毁Mass entity、初始化AgentFacts与Membership fragment、支持despawn后高LifecycleSerial槽位复用、原子membership更新、matching correction和完整entity-set hash；Networking adapter只负责E协议状态副本验证与boundary提交。定向真实World 1/1、MassCrowd 24/24、CrowdDemo 109/109及Development/DebugGame `-DisableUnity`通过；未依赖Demo Round、地图或Scenario。
- [x] [COMPUTED][HIGH] 阶段 G 已建立独立`-CrowdDemoContinuousLifecycle`产品路径：不创建固定Round agents，以E/F生产协议和Runtime store从10实体增量增长到20上限，并持续执行Membership、Death/BusinessRecycle Despawn与同槽位高serial Respawn。当前客户端按StableEntityRef增量Add/Remove/Update单主体ISM，受击闪色由同实例PICD slot 2驱动，不使用T1 `bParticleActive`。9203序列44双端entity-set hash=`12305161180829922642`一致；旧8777双ISM证据只作为历史检查点保留。
- [x] [COMPUTED][HIGH] 阶段 H 已在`MassCrowdRuntime`建立provider/transition API，统一输出Target、Objective、MovementProfile、InteractionIntent与BusinessCommitRequest；Runtime basic provider覆盖Wander/MoveTo/Pursue/Guard/Flee，Demo Logistics/Combat adapters覆盖HaulPickup/HaulDeliver/Attack。commit ledger以确定性CommitId或宿主HitEventId幂等；真实`FCrowdDemoHitFact`同时进入既有Combat damage kernel和统一commit出口，重放不重复伤害/业务提交。Faction不同但Capability相同可得到同类输出，缺Capability即拒绝。Development/DebugGame `-DisableUnity`、定向2/2、MassCrowd 25/25与CrowdDemo 111/111通过；未接入G场景或NavMesh。
- [x] [COMPUTED][HIGH] 阶段 I 已在`MassCrowdCore`建立稳定分层Surface Graph、layer-aware attachment与反向邻接Dijkstra Shared Flow，在`MassCrowdRuntime`建立静态Recast提取器；节点保存StableNodeId、NavLayer、XYZ、法线、有序多边形顶点，边保存宽度、坡度与整数成本。真实地图`CrowdDemo_NavSurfaceGraphVerticalSmall`覆盖坡道、桥上桥下XY重叠、高台、多路线、窄桥及不可通行落差；8800运行通过98 nodes、234 directed edges、4 graph layers、13 overlap、76 reachable sloped edges、8/8 reachable markers且drop保持不可达。Development/DebugGame `-DisableUnity`、定向3/3、MassCrowd 27/27与CrowdDemo 112/112通过；未把H Behavior或G lifecycle组合进该场景。
- [x] [COMPUTED][HIGH] 阶段 J 新增独立`-CrowdDemoMixedSandbox`产品入口，在同一20实体fixed-step场景组合E/F真实LifecycleWorld、H统一Behavior/Logistics/Combat commit、I Recast Surface Graph/Shared Flow及客户端增量ISM。8804双端在step600通过：active/visible=`20/20`，29次行为切换、Cargo pickup/delivery=`4/1`、Combat quantity=`500`、commit/duplicate=`25/25`、spawn/despawn=`3/3`、membership迁移7次、同层最小间距=`71.51cm`、Server fixed-step p95=`0.863ms`、Client frame p95=`4.851ms`，双端entity/membership hash一致且无VIOLATION。Development/DebugGame `-DisableUnity`、定向2/2、MassCrowd 27/27与CrowdDemo 114/114通过；视觉证据为`Saved/StageJ_MixedSandbox_Visual.png`。
- [x] [COMPUTED][HIGH] P0完成只读源码审计与文档收口：确认Round canonical gather后的FlowSample/Facing遍历具有持久写回或fail-closed职责，Combat仍是独立Demo事务；确认J直接拥有Graph/Flow cache、O(N)安全检查、全量状态包和ISM；冻结P1–P5公共接口方向并明确J历史验收不等于插件产品化完成。
- [x] [COMPUTED][HIGH] P3公共运行层完成：Networking/Presentation加载阶段改为`Default`；J 7977真实双端baseline为20实体/3 chunks，step600双端entity/membership hash一致；Continuous 7975延迟加入后通过公共baseline+可靠lifecycle序列追平，sequence 32集合hash一致且visible与active恒等。
- [x] [COMPUTED][HIGH] P5已完成J与ContinuousLifecycle的公共Networking/Presentation切片：两者使用owner-only channel、late-join baseline、可靠增量与Presentation subsystem；生命周期GT写者固定在`TG_PostUpdateWork`，修复了真实运行中同步CreateEntity撞入Mass processing的断言。该切片不等于P5整体关闭。
- [x] [COMPUTED][HIGH] Demo template以Base Movement plugin fragments作为中间运动权威；正式Guidance Compose由Runtime WORK调用Core kernel，旧Demo Intent/Guidance/Composed fragments与Demo WORK生产调用已删除。
- [x] [COMPUTED][HIGH] 正式Local Predictive由Runtime WORK消费prepared composed并调用Core kernel；完整结果校验后发布Runtime local-velocity与prepared结果，旧Demo local-velocity fragment和Demo kernel生产调用已删除。
- [x] [COMPUTED][HIGH] 正式MovementPredict消费统一snapshot、prepared composed/local结果；Particle继续消费prepared预测结果与snapshot属性并调用Core kernel。Solve和applied-state复验均在WORK内完成，完整结果校验后发布Runtime particle与prepared结果，旧Demo particle fragment已删除。
- [x] [COMPUTED][HIGH] 最终Movement由Runtime WORK按Capability分批生成、Bridge全局稳定Merge并在全量AgentId/Lifecycle预验证后提交；Authority/Client Commit已改为消费Runtime MovementOutput，Demo RoundSim保留checkpoint/指标兼容镜像。

- [x] [COMPUTED][HIGH] 删除TargetApproach、TargetSlotLayout、旧Polar Density及其生产/测试兼容面。
- [x] [COMPUTED][HIGH] 建立唯一Guidance Compose writer及稳定provider优先级。
- [x] [COMPUTED][HIGH] Rollback改为不可变plan资源引用加小型可变执行态。
- [x] [COMPUTED][HIGH] 异构Target调试标记按Capability Profile绘制；性能阶段拆为11个准确阶段。
- [x] [COMPUTED][HIGH] Guidance Compose、Local Predictive、Particle进入不可变POD WORK线程。
- [x] [COMPUTED][HIGH] 修正T1测试boundary reset与普通视觉不连续混算；普通不连续现为0。
- [x] [COMPUTED][HIGH] Development、DebugGame（均使用`-DisableUnity`）、当前105/105项`CrowdDemo`自动化及13/13项`MassCrowd`插件自动化通过；默认Unity Development的插件旧`.cpp`辅助函数重名债务未在本切片处理。

## 历史实施记录（非现行下一步）

1. [x] [COMPUTED][HIGH] 已增加Round 1对齐的Game/Render/GPU/资源热身证据；T7普通运行8781/8783连续通过。历史8777首轮失败未被删除，也未被事后归因为单一资源原因。
2. [x] [COMPUTED][HIGH] T5M 8785安全、同步、Transport、性能和稳定诊断通过；移动追随不等同于静态settled。
3. [x] [COMPUTED][HIGH] RoundResultHeader contract v2已排除Server本地Performance payload；高熵自动化1566字节，8790真实异构payload 1970字节，无Native NetSerialize Warning。
4. [x] [COMPUTED][HIGH] 8788证明延长到60秒不能恢复能力；8789证明具备继承资格的634个claim全部迁移且无仍可行claim丢失。
5. [x] [COMPUTED][HIGH] `EngagedHold`由全量世界坐标零速收敛为单向目标跟随；8790 Round末inside/coverage=`20/20`且全部技术与性能门通过。
6. [x] [COMPUTED][HIGH] 用户确认AcquireThenHold实体在交互资格有效期间不需要持续重排Region；8790最后90步的`18/20`、`17/20`与settled window=0降为过程诊断，T6M按Round末20/20、安全、同步和性能门技术放行。
7. [x] [COMPUTED][HIGH] Guidance Compose纯内核与生产段迁移完成；Runtime WORK的provider选择、结果/hash及旧/Core等价fixture通过；迁移期Demo MoveIntent已在第十一切片删除。
8. [x] [COMPUTED][HIGH] Local Predictive纯内核与生产段迁移完成；Runtime WORK的Half-Plane/8518 fixture及旧/Core等价fixture通过，Demo grant/diagnostic/rollback存储语义保持不变。
9. [x] [COMPUTED][HIGH] Particle Safety纯内核与生产段迁移完成；Runtime WORK的pair/result/candidate/applied hash、输入乱序和8372旧/Core等价fixture通过，Demo指标/fixture/rollback兼容消费保持不变。
10. [x] [COMPUTED][HIGH] Facing纯内核与生产段迁移完成；Runtime WORK消费Runtime state/composed/particle并调用Core，完整结果校验后发布Runtime Facing及prepared rollback fact；旧Demo facing已删除，最终Movement只消费Runtime结果。
11. [x] [COMPUTED][HIGH] `MassCrowdRuntime`最小fragment/trait与Gather/Merge/Commit合同完成；Demo适配器等价测试通过。
12. [x] [COMPUTED][HIGH] Demo Base Movement plugin fragments单向镜像与Guidance Compose单段生产切换完成；Runtime镜像由当前Demo事实重建，不加入rollback副本，未增加第二个Movement writer。
13. [x] [COMPUTED][HIGH] Local Predictive生产段已迁移到Runtime WORK；Runtime local-velocity镜像与Demo兼容fragment同时发布，该切片未改变当时的Particle、MovementFinalize及Authority/Client Commit。
14. [x] [COMPUTED][HIGH] Particle生产段已迁移到Runtime WORK；Runtime particle镜像与Demo兼容fragment在完整结果校验后同步发布。
15. [x] [COMPUTED][HIGH] Runtime最终Movement/Commit接管完成：乱序稳定Merge、重复Agent拒绝、全量Lifecycle预校验、Runtime/Demo镜像一致性门及Authority/Client提交均已接入；8663 T2与8664 SF1 authority短运行无回退。
16. [x] [COMPUTED][HIGH] Facing生产WORK接入完成；Runtime乱序/非法输入与旧/Core/Runtime等价测试通过，8665 T2双端Yaw误差为0，8666 SF1无Particle路径无VIOLATION。
17. [x] [COMPUTED][HIGH] Shared Flow生产Build、动态anchor、T3双cohort资源和Preferred candidate已接入Runtime WORK；8667 T2保持20/20 terminal、16/16 coverage、fixed-step p95=`3.166ms`，8668 SF1 golden hash=`267519150`。
18. [x] [COMPUTED][HIGH] Target Region四阶段生产入口已接入Runtime WORK；8669 T2与8671异构T6 Static保持能力、安全、双端hash和性能门，Demo指标/诊断/rollback仍消费兼容镜像。
19. [x] [COMPUTED][HIGH] 单boundary基础运动Gather第一切片完成：Runtime snapshot覆盖身份、状态和运动/Particle属性；Shared Flow与Target Demand复用同一快照及prepared Flow输出。8672 T2与8673异构T6保持能力、安全、hash和性能门。
20. [x] [COMPUTED][HIGH] Guidance overlay与Local Predictive统一输入第二切片完成：Flow/Target/Business candidate作为prepared POD发布，Runtime Bridge与boundary snapshot稳定合并Compose记录；Local Predictive直接消费snapshot和prepared composed。8677 T2与8678异构T6保持能力、安全、hash和性能门。
21. [x] [COMPUTED][HIGH] MovementPredict、Particle与Facing统一输入第三切片完成：三段从boundary snapshot及prepared composed/local/predict/particle链构造WORK输入；Mass查询只保留T1/业务垂直运动、settled历史、诊断、累计器及兼容发布。8681 T2与8682异构T6保持能力、安全、同步、hash和性能门，8683 SF1保持golden Flow hash。
22. [x] [COMPUTED][HIGH] MovementFinalize统一输入第四切片完成：Particle/SF1 Obstacle和Facing发布prepared最终事实，Runtime helper与boundary snapshot组装完整Commit输入；删除旧Finalize第一遍全实体Gather，保留写入前完整身份及镜像原子门。8684/8685保持T2/T6能力、安全、同步和性能门，8686 SF1保持golden Flow hash。
23. [x] [COMPUTED][HIGH] MovementFinalize查询职责第五切片完成：写前一致性检查与提交后业务/指标采集拆为`ValidationQuery`和`ApplyMetricsQuery`；后者删除MoveIntent、Runtime properties、Runtime Particle/Facing冗余读取，原子预验证保持。8687/8688保持T2/T6能力、安全、同步和性能门，8689保持SF1 golden Flow hash。
24. [x] [COMPUTED][HIGH] post-finalize业务/诊断职责已从MovementFinalize提取为独立processor；Finalize只保留原子状态写入，成功step标记阻止失败后采集旧状态或提交旧Movement。8693/8694保持T2/T6能力、安全、同步与性能门，8695保持SF1 golden Flow hash。
25. [x] [COMPUTED][HIGH] post-finalize已删除Formation、Composed Guidance、Particle Properties和未使用Particle Constraint读取；FormationIndex/checkpoint Radius来自boundary formation facts，Composed Guidance来自prepared Runtime结果。8697暴露的异构Radius语义替代错误已由8698 correction replay闭合。
26. [x] [COMPUTED][HIGH] post-finalize的FlowSample改由prepared Runtime Shared Flow输出重建，Obstacle penetration改由boundary起点与Finalize终点直接复验；移除两个fragment读取。8703异构T6 correction与8704 SF1 golden hash通过。
27. [x] [COMPUTED][HIGH] post-finalize的GuidanceCandidates由snapshot与三类prepared overlay稳定重建，Facing连续settle与最终资格由Facing阶段发布精确rollback fact；两个fragment读取均已删除。8705异构T6 correction与8706 SF1 golden hash通过。
28. [x] [COMPUTED][HIGH] T1与Combat/Visual PostFinalize职责拆分完成：OpenSpawn唯一runtime生成prepared boundary facts并删除per-agent fragment；VisualStateResolve完成最终Combat事实；rollback snapshot以movement/combat双完成门进入replay-ready；PostFinalize只读取Identity和最终RoundSim。8707/8708/8709/8710与8714 smoke未出现安全、同步或完整性回退。
29. [x] [COMPUTED][HIGH] 兼容镜像物理删除完成：六个Demo迁移fragment、Mass模板注册、spawn初始化、processor读写、派生rollback副本及旧Gather适配入口已删除；真正承担阶段/诊断语义的ProposedMovement、FlowSample与ObstacleConstraint保留。Development、DebugGame、105/105 CrowdDemo、12/12 MassCrowd及T1/T2/T6/T8/SF1回归通过。
30. [x] [COMPUTED][HIGH] 相邻WORK任务合并完成：`FCrowdMassMovementPipelineWork`在一个ThreadPool任务内严格顺序执行Compose→Local Predictive→MovementPredict；GT一次准备snapshot/overlay/Reactive/T1不可变事实，并在完整AgentId集合验证后一次发布Runtime composed/local、prepared结果和ProposedMovement。旧三个processor实现已物理删除。
31. [x] [COMPUTED][HIGH] Facing→MovementFinalize相邻WORK合并完成：一个Runtime任务生成Facing与CommitPlan，单一GT processor在完整身份/运动事实预验证后一次写入Facing、Runtime movement和Demo RoundSim；旧两个processor实现已删除。Development、DebugGame、14/14 MassCrowd、105/105 CrowdDemo及8727/8728双端回归通过。
32. [x] [COMPUTED][HIGH] Particle结果发布接缝第一步完成：Runtime `FCrowdMassParticlePipelineWork`在Core Solve后构建覆盖active/inactive/fallback/external结果的稳定publish plan和最终kinematics；Demo诊断改读snapshot/prepared链，Particle Mass查询删除ProposedMovement与FlowSample读取。Development、DebugGame、15/15 MassCrowd、105/105 CrowdDemo及8729/8730通过。
33. [x] [COMPUTED][HIGH] Particle发布接缝第二步完成：物理删除临时Runtime Particle fragment及Trait/模板注册；Particle processor成为无Mass query的Demo诊断消费者，FacingFinalize直接消费prepared final kinematics并在原子写回前验证完整集合。Development、DebugGame、15/15 MassCrowd、105/105 CrowdDemo及8731/8732通过。
34. [x] [COMPUTED][HIGH] Particle诊断副作用延迟提交完成：Particle只发布带step/revision/agent-count门的prepared diagnostic commit；PostFinalize在原子终态成功后按原顺序一次性更新Particle/route/stability/OpenSpawn/fixture累计器，再记录rollback snapshot。8733/8734及全自动化通过。
35. [x] [COMPUTED][HIGH] FacingFinalize/PostFinalize重复遍历收敛完成：FacingFinalize在全量原子预验证后的唯一写回遍历中捕获最终RoundSim records；PostFinalize成为无Mass query的prepared消费者。首次8735暴露的per-boundary reset缺失已修复并由结构测试覆盖；8737/8738及全自动化通过。
36. [x] [COMPUTED][HIGH] 最终Engine Commit重复查询已关闭：FacingFinalize的全量预验证后单次写回同时提交RoundSim、Runtime Movement/Facing、Transform、Velocity和Demo Movement；Authority/Client Commit成为无查询的完整记录门。8739/8740及全自动化通过。
37. [x] [COMPUTED][HIGH] 第十九切片完成CheckpointPublisher审计与收敛：VisualStateResolve在Combat/Visual决议后发布整批prepared checkpoint states；Publisher删除九类fragment query并只消费该数组。8741/8742保持T2/T6能力、安全、同步、correction与性能门。
38. [x] [COMPUTED][HIGH] 第二十切片删除VisualStateResolve对Formation与RoundSim的重复读取，最终速度/状态取自FacingFinalize records，半径取自boundary formation facts；Identity保留为可变Combat fragments的稳定Agent映射。8743/8744保持T2/T6全部门。
39. [x] [COMPUTED][HIGH] 第二十一切片物理删除独立VisualStateResolve processor；FacingFinalize在同一次全量预验证后写回中提交运动、Transform/Velocity及Demo Combat/Visual，并发布checkpoint states。PostFinalize保持movement→combat rollback完成顺序。8745–8748覆盖T2、异构T6、T7和T8，能力、安全、同步、业务状态/hash与性能门通过。
40. [x] [COMPUTED][HIGH] 第二十二切片完成剩余Movement中间镜像审计：`FCrowdMassGuidanceCandidatesFragment`、`FCrowdMassComposedGuidanceFragment`和`FCrowdMassLocalVelocityFragment`无真实fragment消费者，已从Runtime合同、Trait、Demo模板和processor中物理删除；Guidance gather改用普通POD，MovementWork不再逐boundary重写Runtime identity/state/properties。Runtime identity在authority spawn及双端Plan激活时同步，Runtime state/properties在bootstrap/Plan激活边界初始化，随后由唯一FacingFinalize写回维护。Development、DebugGame、MassCrowd 15/15、CrowdDemo 105/105通过；8753 T2与8754异构T6能力、安全、同步及性能门通过。
41. [x] [COMPUTED][HIGH] 第二十三切片把RangedCombat→HitResponse→ReactiveMotion合并为单一宿主`CombatBoundaryProcessor`：一次稳定全量gather后严格执行Attack/Projectile→Hit resolve→Reactive advance，再一次原子apply；T8每boundary的Combat Mass遍历由5次降为2次，T7由3次降为2次。旧三个processor与跨processor `PendingProjectileHitFacts`桥已物理删除，Stats/Business/Attack/Reactive/HitFlash/Visual仍是Demo持久业务状态，没有下沉到MassCrowdCore。Development、DebugGame、15/15 MassCrowd与105/105 CrowdDemo通过；8755–8758覆盖T7、T8、T2和异构T6且无能力、安全、同步、业务hash或性能回退。
42. [x] [COMPUTED][HIGH] 第二十四切片已建立[RoundSim Mass查询与数据所有权矩阵](MassQueryOwnershipMatrix.md)：物理删除同boundary中转的`FCrowdDemoReactiveMotionStepFragment`并改用prepared SoA；物理删除零消费者`FCrowdDemoTargetCapabilityFragment`；`TargetRegionGuidance`改读canonical boundary snapshot，Mass遍历由1次降为0；`FlowPreferredVelocity`的身份校验改读同一snapshot，只保留一次持久`RoundFlowSample`写回；SoftPressure MovementWork不再写仅SF1消费的ProposedMovement。Development、DebugGame、15/15 MassCrowd、105/105 CrowdDemo及8759–8762 T7/T8/T2/异构T6均通过。
43. [x] [COMPUTED][HIGH] 第二十五切片已把SF1 `ProposedMovement → ObstacleConstraint`中间Mass桥改为Pipeline prepared POD：MovementWork与ObstacleConstraint均为零Mass遍历，两个中转fragment及模板/spawn/query读写已物理删除，FacingFinalize统一校验prepared final kinematics。Development、DebugGame、MassCrowd 15/15与CrowdDemo 105/105通过；8763 SF1 Single保持Flow hash=`267519150`、路线计数=`1/1/1/1`、双端penetration=0和checkpoint误差0，但correction interval位置误差p95=`26.745cm`，未通过`<1cm`严格同步门；8765 T2 terminal/coverage=`20/20、16/16`、p95=`4.227ms`；8766异构T6 completed/settled/inside/coverage=`20/20`、p95=`5.826ms`。
44. [x] [COMPUTED][HIGH] 第二十六切片把Facing上一boundary的settle计数并入canonical `BoundaryGather`，FacingFinalize由3次Mass遍历降为2次；完整身份/Lifecycle/结果预验证与唯一原子写回继续分离。Development、DebugGame、MassCrowd 15/15与CrowdDemo 105/105通过；8767 SF1 Single保持golden Flow和路线/障碍结果，8768 T2 terminal/inside/coverage=`20/20、20/20、16/16`、p95=`3.296ms`，8769异构T6 completed/settled/inside=`20/20`、coverage=`20`、p95=`4.674ms`。
45. [x] [COMPUTED][HIGH] 第二十七切片把现有128-boundary correction历史、完整Agent/Lifecycle门、零误差快速路径及必要时的恢复/重放扩展到SF1；新Round统一清理history。新增SF1 snapshot自动化后Development、DebugGame、MassCrowd 15/15与CrowdDemo 106/106通过。8770 SF1 Single保持Flow hash=`267519150`和路线/障碍结果，correction interval位置误差p95由`26.745cm`降至`0.064cm`、checkpoint p95=`0.008cm`，Round 1 snapshot hit/miss/mismatch=`36/0/0`；8771 T2和8772异构T6无回退。
46. [x] [COMPUTED][HIGH] 历史 500 启动缺口定位为完整 `RoundBootstrapPacket.Agents` 单属性复制；阶段 D 已以阶段 C primitives替换该复制路径，并用合成500实体transport覆盖分块、逆序、重复、缺块和超时。正式500产品运行仍留在K，不由该合成测试外推。

## 保护与停止门

[INFERRED][HIGH] P0文档阶段执行全文扫描、交叉核对、反向依赖扫描和`git diff --check`，不重复编译；P1–P5源码阶段均执行`git diff --check → Development/DebugGame -DisableUnity → 定向自动化 → 全部当前自动化`，行为、确定性、安全或稳定性能回退时停止。

[COMPUTED][HIGH] 不stage、commit、push；P2/P4只能修改其计划明确列出的验收地图与资产，不修改Lighting、30Hz、Particle硬门、网络频率、chunk size或复制预算；不进入K/L、100/500或原工程迁移。

## 2026-07-28 当前关闭检查点

- [x] [COMPUTED][HIGH] P1保持关闭：Round仍满足一次gather/dispatch/wait、完整预验证、唯一逻辑writer与失败零写入；7948–7951的T2/T6/T7/T8双端复测通过。
- [x] [COMPUTED][HIGH] P3保持关闭：owner-only channel新增有界reliable batch，ACK追赶按批次发送；空Drain不再产生stale，7939 J与7946 Continuous late join无resync。
- [x] [COMPUTED][HIGH] P4关闭：7953在专用地图以20个真实Mass实体完成移动、Pickup、Deliver、死亡恢复、fallback、不可达退避、守恒、late join和Cargo表现；四类近景证据已保存。
- [x] [COMPUTED][HIGH] P5关闭：J、Continuous与Round继续消费公共Networking/Presentation路径；7939、7946、7948–7951无硬错误。
- [x] [COMPUTED][HIGH] 累计门：Development/DebugGame `-DisableUnity`、MassCrowd 43/43、CrowdDemo 115/115、插件反向依赖扫描和`git diff --check`通过。

[COMPUTED][HIGH] 该P0–P5关闭检查点当时停止在K前；随后只完成20实体Behavior Source Mixed和旧Round 100/500分路径基线。K的同路径Behavior Source规模门与L原工程迁移继续开放。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
