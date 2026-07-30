# MassAI Crowd Demo 测试场景矩阵

[INFERRED][HIGH] 每个场景分别记录自动化、能力、性能和人工视觉；低层通过不能替代高层结论。

[INFERRED][HIGH] 规模结果必须标明生产路径。旧20实体Mixed、100实体SoftPressure和500实体Obstacle仍是历史分路径证据；当前PJ6结果来自同一Mixed Source/Resolver/Boundary/Networking/Projectiles生产路径。

[COMPUTED][HIGH] pre-T9提交`5b947389`固定DP0–DP6证据；T9现已由8517双端真实门关闭，不能再沿用“尚未执行”的历史断言。

## PW持久Worker目标验证矩阵

[COMPUTED][HIGH] PW0–PW8已完成。Production Movement由`PersistentRuntimeAuthority`单独拥有；Particle、Target、Combat因fail-closed证据不足继续留在强一致Boundary。最终自动化为MassCrowd 83/83、CrowdDemo 135/135，Development/DebugGame Editor `-DisableUnity`均通过。

| 门 | 目标条件 | 当前状态 |
|---|---|---|
| Input/Mirror合同 | [INFERRED][HIGH] 首次全量后只同步Lifecycle/Command/Resource/Dirty输入；Sequence缺口触发Resnapshot；Worker不读取Mass/World/UObject | [COMPUTED][HIGH] PASS：Round/Mixed/Friendly已接入；命令只在Prepared Commit成功后进入有界Journal并在Worker Batch接受后ACK；定向2/2与Runtime缺序列/Resnapshot专项通过 |
| 可变Batch交换 | [INFERRED][HIGH] GT每帧只交换一次冻结Published Batch并完整消费0/10/9999项；不追逐实时尾部、不固定实体配额 | [COMPUTED][HIGH] PASS：0/1/10/9999、同实体latest-wins、有序Event、三槽不可变和并发压力均通过；生产GT每帧一次Exchange |
| State/Event背压 | [INFERRED][HIGH] 同实体State latest-wins；Spawn/Despawn/Combat Event/Correction不丢失；有界队列满时fail-closed | [COMPUTED][HIGH] PASS：输入/Event容量和Violation锁存专项通过；1k–10k持续运行的Input Queue及ordered event depth均为0 |
| Worker所有权 | [INFERRED][HIGH] 已迁移字段只有Worker Writer；Mass为代理；无输入Echo、双写或旧Generation提交 | [COMPUTED][HIGH] PASS：Production Movement只消费`PersistentRuntimeAuthority` Domain Tail，普通Movement输入Echo被拒绝；Shadow/Canary不与Production并行写 |
| Shadow等价 | [INFERRED][HIGH] Mirror实体/Lifecycle/资源Hash与Mass一致；低耦合Kernel在任务乱序、Shard变化下与现行Boundary结果一致 | [COMPUTED][HIGH] PASS：SharedFlow/Facing/Business乱序与Shard 1–64稳定，Dynamic Flow Hash改为每Fixed Step一次并进入rollback snapshot |
| 生命周期 | [INFERRED][HIGH] Worker在执行时Correction、Plan替换、PIE停止、地图切换和Subsystem Deinitialize均无悬空访问或旧结果应用 | [COMPUTED][HIGH] PASS：跨Round绝对Simulation Time、Correction Revision、单进程双PIE独立Runtime和双World teardown通过 |
| 规模吞吐 | [INFERRED][HIGH] 1k/2k/5k/10k逐级记录Mirror lag、Simulation lag、scan coverage、Task critical、publish-to-consume和GT apply；无持续积压 | [COMPUTED][HIGH] PASS：9174/9175/9177/9179均到step 300且队列为0；10k Worker lag=`9.677ms`、scan=`1.549ms`、owner pump=`2.988ms`、GT apply=`0.403ms`。完整强一致Demo Boundary约145ms/step，不标记10k实时 |
| 视觉连续性 | [INFERRED][HIGH] 可变Batch下无长冻结、批次跳变、错误状态或Correction闪回；录屏与FFmpeg门通过 | [COMPUTED][HIGH] PASS：9180 T7 Production录屏20实体、58状态事件、0 mismatch、0 freeze，knockback/knockup/death三段切片已生成 |
| 单主体ISM闪色 | [INFERRED][HIGH] PICD slot 2只改变受击目标；无第二ISM、红色副本、重影或Z-fighting；0/1/10/9999与swap-remove守恒 | [COMPUTED][HIGH] 表现PASS、最终切片BLOCKED：9208逐帧显示Knockback 2个、KnockUp 2个约5帧衰减、Death 4个目标闪白，实例始终20；自动化与构建全过。9207/9213 Mixed 500在fixed-step 93触发既有PW `RequiresResnapshot`，未把该失败误记为材质回退 |

