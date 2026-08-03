# MassAI Crowd Demo 功能检查表

## 2026-08-03 当前状态

- [x] [COMPUTED][HIGH] 每 Tick Commit 已收敛为唯一 Movement Boundary Commit；重复 CommitPlan、PostFinalize State 和常驻 Checkpoint State 数组已物理删除。
- [x] [COMPUTED][HIGH] Final Business 只为 Dirty Ref 构造；Checkpoint State 只在发布门之后临时构造；DisableUnity、17 项自动化、9785 T5/600 与 9786 T8/900 Golden 通过。
- [x] [COMPUTED][HIGH] Legacy Round 普通 Correction 已默认关闭；只有 `-CrowdDemoLegacyFullCorrectionDiagnostic` 可显式开启。默认双 PIE 的旧 server full frame/client header 均为 0，WA7-R 稀疏 Scope Correction 门通过。
- [x] [COMPUTED][HIGH] WA7-R Digest 已改为 Unreliable 自覆盖传输；丢失、乱序、重复、更新覆盖和 resync reset 单元门通过。
- [x] [COMPUTED][HIGH] Worker Result Apply Proxy 已提供稳定实体视图、Stable Slot、Published Dirty Batch 与 ACK；普通 Intent/Proxy refresh 无完整 membership copy/sort/map。
- [x] [COMPUTED][HIGH] `FCrowdDemoRoundBoundaryGatherStage`、`RequestSubmitQuery`、公共 Runner/Orchestrator/WorkGraph、Round 四阶段和 Poll shell 已物理删除；完整 Snapshot Mass 读取只允许 Input Sync bootstrap/Plan Revision。
- [x] [COMPUTED][HIGH] 9779 T8/900 Golden 与 9781 T5/600 通过；step 600 均为 full-publish/hash/token=`1/3/598`，零 stale lifecycle。
- [x] [COMPUTED][HIGH] 最终 Mass Result Apply 已使用持久 StableEntityRef→Handle 索引和 Dirty EntityCollection；写入前验证完整成员/Lifecycle/Fragment 集合，正常帧无无界完整 Query traversal。
- [x] [COMPUTED][HIGH] 9782 T5/600 与 9784 T8/900 Golden 通过；Dirty Mass 遥测在 T8 动态场景为每批 20/20。
- [x] [COMPUTED][HIGH] 9790 全 Production T5/600 与 9791 T8/900 通过；T8 只保留 1 个 `plan_phase=1.000` RoundResult Checkpoint，普通完整 Correction 为 0。
- [x] [COMPUTED][HIGH] Target 已按 Cohort scope 增量失效；10k 双 Cohort 回归中仅受影响的 5000 实体执行 40 个 128 Guidance shard，未受影响 Cohort 无 Dirty/Topology rebuild。
- [x] [COMPUTED][HIGH] Particle 多闭合 Interaction Island 已独立 Solve 并稳定归并，且有全局 exact Applied-State 验证与单体 fail-closed fallback。
- [ ] [INFERRED][HIGH] Particle 大型单 Island 的 Cell-Pair Owner/逐轮 Barrier 分片仍 OPEN；当前 Cell 遥测不等于 Cell Solver 完成。
- [ ] [COMPUTED][HIGH] 9780 T5 step 886 Target Demand 可行区不足仍 OPEN；不得用 9781 的 600 Tick 门覆盖该长窗口缺陷。
- [ ] [INFERRED][HIGH] WA8 下一项是把 RoundResult/Late Join 迁到专用 Checkpoint 载荷，再物理删除 Legacy Correction producer/consumer/RPC；随后关闭 Target 长窗口缺陷，WA8.5/WA9 继续后置。

## 历史切片记录（不覆盖上方当前状态）

- [x] [COMPUTED][HIGH] Round 公共 Boundary Runner/commit envelope 依赖已移除，插件 Runner 文件与 Legacy Runner 测试已物理删除。
- [x] [COMPUTED][HIGH] Round 直接 Apply Plan 验证与原子提交已通过 DisableUnity、结构自动化和真实 T8 step 300 门。
- [ ] [COMPUTED][HIGH] 完整 T8 回归仍 OPEN：9724 step 415 Particle failure-trace replay hash mismatch。
- [ ] [INFERRED][HIGH] 下一项是修复 Particle replay 后删除 Round 四阶段/Poll shell；WorkGraph/Orchestrator 类型仍有 Round 消费者，当前不得删除。

- [x] [COMPUTED][HIGH] WA8 Demo transaction shell 脱钩：Friendly/Mixed Coordinator 均不再引用 Runner，Mixed 不再引用 WorkGraph；fallback 改为纯 Kernel plan，GT 仍以完整验证后的原子 Apply 为唯一写入边界。
- [x] [COMPUTED][HIGH] 本切片构建/结构/功能回归：Development Editor DisableUnity、Mixed/Friendly Architecture、9706 fallback、9707 Friendly Production、9709 Mixed 六 Domain Production 通过。
- [x] [COMPUTED][HIGH] Behavior Shadow 自主预测合同：bootstrap 严格状态 parity，普通 Intent 严格控制/命令 parity；9714 完成 600/600 expectation、双端业务 PASS、零 violation。
- [ ] [INFERRED][HIGH] WA8 下一结构项：迁移 Round 的最后一个生产 Runner/WorkGraph 消费者，再物理删除公共 API/Legacy 测试并执行 AST/注册审计。
- [ ] [COMPUTED][HIGH] WA9 性能仍 OPEN：9709 客户端 frame p95=`35.949ms`，高于 `33.333ms`，且该运行只有 20 实体。

- [x] [COMPUTED][HIGH] WA8 Friendly Production 直接 Apply：Movement+Behavior Production 不创建 Runner，bootstrap 仅发布空全局 MovementControl 与静态实体 Profile，普通帧只发 Intent 增量；9703 完成全部 Friendly 业务/生命周期门，`direct_worker_apply=1`、双端 hash=`3180435972084878253`、硬失败 0。
- [ ] [INFERRED][HIGH] WA8 下一结构项：让 Mixed/Friendly Shadow/Canary parity 脱离 Runner/WorkGraph transaction shell，删除失去消费者的公共 Legacy API 和测试，再删除 Round 四阶段/Poll shell。

[COMPUTED][HIGH] 9701 更新：full-Worker Mixed Production 已绕过 Legacy Runner/WorkGraph，使用 sparse MovementControl v8 + per-entity profile 和 Worker dirty Facing 原子 Apply；600 Tick PASS，Impact/Damage/Death=`66/66/13`、duplicate/stale=`0/0`、min separation=`70.04cm`、p95=`17.641ms`。

[COMPUTED][HIGH] Mixed 当前没有 TargetControl cohort/flow，因此 LocalPredictive 在该 sparse control 中保持关闭；Worker Movement/Particle 与 GT safety commit 仍启用。WA8 仍因 Friendly Runner、Round 四阶段 shell 与 fallback/public Legacy API 存在而不勾选。

[COMPUTED][HIGH] WA8 增量更新：full-Worker Mixed 9689 已在 Movement/Behavior/Target/Particle/Projectile/Combat 全 Production 下完成 600 Tick，业务计数、projectile 守恒、零 duplicate、零 stale reject 和 p95 门均通过；Production combat/projectile apply 不再被 Legacy parity 否决，指标也改由 Worker output 累计。

[INFERRED][HIGH] WA8 仍不得勾选完成：Mixed Production 尚会执行 Legacy combat/projectile prepare，Friendly/Mixed Legacy Runner 与 Round Boundary shell 尚未物理删除。

[COMPUTED][HIGH] 9690 更新：Mixed Production 的 Legacy projectile/impact/health prepare 已删除，600 Tick 回归 PASS；尚存 GT Attack Planner（Behavior MovementLock/标签消费者）、Friendly/Mixed Legacy Runner 与 Round Boundary shell，因此 WA8 仍不勾选。

[COMPUTED][HIGH] 9691 更新：Combat state v2 已承载 Commit movement lock，Mixed Production GT Attack Planner 已从热路径删除，600 Tick 再次 PASS。WA8 仍因 Friendly/Mixed Boundary Runner/WorkGraph 与 Round transaction shell 存在而不勾选。

## 2026-08-02 WA8 T8 realtime closure

