# MassAI Crowd Demo 当前架构

## 1. 当前事实源

[COMPUTED][HIGH] 当前正式基础由 SF1 Shared Flow V2、SoftPressure Local Predictive + Particle 约束、Target Fact、Target Region Transport、fixed-step correction/rollback 和 client-only visual 组成。

[COMPUTED][HIGH] T5 Static/Moving 使用同一套 Target Region Transport 生产链；旧 Polar Density processor 已从正式 processor 集与源码声明中移除，不再作为 fallback 或 A/B 分支。

## 2. 双端 fixed-step processor 顺序

```text
RoundPlanApply
→ TargetFactApply
→ SharedFlowFieldBuild
→ FlowPreferredVelocity
→ TargetPolarTopologyBuild
→ TargetRegionPopulationBuild
→ TargetRegionTransportSolve
→ TargetRegionGuidance
→ LocalPredictiveInteraction
→ MovementPredict
→ ParticleConstraintSolve
→ MovementFinalize
→ AuthorityCommit / ClientPredictionCommit
```

[COMPUTED][HIGH] Server 和 Client 使用同一组 Mass processors 与纯 C++ kernels；Coordinator 只负责 RoundPlan、RoundResult、correction/checkpoint、readiness 和紧凑汇总，不承载 Topology、Demand、Transport 或 Particle 算法。

[COMPUTED][HIGH] `MovementFinalize` 是 `FCrowdDemoRoundSimStateFragment` 的唯一 fixed-step 写入边界；客户端 visual 只读取 client sim state，不计算 gameplay movement。

[COMPUTED][HIGH] 当前 `SimRoundSoftPressure` 在 `TargetRegionGuidance/FlowPreferredVelocity` 后执行通用 `LocalPredictiveInteraction`，再由 `MovementPredict` 积分有效 `LocalVelocity`。该阶段对全部 SoftPressure 运动场景使用同一规则，不读取 T3/T4/T5/T6、地图名、Portal或“同目标/窄口”标签。

[COMPUTED][HIGH] `CrowdDemoVelocityHalfPlaneKernel`提供无场景语义的连续二维half-plane与1cm/s量化；`CrowdDemoLocalPredictiveInteractionKernel`负责稳定pair/component、有限期grant、联合可行性和hash。Particle仍独立执行位置级Hard/Swept/Obstacle/Bounds最终安全闭环。

[COMPUTED][HIGH] T1 Open Spawn Relaxation 使用独立场景分支，双端顺序为：

```text
RoundPlanApply
→ OpenSpawnRelaxationPhasePrepare
→ SharedFlowFieldBuild
→ FlowPreferredVelocity（T1 固定输出 0）
→ LocalPredictiveInteraction
→ MovementPredict
→ ParticleConstraintSolve（只输入 active 实体）
→ MovementFinalize（仍提交全部 20 个实体）
→ AuthorityCommit / ClientPredictionCommit
```

[COMPUTED][HIGH] T1 的 inactive 实体保留普通 RoundSim state 并停在无 Hard overlap 的 staging 区；`FCrowdDemoOpenSpawnRelaxationFragment` 只表达测试参与、FormationIndex 与 boundary reset，不表达业务 spawn/despawn。

## 3. Target Region Transport

[COMPUTED][HIGH] 纯 kernel 接口分为 `BuildTopology → BuildDemand → SolveTransport → BuildGuidance`。

[COMPUTED][HIGH] Polar Navigation Cell 的扇区数按半径使用 8/16/32/64；Radial Band 为 100cm；Demand 统计固定使用 16 个 Region，因此导航分辨率与验收口径彼此独立。

[COMPUTED][HIGH] Topology 生成稳定 CellKey、CW/CCW 边和角区间重叠的跨环边，并用 FlowBounds、52cm 硬膨胀障碍、Target hard exclusion 及 swept segment 过滤不可行 Cell/Edge。

[COMPUTED][HIGH] 远距离实体通过 Shared Flow V2 的稳定 next-cell 链寻找首个安全 Polar source attachment；它们不再要求从出生点直线穿越障碍连接 Polar domain。

[COMPUTED][HIGH] Demand 计算 Current/Desired/Deficit/Surplus 和环境容量；整数 min-cost flow 依次保证最大运输量、最小物理成本、旧 quota 复用和稳定 key 决胜。

[COMPUTED][HIGH] 同 Cell 实体按 AgentId 消费按 ToCellKey 排序的出口 quota；结果不生成永久 Slot、PositionId 或 per-agent Region owner。

[COMPUTED][HIGH] 当前Edge quota是聚合计划量，`BuildGuidance`会让来自不同source cell的实体同时朝同一`NextCellAnchor`点生成速度；生产代码尚未在积分前根据邻域预测轨迹求解共同可执行速度与公平让行。

[COMPUTED][HIGH] Plan 正常寿命为 15 fixed steps；Target、完整 FeasibleGraphHash、membership、Demand 满足或 validator 失败会触发同一 boundary 确定性重建。Topology、Demand、Transport、Guidance 与 Validation 五类 round hash 均进入 RoundResult 双端比较。

[COMPUTED][HIGH] `FeasibleGraphHash`不折叠Target世界坐标，而是折叠全部Cell可行/终端/Region事实，以及排序后的可行Edge、实际soft clearance cost、径向cost与跨环标志。

