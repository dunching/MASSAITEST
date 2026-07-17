# MassAI Crowd Demo Phase Plan

## 当前唯一阶段：T5 Static联合速度恢复完成，等待人工审片

- [x] [COMPUTED][HIGH] 8496只读稳定窗口已排除Terminal chatter，并确认Shared Guidance持续请求与Particle反向修正共同形成2实体、90步低进展。
- [x] [INFERRED][HIGH] 架构职责已修正为`Shared Guidance → Local Predictive Interaction → MovementPredict → Particle Safety`，不再把问题定义为T6专用Cell admission。
- [x] [INFERRED][HIGH] 已建立`LocalPredictiveInteractionDesign.md`，定义无场景语义的POD、轨迹冲突component、half-plane速度求解、几何并发、公平让行、hash、rollback和测试矩阵。
- [x] [COMPUTED][HIGH] 已审查现有代码：SoftPressure未执行旧ORCA；旧ORCA数值内核可部分提取，但其Portal/Admission/SF4 Agent与processor不得直接复用。
- [x] [COMPUTED][HIGH] 已提取通用`VelocityHalfPlaneKernel`并完成旧数值fixture parity；已新增LocalPredictive kernel、LocalVelocity fragment、processor、prepared SoA、hash、RoundResult与rollback。
- [x] [COMPUTED][HIGH] Development Editor、LocalPredictive 7/7和完整`CrowdDemo.SF` 64/64通过。
- [x] [COMPUTED][HIGH] 8507 T3达到20/20双向交换；8509 T4达到wall/corridor/completed=`20/20/20`；两次运行LocalPredictive均为901/901、invalid=0且双端hash一致。
- [x] [COMPUTED][HIGH] 已增加稳定窗口Region/Edge/供给实体阶段诊断，覆盖Demand、Plan quota、Guidance、terminal成员、进入/离开和`Guidance→Local→Predict→Applied`事实；定向测试与完整`CrowdDemo.SF` 62/62通过。
- [x] [COMPUTED][HIGH] 8517完整8实体fixture排除`CommonVelocity`为8515低速首因，并证明保持相对速度的component共同平移存在安全、更低目标误差的可行解。
- [x] [COMPUTED][HIGH] 已接入通用CoherentTranslation与JointPreferredRecovery并通过Development、LocalPredictive 7/7和完整SF 64/64；不包含T5/Region/Agent特判。
- [x] [COMPUTED][HIGH] 8518六实体fixture证明Agent 5/19存在前向超过200cm/s的量化联合安全解；原9.22cm/s是冻结邻居独立求解的局部最优。
- [x] [COMPUTED][HIGH] 8521 T5 Static单轮达到inside-band=`20/20`、coverage=`16/16`、sub-quantum supply=0，最终90步速度/位置抖动为0，双端安全、hash和误差门通过。
- [ ] [INFERRED][HIGH] T5 Static尚未人工审片；T5 Moving和T6M未运行。

[INFERRED][HIGH] 下一任务应先录制并人工审查T5 Static稳定窗口；通过后才运行T5 Moving。不得直接进入T6M、删除旧场景2/3或进入100/500。

## 当前阶段：T5 Target Region Transport 收敛

[COMPUTED][HIGH] 本阶段已完成纯 kernel、生产 processor、rollback/hash、Static P0 门和旧 Density 正式 processor 删除。

### 已完成

- [x] [COMPUTED][HIGH] 修复 Target Influence 诊断的 Plan 激活锁定、采样和 rollback 基础设施。
- [x] [COMPUTED][HIGH] 实现稳定 Polar Topology：8/16/32/64 扇区、跨环 overlap edge、环境与 swept edge 过滤。
- [x] [COMPUTED][HIGH] 实现固定 16 Region 的 Capacity/Current/Desired/Deficit/Surplus。
- [x] [COMPUTED][HIGH] 实现整数 deterministic min-cost transport、旧 quota 复用和 AgentId 稳定 guidance。
- [x] [COMPUTED][HIGH] 用 Shared Flow V2 next-cell 链实现远距离 source attachment。
- [x] [COMPUTED][HIGH] 接入 Topology/Population/Transport/Guidance 四个双端 processors。
- [x] [COMPUTED][HIGH] 将 prepared SoA、PlanEpoch、quota、hash、重建计数和 solver 样本接入 correction rollback。
- [x] [COMPUTED][HIGH] Target Region Transport 自动化 4/4、Particle 23/23、完整 SF 43/43 和 Development 通过。
- [x] [COMPUTED][HIGH] 8414 Static P0 通过 20/20 有效带、16/16 可行 Region、unrouted=0 和全部安全/同步门。
- [x] [COMPUTED][HIGH] 旧 Polar Density guidance processor 已从生产代码删除，Transport 不含旧 Density fallback。

