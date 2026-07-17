# MassAI Crowd Demo Feature Checklist

## 正式基础

- [x] [COMPUTED][HIGH] SF1 Shared Flow V2 与 SoftPressure Particle 正式链存在。
- [x] [COMPUTED][HIGH] fixed-step correction、SoftPressure rollback、双端 hash 和 client-only visual 已接入。
- [x] [COMPUTED][HIGH] Target Fact 支持静态与量化线性移动目标。

## Target Region Transport

- [x] [COMPUTED][HIGH] 8/16/32/64 Polar Navigation Cell 拓扑与稳定 CellKey。
- [x] [COMPUTED][HIGH] CW/CCW、跨环 overlap edge、Bounds/Obstacle/Target/swept edge 过滤。
- [x] [COMPUTED][HIGH] Shared Flow V2 source attachment。
- [x] [COMPUTED][HIGH] 固定 16 Demand Region、容量限制与人口守恒。
- [x] [COMPUTED][HIGH] deterministic integer min-cost transport 与旧 quota 复用。
- [x] [COMPUTED][HIGH] AgentId 稳定 quota 消费，无永久 Slot/Region owner。
- [x] [COMPUTED][HIGH] 15-step Plan、确定性重建原因、Topology/Demand/Transport/Guidance/Validation五类round hash。
- [x] [COMPUTED][HIGH] prepared SoA 与 correction rollback/replay。
- [x] [COMPUTED][HIGH] 旧 Polar Density 正式 processor 已删除，无生产 fallback。

## 自动化与构建

- [x] [COMPUTED][HIGH] Target Region Transport 5/5。
- [x] [COMPUTED][HIGH] Particle 23/23。
- [x] [COMPUTED][HIGH] 完整 `CrowdDemo.SF` 64/64（2026-07-17 JointPreferredRecovery回归）。
- [x] [COMPUTED][HIGH] Development Editor。

## 运行验收

- [x] [COMPUTED][HIGH] 8414 Static P0：inside band=`20/20`、feasible Region coverage=`16/16`、max Region population=`2`、unrouted=0。
- [x] [COMPUTED][HIGH] 8414 Static P0：Particle 四类安全违规=0、invalid/fallback=0、Transport 四 hash 双端一致、rollback miss/mismatch=0、agents/visible=`20/20`。
- [x] [COMPUTED][HIGH] 8416诊断：step 331双端fixture hash=`10240167`，确认terminal-anchor sink吸收导致Cell 176缺失outgoing quota。
- [x] [COMPUTED][HIGH] FeasibleGraphHash、actual soft clearance、Plan validator、Guidance consumption与round-sticky指标已接入。
- [x] [COMPUTED][HIGH] 8417 Static P0：20/20、16/16、Plan/Guidance unrouted=0、invalid/validation failure=0。
- [x] [COMPUTED][HIGH] 8418 Moving P0：20/20、12/12当前可行Region、Plan/Guidance unrouted=0、invalid/validation failure=0、双端hash与rollback通过。
- [ ] [COMPUTED][HIGH] 100/500 未运行。
- [ ] [COMPUTED][HIGH] DebugGame 正式门未运行。
- [ ] [COMPUTED][HIGH] FFmpeg 与人工审片未运行。

## 典型尝试关卡

- [x] [COMPUTED][HIGH] T1 Open Spawn Relaxation Small 真实 package、生产 processor、空间传播、双 settling、安全/同步门、录像与人工审片均已通过。
- [x] [COMPUTED][HIGH] T2 Open Cohort Movement Small真实package、Polar handoff生产接线、自动化与8426旧口径终态门已通过；稳定性V1待复验。
- [x] [COMPUTED][HIGH] T3 Open Bidirectional Swap Small使用通用LocalPredictive生产链；8507达到20/20交换、local samples=901/901、invalid=0且双端hash一致。
- [x] [COMPUTED][HIGH] T4 Valid Corridor Transit Small使用同一LocalPredictive生产链；8509达到wall/corridor/completed=`20/20/20`、local samples=901/901、invalid=0且双端hash一致。
- [x] [COMPUTED][HIGH] T5 Static 8521自动化、双端技术门与稳定性V1能力门通过：inside-band=`20/20`、coverage=`16/16`、sub-quantum supply=0、最终速度/位置抖动为0。
- [ ] [INFERRED][HIGH] T5 Static人工审片与T5 Moving尚未执行。
- [x] [COMPUTED][HIGH] T6 Heterogeneous Transit/Static/Moving三个Small package已创建；未运行。