[COMPUTED][HIGH] Plan validator检查revision、graph、membership、Edge存在/可行、Supply outgoing quota、正quota可达性与流量守恒；有效Plan必须保证Guidance unrouted为0。

### 3.1 当前终态与稳定性缺口

[COMPUTED][HIGH] `bTerminalStay`由当期Demand人口重新派生；T2的`TerminalSettledAgentIds`仅收集当前`GuidanceMode::TerminalSettle`实体。现有字段没有连续静止、Target-relative低速、位置峰峰抖动或状态chatter语义。

[COMPUTED][HIGH] Particle每步消费当前preferred并保证局部安全，但当前链没有在积分前把“同一局部component长期requested前进、applied被推回”转换为共同可执行速度与公平让行。Hard/Swept/Obstacle/Bounds=0不等于运输或终态合理。

[INFERRED][HIGH] 当前正确职责边界是：Transport维护共享Cell可行集合、Region需求、聚合Edge quota与terminal供需；Local Predictive Interaction根据通用邻域轨迹产生瞬时可执行速度和必要的公平让行；Particle保持现有Soft/Hard安全语义。不得通过Particle调参、Agent特判、固定Cell容量或永久Slot处理振荡。

## 4. Particle 与网络边界

[COMPUTED][HIGH] 当前 MovementPredict 只积分 Transport/Flow 输出的限速 preferred；Particle kernel 继续负责 Pair Soft、Hard/Swept、Environment/Bounds 和量化后安全闭环。

[INFERRED][HIGH] 目标实现中 MovementPredict 改为积分有效的 `LocalVelocity`，同时保留原 `DesiredVelocity` 作为宏观请求事实；宏观请求、局部速度选择和Particle applied结果不得共用一个字段而失去归因边界。

[COMPUTED][HIGH] SoftPressure rollback snapshot 已包含 Target Region Topology、Demand、Plan、quota、Guidance、PlanEpoch、五类 hash、重建计数和 solver 样本长度；correction 在 fixed-step boundary 原子恢复后重放。

[COMPUTED][HIGH] RoundResult 报告 feasible cell/edge/region、coverage、inside band、region population、routed/unrouted、cost、quota change、PlanEpoch、重建原因、solver p95 和 Topology/Demand/Transport/Guidance/Validation 五类 hash。

## 5. 当前技术证据

[COMPUTED][HIGH] Target Region Transport 定向自动化 5/5、Particle 23/23、T2 定向自动化 3/3 和完整 `CrowdDemo.SF` 46/46 已通过；Development Editor 已通过。

[COMPUTED][HIGH] 8414 Static P0 Round 1 通过：feasible cells/edges/regions=`276/1054/16`，raw/feasible coverage=`16/16`，inside band=`20/20`，最大 Region 人口=`2`，routed/unrouted=`0/0`，solver p95=`6.769ms`。

[COMPUTED][HIGH] 8414 Server/Client 四类 hash 完全一致：Topology=`2545299674`、Demand=`3117978081`、Transport=`2519143770`、Guidance=`1128002910`；Particle Hard/Swept/Obstacle/Bounds、invalid/fallback 均为 0，rollback hit/miss/mismatch=`53/0/0`，agents/visible=`20/20`。

[COMPUTED][HIGH] 当前 Shared Flow V2 hash 为 `520862038`；旧 SF1 事实中的 `267519150` 属于更早的连接/膨胀合同，不能继续写成当前运行值。

## 6. 动态图修复与当前停止点

[COMPUTED][HIGH] 8416完整fixture确认：Agent 15实际目标距离约850.37cm、`bTerminal=false`，但附着的Cell 176锚点半径850cm、`bTerminal=true`；旧solver把它直接吸收到terminal sink，Plan没有生成outgoing quota，validator唯一失败为`InsufficientOutgoingQuotaCellCount=1`，双端fixture hash=`10240167`。

[COMPUTED][HIGH] 通用修复规定：位于terminal-anchor的Supply也必须先经过至少一条真实Topology Edge，禁止source直接被terminal sink吸收；修复没有AgentId、CellId或场景特判。

[COMPUTED][HIGH] 8417 Static通过：inside band=`20/20`、coverage=`16/16`、最大Region人口=`2`、Plan/Guidance unrouted=0、invalid/validation failure=0，Validation hash=`1106829831`双端一致。

[COMPUTED][HIGH] 8418 Moving通过：inside band=`20/20`、feasible coverage=`12/12`、最大Region人口=`2`、Plan/Guidance unrouted=0、invalid/validation failure=0，五类hash双端一致，Particle四类安全违规=0，client rollback hit/miss/mismatch=`53/0/0`。

[COMPUTED][HIGH] 8417/8418与8426没有采集局部冲突component、yield waiting、terminal chatter和最终90-step Target-relative抖动窗口；这些结果继续证明旧口径到达、安全与同步，但不能单独证明新版“稳定自然落位”。

[COMPUTED][HIGH] 本任务按计划在Moving Small通过后停止；未运行100/500、DebugGame正式门、录像或FFmpeg，也未恢复旧Density。

## 7. 异构合同的接入边界