### 动态图合同收敛

- [x] [COMPUTED][HIGH] 8416固定step 331完整20实体fixture，唯一归因为terminal-anchor source被sink直接吸收。
- [x] [COMPUTED][HIGH] FeasibleGraphHash覆盖完整Cell/Edge执行结构和实际soft clearance成本。
- [x] [COMPUTED][HIGH] Plan validator与Guidance quota consumption合同已接入同一boundary重建。
- [x] [COMPUTED][HIGH] Round-sticky valid、Plan/Guidance拆分指标、Validation hash与rollback已接入。
- [x] [COMPUTED][HIGH] 8417 Static与8418 Moving Small均通过。
- [ ] [COMPUTED][HIGH] 按本任务边界未运行100/500、DebugGame正式门、录像或FFmpeg。

## 当前任务：典型尝试关卡与异构20实体合同

- [x] [COMPUTED][HIGH] 用Unreal Python创建T1/T2/T3/T4/T6A/T6S/T6M真实Small package，复用T5S/T5M，不复制`.umap`。
- [x] [COMPUTED][HIGH] 所有新地图包含统一Lighting、PreviewFloor、PlayerStart、固定相机和场景配置Actor。
- [x] [COMPUTED][HIGH] 建立SmallLight/Standard/LargeHeavy三档PhysicalRadius/Mobility纯POD profile和异构Particle fixture。
- [x] [COMPUTED][HIGH] 建立Melee/MidRange/Ranged三档Target中心距离带和稳定CapabilityProfileKey。
- [x] [COMPUTED][HIGH] 按profile稳定分cohort，共享Topology/Demand/Transport，不为每个实体独立建图。
- [x] [COMPUTED][HIGH] 增加Capability/Cohort hash、per-profile RoundResult、rollback和agents/visible指标。
- [x] [COMPUTED][HIGH] T1 独立生产链、真实 Soft correction BFS、双 settling、rollback/hash 与 RoundResult 指标已接入并通过。
- [x] [COMPUTED][HIGH] 8422 T1 技术门与能力门通过；8424 已录制视频并生成 contact sheet，仍按任务边界不继续 T2→T6。
- [x] [COMPUTED][HIGH] T2 已复用 Target Region Transport 与 Distance Band，8426 原 P0 Small 20 单轮通过旧口径终态门、安全门、双端 hash 与 correction replay；稳定性V1待复验。
- [ ] [INFERRED][HIGH] 只有对应Small三级验收通过后才设计100/500、DebugGame和真实WORK门。

[INFERRED][HIGH] TransportSpeed、PlanLifetime、Region数量、现有Particle安全公式以及旧Polar Density/Slot/owner不因异构需求重新引入或暗中修改。

## T7 实施状态（2026-07-16）