## 异构群体

- [x] [COMPUTED][HIGH] SmallLight/Standard/LargeHeavy多尺寸、多Mobility profile已接入正式Mass fragment和Particle输入。
- [x] [COMPUTED][HIGH] Melee/MidRange/Ranged多Target中心距离带已接入CapabilityProfile cohort。
- [x] [COMPUTED][HIGH] 尺寸/质量与Target距离交叉组合已由纯自动化覆盖；真实T6运行未执行。
- [x] [COMPUTED][HIGH] per-profile Topology/Demand/Transport、Capability/Cohort hash和rollback已实现。
- [ ] [COMPUTED][HIGH] 跨profile Hard/Swept/Obstacle/Bounds、分层覆盖、unrouted和agents/visible门尚未验证。

## 禁止误报

[COMPUTED][HIGH] Static与Moving Small通过不能外推为100或500已通过。

[COMPUTED][HIGH] 当前同质T5通过不能外推为远程/近战、大/小或轻/重实体联合能力已经成立。

## T7 Combat/VAT 增量

- [x] [COMPUTED][HIGH] HitFact 稳定排序、Lifecycle 校验、EventId 去重、Health/Death、Knockback/KnockUp/LandingRecovery 与 VisualState resolver 纯内核已实现，三项定向测试通过。
- [x] [COMPUTED][HIGH] Combat 状态已进入 correction/rollback/replication/applied hash 合同。
- [x] [COMPUTED][HIGH] 原工程真实配置已核对并仅作为失败需求证据；不迁移其资源。
- [x] [COMPUTED][HIGH] 当前工程已重新生成五状态VAT：Idle=`0–24`、Move=`25–49`、Attack=`50–74`、HitReact=`75–99`、Death=`100–124`，总帧125。
- [x] [COMPUTED][HIGH] 静态网格、骨骼网格、5个AnimSequence、3张VAT纹理、DataAsset、5个状态材质实例、2个运行时材质实例和UV合同只读验证通过。
- [x] [COMPUTED][HIGH] 新 VAT 已替换 crowd 运行时 Cube；T7 package 与 8447 双端完整单轮技术运行通过。
- [x] [COMPUTED][HIGH] 8445近景录像确认三次`2/2/4`红色HitFlash窗口与绿色普通实体同时可见。
- [x] [COMPUTED][HIGH] T7近景动作审片已通过，Attack、水平HitReact、击飞顶点/落地和Death姿态均可辨识，20个实例全程可见。
- [x] [COMPUTED][HIGH] T8 projectile 已在不启用 Target Region Transport 的10射手+10静止目标场景完成独立验收。

## Mass Projectile与通用命中插件修正

- [x] [COMPUTED][HIGH] 已确认T8 20实体静止目标、swept hit、统一HitResponse、rollback和客户端视觉事件基线通过。
- [x] [COMPUTED][HIGH] 已确认当前权威Projectile仍在Pipeline数组，32个Mass pool实体仅镜像Active状态，碰撞为每Projectile全Agent扫描。
- [x] [COMPUTED][HIGH] 已核对外部Mass bullet/hash-grid、Mass projectile/world-trace和原工程Actor/Mass命中桥的复用与禁止照搬边界。
- [ ] [INFERRED][HIGH] 建立不含Actor/UObject/FMassEntityHandle的StableEntityRef、CollisionSnapshot、ImpactFact、HitFact与EffectProfile合同。
- [ ] [INFERRED][HIGH] 建立稳定Broadphase、移动目标相对sweep、环境sweep、Team/Layer/Lifecycle过滤及shape query共享出口。
- [ ] [INFERRED][HIGH] 将Projectile权威状态迁入Mass fragments/processors，删除`PreparedProjectiles`权威数组、`MirrorProjectileStates()`和固定32镜像pool。
- [ ] [INFERRED][HIGH] 原工程Actor目标与Mass目标通过两个Adapter消费同一HitFact；Mass目标不要求常驻Proxy Actor。
- [ ] [INFERRED][HIGH] 最小宿主、Demo T1–T8插件版、原工程命中/被命中与20/100/500并发Projectile规模门通过。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## T6M Demand Phase 修复增量