- [x] [COMPUTED][HIGH] Production Movement/Particle/Facing tail直接从完整验证后的Worker输出构造原子Commit Plan；Shadow/Canary对照路径不变。
- [x] [COMPUTED][HIGH] 普通全Worker T8的Clock Intent在Legacy domain staging前提交；同Generation、同Plan、bootstrap完成、无Target且五域Production门均有结构断言。
- [x] [COMPUTED][HIGH] 9687 Round 1/2性能通过：p95=`33.999/33.981ms`、realtime=`0.998/1.000`、pending=`902/901`。
- [x] [COMPUTED][HIGH] 两轮900 Tick业务闭合；Round 1为50次spawn/impact/damage、duplicate=0、hash=`439379904/1411313634/6141440`，零Violation/Rejected。
- [x] [COMPUTED][HIGH] Development Editor DisableUnity与`CrowdDemo.Architecture.PostFinalizeMinimalQuery`通过。
- [ ] [INFERRED][HIGH] 下一门：全Worker Mixed 600 Tick完整业务覆盖；WA8 Legacy shell仍不得标记删除完成。

## 2026-08-02 WA8 Round transition closure

- [x] [COMPUTED][HIGH] ProjectileControl fresh Revision 可显式替换状态并从 FixedStep 0 开始新 Round；同 Revision CombatClock 仍只做 autonomous continuation。
- [x] [COMPUTED][HIGH] MovementControl 完整 Resource replan 稳定覆盖同轮 anchored TimeWheel continuation，不重复规划。
- [x] [COMPUTED][HIGH] PlanRevision 变化发送一次 input-owned InputSnapshot baseline；普通 Tick 不发送实体状态。
- [x] [COMPUTED][HIGH] 9685 在同一 Generation、`resnapshots=1` 下从 Round 1 的 900 Tick 进入 Round 2，并继续到 step 300，无 Domain failure。
- [x] [COMPUTED][HIGH] 该历史性能缺口已由9686/9687关闭；最新Round 1 p95=`33.999ms`、realtime=`0.998`、boundary pending=`902`。
- [ ] [INFERRED][HIGH] WA8仍不得关闭；下一项是Mixed完整业务门，不是直接宣告Legacy删除完成。

## 2026-08-02 WA8 T8 closure update

- [x] [COMPUTED][HIGH] Round autonomous Behavior expectation 不再为无消费者路径累计 MatchedEventBatch；容量专项通过。
- [x] [COMPUTED][HIGH] Combat/HitFlash 使用 canonical SimulationTick 时钟，900 Tick Worker/Legacy Combat payload 一致。
- [x] [COMPUTED][HIGH] Round 末尾不再提交重复 Clock Tick；9680 终局 50/50/50、duplicate=0，ProjectileControl `published=1/reused=899`。
- [ ] [COMPUTED][HIGH] 性能未通过：fixed-step p95=`67.871ms`、realtime=`0.500`。
- [x] [COMPUTED][HIGH] 该9680历史失败已由9685关闭：同 World Round 2 已继续到step 300。
- [ ] [INFERRED][HIGH] WA8 不得关闭，Legacy shell 不得删除。

## 2026-08-02 WA8 Projectile clock slice

- [COMPUTED][HIGH] PASS：不更新 ProjectileControl revision 的 CombatClock 可连续推进 Projectile/Combat executor。
- [COMPUTED][HIGH] PASS：Projectile 语义 hash 排除 tick/time、动态 position/velocity/health/attack state 与当前 projectile mirror。
- [COMPUTED][HIGH] PASS：Mixed 等待/校验区分 Worker StateRevision 与配置 ControlRevision。
- [COMPUTED][HIGH] PASS：Shadow/Canary 保留逐 Tick HostInput；9664 Mixed Behavior+Combat Production 兼容门在 step 600 PASS。
- [x] [COMPUTED][HIGH] RESOLVED：全 Worker T8 step 9 Behavior Authority event-batch容量问题已修复；9680完成900 Tick业务终局。
- [COMPUTED][HIGH] BLOCKED：全 Worker Mixed 到 step 2106 仍未满足 death/target-switch/impact 业务覆盖。
- [INFERRED][HIGH] WA8 不得关闭，Legacy Runner/四阶段不得开始删除。

## WA8 Mixed ordinary intent checkpoint (2026-08-02)

- [x] [COMPUTED][HIGH] Mixed bootstrap保留一次完整`BoundarySnapshot`；普通Worker帧改用`SubmitIntentBatch`，不把普通帧Snapshot传入Worker输入合同。
- [x] [COMPUTED][HIGH] Mixed生产生命周期操作通过有界Coordinator journal提交显式Despawn、Spawn与Movement Profile Revision；Runtime接受后才清空journal。
- [x] [COMPUTED][HIGH] Worker Despawn `ReasonId`与零基业务枚举解耦并映射为1-based非零值；9651在step 226接受首次Despawn、step 271接受Spawn/Profile Revision。
- [x] [COMPUTED][HIGH] Mixed Production提交Worker-owned Behavior输出，不要求异步Worker运动学或Source输出Hash等于GT Prepared镜像；生命周期、输入水位、fixed step、事件和事务token仍fail-closed验证。
- [x] [COMPUTED][HIGH] 9651真实Production服务端运行到step 600 PASS，spawn/despawn=`1/1`、stale reject=`0`、Worker普通Intent submitted=`600`且零`VIOLATION`；Development Editor DisableUnity、Architecture 1/1与`WorkerAuthoritativeSparse` 1/1通过。

## WA8 Friendly Behavior incremental checkpoint (2026-08-02)

- [x] [COMPUTED][HIGH] Added ordered Spawn, Despawn, and typed Movement Profile Revision input adapters with stable sequence assignment.
- [x] [COMPUTED][HIGH] Added per-entity MovementProfile codec/store field and shared live-profile resolution for MovementPlanning, Movement, and Particle.
- [x] [COMPUTED][HIGH] Round bootstrap publishes profiles once; ordinary intent frames do not encode the complete MovementControl profile.
- [x] [COMPUTED][HIGH] Same-batch LifecycleSerial reuse plus Profile Revision is covered by RuntimeV2 automation.
- [x] [COMPUTED][HIGH] Sparse Movement correction invalidates derived continuation and passes the formal dual-PIE recovery gate.
- [x] [COMPUTED][HIGH] Friendly Logistics real Mass recycle now appends a bounded Despawn -> Spawn -> Movement Profile Revision journal from the production entity-replacement site; Worker Input Sync drains and acknowledges it only after Runtime accepts the intent batch.
- [x] [COMPUTED][HIGH] Friendly Worker bootstrap uses one BoundarySnapshot, while ordinary Worker frames use the intent-only entry and publish an ordered reliable lifecycle Spawn before records for the replacement lifecycle.
- [x] [COMPUTED][HIGH] Friendly ordinary intents no longer carry complete per-entity Behavior contexts. Clock intents schedule local Behavior evaluation from Worker Movement; only contexts containing typed external records enter the intent batch, and sparse parity validates only the affected entities.
- [x] [COMPUTED][HIGH] Friendly Production commits Worker-owned Source/Resolved/Business output after validating lifecycle, input sequence, fixed step, ordered events, and the unchanged GT transaction token. The dedicated `WorkerAuthoritativeSparse` test locks sparse matching and rejects duplicate authoritative commits.
- [x] [COMPUTED][HIGH] Friendly server run 9644 and real server+client run 9645 passed the 40-second Claim/Pickup/recycle/Fallback/Deliver gate. Run 9645 closed the previous external-client backlog failure.
- [ ] [INFERRED][HIGH] Delete the Legacy Round BoundarySnapshot/four-stage transaction only after the remaining Target/Projectile inputs are independent deltas and Friendly/Mixed no longer consume the Legacy Runner.

[INFERRED][HIGH] 本表只在生产调用链和对应专项门同时满足时勾选。接口、Codec或测试夹具存在但未接入生产，不得标为完整通过；Behavior Source现行状态以`EntityBehaviorSourceArchitecture.md`为准。

[COMPUTED][HIGH] T9 Mixed Combat Integration、DP0–DP6、PJ0–PJ6、S0–S6和R0–R7均已关闭。pre-T9提交`5b947389`只作为历史恢复基线；当前未关闭的游戏循环阶段是T10，另有独立性能架构阶段AB1–AB6尚未关闭。

## WA0–WA9 全面Worker权威检查表