[COMPUTED][HIGH] 原 T5 地图仍使用同质配置；T6 场景已按实体保存并生产消费多档 PhysicalRadius、SoftMargin、Mobility 和 Minimum/MaximumCenterDistance。T6A 8464通过，T6S 8479通过旧口径到达/安全门，T6M 8492为19/20且尚未通过新版稳定终态合同。

[COMPUTED][HIGH] T1-T6 独立 20 实体 package、稳定 `CapabilityProfileKey` 和 profile→cohort prepared SoA 已建立；同 profile 共享 Topology/Demand/Transport，Particle 逐实体消费 Radius/Gap/Margin/Mobility。package存在不表示对应场景能力已经接入。

[INFERRED][HIGH] 首轮异构矩阵包含SmallLight/Standard/LargeHeavy与Melee/MidRange/Ranged的交叉组合；攻击距离只决定Target terminal band，不在Transport或Particle内实现攻击业务。

[INFERRED][HIGH] T1-T6 Small 三级验收完成前不进入 100/500；生产接入完成不等于真实能力通过。

## 11. T7 Combat 与 VAT 状态链（增量，T7 已通过）

[COMPUTED][HIGH] 当前已存在唯一 fixed-step 写入链：`HitResponseBoundaryApply → ReactiveMotionIntentCompose → LocalPredictiveInteraction → MovementPredict → ParticleConstraintSolve → MovementFinalize → VisualStateResolve`。HitFact 按 ApplyStep/Target/Event 排序，Stats 仍是唯一 Health/Lifecycle 事实源；Reactive XY 经局部速度层后进入 Particle，Z 使用固定步 ballistic，MovementFinalize 仍是 RoundSimState 唯一最终写入点。

[COMPUTED][HIGH] `FCrowdDemoRoundAgentState`、SoftPressure rollback、Mass FastArray 与 Particle applied-state hash 已包含 Combat 状态和 LastConsumedHitEventId。T8 已新增32实体的projectile Mass镜像pool、稳定 projectile fragment、fixed-step previous→proposed swept hit、统一 HitFact/HitResponse 和客户端 projectile ISM visual event stream；权威轨迹仍在Pipeline数组，详见第13节。

[COMPUTED][HIGH] 当前工程已独立生成并接入合法的五段 VAT：Idle `0–24`、Move `25–49`、Attack `50–74`、HitReact `75–99`、Death `100–124`。运行时材质关闭 AutoPlay，由客户端按同步 ServerTime、VisualState、StateStartServerTime 和 PlayRate 计算帧；Idle/Move 循环，Attack/HitReact/Death 钳制。

[COMPUTED][HIGH] T7 为了可归因审片，使用独立的确定性展示意图：FormationIndex `4–7` 以 `60cm/s` 沿 X 轴在出生锚点两侧最多 `12cm` 往返，其余常态组基础意图为零；ReactiveMotion 仍可覆盖该基础意图，Particle 仍是统一局部安全 owner。该规则只在 `MultiStateVatHitResponse` testcase 启用，不改变其他场景的 Shared Flow 或普通 velocity-driven VisualState 解析。

[COMPUTED][HIGH] `CrowdDemoReplicator` 已加载 T7 VAT 静态网格和运行时材质；客户端 visual processor 每实体提交 `Frame/PreviousFrame/HitFlashIntensity` 三项 custom data，且只读取 client sim/authority visual state，不计算 gameplay movement。

[COMPUTED][HIGH] 8447 完整单轮技术运行中，客户端始终提交 `20/20` VAT instances，稳定终态为 Idle/Move/Attack/HitReact/Death=`8/4/4/0/4`，HitFlash active=`2/2/4` 均被观测；Particle Hard/Swept/Obstacle/Bounds violation=`0/0/0/0`，candidate/applied hash=`961313407/3034185224` 且双端一致，correction rollback hit/miss/mismatch=`54/0/0`、replayed steps=`61`，checkpoint 与 interval position error p95 均为 `0cm`。

[COMPUTED][HIGH] HitFlash 使用第二个同帧 VAT overlay ISM：非命中时零缩放，命中时显示红色运行时材质。8445 近景录像及均匀帧检查覆盖了 `2/2/4` 三次红色命中窗口、水平击退、击飞上升/顶点/落地、Attack 与四个 Death 倒地姿态；20 个实例全程可见，未观察到隐藏实例、错误 visual owner 或明显 fixed-step 跳变。T7 自动化、双端技术门和人工视觉门均已关闭。

## 8. 2026-07-15 典型场景与异构接入状态

[COMPUTED][HIGH] 已新增稳定 Capability/Profile 纯内核：SmallLight、Standard、LargeHeavy 的 Radius/HardGap/SoftMargin/Mobility，以及 Melee/MidRange/Ranged 的 Target terminal band 共同折叠为 `CapabilityProfileKey`；固定 20 实体组合为 `3/3/3/2/3/3/3`。

[COMPUTED][HIGH] `RoundPlanApply` 已按 FormationIndex 写入逐实体 Particle/Target capability，Target Region 的 Topology、Demand、Transport、Guidance 按稳定 `CapabilityProfileKey` cohort 执行；Particle 仍在统一实体集合中处理跨 profile pair。