## AB异步Boundary验证矩阵

| 门 | 目标条件 | 当前状态 |
|---|---|---|
| 非阻塞Mailbox | [INFERRED][HIGH] 每World最多一个InFlight；Pending立即返回；普通Tick `Wait/Get/Event.Wait`计数为0 | [COMPUTED][HIGH] PASS：生产源码已无阻塞入口，Boundary定向自动化7/7通过；T5/T6五个双端场景的`ordinary_block_wait_count`均为0 |
| 事务原子性 | [INFERRED][HIGH] Round/Generation/Plan/Step/Sequence/Snapshot任一不匹配均整批拒绝且零写入 | [COMPUTED][HIGH] UNIT PASS：显式事务身份、snapshot mismatch和完整集合拒绝已覆盖；Correction跨帧专项仍待运行 |
| Worker所有权 | [INFERRED][HIGH] Worker只访问不可变POD/SoA；不访问Mass/World/UObject；GT是唯一持久writer | [COMPUTED][HIGH] CODE PASS：跨帧闭包只持有Snapshot、WorkState和Nav只读资源；GT continuation执行最终提交 |
| 调度确定性 | [INFERRED][HIGH] Task完成顺序、输入反序和1/7 Cohort产生相同Stage/Commit Hash | [INFERRED][HIGH] 待补异步完成顺序专项 |
| T5/T6功能 | [INFERRED][HIGH] T5S/T5M/T6A/T6S/T6M保持现有安全、inside/coverage、稳定和双端Hash合同 | [COMPUTED][HIGH] INDEPENDENT PASS：8822/8824/8825/8826/8823全部通过；T5S为inside20、coverage16/16，T5M为inside20、coverage12/16，T6A/T6S/T6M均为inside20、feasible coverage20且T6专项valid=1 |
| 独立双端性能 | [INFERRED][HIGH] `ordinary_block_wait_count=0`、稳态catch-up budget hit=0、backlog p95≤66.667ms、client frame p95≤33.333ms | [COMPUTED][HIGH] PASS：五图fixed-step p95为17.980/17.947/17.825/17.857/18.338ms，backlog p95为31.284/31.809/31.668/31.788/32.391ms，realtime均约0.999，阻塞/stale/catch-up均为0 |
| 单进程双PIE性能 | [INFERRED][HIGH] 与独立双端使用相同门槛，且不能由独立进程结果替代 | [INFERRED][HIGH] 待运行 |
| 生命周期 | [INFERRED][HIGH] Correction/新Plan/PIE停止/地图切换均无stale commit、悬空Event或Worker回调已销毁World | [COMPUTED][HIGH] 单元合同已实现；真实Correction/PIE停止/地图切换专项待运行 |
| 视觉连续性 | [INFERRED][HIGH] Pending帧继续插值；FFmpeg无长冻结、非Correction跳变或错误状态切换 | [INFERRED][HIGH] 待异步生产路径录制 |

[INFERRED][HIGH] AB矩阵关闭顺序固定为Mailbox单元测试→T5S首图→全部T5/T6→Round T1–T9/Mixed/Continuous/Friendly回归；不得用独立进程性能替代单进程双PIE门。

## T9 Mixed Combat Integration当前门

| 门 | 当前状态 | 最新证据 |
|---|---|---|
| 角色与攻击 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] 20实体10对10；每方4 Melee、2 MidRange、4 Ranged；三类intent=`61/31/21`。 |
| Impact/Damage/Death | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] 8517 impact/damage/death=`62/61/9`，friendly fire和重复提交由Prepared Adapter拒绝，运行无VIOLATION。 |
| 目标失效与重建 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] target switch/TargetRegion rebuild=`227/227`，referenced dead=`0`，死亡实体从即时Spatial目标集合排除。 |
| Projectile守恒 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] spawn/impact/expire/active=`21/5/15/1`，duplicate=`0`，守恒成立。 |
| 网络与性能 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] 双端entity hash=`7972685099634826285`、membership hash=`6035199850779644907`；resync/安全违规=`0`，服务端fixed-step p95=`2.910ms`。 |
| 自动化 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] MassCrowd 64/64、CrowdDemo 133/133。 |
| 构建矩阵 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] Development/DebugGame × ForceUnity/DisableUnity四构建成功。 |
| 旧Mixed回归 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] 8520双端旧Mixed生产门通过，4发Projectile守恒且Hash=`1098769576993558422`。 |