- [x] [COMPUTED][HIGH] WA0：全面Worker终态、Domain DAG、插件/项目边界、逐字段Ownership Matrix、Legacy四节点迁移地位和WA0–WA9顺序已冻结。
- [x] [COMPUTED][HIGH] WA1：Work Ring、Time Wheel、Dependency Index、Dirty/Resource/Ordered Event Store、Checkpoint、Domain Registry、非阻塞短任务Shard Host、跨Poll续跑、固定Domain屏障、稳定Merge、Dependency漏标审计、Correction/Generation失效、in-flight teardown及传播预算延期已实现；四构建、RuntimeV2 11/11、MassCrowd 96/96通过，并修复ForceUnity私有辅助符号冲突。Synthetic Shadow默认关闭，未改变Production Writer。
- [x] [COMPUTED][HIGH] WA2：版本化Nav/Flow/Environment与MovementControl Resource、Epoch边界交换、Time Wheel自主Movement、Worker Local Predictive/静态环境约束、稳定Domain执行Rank、Planning覆盖到期Movement去重、Shadow→封闭Canary→Production Writer切换均已实现。Development Editor `-DisableUnity`、RuntimeV2 19/19、20实体Obstacle Canary、Obstacle Production和SoftPressure Production通过；Production Movement状态从Worker Store读取，GT只提供版本化Guidance/Resource和迁移期代理应用。
- [x] [COMPUTED][HIGH] WA3：Worker执行完整闭合Particle集合、唯一Pair审计、稳定双向约束、字段级NeedsRecompute传播与OutputDirty发布；Particle后最终状态成为下一Epoch Movement基线。独立Shadow/封闭Canary/Production Writer门已接入，Legacy Particle仅保留诊断/Canary对照，不再决定Production提交。Development Editor `-DisableUnity`、RuntimeV2 19/19、20实体SoftPressure Canary与Production均无硬失败。
- [x] [COMPUTED][HIGH] WA4：Worker Target Executor持有Cohort Membership、Demand、Plan、Quota execution、Target Revision与Flow/Target资源订阅；Topology/Plan按Revision缓存，OutputDirty只发布变化代理。Shadow/封闭Canary/Production门已接入，Production停止Legacy Target资源发布并以Worker Target proxy驱动Guidance，同时保持BusinessOverride优先级。RuntimeV2 20/20、Development Editor DisableUnity、9424/9425/9431通过；9431到step 300为20/20 verified且无Violation。
- [x] [COMPUTED][HIGH] WA5：Projectile/CombatReactive Domain与全局闭合集Writer、终态不回灌、ordered lifecycle/hit event已实现；Round T8及Mixed T9的Attack/Cooldown、Damage/Death/Hit React和同Epoch Combat→Movement均已切为Worker Commit Writer。9451/9452/9453完成T8 Shadow/闭合Canary/Production到step 300；9467/9468/9469完成T9 Shadow/闭合Canary/Production到step 600，三次业务计数和entity/membership/commit hash一致。Demo Combat状态、active Projectile checkpoint和event baseline已通过新Executor下一步逐字重放；Codec专项与RuntimeV2 21/21通过。
- [x] [COMPUTED][HIGH] WA6：Lifecycle与Behavior Worker权威已关闭；覆盖同批StableEntityRef复用、LifecycleSerial失效、Capability Binding、Source/Command幂等、Business Commit Ledger、Worker prepared commit及Behavior→Movement同Epoch消费。RuntimeV2 24/24，T6M 9531、Friendly 9532、Mixed 9533 Production正式门通过。
- [x] [COMPUTED][HIGH] WA7-R：有序Intent、Digest、稀疏Correction、Checkpoint、Late Join顺序门和300 Tick双PIE无纠错/单Scope恢复门已通过。
- [ ] [COMPUTED][HIGH] WA8：Round、Friendly与Mixed普通Worker帧均已切到Intent入口；完整MovementControl Profile已冻结，同Plan Mass与Mixed Coordinator的Spawn/Despawn/Profile journal已接通，Friendly/Mixed Behavior提交均消费Worker-owned输出。仍待Target/Projectile增量化、完整Legacy Mass Snapshot/Runner及四节点Boundary/旧Mailbox物理删除。
- [ ] [INFERRED][HIGH] WA9：完整自动化、构建、场景、规模、网络、视觉与性能关闭门。

## PW0–PW8 持久Worker Simulation Runtime检查表

- [x] [INFERRED][HIGH] PW0：目标设计已冻结Worker权威镜像、单向输入/输出、可变Published Batch、Processor边界、Simulation Time、混合Consistency Domain和实施顺序；生产代码尚未迁移。
- [x] [COMPUTED][HIGH] PW1：通用Schema Input/Result合同、Generation/Sequence门、显式Limits、Building/Published/Consuming三缓冲、State latest-wins、有界有序Event和GT单帧一次交换已实现；未接入Demo生产。Development/DebugGame Editor `-DisableUnity`、定向7/7、MassCrowd 72/72与CrowdDemo 134/134通过。
- [x] [COMPUTED][HIGH] PW2：每World唯一Worker Runtime宿主、SoA Mirror、固定Simulation Clock、单Owner短Pump、显式队列边界、Invalidate/Stop/Resnapshot已实现；不访问Mass/World/UObject且不写生产结果。Development/DebugGame Editor `-DisableUnity`、定向10/10、MassCrowd 75/75与CrowdDemo 134/134通过。
- [x] [COMPUTED][HIGH] PW3：Round/Mixed/Friendly已接入首次全量、后续Lifecycle/Dirty State/Resource/已提交Command增量；按Input Sequence比较Entity/Lifecycle、State与源Snapshot元数据Hash且不写Mass。Mixed实际300批含165条Command，Mixed/Friendly各连续600批保持`pending=0`、`superseded=0`、无Violation；Development/DebugGame `-DisableUnity`、定向2/2、MassCrowd 77/77与CrowdDemo 134/134通过。
- [x] [COMPUTED][HIGH] PW4：SharedFlow/Facing/Business短Task Shard、每World Runtime异步Shadow Scheduler和生产逐批比较已接入；Task可乱序完成但结果按全局提交序交付，Shard大小1–64轮换且正反派发交替。9111 step 300累计900/900完成、in-flight=0、mismatch=0；Development/DebugGame `-DisableUnity`、MassCrowd 78/78、CrowdDemo 134/134通过。
- [x] [COMPUTED][HIGH] PW5：Worker Owner可发布空或可变State Batch；GT Result Apply Processor每帧一次交换并只写Presentation/诊断代理，验证Owner Mask、Lifecycle、Publish/Event Sequence和Hash。0/1/10/9999及Exchange并发门、ResultApply 2/2、9112生产20 Patch零stale通过；Development/DebugGame `-DisableUnity`、MassCrowd 80/80、CrowdDemo 134/134通过。
- [x] [COMPUTED][HIGH] PW6：Movement Position/Velocity/Facing字段Owner、普通输入Echo拒绝、Correction Revision、Client插值及Shadow→Canary→Production切换已关闭；Production最终Mass代理Writer消费Runtime尾链而非旧Boundary尾链。9118/9116/9117/9119覆盖Shadow、5实体Canary、Obstacle/SoftPressure Production；Development/DebugGame `-DisableUnity`、Movement 2/2、MassCrowd 82/82、CrowdDemo 134/134通过。
- [x] [COMPUTED][HIGH] PW7：Particle/Target/Combat强一致Domain已接入显式Evidence与fail-closed迁移判定；定向自动化1/1及9121真实Round检查点通过，当前三类Domain均因缺少专项证明继续保留Boundary。
- [x] [COMPUTED][HIGH] PW8：Production Movement单Owner、跨Round绝对Simulation Time、rollback-safe Dynamic Flow Hash、1k/2k/5k/10k持续扫描、单进程双PIE、Correction/teardown、T1–T9、Mixed/Friendly/Continuous及T7录屏/FFmpeg连续性门已关闭。最终规模step 300的接受状态数为`301000/602000/1505000/3010000`且队列均为0；10k Worker lag=`9.677ms`，但完整强一致Demo Boundary约145ms/step，不标记为10k实时游戏门。Development/DebugGame `-DisableUnity`、MassCrowd 83/83与CrowdDemo 135/135通过。