- [x] [COMPUTED][HIGH] 已新增独立 Business/Attack/Reactive/HitFlash/VisualState 数据、确定性 HitFact 内核、fixed-step reactive ballistic 与三项 `CrowdDemo.Combat.T7` 纯自动化。
- [x] [COMPUTED][HIGH] Round checkpoint、correction、SoftPressure rollback、FastArray replication 与 applied-state hash 已携带 Combat 状态，不再在客户端强制恢复 Alive。
- [x] [COMPUTED][HIGH] T7 生产顺序已接入 `HitResponseBoundaryApply → ReactiveMotionIntentCompose → MovementPredict → Particle → MovementFinalize → VisualStateResolve`；T8 已在该边界上增加确定性攻击 phase、Mass projectile、swept hit 与客户端 visual event。
- [x] [COMPUTED][HIGH] 已取消原工程资源迁移，并通过Blender+完整UE Editor生成独立静态网格、骨骼网格、5个AnimSequence、125帧Bone VAT、5个状态材质实例和2个运行时手动播放材质实例；Death=`100–124`合法。
- [x] [COMPUTED][HIGH] VAT只读验证通过：五段范围、UV1、三张纹理尺寸、资产引用与材质Parent均有效。
- [x] [COMPUTED][HIGH] 已创建真实 T7 package，并将 VAT mesh/runtime material 接入 crowd ISM；客户端按 VisualState/Revision/ServerTime 播放五段 VAT，提交 Frame/PreviousFrame/HitFlashIntensity。
- [x] [COMPUTED][HIGH] 8447 原参数完整单轮技术门通过：20/20 VAT instances、Particle 四类安全违规为零、双端 candidate/applied hash=`961313407/3034185224`、rollback `54/0/0`、replayed steps=`61` 与位置误差 p95=`0cm`。
- [x] [COMPUTED][HIGH] 已修复视觉单位缩放与句柄派生窗口捕获；8445近景录像逐帧确认真实VAT虫体、`2/2/4`红色HitFlash、水平击退、击飞顶点/落地、Attack与Death姿态可辨识。
- [x] [COMPUTED][HIGH] T7七项定向自动化、46项完整`CrowdDemo.SF`、双端技术门与近景人工视觉门全部通过。
- [x] [COMPUTED][HIGH] T8 projectile 已完成7/7定向自动化、46/46完整SF、8451双端单轮技术门和8450录像人工审片；本结果只覆盖10射手+10静止目标，不表示移动目标、T9或100/500已经成立。

## 2026-07-15 执行结果与停止点

- [x] [COMPUTED][HIGH] Development Editor、Capability 3/3、完整 `CrowdDemo.SF` 43/43 通过。
- [x] [COMPUTED][HIGH] Capability/Profile 纯内核、逐实体 fragments、按 profile 的 Target Region prepared cohort、RoundResult/hash 与 rollback 接入完成。
- [x] [COMPUTED][HIGH] T1/T2/T3/T4/T6A/T6S/T6M 七张 20 实体真实 package 已创建；每张包含规定 Lighting、PreviewFloor、PlayerStart、固定俯视 Camera 与配置 Actor。
- [x] [COMPUTED][HIGH] T1 能力门通过：phase Completed、BFS layer max=3、insert/post-removal settling=`78/94`、移除后 12 个保留实体形成非旧布局的新平衡。
- [x] [COMPUTED][HIGH] T1 自动化 3/3、Particle 23/23、Capability 3/3、完整 SF 43/43 与 Development Editor 通过。
- [x] [COMPUTED][HIGH] 8424 录像与 contact sheet 已完成；人工逐帧检查未见整体爆散、实例隐藏或持续抖动，staging→分批激活→插入→移除后的稳定末态可见。
- [x] [COMPUTED][HIGH] T2 已接入独立开放 Flow 配置、稳定阵型、Target Region Transport/Distance Band、双端 checkpoint、进度 rollback 与回归自动化；完整 `CrowdDemo.SF` 更新为 46/46。
- [x] [COMPUTED][HIGH] T2 定向自动化 3/3、Target Region Transport 5/5、Particle 23/23、完整 `CrowdDemo.SF` 46/46 与 Development Editor 通过。
- [x] [COMPUTED][HIGH] 8426 T2 原 P0 Round 1 达到 inside-band=`20/20`、feasible Region coverage=`16/16`、最大 Region 人口=`2`、unrouted=`0`、validation failure=`0`、当帧terminal mode=`20/20`、final deadlock=`0`；没有连续静止证明。
- [x] [COMPUTED][HIGH] 8426 四类 Particle 安全违规、invalid/fallback 均为 0；Transport 五类 hash、Particle candidate/applied hash 与 T2 progress hash 双端一致，rollback=`53/0/0`、replayed=`55`，误差 p95=`0cm`，agents/visible=`20/20`。
- [x] [COMPUTED][HIGH] T3双cohort布局、相反Shared Flow、中心/完成平面、独立hash/rollback和4项自动化已接入；8455达到center/completed=`20/20`、throughput difference=0、final deadlock=0，8456录像与contact sheet已完成。
- [x] [COMPUTED][HIGH] T4稳定布局、Shared Flow V2、wall/corridor/completion平面、独立hash/rollback和3项定向自动化已接入；8460达到wall/corridor/completed=`20/20/20`、final deadlock=0，8461录像与contact sheet已完成人工审片。