- [x] [COMPUTED][HIGH] 可行Region过滤后的归一化phase投影已实现。
- [x] [COMPUTED][HIGH] phase=10、可行Region=`[0,11,12,13,14,15]`的fixture选择`13,14,15`，不再折叠为`11,12,13`。
- [x] [COMPUTED][HIGH] Plan edge及Guidance consumption输入反序保持hash一致。
- [x] [COMPUTED][HIGH] Development、Transport 5/5及完整SF通过。
- [ ] [COMPUTED][HIGH] T6M能力为`19/20`、coverage=`19`，尚未关闭。
- [x] [COMPUTED][HIGH] 未通过的next-cell单准入生产实验已撤销。
- [x] [INFERRED][HIGH] 聚合Flow merge已重新归入通用Local Predictive Interaction设计，不再按Target/Cell专用admission建模。
- [x] [COMPUTED][HIGH] Local Predictive Interaction纯kernel、processor、LocalVelocity fragment、prepared SoA、hash、RoundResult与rollback已实现。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## Target Transport稳定落位验收V1

- [x] [COMPUTED][HIGH] Cell/Region/Anchor语义已对齐：Cell可共享通过，anchor不是多人必须占据的站位点。
- [x] [COMPUTED][HIGH] Particle职责已对齐：只负责Soft压力与Hard/Swept/Environment安全，不负责Cell容量或终态调度。
- [x] [COMPUTED][HIGH] 文档验收已拆分到达分布、merge吞吐、稳定落位、安全同步和人工审片五类。
- [x] [INFERRED][HIGH] 目标Cell不再被定义为固定容量入口；局部并发容量由预测速度共同可行域自然产生。
- [ ] [INFERRED][HIGH] 通用BlockedAge、公平grant、wait p95/max、零吞吐与starvation指标尚未实现。
- [ ] [INFERRED][HIGH] terminal进入/退出迟滞与有因transition分类尚未实现。
- [x] [COMPUTED][HIGH] 90-step Target-relative速度/位置、merge阻塞、chatter/attraction-rejection和Particle settling诊断已实现，支持rollback与双端hash。
- [ ] [COMPUTED][HIGH] T6M最新8496仍未通过稳定性V1；T6S 8479、T2 8426及T5 8417/8418也尚未按V1复验。

## Target稳定窗口诊断门

- [x] [COMPUTED][HIGH] Development与完整`CrowdDemo.SF` 57/57通过。
- [x] [COMPUTED][HIGH] 8496诊断server/client hash=`4081220932`一致，samples=`90/90`，无诊断VIOLATION。
- [x] [COMPUTED][HIGH] Terminal chatter=`0`，排除无因`TerminalStay↔Supply/Transport`反复切换作为首因。
- [ ] [COMPUTED][HIGH] merge阻塞未通过：2个实体连续90步被同next-cell争抢与Particle反向修正阻塞，首witness为Agent14→cell16。
- [ ] [COMPUTED][HIGH] 稳定终态未通过：inside-band最低17、coverage 16/20、settled window=0、位置peak-to-peak p95=`96.881cm`。

## Local Predictive Interaction 设计与实施门