## DP0–DP6 Demo业务规划当前门

| 门 | 当前状态 | 完成要求 |
|---|---|---|
| DP0 基线 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] `07359ed`、65/65、125/125、四构建与Mixed 20/100/500结果已冻结。 |
| Planner Core | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] Registry/冻结、NoBusiness、反序、缺事实、容量、Host Intent和Stable Hash专项通过。 |
| Mixed角色 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] 五Planner、Reaction、目标丢失、Source精确恢复和Coordinator结构门通过。 |
| Friendly | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] 8303通过Claim/Pickup/Deliver/Requeue/fallback/backoff/cancel、守恒和失败零写入。 |
| Round T7/T8 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] 8349 T7通过；T8保持50/50/50、duplicate=0、双端一致。统一攻击状态机后版本化attack/projectile/event Hash为41852579/488896174/4204062592。 |
| NoBusiness | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] 8350 Continuous、8353–8363 T1–T6与8351 NavFlow通过统一入口且保持专项结果。 |
| 最终门 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] MassCrowd 64/64、CrowdDemo 131/131、四构建、全部真实入口及Mixed 8311/8314/8315的20/100/500通过。 |

| 场景 | 核心能力 | 最新20实体技术/能力结果 | fixed-step p95 | 视觉状态 |
|---|---|---|---:|---|
| T1 | 测试参与集切换、压力传播、staging reset、新平衡 | [COMPUTED][HIGH] 6阶段、layer3、settling通过；全部 Mass 实体始终存在，不是 spawn/despawn；普通不连续=0，测试reset单列 | [COMPUTED][HIGH] 1.131ms | [INFERRED][HIGH] 当前版人工审片待补 |
| T2 | 开放cohort移动与目标handoff | [COMPUTED][HIGH] handoff/band/settled=20，coverage=16/16 | [COMPUTED][HIGH] 3.073ms | [INFERRED][HIGH] 当前版人工审片待补 |
| T3 | 开放双向交换 | [COMPUTED][HIGH] 10/10完成，deadlock=0 | [COMPUTED][HIGH] 2.938ms | [INFERRED][HIGH] 当前版人工审片待补 |
| T4 | 障碍走廊与汇入 | [COMPUTED][HIGH] wall/corridor/completed/settled=20 | [COMPUTED][HIGH] 3.376ms | [INFERRED][HIGH] 当前版人工审片待补 |
| T5S | 静态目标Region分布与稳定落位 | [COMPUTED][HIGH] inside20、coverage16/16；收敛后性能/技术门通过 | [COMPUTED][HIGH] 5.362ms | [INFERRED][HIGH] 当前版人工审片待补 |
| T5M | 移动目标Region分布 | [COMPUTED][HIGH] 8785安全/同步/Transport通过；稳定诊断valid=1、merge/chatter=0 | [COMPUTED][HIGH] 6.196ms；client Game/Render/GPU=4.234/5.536/5.073ms | [INFERRED][HIGH] 移动追随审片待补；不宣称静态settled |
| T6A | 异构走廊后按能力自然落位 | [COMPUTED][HIGH] corridor/completed/inside/coverage=20，7 profiles通过 | [COMPUTED][HIGH] 3.114ms | [INFERRED][HIGH] Region标记与朝向审片待补 |
| T6S/T6M | 异构静态/移动目标 | [COMPUTED][HIGH] T6S通过；T6M 8790 Round末inside/coverage=20/20，AcquireThenHold资格保持合同技术放行；90步最低18/17保留为过程诊断 | [COMPUTED][HIGH] T6S 4.261ms / T6M 12.137ms；client phases 10.332/6.852/5.802ms | [INFERRED][HIGH] 当前版人工审片待补 |
| T7 | VAT、受击、击退、死亡 | [COMPUTED][HIGH] 新阶段证据下8781/8783连续普通运行通过；历史8777失败未唯一归因 | [COMPUTED][HIGH] fixed-step约1.95–2.12ms；client frame p95 6.016/5.820ms | [INFERRED][HIGH] 当前版人工审片待补 |
| T8 | 远程攻击、Projectile、swept hit | [COMPUTED][HIGH] spawn/impact/damage=50，duplicate=0 | [COMPUTED][HIGH] 1.598ms | [INFERRED][HIGH] 当前版人工审片待补 |