[COMPUTED][HIGH] RoundResult 与客户端 checkpoint 已增加 capability membership、逐 cohort agent/region/routed/unrouted 和 Topology/Demand/Transport/Guidance/Validation 五类 hash；SoftPressure rollback snapshot 已保存 cohort prepared 状态。

[COMPUTED][HIGH] 七张 T1/T2/T3/T4/T6 Small 真实 package 已通过 Unreal Python `delete_asset → new_level → save_current_level` 创建，且未修改现有 T5 地图。

[COMPUTED][HIGH] 端口 8422 的 T1 Round 1 已完成：phase=`Completed`，active transition=`0,5,10,15,19,20,19`，batch=4，inserted AgentId=19，removed AgentId=10，真实 Soft correction BFS layer max=3，insert/post-removal settling step=`78/94`，external preferred nonzero=0，old-layout restoration guidance=0，new-equilibrium displaced agents=12。

[COMPUTED][HIGH] T1 的传播图只累计实际保留的非零 Soft correction pair；requested 但被其他约束撤销的修正不形成边。BFS 以 inserted source 为 layer 0，稳定按 pair key 与 AgentId 展开；`FirstInfluencedIteration` 继续只表示单次 solver 内部迭代，不作为空间层。

[COMPUTED][HIGH] 8422 双端 participation/propagation/phase hash 分别为 `544066683/611677193/684083438` 且完全一致；Particle candidate/applied hash 为 `1664993798/1393540489` 且双端一致；Hard/Swept/Obstacle/Bounds=0，invalid/fallback=0，rollback hit/miss/mismatch=`53/0/0`、replayed steps=59，checkpoint 与 interval error p95 均为 0cm，client agents/visible=`20/20`。

[COMPUTED][HIGH] T1 生产能力门通过后按任务边界停止，未运行 T2–T6、100/500 或 DebugGame 正式门。

## 9. T2 Open Cohort Movement 当前接入

[COMPUTED][HIGH] T2 继续使用 `SimRoundSoftPressure` 正式 processor 链，但以 `OpenCohortMovement` testcase 隔离规则：Shared Flow V2 的 Bounds/Goal 保持不变，障碍列表清空；Arena 不生成障碍视觉。T1 生命周期不在 T2 启用，20 个实体从稳定 `10×2`、128cm 间距阵型同时参与移动。

[COMPUTED][HIGH] `RoundPlanApply` 构建并缓存按 FormationIndex/AgentId 排序的 T2 layout；layout 进入 correction rollback 与 Particle candidate hash。T2 只在 `OpenCohortMovement` testcase 启用现有 Target Influence、Target Region Transport 与 Distance Band；T1/T3/T4 不会被该条件误启用。

[COMPUTED][HIGH] T2 双端 fixed-step 顺序为 `RoundPlanApply → SharedFlowFieldBuild → FlowPreferredVelocity → TargetPolarTopologyBuild → TargetRegionPopulationBuild → TargetRegionTransportSolve → TargetRegionGuidance → LocalPredictiveInteraction → MovementPredict → ParticleConstraintSolve → MovementFinalize → AuthorityCommit / ClientPredictionCommit`。Polar domain 外由 Shared Flow 提供 Preferred；进入 domain 后由 Transport guidance 成为唯一宏观 owner；局部层只将该 Preferred 变为当前可执行速度。`MovementFinalize` 仍是 RoundSim state 唯一写入点。

[COMPUTED][HIGH] T2 新增 sticky `flow_approach_entered_count`、`transport_handoff_count` 与当前 `terminal_settled_count`，并报告 inside-band、feasible Region coverage、Plan/Guidance unrouted、Transport validation failure 和首次全体 terminal settled step。进度状态、prepared SoA、PlanEpoch、quota、指标样本与 hash 均进入既有 SoftPressure correction rollback。

[COMPUTED][HIGH] 8426 原 P0 Round 1 已通过 T2 旧口径门：flow approach/handoff/inside-band/当帧terminal mode=`20/20/20/20`，首次全体terminal mode step=`435`，feasible Region coverage=`16/16`，最大 Region 人口=`2`，Plan/Guidance unrouted=`0/0`，Transport validation failure=`0`，final deadlock=`0`；稳定性V1待复验。

[COMPUTED][HIGH] 8426 的 Particle Hard/Swept/Obstacle/Bounds、invalid/fallback 均为 0；Topology/Demand/Transport/Guidance/Validation 五类 hash、Particle candidate/applied hash、T2 layout/route/progress hash 均双端一致。correction rollback hit/miss/mismatch=`53/0/0`、replayed steps=`55`，checkpoint 与 interval position error p95 均为 `0cm`，client agents/visible=`20/20`。

[COMPUTED][HIGH] 8425 的旧 `flow_goal_reached_count=16/20` 与 8426 的 point-flow route mask 8 只保留为旧 140cm 点目标/Flow owner 对照诊断；Transport 接管 Preferred 后不得再把它们作为 T2 能力门。T2 不包含永久 Slot、PositionId、per-agent Region owner、ORCA、Portal 或 passing band。

## 10. T3 Open Bidirectional Swap 当前接入

[COMPUTED][HIGH] T3继续复用`SimRoundSoftPressure`正式链与Particle P0，但由`FCrowdDemoBidirectionalSwapKernel`建立稳定20实体布局：FormationIndex 0–9属于cohort 0，从南向北；10–19属于cohort 1，从北向南。两侧均为128cm间距单排，第二群横向错开64cm，初始Hard overlap为0。