- [x] [INFERRED][HIGH] 权威设计明确生产kernel不得读取TestCase、地图名、Portal、Target或窄口语义。
- [x] [INFERRED][HIGH] 职责链明确为`Shared Guidance → Local Predictive Interaction → MovementPredict → Particle Safety`。
- [x] [COMPUTED][HIGH] 代码审查确认SoftPressure当前未运行旧ORCA；旧ORCA Agent/processor混有历史Portal/Admission/SF4语义。
- [x] [INFERRED][HIGH] 复用边界限定为提取通用half-plane连续求解、验证与量化修复，不直接复用旧Agent、fallback或processor。
- [x] [COMPUTED][HIGH] `CrowdDemoVelocityHalfPlaneKernel`提取与旧数值fixture parity已实施。
- [x] [COMPUTED][HIGH] LocalPredictive POD、稳定swept pair、constraint component、联合可行性、量化安全与有限期grant纯测试已实施。
- [x] [COMPUTED][HIGH] 独立LocalVelocity fragment、processor、prepared SoA、hash、rollback和RoundResult指标已实施。
- [x] [COMPUTED][HIGH] T3 8507与T4 8509使用同一局部kernel通过能力、安全、hash和rollback门。
- [x] [COMPUTED][HIGH] T5稳定窗口诊断已覆盖Region Desired/Current、terminal/supply成员、enter/exit、Edge quota/consumption和Guidance/Local/Predict/Applied阶段，双端hash与rollback一致。
- [x] [COMPUTED][HIGH] 8517最终8实体fixture、双端hash、输入反序和安全共同平移自动化已建立；明确排除CommonVelocity为原低速首因。
- [x] [COMPUTED][HIGH] 8518六实体fixture和JointPreferredRecovery自动化已建立；量化联合解保持完整安全并让两个供给实体前向速度超过200cm/s。
- [x] [COMPUTED][HIGH] T5 Static 8521为Region coverage=`16/16`、`sub_quantum_supply=0`，LocalPredictive samples=`901/901`、invalid=`0/0`且双端hash一致。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 2026-07-17 Round 与动态 Flow 验收清单

- [x] [COMPUTED][HIGH] RoundStartPolicy显式化，全部Demo验证场景使用稳定初态重置。
- [x] [COMPUTED][HIGH] Round 1/2通过同一纯kernel生成初态；8487两轮input/initial hash完全相同且双端一致。
- [x] [COMPUTED][HIGH] Shared Flow Topology与动态Goal Integration接口拆分；anchor cell改变才发生语义上的Integration rebuild。
- [x] [COMPUTED][HIGH] 动态Flow的anchor、Integration、rebuild与round hash进入RoundResult、双端比较和rollback合同。
- [x] [COMPUTED][HIGH] Development Editor与完整`CrowdDemo.SF` 56/56通过。
- [x] [COMPUTED][HIGH] 8487两轮Particle安全、Transport validity、rollback、同步误差和agents/visible技术门通过。
- [ ] [COMPUTED][HIGH] T6M能力门：inside-band仅`17/20`，可行Region coverage为`16`；不得标记通过。
- [ ] [COMPUTED][HIGH] 两个独立Round 1最终AgentState hash比较未运行，因为连续两轮能力门已先失败。
- [ ] [COMPUTED][HIGH] DebugGame、旧场景/processor/fragment删除、GT/WORK边界、RoundResultHeader 2048字节门和清理后T1–T8回归未执行。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## T6 异构 Small 最新验收

- [x] [COMPUTED][HIGH] T6A 8464：20/20 通道完成、跨 profile 硬安全、双端 hash、rollback 与 20/20 显示通过。
- [x] [COMPUTED][HIGH] T6S 8479：7 个 capability cohort 静态目标距离带与 Region 覆盖、unrouted/validation、安全、双端 hash、rollback 与 20/20 显示通过旧口径；稳定性V1待复验。
- [x] [COMPUTED][HIGH] Moving FarFlow 的 `SharedFlowPreferred + TargetVelocity` 纯函数、MaxSpeed 截断和双 processor 接线已通过 5/5 定向与 54/54 完整自动化。
- [ ] [COMPUTED][HIGH] T6M 8481：仅 `10/20` 进入对应有效距离带；LargeMelee、StandardMelee 为 `0/3`，能力门未关闭。
- [x] [COMPUTED][HIGH] T6M 8481 Round 1 的 Particle Hard/Swept/Obstacle/Bounds、invalid/fallback、双端 hash、rollback miss/mismatch、位置误差与 agents/visible 技术门均通过。
- [ ] [COMPUTED][HIGH] T6 DebugGame、T6A/T6S/T6M录像与人工审片未执行。
- [ ] [COMPUTED][HIGH] Mass Projectile 插件生产迁移仍被 T6M 前置门阻塞。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 远程攻击、VAT 与受击响应