## 当前版人工验收矩阵（2026-07-29）

| 场景组 | 无录屏性能轮 | 可视化/FFmpeg轮 | 人工判定重点 |
|---|---|---|---|
| T1 | [INFERRED][HIGH] 独立双端、20实体、性能门；test boundary reset单列 | [INFERRED][HIGH] 完整录像与连续性检查 | [INFERRED][HIGH] 参与集切换可见且新平衡成立；只允许已标记reset，不允许普通帧瞬移 |
| T2/T5/T6S/T6M | [INFERRED][HIGH] 独立双端、稳定窗口与性能门 | [INFERRED][HIGH] Region标记、完整录像、contact sheet | [INFERRED][HIGH] inside/coverage只是前提；必须继续检查目标相对速度、位置抖动、状态chatter和连续稳定落位 |
| T3/T4/T6A | [INFERRED][HIGH] 独立双端、完成/安全/性能门 | [INFERRED][HIGH] 通道入口、窄口、出口连续录像 | [INFERRED][HIGH] 验收安全通过和稳定离开出口；除非后续显式进入Target场景，否则不要求出口形成Region站位 |
| T7 | [INFERRED][HIGH] 不开录屏和标签，核对20实体、五态、Hit/Reactive计数、双端hash和性能 | [COMPUTED][HIGH] `-T7StateAcceptance`已提供expected/actual标签、JSONL sidecar、step 30/60/90自动短片和冻结诊断 | [INFERRED][HIGH] 分别核对Knockback进入/退出、KnockUp上升/apex/landing/recovery、Death与VAT；短暂复制延迟差异保留诊断 |
| T8 | [INFERRED][HIGH] 不开录屏，核对windup/spawn/impact/damage守恒、重复数和性能 | [INFERRED][HIGH] 后续复用T7 sidecar/切片机制，按Fire/Impact/Death事件切片 | [INFERRED][HIGH] 攻击VAT、发射、弹道、impact、HitFlash和受击响应时序一致 |

[INFERRED][HIGH] 每次人工验收必须保存五类产物：Server/Client日志、完整视频、完整contact sheet、权威状态/事件sidecar、专项事件短片。缺少权威sidecar时，视频只能证明“看起来发生了”，不能证明实体真实业务状态。

## 公共门

[COMPUTED][HIGH] 8790无Fatal、Assertion、Ensure、`LogWindows: Error`、VIOLATION或Native NetSerialize Warning，双端correction误差为0；T6M按AcquireThenHold资格保持合同技术放行，18/17严格窗口只保留为过程诊断。

[INFERRED][HIGH] 异步`fixed_step_ms`表示Request Submit到Result Commit端到端延迟，性能门为fixed-step p95≤66.667ms、client frame p95≤33.333ms、visual p95≤16.667ms、realtime≥0.95、step-limit hit=0；启动max、Round reset、catch-up和steady discontinuity必须单列。

| 规模 | 当前结论 |
|---|---|
| 20 | [COMPUTED][HIGH] 8402在step600通过同一Source/Resolver/Boundary/Networking/Projectiles路径；active/visible=`20/20`，4发Projectile的spawn/impact/damage=`4/4/4`、duplicate=`0`，服务端/客户端p95=`2.152/4.963ms`，最小间距=`70.11cm`，双端实体/成员/Projectile Hash一致且无resync或违规。 |
| 100 | [COMPUTED][HIGH] 8403在step630通过同一路径；active/visible=`100/100`，20发Projectile的spawn/impact/damage=`20/20/20`、duplicate=`0`，服务端/客户端p95=`9.675/4.938ms`，最小间距=`70.03cm`，双端实体/成员/Projectile Hash一致且无resync或违规。 |
| 500 | [COMPUTED][HIGH] 8401在step630通过同一路径；active/visible=`500/500`，100发Projectile的spawn/impact/damage=`100/100/100`、duplicate=`0`，服务端/客户端p95=`30.016/5.171ms`，最小间距=`70.00cm`，双端实体/成员/Projectile Hash一致且无resync或违规。 |