[COMPUTED][HIGH] 当前准确停止点已推进到T4 Small关闭：8460完成自动化、双端技术与20/20能力门，8461完成人工审片。下一任务进入T6A/T6S/T6M异构Small验收；Projectile插件迁移仍不得提前开始。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 当前停止点更新：T6M 19/20

- [x] [COMPUTED][HIGH] 通过8490见证定位不可行Region过滤后的phase折叠。
- [x] [COMPUTED][HIGH] 将Demand phase投影到稳定可行Region序列，并增加边界可行集合红测与反序hash覆盖。
- [x] [COMPUTED][HIGH] Guidance稳定排序Plan edges，消除consumption hash对输入数组顺序的依赖。
- [x] [COMPUTED][HIGH] 最终Development、Target Region Transport 5/5和完整`CrowdDemo.SF`通过。
- [ ] [COMPUTED][HIGH] T6M仍为inside-band=`19/20`、coverage=`19`，Agent15超出LargeHeavy Melee上界`8.3cm`。
- [x] [COMPUTED][HIGH] 失败的next-cell单准入实验已撤销；未继续叠加第二种调度策略。
- [ ] [COMPUTED][HIGH] T6M未通过，因此架构删除、DebugGame、独立Round 1比较、GT/WORK迁移和Projectile插件仍未开始。

[INFERRED][HIGH] 下一任务先增加只读稳定窗口诊断，区分同next-cell容量争抢、Terminal/Supply状态chatter和Particle未收敛；随后为多source聚合Flow建立纯执行fixture和容量/时序合同。必须先证明不会降低coverage、制造starvation或引入永久Slot，再接生产。

## 当前阶段更新：T6M merge容量已唯一归因

- [x] [COMPUTED][HIGH] 默认关闭的90-step Target稳定窗口诊断、rollback、紧凑RoundResult及双端hash比较已接入。
- [x] [COMPUTED][HIGH] Development Editor通过；`CrowdDemo.SF.TargetStabilityDiagnostic`通过；完整`CrowdDemo.SF`为57/57通过。
- [x] [COMPUTED][HIGH] 8496原P0单轮诊断有效，server/client hash=`4081220932`一致，无Terminal chatter。
- [ ] [COMPUTED][HIGH] T6M仍未通过：最后90步inside-band最小17、coverage 16/20；2个实体在同next-cell merge连续90步低进展。
- [ ] [INFERRED][HIGH] 下一任务建立通用Local Predictive Interaction纯fixture；不得直接恢复失败的单cell单准入实验，也不得把T6M专用merge语义写入生产kernel。

[COMPUTED][HIGH] 当前仍不执行SF3/SF4删除、GT/WORK迁移、Projectile插件、100/500、DebugGame正式门或录像。

### 当前验收口径修正

- [x] [COMPUTED][HIGH] 文档已明确Flow/Navigation Cell是共享通行区域，不是per-agent站位或必须命中的精确点。
- [x] [COMPUTED][HIGH] 文档已明确当前`terminal_settled_count`只有当帧Guidance mode语义，没有连续静止证明。
- [x] [COMPUTED][HIGH] Target类场景验收V1已增加90-step到达/分布窗口、merge容量/等待、Target-relative速度/位置抖动、状态chatter、Particle settling和人工审片门。
- [x] [COMPUTED][HIGH] 生产代码已实现通用局部预测速度、有限期grant和rollback；terminal迟滞仍属于Transport/Demand终态合同，尚未实施。
- [ ] [INFERRED][HIGH] 8426、8417/8418与8479尚未按稳定性V1复验，旧结果只保留为到达、安全和同步证据。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 当前唯一阶段：T6M Round/动态 Flow 合同已修复，能力门停止