- [ ] [COMPUTED][HIGH] 2026-07-30单主体ISM受击闪色：实现与主体表现门已完成。项目VAT父材质读取PICD slot 2，旧HitFlash MI/ISM和双份Add/Update/Remove已删除；0/1/10/9999与SwapRemove自动化、Development/DebugGame `-DisableUnity`、MassCrowd 83/83、CrowdDemo 137/137、T8、Continuous、Friendly、T7逐帧白闪及1k/2k/5k/10k服务端吞吐门通过。最终关闭仍被Mixed 500现有PW输入序列问题阻塞：9207与9213都在fixed-step 93收到`ECrowdWorkerShadowSubmitResult::RequiresResnapshot`，本切片按边界不修改PW Runtime。

## AB0–AB6 异步Fixed-Step Boundary检查表

- [x] [INFERRED][HIGH] AB0：详细架构文档已区分GT Processor、非GT Mass Work Processor与Boundary Work Stage，并冻结Thread Pool DAG、Mailbox、Request/Result、事务身份和完成定义。
- [x] [COMPUTED][HIGH] AB1：Runtime阻塞等待接口已删除；`PollAndDrain()`、Task queue/run/critical-path和`ordinary_block_wait_count`已接入。
- [x] [COMPUTED][HIGH] AB2：Runner即每World深度1 Mailbox，事务身份与Generation失效合同已实现。
- [x] [COMPUTED][HIGH] AB3：跨Game Frame Result消费和Request生产代码已接入；8822 T5S功能、稳定性和性能门通过。
- [x] [COMPUTED][HIGH] AB4：T5M/T6A/T6S/T6M推广、Worker Topology缓存与共享Flow lookup已完成；四图独立双端门通过。
- [ ] [INFERRED][HIGH] AB5：已重新定义为全面Worker权威门；四节点与1/0/1 Boundary证据降为Legacy基线。只有WA8删除Legacy Boundary且WA9新证据通过后关闭。
- [ ] [INFERRED][HIGH] AB6：必须在全面Worker新路径上重跑双PIE、强制Correction、teardown/地图切换、完成顺序、全场景和FFmpeg；不复用PW8/四节点旧日志。

## T9 Mixed Combat Integration检查表

- [x] [COMPUTED][HIGH] 公共业务合同包含MixedCombat Scenario/Planner/Attack Action、Melee/MidRange/Ranged Payload与Profile稳定ID，以及统一五阶段攻击状态机。
- [x] [COMPUTED][HIGH] `AttackTarget`生产Source已删除；三类攻击由Intent驱动，Commit步MovementLock只持续一帧。
- [x] [COMPUTED][HIGH] Melee Moving Sphere、MidRange Capsule等价Sweep和Ranged Mass Projectile共享Spatial索引、通用Impact/Hit与Combat Resolver出口。
- [x] [COMPUTED][HIGH] 死亡、目标失效、下一Boundary重选、TargetRegion/Flow缓存重建和死亡实体不再被引用/即时Sweep命中均已接入。
- [x] [COMPUTED][HIGH] v2 Mixed Agent可靠Payload包含Profile、Target、Phase、PhaseEnter、CooldownEnd和FireSequence；旧Payload精确拒绝，late join恢复攻击状态。
- [x] [COMPUTED][HIGH] 8517双端20实体门通过三类攻击、9次死亡、227次目标切换/区域计划重建、零死亡引用、零重复Projectile、Hash一致和`2.910ms` p95。
- [x] [COMPUTED][HIGH] 完整自动化为MassCrowd 64/64、CrowdDemo 133/133；Registry黄金值为`11335697795273479593`。
- [x] [COMPUTED][HIGH] T8双端版本化黄金门与Development/DebugGame × ForceUnity/DisableUnity四构建均通过。

## DP0–DP6 Demo业务规划检查表

- [x] [COMPUTED][HIGH] DP0：冻结`07359ed`代码、ID、角色比例、业务Hash、65/65、125/125、四构建及20/100/500基线。
- [x] [COMPUTED][HIGH] DP1：独立`MassCrowdDemoBusiness`模块、Planner Registry/Snapshot/Decision/Writer/Runner和结构边界已实现。
- [x] [COMPUTED][HIGH] DP2：Logistics/PursueAttack/GuardFlee/Roam/Escort及Reaction Planner、稳定角色分配和反序等价已实现。
- [x] [COMPUTED][HIGH] DP3：Provider、Diff、Ledger、Combat/RangedAttack规划和Prepared Adapter已迁移，Runtime领域API已删除。
- [x] [COMPUTED][HIGH] DP4：Mixed/Friendly/T7/T8生产迁移完成，T1–T6/Continuous使用NoBusiness，旧双路径已删除。
- [x] [COMPUTED][HIGH] DP5：共享Planning Host、Source状态发布、零写入与模块依赖结构门已通过。
- [x] [COMPUTED][HIGH] DP6：MassCrowd 64/64、CrowdDemo 131/131、四构建、全部真实入口及Mixed 20/100/500规模门通过。

## PJ0–PJ6 Projectile模块化关闭检查表

- [x] [COMPUTED][HIGH] PJ0：当前文档与历史快照已分离，Projectile当前数组事务、模块所有权和最新测试数据无冲突描述。
- [x] [COMPUTED][HIGH] PJ1：`MassCrowdSpatial`、`MassCrowdCombat`、`MassCrowdProjectiles`模块和单向依赖已建立；公共模块无Demo引用，Runtime不反向依赖Projectiles。
- [x] [COMPUTED][HIGH] PJ2：Movement SpatialSafety与Projectile稳定Body/Grid/相对Sweep/环境Sweep已统一迁入Spatial模块；候选NavLayer随安全结果原子提交。
- [x] [COMPUTED][HIGH] PJ3：Impact/Hit、Effect Profile、纯Resolver及Prepared Host Commit已迁入Combat模块。
- [x] [COMPUTED][HIGH] PJ4：Projectile Mass Fragment/Trait、动态Mass实体池、Boundary WORK与Prepared Patch已迁入Projectiles模块；Mass Fragment保持唯一持久权威。
- [x] [COMPUTED][HIGH] PJ5：Demo只保留攻击业务、伤害/VAT/视觉映射和清晰Adapter；旧Demo Kernel/Fragment/Store及重复Round入口已删除。
- [x] [COMPUTED][HIGH] PJ6：三模块专项、结构门、T8/R7、`MassCrowd 65/65`、`CrowdDemo 125/125`、四构建及20/100/500并发4/20/100 Projectile双端门通过；服务端p95=`2.152/9.675/30.016ms`，最小间距=`70.11/70.03/70.00cm`。

## S0–S6 Standard Sources当前检查表

- [x] [COMPUTED][HIGH] S0：RootMotionSource式扩展类比、StandardSources模块边界、通用/产品Source归属、Context/State、组合和验收边界已经写入事实源。
- [x] [COMPUTED][HIGH] S1：Mixed已消费Resolved Movement/Facing/Constraint并执行Movement Pipeline、Particle与Facing Finalize；Business与Movement不再隐式耦合，InteractionLayer隔离跨层邻居，Prepared Final Apply保持失败零写入。Development `-DisableUnity`、Core 13/13、Mixed定向3/3及20实体服务端/双端真实门通过。
- [x] [COMPUTED][HIGH] S2：`MassCrowdStandardSources`模块、稳定Provider/TypeId、公共TargetKinematics/FormationAnchor Context、Registry Hash和单向模块边界已实现。
- [x] [COMPUTED][HIGH] S3：MoveTo/Arrive/Follow/Pursue/Flee/MaintainDistance/Facing/Constraint第一批自主Evaluator及Payload/Context/State专项已实现。
- [x] [COMPUTED][HIGH] S4：Demo已迁移为五Controller直接维护稳定Source集合Diff；产品Provider只保留SharedFlow、Cargo、Pickup/Deliver/Attack，生产Movement/Facing不扫描SourceSet或TypeId。
- [x] [COMPUTED][HIGH] S5：确定性Wander、FormationOffset、TimedImpulse、Escort、Pursue+Attack、一帧显式Lock和HitReaction精确恢复门已实现。
- [x] [COMPUTED][HIGH] S6：StandardSources 8/8、Mixed组合5/5、第三方Fixture与20/100/500同路径双端late join继续保持关闭；PJ6最终完整回归更新为MassCrowd 65/65、CrowdDemo 125/125和四构建全通过。

## R0–R7 基础框架历史关闭表