[COMPUTED][HIGH] `SharedFlowFieldBuild`为两个cohort分别构建无障碍Shared Flow V2：cohort 0的共享目标中心为`(400,2850)`，cohort 1为`(-400,-2850)`；`FlowPreferredVelocity`按FormationIndex选择对应场。该`±400cm`是cohort级目标中心，不是per-agent passing lane、slot或ORCA规则。

[COMPUTED][HIGH] T3双端顺序为`RoundPlanApply → SharedFlowFieldBuild(two cohort fields) → FlowPreferredVelocity → LocalPredictiveInteraction → MovementPredict → ParticleConstraintSolve → MovementFinalize → AuthorityCommit / ClientPredictionCommit`。`MovementFinalize`按中心平面和对侧完成平面累计sticky progress；当前final-deadlock只统计仍未完成且连续90步forward speed不超过10cm/s的实体。

[COMPUTED][HIGH] T3 layout、两套Flow build和progress均进入Particle candidate hash、SoftPressure rollback与RoundResult checkpoint。8455证明layout/flow/progress、Particle candidate/applied以及correction replay双端一致，并达到center/completed=`10+10/10+10`、final deadlock=0。

## 10.1 T4 Valid Corridor Transit 当前接入

[COMPUTED][HIGH] T4复用SF1障碍、Shared Flow V2与SoftPressure Particle P0。`FCrowdDemoValidCorridorTransitKernel`建立20实体、10列、128cm间距的稳定布局，并直接复用`MakeSf1Config(1)`、52cm AgentInflate和Cell Connection Graph V2；没有启用Target Region Transport、Slot、ORCA或场景专用避让。

[COMPUTED][HIGH] T4双端顺序为`RoundPlanApply → SharedFlowFieldBuild → FlowPreferredVelocity → LocalPredictiveInteraction → MovementPredict → ParticleConstraintSolve → MovementFinalize → AuthorityCommit / ClientPredictionCommit`。`MovementFinalize`以`Y=-1950/-650/750cm`三个平面分别记录wall passed、corridor exit和completed；final-deadlock只统计未完成且连续90步速度不超过10cm/s的实体。

[COMPUTED][HIGH] T4 layout/flow/progress状态进入Particle candidate hash、专用SoftPressure correction rollback与RoundResult。8460达到wall/corridor/completed=`20/20/20`、final deadlock=`0`、unreachable=`0`，三类T4 hash双端一致，Particle四类安全违规与invalid/fallback均为0；8461完成人工审片。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 15. 2026-07-17 T6M 可行Region相位修复与新停止点

[COMPUTED][HIGH] 8490只读见证确认：Target靠近Bounds后，各Capability cohort的可行Region集合不再连续；旧`BuildDemand`先把phase当作原始Region key，再跳过不可行Region，使phase=5和phase=10都折叠到`11,12,13`。

[COMPUTED][HIGH] 当前实现先按StableRegionKey构建可行Region有序集合，再将16区phase按比例投影为可行集合ordinal后分配DesiredPopulation；完整16区可行时保持原行为。Guidance还会先稳定排序Plan edges，因此输入容器顺序不再影响consumption hash。

[COMPUTED][HIGH] 该修复将T6M从8487的inside-band=`17/20`提高到8491/8492的`19/20`，可行Region coverage提高到`19`；Particle、Transport validity、rollback、双端hash和完整显示继续通过。

[COMPUTED][HIGH] 剩余Agent15属于LargeHeavy Melee：target=`(-2400,1900)`、band=`170–300cm`、distance=`308.3cm`、source attached、cell32可行、next cell16。Agent14从cell31、Agent15从cell32同时获得指向cell16的聚合Flow guidance，Particle安全链保持Hard安全但使Agent15径向进展为负。

[COMPUTED][HIGH] 实验性“每next-cell只准入一个实体”在8493仍为inside-band=`19/20`且coverage从19降至18，因此已从生产代码和测试中撤销；没有把失败实验保留为正式路径。

[INFERRED][HIGH] 当前新停止点是 Shared Guidance 与 Particle 之间缺少可证明安全且保持吞吐的通用局部预测速度合同。下一阶段需要先建立无场景语义的 half-plane/trajectory 纯fixture；不得再次用Agent特判、固定Cell admission或Particle调参处理Agent15。

[INFERRED][HIGH] 在生产修复前还必须增加只读稳定窗口诊断，区分同next-cell容量争抢、`TerminalStay↔Supply/Transport`状态chatter和Particle自身未收敛；没有唯一证据时不得把三者混成同一个“抖动”问题。

## 16. Target稳定窗口诊断与T6M 8496归因

[COMPUTED][HIGH] 已新增默认关闭的`-CrowdDemoTargetStabilityDiagnostic`及纯C++ `CrowdDemoTargetStabilityDiagnosticKernel`。它只读取Target Region Guidance、Demand、Target-relative状态与Particle实际修正，保存最后90个fixed steps；不写Preferred、Transport计划、Particle结果或RoundSim状态。