## 生产生命周期与复制场景

| 场景 | 当前状态 |
|---|---|
| StableEntityRef/Capability/Behavior POD 与 Runtime 映射 | [COMPUTED][HIGH] 阶段 B 已通过 Core AgentFacts 与 Runtime AgentFactsMapping 自动化；覆盖 lifecycle 区分、能力门、Faction/Capability 解耦、非法位与可选引用、Runtime 往返映射。 |
| Relevant Snapshot header/chunks 与 assembly | [COMPUTED][HIGH] 阶段 C 纯协议3/3与阶段 D Demo adapter 3/3通过；8773客户端经真实网络组装20 agents、1 chunk、3720 bytes并进入现有bootstrap消费入口。覆盖重分块、任意顺序、重复/冲突、stale、损坏hash、bounds、empty、缺块、timeout与合成500实体。 |
| 分批 spawn/despawn、死亡移除、LifecycleSerial 复用 | [COMPUTED][HIGH] E协议与F最小Mass World通过；G的8777真实双端路径从10增至20上限并持续Membership/Despawn/Respawn。序列12明确slot 2 serial 1以Death移除，序列13以serial 2重生；T1仍不覆盖生命周期。 |
| Spawn/Despawn 乱序与 stale Lifecycle 拒绝 | [COMPUTED][HIGH] E覆盖严格sequence/重复/缺序列/原子拒绝；F真实World覆盖stale correction/despawn不改变active entity与完整集合hash。 |
| late join snapshot + 后续 delta | [COMPUTED][HIGH] P3公共channel已通过真实延迟加入：J 7977 baseline=`20 entities/3 chunks`后连续消费state/correction至step600；Continuous 7975从当前19实体baseline恢复并继续可靠序列，双端在sequence 32集合hash一致。 |
| 动态 Relevancy 与 Membership Delta | [COMPUTED][HIGH] `FCrowdSpatialGridRelevantSetProvider`已通过稳定排序与关系闭包自动化；J/Continuous已通过Membership可靠序列。真实视区移动触发enter/exit的双客户端场景尚未单独保存证据。 |
| 客户端视觉实例增量创建/回收 | [COMPUTED][HIGH] 公共Presentation slot table与Demo ISM sink已接管J/Continuous；7975 active/visible恒等，7977 step600 active/visible=`20/20`，swap-remove和重复/stale由定向测试覆盖。 |
| Cargo/Combat 跨 Source 组合 | [COMPUTED][HIGH] Demo Provider覆盖领域Source，控制器对期望持久集合执行Start/Update/Stop Diff；临时压制结束后原Source实例与持久状态继续存在。Mixed 20/100/500均走同一Resolver/安全链。 |
| NavMesh Surface Graph / Shared Flow | [COMPUTED][HIGH] I的合成图测试覆盖确定性、桥上桥下XY重叠、窄门/落差拒绝、layer attach、动态目标rebind；`CrowdDemo_NavSurfaceGraphVerticalSmall`的8800真实Recast运行得到98 nodes、234 directed edges、4 layers、13 overlap、76 reachable sloped edges、8/8 reachable markers与drop unreachable。视觉证据为`Saved/StageI_NavSurfaceGraph_Visual.png`。 |
| continuous lifecycle / Sandbox | [COMPUTED][HIGH] 当前Mixed双端路径组合LifecycleWorld、Source World Store、Combat、Logistics、NavMesh Flow与增量ISM；Movement/Facing/Constraint消费Resolver结果并进入`FCrowdMassMovementPipelineWork → Particle Constraint → Facing Finalize`。StandardSources升级后的20/100/500均已通过同一路径复测。 |

## Behavior Source专项门