- [x] [COMPUTED][HIGH] 增加显式 RoundStartPolicy 与稳定初态纯kernel，删除 Revision==1 的隐式Round重置语义。
- [x] [COMPUTED][HIGH] 修复结果冻结、Plan网络预发布、本地激活和静止boundary重新claim的顺序；Round 2能够完整运行。
- [x] [COMPUTED][HIGH] 拆分Shared Flow Topology、Goal Anchor与Integration，并在动态anchor cell改变时确定性重建Integration。
- [x] [COMPUTED][HIGH] Development Editor与完整`CrowdDemo.SF` 56/56通过。
- [x] [COMPUTED][HIGH] 8487 T6M原P0连续两轮完成；初态、动态Flow、Transport与Particle关键hash双端一致，安全、rollback、同步和完整显示门通过。
- [ ] [COMPUTED][HIGH] T6M能力门未通过：两轮均为inside-band=`17/20`、可行Region coverage=`16`，不是要求的aggregate=`20/20`。
- [ ] [COMPUTED][HIGH] 因能力门失败，没有运行两个独立Round 1进程、DebugGame、架构删除、GT/WORK迁移、结果头收缩或T1–T8清理后回归。

[INFERRED][HIGH] 当前准确停止点是“T6M三实体终态能力归因”。在该门关闭前，不得删除SF3/SF4，不得启动Projectile插件迁移、100/500、T9/T10、自由游荡或动态刷怪。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 2026-07-17 T6 异构 Small 执行结果

- [x] [COMPUTED][HIGH] T6A 8464 技术门与能力门通过。
- [x] [COMPUTED][HIGH] T6S 8479 技术门与旧口径能力门通过；同距离带 external population、真实异构 SoftDistance cost 与 `ExternalPopulationHash` Plan revision 合同已由真实运行验证，稳定性V1待复验。
- [x] [COMPUTED][HIGH] 移动目标 FarFlow 速度平移合成已接入纯 kernel 和双端 processor；Development Editor、Target Region Transport `5/5`、完整 `CrowdDemo.SF` `54/54` 通过。
- [ ] [COMPUTED][HIGH] T6M 8481 能力门失败：inside-band/coverage=`10/20`，安全、同步、hash、rollback 和显示门通过。
- [ ] [COMPUTED][HIGH] T6M 失败后未运行 DebugGame、正式录像或人工审片，未启动 Mass Projectile 插件迁移。

[INFERRED][HIGH] 当前下一任务必须单独设计“移动目标空间锚点与 Shared Flow 场平移/重建合同”，先用纯 fixture 证明空间采样与 Target 位移一致，再重跑 T6M。不得继续用速度叠加、直接追目标或调 Particle 参数掩盖静态势场锚点问题。

[COMPUTED][HIGH] 当前准确停止点为 T6A/T6S 关闭、T6M 首轮能力失败；本轮没有进入第二修复分支。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 已完成独立阶段：多状态 VAT、受击响应与静止目标远程投射物

[COMPUTED][HIGH] T7 的状态、受击、VAT 资产、运行时播放、地图、单轮双端技术验证和近景人工审片均已完成；T8 的静止目标攻击phase、Mass projectile、swept hit、damage、客户端事件视觉、地图、双端技术门和人工审片也已完成。

- [x] [COMPUTED][HIGH] 固定MassAIExample、MassSample、ue-mass-extension-plugin和原工程参考版本，记录可复用与禁止照搬边界。
- [x] [INFERRED][HIGH] 将BusinessState、AttackPhase、ReactiveMotionMode、VisualState和HitFlash拆为独立合同。
- [x] [INFERRED][HIGH] 定义服务端权威HitFact、fixed-step击退/击飞、Particle安全闭环和客户端时间派生命中改色。
- [x] [INFERRED][HIGH] 定义轻量Mass projectile、稳定批量spawn、previous→proposed swept collision和Spawn/Impact/Expire视觉事件。
- [x] [COMPUTED][HIGH] 原工程只读审计已完成；已改为不迁移旧资产、在当前工程重新生成五状态VAT profile。
- [x] [COMPUTED][HIGH] 多状态、唯一VisualState resolver、rollback/hash及T7纯自动化已实现。
- [x] [COMPUTED][HIGH] T7 HitFlash 材质可见链与近景录像人工审片完成，已分别确认击退、击飞、落地、五段 VAT 和命中改色。
- [x] [COMPUTED][HIGH] 已实现远程攻击阶段、轻量Mass projectile、swept hit、统一HitResponse与Spawn/Impact/Expire事件复制。
- [x] [COMPUTED][HIGH] 已创建并验收T8 Ranged Projectile Combat Small；最终技术证据为8451，人工录像证据为8450。
- [ ] [INFERRED][HIGH] T7/T8分别通过后，才允许设计T9 Mixed Combat Integration；不直接进入100/500。