[COMPUTED][HIGH] 诊断按Plan激活时锁定开关，运行时进入SoftPressure rollback snapshot，RoundResult只复制紧凑summary/hash；server/client独立计算。自动化覆盖输入反序、合法同cell并行、连续merge阻塞、Terminal显式surplus与无因chatter、移动目标相对坐标和checkpoint恢复。

[COMPUTED][HIGH] 8496原P0 T6M单轮得到稳定窗口hash=`4081220932`且双端一致：最后90步`contended_steps=90`、`contended_groups=90`、`merge_blocked_agents=2`、最大连续阻塞=`90`；首witness为step 825、Agent14、next cell 16。Terminal chatter与attraction-rejection cycle均为0；客户端rollback hit/miss/mismatch=`53/0/0`、replayed steps=`66`后仍保持相同诊断hash。

[COMPUTED][HIGH] 8496最后窗口的inside-band最小值为17、coverage最小值为16/20，Particle settled window为0、Target-relative speed p95=`300cm/s`、30步位置peak-to-peak p95=`96.881cm`。由于目标带尚未完整覆盖，未收敛属于仍在Transport中的结果而不是独立首因；诊断首因稳定归类为`MergeCapacity`。

[INFERRED][HIGH] 下一生产设计边界是新增通用 Local Predictive Interaction：几何允许时保留多人并行，只有局部共同前进不可行时才按可回滚的等待/进展/AgentId规则有限期让行。不得把Navigation Cell改成永久站位、固定容量入口或per-agent Slot。

## 17. Local Predictive Interaction 当前实现

[COMPUTED][HIGH] `FCrowdDemoRoundMoveIntentFragment` 是不可变宏观输入，`FCrowdDemoRoundLocalVelocityFragment`保存局部选择，`FCrowdDemoRoundProposedMovementFragment`与`FCrowdDemoRoundParticleConstraintFragment`分别表达积分结果和最终安全结果。

[COMPUTED][HIGH] `CrowdDemoVelocityHalfPlaneKernel`只保留无场景语义的half-plane、连续精确求解、速度圆验证和1cm/s量化修复；新生产路径不依赖旧ORCA Agent、fallback或processor。

[COMPUTED][HIGH] `CrowdDemoLocalPredictiveInteractionKernel`、`FCrowdDemoRoundLocalVelocityFragment`与`UCrowdDemoRoundLocalPredictiveInteractionProcessor`已接入；PipelineSubsystem保存prepared SoA、grant state、hash和rollback，Coordinator只复制紧凑汇总。

[COMPUTED][HIGH] 8507 T3与8509 T4已经用同一生产kernel通过20实体能力、安全、双端hash和rollback门；8521 T5 Static进一步通过inside-band=`20/20`、Region coverage=`16/16`及最终90步稳定窗口门。

[COMPUTED][HIGH] T5稳定窗口诊断现按Region记录Desired/Current、terminal/supply成员、进入/离开和Edge quota，并对供给实体记录Guidance、LocalVelocity、MovementPredict与Particle Applied。8515确认两个空Region并非缺少Demand或路径：Agent 8/16在约300cm/s Guidance后分别得到约13.45/9.06cm/s Local速度，低于30Hz、1cm位置量化的15cm/s执行门，最终Applied为0；双端诊断hash=`2690604116`一致。

[COMPUTED][HIGH] 8517完整fixture证明低速来自独立half-plane与补全pair后的安全解提前停止，不来自`CommonVelocity` fallback。生产kernel已增加保持pair相对速度不变的确定性component共同平移，并逐实体复验速度圆、Bounds和Obstacle；该规则不读取T5、Region或Agent身份。

[COMPUTED][HIGH] 8518中共同平移将覆盖从14/16提高到15/16，但六实体fixture随后证明仍存在前向超过200cm/s的联合安全解；9.22cm/s不是几何必然。

[COMPUTED][HIGH] 当前Local Predictive在独立half-plane与CoherentTranslation后执行`JointPreferredRecovery`：以component完整Preferred向量为目标进行固定轮数联合Pair投影，并在量化后用同一通用Pair/Obstacle/Bounds合同完整复验；仅当总目标误差下降且grant进展不回退时采用。

[COMPUTED][HIGH] 8521 T5 Static达到最终90步inside-band=`20/20`、coverage=`16/16`、sub-quantum supply=0、Target-relative speed与位置峰峰值p95/max=0；LocalPredictive、Particle、Transport与双端同步门通过。当前未完成项仅为人工审片与T5 Moving复验。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 14. 2026-07-17 Round 重置与动态 Shared Flow 当前合同

[COMPUTED][HIGH] `FCrowdDemoRoundRules` 已显式携带 `RoundStartPolicy`。当前全部 Demo 验证场景固定使用 `ResetToStableInitialState`；`ContinueFromCheckpoint` 仅为未启用的接口预留。Round 1 与 Round 2 均由 `CrowdDemoRoundInitialStateKernel` 按 AgentId、FormationIndex、testcase 与 CapabilityProfile 构建稳定初态，Bootstrap 不再承担后续 Round 的隐式重置。

[COMPUTED][HIGH] Round 过渡现按“冻结旧 Round 终态并构建 checkpoint → 发布结果 → 激活下一 Plan → 原子恢复初态”执行。Authority Plan 的网络预发布与本地激活已拆开；新的 Plan 到达时会重新开放已静止 fixed-step boundary 的 PlanApply claim，避免 Round 2 因旧 boundary 已检查而永远不激活。