- [x] [COMPUTED][HIGH] 外部MassAIExample/MassSample/ue-mass-extension-plugin与原工程参考边界已记录到权威设计文档。
- [x] [INFERRED][HIGH] BusinessState、AttackPhase、ReactiveMotionMode、VisualState与HitFlash职责已在设计中分离。
- [x] [COMPUTED][HIGH] 当前工程自制AnimToTexture资源、五状态材质实例、运行时手动播放材质和共享VAT profile已生成；crowd运行时使用真实VAT静态网格。
- [x] [COMPUTED][HIGH] Attack/HitReact/Death VisualState与唯一resolver已实现；真实VAT已按同步ServerTime播放。
- [x] [COMPUTED][HIGH] HitFact、伤害去重、击退、击飞、落地恢复和HitFlash状态已实现并通过纯测试；红色命中overlay与完整受击动作已在8445近景录像中可见。
- [x] [COMPUTED][HIGH] Projectile数组权威状态、32实体Mass镜像pool/fragment、稳定batch spawn、fixed-step previous→proposed运动、swept collision和统一hit resolve已实现并通过T8自动化与8451双端运行；该条不代表entity-native或规模门通过。
- [x] [COMPUTED][HIGH] Projectile Spawn/Impact/Expire客户端视觉事件和数量守恒已实现；8451为`50/50/0`且active=0，与服务端事件完全一致。
- [x] [COMPUTED][HIGH] T7 Multi-State VAT and Hit Response Small真实package已创建，8447完整单轮技术运行与双端同步通过。
- [x] [COMPUTED][HIGH] T7七项定向自动化、46项完整SF、双端技术门和8445近景人工审片均已通过，最终能力门关闭。
- [x] [COMPUTED][HIGH] T8 Ranged Projectile Combat Small真实package已创建；7/7定向自动化、46/46完整SF、8451技术门和8450人工视觉门通过。
- [ ] [COMPUTED][HIGH] T9 Mixed Combat Integration尚未设计；100/500战斗能力不得外推。

[COMPUTED][HIGH] Ranged capability本身仍只表示Target terminal distance band；T8通过的是另一条显式攻击业务链，已经证明静止目标条件下能够windup、射击、造成damage并播放Attack/HitFlash/Death VAT。

## 插件化与类游戏Sandbox前置门

- [ ] [INFERRED][HIGH] T3/T4已完成20实体自动化、双端技术、能力和人工审片门；T6A/T6S/T6M仍需分别关闭相同层级的独立门。
- [x] [COMPUTED][HIGH] T7已完成近景Attack/HitReact/Death人工审片并关闭最终视觉门。
- [x] [COMPUTED][HIGH] T8独立投射物、swept命中、统一HitFact、correction rollback和客户端视觉事件门通过。
- [ ] [INFERRED][HIGH] T1-T8在Development与DebugGame Editor下无已知Fatal、Assertion、Ensure、`LogWindows: Error`或VIOLATION。
- [ ] [INFERRED][HIGH] 可复用kernel、Mass运行时、rollback/hash和VAT适配从Demo场景代码中提取为无反向依赖插件。
- [ ] [INFERRED][HIGH] 插件在不包含Demo地图和Coordinator的最小宿主工程中完成构建与自动化。
- [ ] [INFERRED][HIGH] Demo切换为插件实现后重新通过T1-T8，且不存在长期旧实现/插件实现双路径。
- [ ] [INFERRED][HIGH] 插件迁移主工程前完成代表性20/100/500 Shared Flow、Particle、correction、完整显示和业务事件回归。
- [ ] [INFERRED][HIGH] T10 Gameplay Swarm Sandbox实现玩家Pawn、持续有界刷怪、fixed-step线形/圆形伤害、击退、击飞、死亡和动态membership。
- [ ] [INFERRED][HIGH] NavMesh稳定Surface Graph、分层Flow、坡道贴合、桥上桥下隔离和合法Landing通过独立高低差地图验收。