- [x] [COMPUTED][HIGH] R0：旧B0–B7降级为历史证据，现行事实源、阶段顺序和验收口径已对齐。
- [x] [COMPUTED][HIGH] R1：公共Provider/Context/State/Registry Hash与独立第三方Fixture插件已通过Public API、冻结冲突、持久状态、六通道和网络回放专项。
- [x] [COMPUTED][HIGH] R2：领域Source已迁到Demo Provider，控制器使用持久集合Diff，Mixed生产移动消费Resolved Movement；该历史框架门不证明完整Movement Pipeline或通用Source库。
- [x] [COMPUTED][HIGH] R3：通用Task DAG、稳定Patch Adapter和Round/Mixed预验证后不可失败Final Apply已实现，失败零写入有自动化证据。
- [x] [COMPUTED][HIGH] R4：Behavior网络v3已接入生产可靠状态、late join与resync，Registry/Schema/State进入校验。
- [x] [COMPUTED][HIGH] R5：Mass Fragment已成为Projectile唯一权威；网格Broadphase、相对/环境Sweep、通用Impact/Hit、阵营/NavLayer、Pierce及旧数组路径删除已完成。
- [x] [COMPUTED][HIGH] R6：StateTree Adapter已拆为默认禁用兄弟插件；主插件无StateTree依赖构建与独立Smoke通过。
- [x] [COMPUTED][HIGH] R7：同路径20/100/500、20实体第三方Fixture + 持久Movement/Cargo/Business Source + 临时HitReaction压制/恢复 + 移动安全阶段 + 10发并发Mass Projectile组合门、Development/DebugGame、Unity/DisableUnity及完整自动化均已通过；组合门验证Mass Fragment权威、Broadphase/Sweep 10/10精确命中和恢复后持久Source 20/20不丢失。

## 核心模拟与架构

- [x] [COMPUTED][HIGH] `MassCrowdSimulation`插件阶段1骨架与五模块单向依赖建立。
- [x] [COMPUTED][HIGH] Core公共API无`CrowdDemo`命名，插件边界源码扫描1/1通过。
- [x] [COMPUTED][HIGH] Shared Flow已提取到Core并接入Runtime生产WORK；Runtime定义的权威resource覆盖静态场、动态anchor和T3双cohort，当前由Demo Pipeline托管，Demo field/sample仅为迁移期镜像。SF1 golden hash=`267519150`。
- [x] [COMPUTED][HIGH] Target Region Transport已提取到Core并接入Runtime生产WORK，旧/Core/Runtime全链fixture覆盖Topology、Demand、Plan、quota execution、Guidance与claim replacement。
- [x] [COMPUTED][HIGH] Guidance Compose已提取到Core，旧/Core fixture覆盖provider优先级、乱序、stale revision、Stop fallback、量化与hash。
- [x] [COMPUTED][HIGH] Local Predictive及Velocity Half-Plane已提取到Core，旧/Core fixture覆盖真实8518六实体联合恢复、pair、grant、result与component hash。
- [x] [COMPUTED][HIGH] Particle Safety已提取到Core，8372完整20实体fixture覆盖Soft、Environment、UnifiedHard、Quantized、FinalSafety、candidate及applied几何hash。
- [x] [COMPUTED][HIGH] Facing已提取到Core，旧/Core fixture覆盖自主朝向、最终落位后朝目标、转速限制、保持Yaw、角度跨界、输入乱序及稳定hash。
- [x] [COMPUTED][HIGH] Runtime Base Movement trait及identity/state/properties/facing/output持久 fragments 已建立；Guidance、local velocity 与 Particle 中间结果使用 prepared POD。Gather按Capability稳定分批，Merge按AgentId唯一化，Commit先全量验证Lifecycle再允许写回。
- [x] [COMPUTED][HIGH] Demo template只保留Runtime Base Movement fragments作为中间运动权威；正式Guidance Compose由Runtime WORK执行Core kernel并写Runtime composed，旧Demo MoveIntent/GuidanceCandidates/ComposedGuidance fragments已删除。
- [x] [COMPUTED][HIGH] 正式Local Predictive由Runtime WORK消费prepared composed并执行Core kernel；Runtime local-velocity与同源prepared SoA一次发布，旧Demo local-velocity fragment及Demo kernel生产调用均已删除。
- [x] [COMPUTED][HIGH] 正式Particle Safety由Runtime WORK执行Core Solve及applied-state安全复验；Runtime particle结果与同源prepared SoA一次发布，旧Demo particle fragment已删除，MovementFinalize仍是RoundSim唯一写入点。
- [x] [COMPUTED][HIGH] 正式Facing由Runtime WORK调用Core kernel；完整AgentId/result集合校验后发布Runtime Facing及精确rollback fact，旧Demo facing fragment已删除，MovementFinalize只消费Runtime Facing。
- [x] [COMPUTED][HIGH] 最终Movement由Runtime WORK生成并经稳定Merge形成唯一Commit plan；完整AgentId/Lifecycle集合通过后才同步写Runtime/Demo状态，Authority/Client Commit只消费Runtime MovementOutput。

- [x] [COMPUTED][HIGH] 顶层parser只接受0/1及`SimRoundObstacle/SimRoundSoftPressure`。
- [x] [COMPUTED][HIGH] TargetApproach、TargetSlotLayout和旧Polar Density生产引用已删除。
- [x] [COMPUTED][HIGH] Flow、Target Region和Business输出独立candidate；唯一Guidance Compose写自主速度。
- [x] [COMPUTED][HIGH] Local Predictive与Particle不反向改写自主向量或Facing。
- [x] [COMPUTED][HIGH] Rollback使用不可变资源引用与可变执行态，correction仍只在fixed boundary应用。
- [x] [COMPUTED][HIGH] Compose、Local Predictive、MovementPredict、Particle和Facing已接入统一snapshot/prepared POD WORK输入链。
- [x] [COMPUTED][HIGH] P1生产代码已收敛为一次gather/dispatch/wait/writer，Business/Combat、按Cohort Target、SharedFlow、Obstacle、Movement、Particle与Facing均由typed Worker DAG输出；8132/8137/8138/8139当前版本T2/T6/T7/T8双端复测通过。
- [x] [COMPUTED][HIGH] Authority Mass archetype已按Base/Target/Combat bitset拆成四种精确template，Projectile继续使用独立archetype；Round与Replication Query对Combat bundle采用Optional并生成确定性默认事实。capability结构3/3和Base+Target真实功能已通过；其backlog性能回退归AB5继续处理。

## 生产运行与复制（与 Demo 验收分开）

- [x] [COMPUTED][HIGH] 通用运行、行为组合、持续生命周期和生产复制合同已冻结在`MassCrowdUnifiedRuntimeAndReplicationContract.md`。
- [x] [COMPUTED][HIGH] `FCrowdStableEntityRef`、Capability Profile/Modifier、Source Handle/Spec/Command/Instance、六通道 Contribution 与组合式 AgentFacts POD 已在`MassCrowdCore`实现；Fragment不再保存权威ActiveBehavior，Faction不决定Capability。
- [x] [COMPUTED][HIGH] `MassCrowdNetworking`已实现通用 Relevant Snapshot、lifecycle batches、owner-only replication actor、late-join baseline、可靠状态序列、latest-wins correction、空间RelevantSet与resync重建。
- [x] [COMPUTED][HIGH] `MassCrowdPresentation`已实现按StableEntityRef管理的slot table、swap-remove反向表、幂等spawn/update/despawn、stale tombstone、Profile与可选Cargo引用；J和ContinuousLifecycle已通过Demo ISM sink消费该公共层。
- [x] [COMPUTED][HIGH] 阶段F已在插件最小World实现真实Mass entity创建/销毁、boundary apply、LifecycleSerial槽位复用、membership迁移与stale correction/despawn拒绝。
- [x] [COMPUTED][HIGH] Demo continuous lifecycle 与J均使用公共owner-only replication channel和Presentation subsystem；Continuous 7975延迟加入追平可靠生命周期序列，J 7977 step600双端hash与active/visible通过。
- [x] [COMPUTED][HIGH] Runtime Behavior Source六通道、开放Provider、持久状态和Resolver已实现；Provider选择API与中心`CanActivate`已删除，Mixed Movement/Facing消费Resolved Channels。
- [x] [COMPUTED][HIGH] Commit Envelope与Snapshot/Lifecycle/Apply协议、旧版拒绝和网络单字节Behavior删除已完成；Source Command/SourceSet Codec v3接入生产发送/接收、late join与resync。
- [x] [COMPUTED][HIGH] `MassCrowdStateTreeAdapter`已拆为默认禁用兄弟插件，只依赖Runtime并提交Source Command；真实业务Task不属于现行框架验收。
- [x] [COMPUTED][HIGH] Core稳定分层Surface Graph与Runtime静态Recast提取器已实现；Shared Flow按NavLayer构建integration/direction，支持动态目标重新attachment而不改变拓扑，真实高低差地图已验证坡道、桥上桥下、高台、多路线、窄桥与不可通行落差。
- [x] [COMPUTED][HIGH] Mixed Sandbox在独立非Round入口组合continuous lifecycle、Source World Store、Cargo、Combat、Recast Shared Flow和增量ISM；Movement/Facing消费Resolver结果，且同一路径20/100/500通过。
- [x] [COMPUTED][HIGH] Demo固定Round初态已通过显式版本化adapter进入通用Relevant Snapshot；完整`RoundBootstrapPacket.Agents`不再作为复制属性，本地packet只作为既有Pipeline消费对象。
- [ ] [COMPUTED][HIGH] T1 OpenSpawn 只切换既存实体的 Particle 参与状态和 staging 布局，不创建或销毁 Mass Agent，不能计为生产 spawn/despawn 通过。
- [x] [COMPUTED][HIGH] 插件 Source 当前未检出 Enemy/Friendly/Faction 运动或复制特判；provider-neutral 边界保持。