[COMPUTED][HIGH] 本独立阶段没有撤销T2当前停止事实，也没有把T2/T6距离带误写成攻击能力。T8完成后在此停止，不自动进入T9、移动目标、100/500或原工程资源迁移。

## 后续强制阶段：现有测试收敛 → 插件化 → 类游戏Sandbox

[INFERRED][HIGH] 在新增玩家Pawn、持续刷怪、形状伤害、NavMesh Flow或高低差地图前，必须先完成剩余T6真实Small验收，并重新通过Development、DebugGame Editor和完整自动化；T3/T4、T7近景视觉门与T8独立投射物门已完成。

[INFERRED][HIGH] T1-T8无已知硬失败后，下一阶段不是直接实施综合业务场景，而是先提取可复用插件：纯kernel、最小Mass数据/processor、fixed-step接口、rollback/hash与VAT播放适配进入插件；Coordinator、测试地图、端口、录像、fixture和场景脚本继续留在Demo。

[INFERRED][HIGH] 插件必须先通过独立最小宿主构建/自动化，再回到Demo重跑T1-T8；随后才能设计T9 Mixed Combat和T10 Gameplay Swarm Sandbox。

[INFERRED][HIGH] T10目标包括玩家移动、固定步GameplayCommand、稳定线形/圆形命中、持续但有上限的敌群生成、动态cohort、伤害/击退/击飞/死亡，以及NavMesh分层Surface Flow和真实高低差地图；详细阶段与迁移门见`GameplaySwarmSandboxAndPluginMigrationPlan.md`。

## 已排队插件修正：Mass Projectile与通用Hit接口

[COMPUTED][HIGH] T8静止目标Small已通过，但当前Projectile仍是Pipeline数组权威、Mass pool镜像和全Agent扫描；它不能原样封装为大量远程敌人的最终插件实现。

[INFERRED][HIGH] 在T3/T4/T6等现有Small门大致收敛后，插件阶段按以下顺序实施：通用StableEntityRef/ImpactFact/HitFact → 稳定spatial grid → 移动目标与环境sweep → entity-native Mass Projectile → 最小宿主 → Demo T1–T8插件版回归 → 原工程Combat Adapter → 20/100/500与并发Projectile规模门。

[INFERRED][HIGH] 原工程现有Actor伤害、Mass被命中、状态、击退/击飞、死亡、掉落和表现只通过Adapter消费插件HitFact；不得把Actor指针、VisualId代理或项目ItemTag写入插件纯kernel。

[INFERRED][HIGH] 完整接口、处理顺序、回滚和删除条件见`MassProjectileHitFrameworkDesign.md`。本轮只更新文档，不实施Source修改。

## 2026-07-17 Mass Projectile插件前置核对结果

- [x] [COMPUTED][HIGH] `git diff --check`和Development Editor通过。
- [x] [COMPUTED][HIGH] 当前完整`CrowdDemo.SF` 46/46通过且无禁止错误。
- [x] [COMPUTED][HIGH] 已只读核对Demo T8、原工程Combat/Mass projectile接口及外部BulletHell、ProjectileSim、Octree参考实现。
- [x] [COMPUTED][HIGH] 8452 T3底层技术门通过但生产合同缺失；随后已完成T3生产接线，8455达到双cohort center/completed=`10+10/10+10`、安全/同步/hash/rollback全部通过，8456人工审片未见群体级硬失败。
- [ ] [COMPUTED][HIGH] 因T3硬失败，未创建CrowdRuntime/Spatial/Projectile/Combat/Presentation插件Module，未运行插件纯测试或最小宿主。
- [ ] [COMPUTED][HIGH] 未迁移或删除旧T8数组权威、32实体镜像pool、全Agent扫描、单调HitEventId去重及Reliable-only视觉路径。

[INFERRED][HIGH] 下一顺序固定为T6A→T6S→T6M；只有三个异构场景前置门关闭后，才重新启动Mass Projectile插件生产迁移。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