[COMPUTED][HIGH] Shared Flow V2 现公开 `BuildTopology`、`ResolveGoalAnchor` 与 `BuildIntegrationForAnchor`。动态目标只在量化 anchor cell 改变时重建 Integration，世界坐标中的 Bounds 与 Obstacle Topology 保持缓存；FlowPreferredVelocity 继续在实体真实世界位置采样，并保留已有的 TargetVelocity 参考系补偿。

[COMPUTED][HIGH] 8487 T6M 连续两轮的 `round_input_hash=4290856233`、`round_initial_state_hash=3920329045`、`flow_topology_hash=4029698379`、`flow_anchor_cell_key=2706`、`flow_integration_hash=338742383`、`flow_integration_rebuild_count=25` 与 `dynamic_flow_round_hash=905043919` 均相同且双端一致；每轮 reset=1、transition violation=0。

[COMPUTED][HIGH] 8487 的 Particle 四类安全违规、invalid/fallback、Transport unrouted/validation failure、rollback miss/mismatch、checkpoint/interval p95 与跨轮扩散均为0，agents/visible=`20/20`。但两轮能力结果都只有 inside-band=`17/20`、可行 Region coverage=`16`，因此 T6M 仍未通过。

[INFERRED][HIGH] 按硬门，当前架构清理未执行：场景2/3、旧 processor/fragment、GT/WORK拆分、RoundResultHeader收缩、DebugGame与独立 Round 1 进程比较均保持未开始。下一任务应从三个未进入有效距离带实体的 Capability/Transport 终态事实继续归因，而不是回退到直接追目标或修改 Particle 参数。

[COMPUTED][HIGH] 现有 Flow recovered 计数在8487仍表现为跨Round累计（Round 1为14449、Round 2为28898）；该字段不是本次能力门，但在宣称所有Round指标均为逐轮口径前仍需清理其生命周期。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 2026-07-17 T6 异构 Small 当前停止点

[COMPUTED][HIGH] T6A 8464 已通过通道能力门：wall/corridor/completed=`20/20/20`、final deadlock=`0`、unreachable=`0`、跨 profile Hard/Swept=`0/0`，Particle 与 T6 三类 hash 双端一致，client rollback hit/miss/mismatch=`53/0/0`，agents/visible=`20/20`。

[COMPUTED][HIGH] T6S 8479 已通过旧口径静态目标分层门：7 个 capability cohort 均达到各自有效距离带及可行 Region 覆盖，aggregate inside-band/coverage=`20/20`，Plan/Guidance unrouted=`0`、invalid/validation failure=`0`，Particle Hard/Swept/Obstacle/Bounds=`0/0/0/0`，candidate/applied hash=`2741316756/3875363092` 且双端一致；未采集稳定性V1窗口。

[COMPUTED][HIGH] 为移动目标 FarFlow 新增了纯函数 `ComposeTargetAdvectedFarFlowVelocity()`：生产路径使用 `SharedFlowPreferredVelocity + TargetVelocity`，并按 Agent MaxSpeed 截断；Target Region Transport 定向自动化 `5/5` 与完整 `CrowdDemo.SF` `54/54` 均通过。

[COMPUTED][HIGH] T6M 8481 Round 1 未通过能力门：aggregate inside-band/coverage=`10/20`；LargeMelee 与 StandardMelee 均为 `0/3`，各自最大目标相对误差约 `2228.4cm/2139.4cm`。Particle 四类安全违规、invalid/fallback 均为0，candidate/applied hash=`2063851792/1449057613` 双端一致，Transport 五类 hash 双端一致，rollback hit/miss/mismatch=`53/0/0`、replayed steps=`66`，agents/visible=`20/20`。

[INFERRED][HIGH] 8481 证明仅修正 FarFlow 的速度参考系不足以支持 T6 异构移动目标：Shared Flow 的空间势场仍锚定静态目标位置，而移动 Target 在一轮内平移约2400cm；这是当前第一个未关闭阶段。没有加入直接追目标、第二套 Flow、场景特判或 Particle 参数调整。

[COMPUTED][HIGH] T6M 未关闭，因此尚未运行 T6 DebugGame、三段录像/人工审片，也未启动 Mass Projectile 插件生产迁移。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 10. 远程攻击、VAT 与受击响应的当前边界

[COMPUTED][HIGH] 当前已实现Idle/Move/Attack/HitReact/Death五状态枚举、唯一VisualState resolver、确定性HitFact、ReactiveMotion、HitFlash事实，以及Combat correction/rollback/replication/applied-hash合同；当前 `CrowdDemo.Combat.T7` 七项纯自动化已通过。

[COMPUTED][HIGH] `/Game/CrowdDemo/VAT/T7` 已由当前工程新生成1个骨骼网格、1个VAT静态网格、5个AnimSequence、3张Bone VAT纹理、1个DataAsset、5个状态材质实例和2个运行时手动播放材质实例；只读验证确认125帧与五段合法范围。生产入口见`VatAssetProductionPipeline.md`。