[COMPUTED][HIGH] 上述插件与T10功能当前均未实施；不得将目标条目写成现有能力。权威顺序见`GameplaySwarmSandboxAndPluginMigrationPlan.md`。

[COMPUTED][HIGH] 2026-07-17 Projectile插件前置核对最初在T3停止；T3已由8455/8456关闭，T4已由8460/8461关闭，但T6仍未验收，因此仍没有创建插件、最小宿主或新Moving/Wall projectile场景，也没有删除旧T8路径。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 2026-07-15 典型关卡增量

- [x] [COMPUTED][HIGH] 七张 T1/T2/T3/T4/T6 Small 真实 package 已创建，旧 T5 地图未改动。
- [x] [COMPUTED][HIGH] Capability/Profile 自动化 3/3、完整 SF 43/43、Development Editor 通过。
- [x] [COMPUTED][HIGH] 固定 20 实体 profile membership、逐 cohort Transport 与五类 hash、rollback prepared cohort 已接入。
- [x] [COMPUTED][HIGH] T1 Round 1 安全门：Hard/Swept/Obstacle/Bounds=0，invalid/fallback=0，agents/visible=20/20，双端 candidate/applied hash 一致，rollback miss/mismatch=0，误差 p95=0。
- [x] [COMPUTED][HIGH] T1 能力门：phase Completed，active=`0→5→10→15→19→20→19`，BFS layer max=3，insert/post-removal settling step=`78/94`，new-equilibrium displaced agents=12。
- [x] [COMPUTED][HIGH] T1 双端门：Hard/Swept/Obstacle/Bounds 与 invalid/fallback 均为 0，Particle 与三类 T1 hash 一致，rollback=`53/0/0` 且 replayed=59，误差 p95=0cm，agents/visible=`20/20`。
- [x] [COMPUTED][HIGH] T1 自动化 3/3、Particle 23/23、Capability 3/3、完整 SF 43/43、Development Editor 和 `git diff --check` 通过。
- [x] [COMPUTED][HIGH] T1 8424 视频、亮度 QA 与 contact sheet 已生成；人工审片未见隐藏实例、整体爆散、持续抖动或错误 visual owner。
- [x] [COMPUTED][HIGH] T2 开放 Flow 隔离、稳定 20 实体布局、Transport/Distance Band、双端 layout/route/progress hash 与 correction rollback 已接入；T2 3/3、Transport 5/5、Particle 23/23、完整 SF 46/46 通过。
- [x] [COMPUTED][HIGH] 8425 T2 安全/同步门通过：corridor=20、final deadlock=0、四类安全违规=0、invalid/fallback=0、双端 hash 一致、rollback miss/mismatch=0、agents/visible=20/20。
- [x] [COMPUTED][HIGH] 8425旧合同数据已重新分类：`goal=16/20`、never reached=4、Flow contract violation=68仅表示140cm点目标合同与宽cohort不相容，不再用于T2最终能力判定。
- [x] [COMPUTED][HIGH] 8426 T2 旧口径能力门通过：flow approach/handoff=`20/20`、inside-band=`20/20`、feasible Region coverage=`16/16`、最大 Region 人口=`2`、Plan/Guidance unrouted=`0/0`、validation failure=`0`、当帧terminal mode=`20/20` at step `435`、final deadlock=`0`；该字段不证明连续静止。
- [x] [COMPUTED][HIGH] 8426 T2 安全/同步门通过：Hard/Swept/Obstacle/Bounds=`0/0/0/0`、invalid/fallback=`0/0`、Transport 五类与 Particle/T2 hash 双端一致、rollback hit/miss/mismatch=`53/0/0`、replayed=`55`、误差 p95=`0cm`、agents/visible=`20/20`。
- [x] [COMPUTED][HIGH] T3已使用两侧双cohort、中心穿越和完成平面，不以140cm点目标计能力门；8455完成20/20交换。
- [x] [COMPUTED][HIGH] T4使用wall/corridor/completion出口平面而非140cm点目标；8460达到20/20完成且final deadlock=0。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