## Behavior Source B0–B7 历史执行快照（不表示当前状态）

[COMPUTED][HIGH] 下列勾选状态冻结自R0重构前的旧B0–B7审计，用来保留“当时为何废止旧计划”的证据；它们不与顶部S0–S6及R0–R7当前关闭表合并，也不得被解释为当前缺口。

- [COMPUTED][HIGH] 旧B0快照：可恢复检查点当时已完成。
- [COMPUTED][HIGH] 旧B1快照：Core稳定ID、Capability Profile/Modifier、Source POD、容量、命令状态机和Stable Hash主体当时已完成。
- [COMPUTED][HIGH] 旧B1快照：Fragment收口在该时点未完成；该断言不描述当前S0–S6状态。
- [COMPUTED][HIGH] 旧B2快照：Presentation Additive及完整Blend专项在该时点缺失；后续由R/S阶段替代验收。
- [COMPUTED][HIGH] 旧B3快照：Mixed在该时点尚未消费Resolved Movement/Facing；后续由S1/S4替代验收。
- [COMPUTED][HIGH] 旧B4快照：Mixed在该时点仍使用补偿回滚；后续由R3/S1替代验收。
- [COMPUTED][HIGH] 旧B5快照：生产迁移在该时点未完成；后续由S1–S6替代验收。
- [COMPUTED][HIGH] 旧B6快照：真实StateTree业务Task未执行；该项目后来明确移出当前框架验收门。
- [COMPUTED][HIGH] 旧B7快照：网络与规模门在该时点未完成；后续由R4/R7/S6替代验收。单进程DebugGame PIE和人工审片没有被冒充为本轮门。

## P0–P5 产品化闭环

- [x] [COMPUTED][HIGH] P0已关闭“A–J历史完成”与“J尚未组合”的文档冲突，并冻结GT/WORK、静态Recast Nav V1、物流混合边界、late join/relevancy和模块依赖方向；P0未修改源码、配置、地图或资产。
- [x] [COMPUTED][HIGH] P1公共Orchestrator与完整Worker DAG已接入Round；T6首次门发现并修复旧同步验证在Wait前读取空集合的问题，随后T2/T6/T7/T8全部通过。
- [x] [COMPUTED][HIGH] P2 Runtime Nav provider、Graph resource、revision/hash、Flow handle/refcount/有界LRU及`NavFlowProductSmall`已通过真实20实体垂直Recast双端门。
- [x] [COMPUTED][HIGH] P3 owner-only channel、late-join baseline、可靠state、latest-wins correction、空间相关集、resync与Presentation实例生命周期已实现并通过低层及真实双端验证。
- [x] [COMPUTED][HIGH] P4公共事务Store与专用`CrowdDemo_FriendlyLogisticsSmall`地图真实通过20实体、late join、竞争、守恒、幂等、死亡恢复、fallback、退避和取消；8154客户端Cargo attach/detach=`2/2`、实例=`20`，携货/交付近景证据已保存。
- [x] [COMPUTED][HIGH] P5已移除J私有Graph/Flow cache、完整MixedState multicast、直接ISM表与O(N)安全检查；旧Round bootstrap/correction/ResultHeader/projectile及实体视觉已迁移到公共Networking/Presentation。8151/8153/8157分别关闭Round、J和双客户端late join门。

## 自动化与构建

- [x] [COMPUTED][HIGH] Development Editor使用`-DisableUnity`通过。
- [x] [COMPUTED][HIGH] DebugGame Editor使用`-DisableUnity`通过。
- [x] [COMPUTED][HIGH] B7已消除插件旧`.cpp`匿名命名空间辅助函数重名，Development `-ForceUnity`最终构建通过。
- [x] [COMPUTED][HIGH] 阶段H收口时`CrowdDemo` 111/111与`MassCrowd` 25/25自动化通过；RuntimeBehavior与BehaviorAdapters定向2/2通过。
- [x] [COMPUTED][HIGH] 阶段I收口时Development/DebugGame `-DisableUnity`、NavSurfaceGraph定向3/3、`MassCrowd` 27/27与`CrowdDemo` 112/112通过；8800真实Recast probe的8/8可达marker与不可达drop门通过，并保留视觉截图。
- [x] [COMPUTED][HIGH] 阶段J收口时Development/DebugGame `-DisableUnity`、MixedSandbox定向2/2、`MassCrowd` 27/27与`CrowdDemo` 114/114通过；8804双端20实体业务、安全、同步和视觉门通过。
- [x] [COMPUTED][HIGH] P0–P5最终Development/DebugGame `-DisableUnity`通过；累计`MassCrowd` 40/40与`CrowdDemo` 115/115通过。8156 NavFlow、8154 Friendly Logistics、8153 J、8151 Round及8157双客户端late join均无Fatal、Assertion、Ensure、`LogWindows: Error`或VIOLATION。
- [x] [COMPUTED][HIGH] B0–B7代码增量后的完整自动化日志为`MassCrowd 50/50`与`CrowdDemo 115/115`、失败数0；该结果证明现有测试通过，不代表上述缺失专项已被覆盖。

## 2026-07-28 回归增量