[COMPUTED][HIGH] `CrowdDemoReplicator` 已以真实 VAT mesh/runtime material 替换 crowd 的 Engine Cube；客户端按实体 VisualState 与 ServerTime 写入主体和 HitFlash overlay 的同一 VAT 帧。8445 近景录像已确认红色闪色、Attack、HitReact、击飞落地与 Death 细动作可辨识。

[COMPUTED][HIGH] T8 当前生产顺序为 `RoundPlanApply → RangedCombatPhase/ProjectileStep → HitResponseBoundaryApply → FlowPreferredVelocity(T8为零) → ReactiveMotionIntentCompose → LocalPredictiveInteraction → MovementPredict → ParticleConstraintSolve → MovementFinalize → VisualStateResolve → Authority/ClientPredictionCommit`。T8 使用静止目标且不启用 Target Region Transport；MovementFinalize 仍是 RoundSimState 唯一写入点。

[COMPUTED][HIGH] Projectile gameplay 状态只由双端同构 processor 和纯 C++ kernel计算；Coordinator只复制RoundResult、checkpoint/correction和紧凑 projectile visual event。客户端 event ISM按同步ServerTime插值显示Spawn/Impact/Expire，不计算命中或damage，也不为每发创建持续复制Actor。

[COMPUTED][HIGH] 8451最终构建验证50次windup对应50次spawn和50次swept impact/damage，duplicate fire/hit均为0；server/client attack/projectile/event hash完全一致，Particle四类安全违规与同步误差均为0。8450录像确认20实例、10发齐射、Attack、impact/HitFlash和Death可见。

[INFERRED][HIGH] 当前 Target terminal band 仍只属于Transport capability；只有T8这条静止目标测试链已证明实体能够射击。移动目标、目标相对swept轨迹、远程Transport handoff和混合战斗仍需独立阶段。

## 13. 2026-07-17 Projectile插件前置门结果

[COMPUTED][HIGH] 当前代码复核确认：Projectile gameplay权威仍是`PipelineSubsystem::PreparedProjectiles`；32个Mass实体只由`MirrorProjectileStates()`镜像Active状态；`AdvanceProjectiles()`仍逐Projectile遍历全部存活Agent并使用静态目标球心；命中去重仍是`HitEventId <= LastConsumedHitEventId`；客户端视觉事件仍通过Reliable Multicast发送。

[COMPUTED][HIGH] 前置`git diff --check`、Development Editor和完整`CrowdDemo.SF` 46/46通过，自动化日志没有Fatal、Assertion、Ensure、`LogWindows: Error`或VIOLATION。

[COMPUTED][HIGH] 8452保留为“尚无T3生产合同”的历史失败，8454保留为同轴相反Preferred形成18/20安全平衡的诊断。当前生产状态以8455为准：双cohort布局、相反Shared Flow、中心/完成平面和独立hash/rollback已接入，能力门为20/20、throughput difference=0、final deadlock=0。

[COMPUTED][HIGH] 按插件迁移前置规则，T3与T4通过后仍未创建插件Module、未修改Projectile生产Source、未切换T8、未删除`PreparedProjectiles`/镜像pool/旧碰撞/旧命中去重或Reliable视觉路径；T6A/T6S/T6M仍是未关闭前置门。

## 12. 尚未接入的插件化与类游戏业务边界

[COMPUTED][HIGH] 当前RoundSim仍由测试RoundPlan与SoftPressureTestCase驱动；没有持续玩家Pawn输入、动态敌群spawn/despawn、运行时业务Planner、线形/圆形GameplayCommand或NavMesh/Recast生产接入。

[COMPUTED][HIGH] 当前Shared Flow和Particle主要使用XY平面合同；Z只用于有限状态与2.5D受击表现，不能描述为已经支持坡道、桥上桥下Nav层隔离或任意高低差导航。

[INFERRED][HIGH] 在T1-T8现有独立测试收敛后，计划先把纯kernel、最小Mass运行时、fixed-step/rollback/hash和VAT适配提取为可替换插件；当前PipelineSubsystem、Coordinator和测试场景不能整体视为插件API。

[INFERRED][HIGH] 插件版回归通过后才实施T10 Gameplay Swarm Sandbox和NavMesh分层Surface Flow；目标、依赖方向和迁移门见`GameplaySwarmSandboxAndPluginMigrationPlan.md`。本节只记录未实现边界，不改变当前processor顺序。

## 13. Projectile当前真实边界与后续替换

[COMPUTED][HIGH] 当前Projectile gameplay权威状态是`PipelineSubsystem::PreparedProjectiles`数组；`MassSubsystem`预建32个`FCrowdDemoMassProjectileFragment`实体后，由`MirrorProjectileStates()`按ProjectileId排序镜像Active数组。Mass实体目前不独立执行spawn、movement、collision或lifetime，因此不能称为最终entity-native projectile simulation。

[COMPUTED][HIGH] 当前碰撞具备previous→proposed segment-sphere、最早量化hit和AgentId决胜，但每Projectile遍历全部存活Agent；没有Agent broadphase、环境障碍、移动目标相对sweep或高低差layer过滤。

[INFERRED][HIGH] 现有T8链保留为已通过静止目标基线；插件阶段将替换为权威Mass Projectile fragments/processors、稳定空间索引、通用ImpactFact/HitFact和原工程Combat Adapter。修正设计见`MassProjectileHitFrameworkDesign.md`。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