| 专项 | 当前状态 |
|---|---|
| Core Source状态机 | [COMPUTED][HIGH] 已覆盖Profile/Modifier、16 Source、命令幂等/冲突/缺口、过期、Capability撤销和反序Hash。 |
| Resolver | [COMPUTED][HIGH] 六通道排序、Q15 Blend、Override/Additive、Constraint、冲突、容量、输入反序和稳定Hash由Core/Fixture自动化覆盖；Mixed生产Movement/Facing直接消费Resolved结果。 |
| Runtime原子性 | [COMPUTED][HIGH] Source staging、Prepared Hash、Patch稳定排序、篡改拒绝与失败零写入自动化存在；Mixed只在全部Prepare/Validate成功后Final Apply。 |
| Behavior网络 | [COMPUTED][HIGH] v3 Codec携带Registry/Context/State Schema与实例状态，已接入生产可靠状态、late join、分批发送和Hash resync；Fixture覆盖Predictable/ResolvedOnly回放。 |
| StateTree | [COMPUTED][HIGH] Adapter已拆为默认禁用兄弟插件并通过独立构建Smoke；真实业务Task已从现行框架门移除。 |
| Mass Projectile | [COMPUTED][HIGH] T8专项13/13继续覆盖Mass权威生命周期、相对/环境Sweep、Broadphase、墙体优先、Faction/NavLayer、Pierce、通用Impact/Hit与唯一宿主伤害；PJ6新增Spatial/Combat/Projectiles公共专项与结构门，禁止公共模块引用Demo、持久Projectile数组、Projectile×Agent全扫描和Runtime反向依赖Projectiles。 |
| 同路径规模 | [COMPUTED][HIGH] PJ6 Mixed Source/Resolver/Boundary/Networking/Projectiles路径已依次通过20/100/500；每种规模全部实体执行Standard Source，并并发4/20/100发公共Mass Projectile，spawn/impact/damage守恒、零重复、双端Hash一致、零安全违规。 |

## Standard Sources S0–S6专项门

| 专项 | 当前状态 |
|---|---|
| 模块边界 | [COMPUTED][HIGH] `MassCrowdStandardSources`已作为随包Runtime模块加载，只单向依赖Core/Runtime；Runtime/Core无反向依赖或Standard TypeId分支。 |
| Target Context | [COMPUTED][HIGH] `TargetKinematicsV1`与`FormationAnchorV1`均为不超过96字节的trivially-copyable POD；缺失、版本、Ref、Revision和非有限值均有拒绝专项。 |
| 基础Source库 | [COMPUTED][HIGH] 13种标准Source自主Evaluator已实现；定向8/8覆盖位置Goal、目标预测、Flee、Distance迟滞、Facing/Constraint、Wander回放、Formation和Impulse。 |
| 组合Recipe | [COMPUTED][HIGH] Mixed五Controller稳定Diff专项5/5覆盖无变化零命令、Escort、Pursue+Attack、显式一帧Lock、目标丢失Stop和HitReaction持久Source精确恢复。 |
| 完整运动安全链 | [COMPUTED][HIGH] 20/100/500均执行Resolved Goal/Movement/Facing/Constraint → Local Predictive → Particle/Bounds → Facing → Final Safety/Prepared Apply；PJ6修复候选NavLayer与安全Hold的原子提交缺口后，8402/8403/8401最小同层间距分别为`70.11/70.03/70.00cm`且零违规。 |
| 网络与规模 | [COMPUTED][HIGH] Codec v3覆盖Registry/Context/State、旧版本拒绝和Hash不符；第三方Fixture覆盖三复制策略。PJ6 20/100/500双端late join均达到全集active/visible、实体/成员/Projectile Hash一致和零resync；服务端p95=`2.152/9.675/30.016ms`，客户端p95=`4.963/4.938/5.171ms`。 |

## P0–P5 产品化验证矩阵

| 阶段 | 当前证据与关闭条件 |
|---|---|
| P0 合同与事实 | [COMPUTED][HIGH] 文档状态、查询所有权、J直接所有权、模块加载状态与公共API缺口已交叉核对；只需全文扫描、反向依赖扫描和`git diff --check`，不以编译替代文档闭环。 |
| P1 Boundary Orchestrator | [COMPUTED][HIGH] 历史P1覆盖依赖、Worker执行、失败、稳定hash和两阶段patch；2026-07-30已迁移为非阻塞Poll与事务身份，定向自动化7/7通过。 |
| P2 Nav Runtime | [COMPUTED][HIGH] provider/resource/Flow key/refcount/LRU/budget定向测试已通过；8156 `NavFlowProductSmall`通过98 nodes、234 directed edges、4 layers、2个Flow资源与20实体boundary。 |
| P3 Networking/Presentation | [COMPUTED][HIGH] Networking 9/9、Presentation 1/1及累计MassCrowd 36/36通过；真实J/Continuous late join、可靠序列、实例恒等通过。真实移动视区enter/exit仍是保留风险，但不再是公共API缺失。 |
| P4 FriendlyLogisticsSmall | [COMPUTED][HIGH] 8154专用地图通过20实体竞争、数量守恒、幂等、死亡恢复、fallback、不可达退避和late join；双端hash=`3180435972084878253`，Cargo attach/detach=`2/2`、实例=`20`并保存近景证据。 |
| P5 统一路径 | [COMPUTED][HIGH] 8151旧Round公共baseline/state/correction/ResultHeader通过；8153 J step600双端通过；8157常驻与延迟客户端分别从resume=`1766/4508`恢复并通过。实体Presentation profile所有权固定为单一公共路径。 |