- [x] [COMPUTED][HIGH] P3可靠记录支持有界batch和ACK后分批追赶；空ApplyFrame队列不再误报stale，未放宽缓存或网络预算。
- [x] [COMPUTED][HIGH] P4 7953通过20实体真实运输、守恒、竞争、死亡恢复、fallback、不可达退避、late join及Cargo attach/detach；EmptyHand/Pickup/Carrying/Delivered四类近景文件已生成。
- [x] [COMPUTED][HIGH] P5 7939 J、7946 Continuous和7948–7951 Round T2/T6/T7/T8通过，硬错误扫描为0。
- [x] [COMPUTED][HIGH] 最新Development/DebugGame `-DisableUnity`、MassCrowd 43/43、CrowdDemo 115/115、反向依赖扫描与`git diff --check`通过。
- [ ] [COMPUTED][HIGH] J隐藏客户端Actor Tick p95=`400ms`不是有效渲染帧性能证据；正式客户端帧性能仍须由前台/非节流采样门证明，不能用该数字标记通过。
- [x] [COMPUTED][HIGH] 8663 T2生产回归通过Runtime Finalize/Commit链，fixed-step/Commit p95=`3.529/0.021ms`；8664 SF1 Single authority短运行无VIOLATION。
- [x] [COMPUTED][HIGH] Facing迁移后8665 T2维持20/20 terminal、16/16 coverage和双端Yaw误差0，fixed-step/Commit p95=`3.638/0.020ms`；8666 SF1无Particle路径无VIOLATION。
- [x] [COMPUTED][HIGH] Shared Flow迁移后8667 T2维持20/20 terminal、16/16 coverage、安全和双端hash通过，fixed-step/Flow p95=`3.166/0.264ms`；8668 SF1确认hash=`267519150`、rebuild=1且无硬错误。
- [x] [COMPUTED][HIGH] Target Region迁移后8669 T2维持20/20 terminal、16/16 coverage、五类hash与性能门；8671异构T6 Static维持inside-band/coverage=`20/20`、unrouted/invalid/validation failure=0，安全、同步及性能门通过。
- [x] [COMPUTED][HIGH] Runtime单boundary基础snapshot通过乱序、重复Agent拒绝和完整字段hash测试；Flow与Target Demand已复用该snapshot和prepared Flow SoA。8672 T2为20/20 terminal、16/16 coverage、fixed-step p95=`3.599ms`；8673异构T6为20/20 inside/coverage、fixed-step p95=`4.844ms`，两者安全与双端hash通过。
- [x] [COMPUTED][HIGH] Flow/Target/Business Guidance overlay已与boundary snapshot稳定合并；Compose和Local Predictive不再为WORK输入重复读取基础Mass fragments。8677 T2、8678异构T6保持能力、安全、双端hash及性能门。
- [x] [COMPUTED][HIGH] MovementPredict、Particle与Facing的基础WORK输入已从统一snapshot/prepared链消费；8681 T2、8682异构T6及8683 SF1 smoke无行为、安全或hash回退。
- [x] [COMPUTED][HIGH] MovementFinalize从snapshot与prepared kinematics/facing组装Commit输入；旧第一遍全实体Gather已删除，完整镜像原子预验证保留。8684 T2、8685异构T6及8686 SF1 smoke通过。
- [x] [COMPUTED][HIGH] MovementFinalize的原子镜像验证与提交后业务/指标采集已拆为最小查询；ApplyMetrics不再读取MoveIntent、Runtime properties、Runtime Particle/Facing。8687 T2、8688异构T6及8689 SF1 smoke通过。
- [x] [COMPUTED][HIGH] Finalize状态写入与post-finalize业务/诊断采集已成为独立processor；每boundary Finalize成功标记同时保护post-finalize及Authority/Client Commit。8693 T2、8694异构T6和8695 SF1 smoke通过。
- [x] [COMPUTED][HIGH] Post-finalize不再读取Formation、Composed Guidance、Particle Properties或未使用Particle Constraint fragment；精确Formation Radius由boundary fact保存。8698异构T6 rollback=`80/0/0`，8699 T2 rollback=`54/0/0`，8702 SF1 hash=`267519150`。
- [x] [COMPUTED][HIGH] Post-finalize不再读取FlowSample与ObstacleConstraint fragment；prepared Flow恢复rollback事实，snapshot+final state复验penetration。8703异构T6 rollback=`80/0/0`、fixed-step p95=`4.595ms`，8704 SF1 hash=`267519150`。
- [x] [COMPUTED][HIGH] Post-finalize不再读取GuidanceCandidates与Facing fragment；Guidance由snapshot+prepared overlays重建，Facing rollback fact由Facing阶段精确发布。8705异构T6 rollback=`80/0/0`、inside/coverage=`20/20`、fixed-step p95=`4.551ms`，8706 SF1 hash=`267519150`。
- [x] [COMPUTED][HIGH] T1 OpenSpawn唯一runtime生成稳定prepared boundary facts；per-agent OpenSpawn fragment已物理删除，pending reset完整验证后原子消费。
- [x] [COMPUTED][HIGH] 第十切片当时由VisualStateResolve完成Combat/Visual rollback事实；第二十一切片已把同一职责并入FacingFinalize原子写回。movement/combat双完成门继续阻止不完整snapshot replay或checkpoint。
- [x] [COMPUTED][HIGH] 8707 T1、8708 T7、8709异构T6及8710 T8双端回归通过安全、同步、snapshot完整性及性能门；8714 SF1 smoke保持hash=`267519150`、rebuild=1。
- [x] [COMPUTED][HIGH] 六个Demo迁移运动镜像及其模板/processor/rollback/适配入口已物理删除；结构自动化阻止类型和Mass模板注册回流。
- [x] [COMPUTED][HIGH] 第十二切片Development、DebugGame、`CrowdDemo` 105/105、`MassCrowd` 13/13通过；8723/8724/8725/8726分别覆盖T2、异构T6、T1和T8，四次双端运行无安全、同步、性能或业务hash回退。
- [x] [COMPUTED][HIGH] Compose→Local Predictive→MovementPredict已合并为一次GT准备、一次ThreadPool dispatch和一次原子发布；旧三个processor实现引用为0，阶段结果/hash等价测试通过。
- [x] [COMPUTED][HIGH] Facing与MovementFinalize已合并为一次Runtime WORK和一次GT原子提交；组合/旧链stage hash等价、反序稳定及revision mismatch整批拒绝测试通过，8727/8728无能力、安全、同步或性能回退。
- [x] [COMPUTED][HIGH] Particle通用结果发布已由Runtime publish plan统一：active/inactive/fallback/external结果、最终kinematics与稳定hash一次生成；Demo不再从ProposedMovement/FlowSample Mass fragments重建诊断输入。8729/8730保持T2/T6能力、安全、同步和性能门。
- [ ] [COMPUTED][HIGH] Particle后的Demo专用诊断/累计器组装与按能力archetype尚未完成；完整boundary单次Mass读取仍未关闭。
- [x] [COMPUTED][HIGH] RoundResultHeader contract v2的高熵自动化为1566字节，8790真实异构T6M为1970字节；均低于2048字节且无Native NetSerialize Warning。

## 20实体能力与性能

- [x] [COMPUTED][HIGH] T1六阶段/传播/settling通过；普通视觉不连续=0，测试reset单列。
- [x] [COMPUTED][HIGH] T2 handoff/band/settled=`20/20/20`，coverage=`16/16`。
- [x] [COMPUTED][HIGH] T3双cohort=`10/10`、deadlock=0。
- [x] [COMPUTED][HIGH] T4 wall/corridor/completed/settled=`20/20/20/20`。
- [x] [COMPUTED][HIGH] T5S inside=`20/20`、coverage=`16/16`；当前版性能门通过。
- [x] [COMPUTED][HIGH] T6A corridor/completed/inside/coverage=`20/20/20/20`；T6S七类profile技术门通过。
- [x] [COMPUTED][HIGH] T7新增阶段证据后的普通运行8781/8783连续通过；Round内shader/loading/PSO帧为0。历史8777冷运行112.235ms仍保留为未唯一归因证据。
- [x] [COMPUTED][HIGH] 2026-07-29 T7可解释验收首切片完成：六类代表实体的expected/actual引线标签区分authority sample step、client observation step、pre-Round WAIT与±1 step EDGE；全部20实体状态变化写入JSONL sidecar；`CaptureCrowdDemo.ps1 -T7StateAcceptance`按实际Knockback/KnockUp/Death事件生成step 30/60/90短片、contact sheet和manifest。7971原始帧确认3×2标签布局可读；最终7972端到端短录制观察20/20 Formation、60条状态事件、WAIT/EDGE/mismatch=`20/4/0`、事件sample/observation=`30/31、63/63、91/94`、长冻结0；Development `-DisableUnity`与`CrowdDemo.Acceptance.T7.StateOracle` 1/1通过。
- [x] [COMPUTED][HIGH] T5M 8785技术、性能与稳定诊断通过；无merge block/chatter。
- [x] [COMPUTED][HIGH] T6M 8790的Round末inside/coverage=`20/20`且安全、同步、性能通过；AcquireThenHold资格有效期间不要求持续重排Region，最后90步18/17保留为过程诊断。
- [ ] [COMPUTED][HIGH] 单进程DebugGame PIE和当前版人工审片未完成。
- [x] [COMPUTED][HIGH] 8210/8215仅保留为S6之前的旧Round历史基线；当前同一Behavior Source生产路径的100/500已由8384/8379双端PASS替代，不再以8215作为当前关闭证据。

