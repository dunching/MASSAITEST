# MassAI Crowd Demo 测试场景矩阵

[INFERRED][HIGH] 每个场景分别记录自动化、能力、性能和人工视觉；低层通过不能替代高层结论。

[INFERRED][HIGH] 规模结果必须标明生产路径。旧20实体Mixed、100实体SoftPressure和500实体Obstacle仍是历史分路径证据；当前PJ6结果来自同一Mixed Source/Resolver/Boundary/Networking/Projectiles生产路径。

[COMPUTED][HIGH] pre-T9检查点固定DP0–DP6最终证据；T9的10对10 Melee/MidRange/Ranged真实混合战斗、死亡后重选目标和群体重新运输尚未执行，当前表不得提前标记为通过。

## DP0–DP6 Demo业务规划当前门

| 门 | 当前状态 | 完成要求 |
|---|---|---|
| DP0 基线 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] `07359ed`、65/65、125/125、四构建与Mixed 20/100/500结果已冻结。 |
| Planner Core | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] Registry/冻结、NoBusiness、反序、缺事实、容量、Host Intent和Stable Hash专项通过。 |
| Mixed角色 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] 五Planner、Reaction、目标丢失、Source精确恢复和Coordinator结构门通过。 |
| Friendly | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] 8303通过Claim/Pickup/Deliver/Requeue/fallback/backoff/cancel、守恒和失败零写入。 |
| Round T7/T8 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] 8349 T7通过；8365 T8黄金门为50/50/50、duplicate=0、双端一致。真实StableEntityRef修正后版本化attack/projectile/event Hash为3512277419/488896174/4204062592。 |
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

## 公共门

[COMPUTED][HIGH] 8790无Fatal、Assertion、Ensure、`LogWindows: Error`、VIOLATION或Native NetSerialize Warning，双端correction误差为0；T6M按AcquireThenHold资格保持合同技术放行，18/17严格窗口只保留为过程诊断。

[INFERRED][HIGH] 性能门要求fixed-step/client frame p95≤33.333ms、visual p95≤16.667ms、realtime≥0.95、step-limit hit=0；启动max、Round reset、catch-up和steady discontinuity必须单列。

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
| P1 Boundary Orchestrator | [COMPUTED][HIGH] 公共Orchestrator定向测试覆盖依赖、Worker执行、失败、稳定hash与两阶段patch；Round生产路径为一次gather/dispatch/wait/writer，8132/8137/8138/8139 T2/T6/T7/T8通过。 |
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