## 2026-07-23 产品化续跑

| 入口 | 结果 |
|---|---|
| `NavFlowProductSmall` 8122 | [COMPUTED][HIGH] 双端通过；98节点、234有向边、4层，Flow resource/ref=`2/2`、cache hit/miss=`1/2`、9504字节；20实体P1 boundary提交hash=`9514377555178196070`，无硬错误。 |
| `FriendlyLogisticsSmall` 8125 | [COMPUTED][HIGH] 延迟客户端通过公共baseline/reliable state恢复；20实体、source/sink=`35/5`、in-transit=0、竞争/死亡恢复/fallback/取消=`1/1/1/1`、退避=2，双端hash=`3180435972084878253`，无硬错误。 |
| J Mixed 8126 | [COMPUTED][HIGH] 删除O(N)安全旁路后的step600双端通过；active/visible=`20/20`、transitions=29、pickup/delivery=`4/1`、combat=500、spawn/despawn=`3/3`、membership=7、最小间距=`71.51cm`、p95=`1.763/4.640ms`，双端hash一致。 |
| 累计自动化 | [COMPUTED][HIGH] Development/DebugGame `-DisableUnity`通过，MassCrowd=`40/40`，CrowdDemo=`115/115`。 |
| P1 Round 8132/8137/8138/8139 | [COMPUTED][HIGH] T2/T6/T7/T8双端通过，fixed-step p95=`2.581/5.140/1.853/1.525ms`，客户端frame p95均低于门限；T6首轮旧同步预Wait验证误报已修复并重跑，无硬错误。 |
| P5 Round/J/P4/Nav/late join 8151/8153/8154/8156/8157 | [COMPUTED][HIGH] Round公共ResultHeader=`1146 bytes`且correction=`20/20`；J active/visible=`20/20`、业务与hash无回退；P4 Cargo视觉通过；Nav graph/boundary通过；双客户端baseline resume连续。全部场景零硬错误。 |

[RULES I BROKE]：[COMPUTED][HIGH] P1未关闭时继续实施了P2/P3/P5切片，违反“失败留在当前阶段、不得跨阶段规避”的阶段顺序；修改本身保持模块边界，但阶段门没有被遵守。

## 2026-07-28 当前工作树回归

| 场景/门 | 当前证据 |
|---|---|
| P5 J 7939 | [COMPUTED][HIGH] step600 active/visible=`20/20`，transition=29，pickup/delivery=`4/1`，spawn/despawn=`3/3`，membership=7，最小间距=`71.51cm`，服务端fixed-step p95=`1.972ms`；双端无resync和硬错误。隐藏客户端Actor Tick p95=`400ms`不作为渲染性能通过证据。 |
| P4 FriendlyLogistics 7953 | [COMPUTED][HIGH] 20实体、总量40、交付5、竞争/死亡恢复/fallback/取消=`1/1/1/1`、退避2、最大单步位移=`8.667cm`、双端hash=`3180435972084878253`；客户端实例20、Cargo attach/detach=`1/1`。 |
| Continuous 7946 | [COMPUTED][HIGH] late join后active/visible保持恒等，最终sequence 53、entity-set hash=`7875336925641762435`，无stale或硬错误。 |
| Round 7948–7951 | [COMPUTED][HIGH] T2/T6/T7/T8双端通过，服务端fixed-step p95=`2.869/5.628/2.079/1.628ms`，correction与输入hash门通过，无硬错误。 |
| 累计自动化/构建 | [COMPUTED][HIGH] Development/DebugGame Editor `-DisableUnity`通过；MassCrowd 43/43、CrowdDemo 115/115通过。测试发现前两条既有`Condition failed`启动噪声保留记录，但没有失败测试。 |