- [x] [COMPUTED][HIGH] 临时`FCrowdMassParticleConstraintFragment`已物理删除；Particle processor无Mass query，FacingFinalize从prepared final kinematics验证并提交最终运动事实。
- [x] [COMPUTED][HIGH] 第十五切片Development、DebugGame、MassCrowd 15/15、CrowdDemo 105/105通过；8731 T2为20/20 settled、16/16 coverage，8732异构T6为20/20 completed/settled/inside/coverage，双端安全、同步与性能门通过。
- [x] [COMPUTED][HIGH] Particle持久诊断已改为Finalize成功后的单次延迟提交；失败的FacingFinalize boundary不会累计候选Summary、route/stability、OpenSpawn或fixture事实。8733/8734保持原能力、candidate hash、安全、同步和性能门。
- [x] [COMPUTED][HIGH] PostFinalize已删除最后的Identity/RoundSim Mass查询；FacingFinalize写回时捕获稳定最终记录，PostFinalize仅消费prepared事实。结构测试同时要求这些记录在每个BoundarySnapshot发布时清空。
- [x] [COMPUTED][HIGH] 第十七切片Development、DebugGame、MassCrowd 15/15、CrowdDemo 105/105通过；8737 T2为20/20 terminal、16/16 coverage、fixed-step p95=`4.067ms`，8738异构T6为20/20 completed/settled/inside/coverage、fixed-step p95=`4.716ms`，双端同步与性能门通过。
- [x] [COMPUTED][HIGH] Transform、Velocity与Demo Movement已并入FacingFinalize的全量预验证后单次原子写回；Authority/Client Commit无Mass query，旧`ConfigureCommitQuery/CommitRoundState`重复两遍遍历已物理删除。
- [x] [COMPUTED][HIGH] 第十八切片Development、DebugGame、MassCrowd 15/15、CrowdDemo 105/105通过；8739 T2 fixed-step p95=`3.638ms`，8740异构T6=`5.003ms`，能力、安全、同步、可见实例与性能门均通过。
- [x] [COMPUTED][HIGH] 第十九切片删除CheckpointPublisher九类fragment查询，prepared checkpoint states在Visual/Combat决议后整批发布并按最终记录校验；Development、DebugGame、MassCrowd 15/15、CrowdDemo 105/105通过，8741 T2与8742异构T6的能力、安全、同步、correction和性能门通过。
- [x] [COMPUTED][HIGH] 第二十切片删除VisualStateResolve的Formation/RoundSim query requirements，并用最终记录与boundary formation facts重建相同状态；8743 T2 fixed-step p95=`3.137ms`，8744异构T6=`4.023ms`，能力、安全、同步、可见实例与性能门通过。
- [x] [COMPUTED][HIGH] 第二十一切片删除独立VisualStateResolve query；FacingFinalize单次原子写回同时提交运动与Demo Combat/Visual，PostFinalize保持rollback双完成门。Development、DebugGame、MassCrowd 15/15、CrowdDemo 105/105通过；8745–8748覆盖T2/T6/T7/T8且无能力、安全、同步或业务hash回退。
- [x] [COMPUTED][HIGH] 第二十二切片删除三个无fragment消费者的Runtime Movement中间fragments，MovementWork不再逐步回写持久Runtime identity/state/properties；双端Plan边界初始化合同已补齐。Development、DebugGame、MassCrowd 15/15、CrowdDemo 105/105通过；8753 T2 terminal/inside=`20/20`、coverage=`16/16`、fixed-step p95=`4.021ms`，8754异构T6 completed/settled/inside/coverage=`20/20`、fixed-step p95=`4.920ms`，两次运行硬错误与correction误差均为0。
- [x] [COMPUTED][HIGH] 第二十三切片已将RangedCombat、HitResponse、ReactiveMotion收敛为单一宿主Combat boundary transaction；旧三个processor与Pending HitFact桥已删除，T7/T8分别只保留一次gather和一次原子apply。Development、DebugGame、MassCrowd 15/15、CrowdDemo 105/105通过；8755 T7 fixed-step p95=`2.452ms`，8756 T8 spawn/impact/damage=`50/50/50`、duplicate fire/hit=`0/0`且三类业务hash双端一致；8757 T2 terminal/coverage=`20/20、16/16`，8758异构T6 completed/settled/inside/coverage=`20/20`，fixed-step p95=`4.263/5.230ms`。
- [x] [COMPUTED][HIGH] 第二十四切片完成剩余查询/写入接缝矩阵：删除ReactiveMotionStep与TargetCapability两个无持久所有权fragment；TargetRegionGuidance改读boundary snapshot且零Mass遍历；FlowPreferred只保留一次FlowSample持久写回；SoftPressure MovementWork不再写SF1 ProposedMovement桥。Development、DebugGame、MassCrowd 15/15、CrowdDemo 105/105通过；8759–8762覆盖T7/T8/T2/T6，能力、安全、同步、可见实例及性能门均通过。
- [x] [COMPUTED][HIGH] 第二十五切片已删除SF1 ProposedMovement/ObstacleConstraint两个中间fragment；MovementWork与ObstacleConstraint均为零Mass遍历并通过prepared POD交换，8763 SF1 Single保持golden Flow与障碍安全，8765/8766 T2/T6无回退。
- [x] [COMPUTED][HIGH] 8763曾出现correction interval位置误差p95=`26.745cm`；第二十七切片已由8770修复到`0.064cm`，该项不再是开放失败。
- [x] [COMPUTED][HIGH] 历史8764的500实体单属性bunch失败已由紧凑Agent NetSerialize、128项渐进FastArray和有界correction可靠批次关闭；8215客户端完成500实体基线、5块连续correction与5轮Checkpoint，未出现bunch过大或revision gap。
- [x] [COMPUTED][HIGH] 第二十六切片把Facing历史settle事实并入BoundaryGather，FacingFinalize由3次Mass遍历降为2次；全量预验证与唯一原子提交仍独立。Development、DebugGame、MassCrowd 15/15、CrowdDemo 105/105及8767–8769 SF1/T2/T6回归通过。
- [x] [COMPUTED][HIGH] 第二十七切片已把fixed-step correction history/replay扩展到SF1。8770 snapshot hit/miss/mismatch=`36/0/0`，correction interval位置误差p95=`0.064cm`、checkpoint p95=`0.008cm`，Flow golden hash与路线/障碍结果不变；Development、DebugGame、MassCrowd 15/15、CrowdDemo 106/106通过。
- [x] [COMPUTED][HIGH] 阶段 B 通过 Development Editor `-DisableUnity`、`MassCrowd` 17/17与完整`CrowdDemo` 106/106自动化；新增 Core POD/capability/lifecycle fixture 与 Runtime AgentFacts 映射测试。
- [x] [COMPUTED][HIGH] 阶段 C 通过 Development Editor `-DisableUnity`、Relevant Snapshot定向3/3、`MassCrowd` 20/20与`CrowdDemo` 106/106；插件Networking Source未检出Demo/Round/Scenario/Combat依赖。
- [x] [COMPUTED][HIGH] 阶段 D 已删除完整`RoundBootstrapPacket.Agents`复制路径；版本化字段往返、合成500实体transport、逆序/重复/缺块/超时及源码架构定向3/3通过。Development/DebugGame、MassCrowd 20/20、CrowdDemo 109/109与8773真实20实体双端bootstrap通过；正式500产品运行仍待K。
- [x] [COMPUTED][HIGH] 阶段 E 的trivially-copyable batch header/entries、四类Despawn原因、严格排序、bounds、Snapshot→Delta连续性、重复/乱序/缺序列、原子拒绝、槽位复用及路径无关membership hash已通过定向3/3；Development/DebugGame、MassCrowd 23/23、CrowdDemo 109/109通过。
- [x] [COMPUTED][HIGH] 阶段 F 的最小Mass World定向1/1验证snapshot初始化、不同fixed-step spawn、真实destroy、Mass handle serial变化、StableEntityRef高serial复用、membership原子迁移、stale correction/despawn拒绝及完整entity-set hash；Development/DebugGame、MassCrowd 24/24与CrowdDemo 109/109通过。
- [x] [COMPUTED][HIGH] 8771 T2 rollback hit/miss/mismatch=`54/0/0`、terminal/inside/coverage=`20/20、20/20、16/16`；8772异构T6 rollback=`80/0/0`、completed/settled/inside=`20/20`、coverage=`20`，通用历史没有造成SoftPressure能力、安全、同步或性能回退。
- [ ] [INFERRED][HIGH] 整个fixed-step尚未达到一次Mass读取；该长期优化不再阻塞Behavior Source。Resolver权威化、Final Apply原子性、行为网络和同路径20/100/500已由S0–S6关闭；真实StateTree业务Task不属于当前验收门。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
