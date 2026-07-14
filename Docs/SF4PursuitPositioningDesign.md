# SF4 确定性追逐与稳定站位设计

## 33. 当前修订：SF4降级为历史fixture，不再拥有局部运动架构

[INFERRED][HIGH] 最新群体模型不再把Commit标记为Transit Source，也不依赖Stable/Reserve让路、Position/Holding回位或Priority ORCA责任比例。任意实体只通过PhysicalRadius、HardSafetyGap、SoftMargin和Mobility参与统一pair压力求解。

[INFERRED][HIGH] SF4的Target、Candidate、Position、Holding和Commit只属于历史“精确站位”实验，不得继续进入通用Soft-Pair solver的数据合同。若最新Demo不再要求精确槽位，这整套状态机及其processor、fragment、rollback和指标应在新场景通过后删除。

[INFERRED][HIGH] ring-entry、ring-exit和中心插入不是SF4业务状态，而是通用粒子约束kernel的纯fixture。它们只验证局部压力传播、硬安全和确定性，不验证某个实体回到指定PositionId。

[COMPUTED][HIGH] 当前Source尚未执行该删除；正式SF4、Elastic、Joint与Shadow代码仍存在。本节定义重构方向，不得把历史17/20、19/20或Joint capacity结果写成新模型验收。

## 32. 能力边界重分类：SF4 fixture 与通用群体穿行

[COMPUTED][HIGH] Generic Priority ORCA 接入后，Static Small 已达到物理 `19/20`，但 Agent 6 的 preferred/ORCA/final=`(79,796)/(0,78)/(0,0)cm/s`，ORCA `stop_violates=427`。现有证据只能说明失败发生在 ORCA 输出及其后的安全链，不能说明继续调整 pair responsibility 就能完成落位。

[INFERRED][HIGH] 参考视频中“大尺寸单位进入密集小型单位群并触发局部让行与重组”的效果，把问题提升为通用群体穿行能力：Commit Agent、玩家实体或大型单位都可能要求邻近多个实体协同释放空间。该问题不应继续作为 Agent 6 或 Position/Holding 的专用例外堆叠在 SF4 状态机中。

[INFERRED][HIGH] SF4 的职责是提供真实 Agent 6 最小约束 fixture、现有 Priority ORCA 对照结果、Position/Holding 恢复目标和安全链输入；通用需求、参考视频证据和局部联合速度候选方案由 [CrowdTransitCapabilityDesign.md](CrowdTransitCapabilityDesign.md) 统一定义。

[INFERRED][HIGH] 在纯 fixture 证明局部联合 solver 能以相同 pair/obstacle安全约束恢复正向速度前，不修改生产 ORCA，不新增退圈/放行/围回状态，不把 `19/20` 写成 SF4 或群体穿行能力通过。

## 31. Generic Priority ORCA 与 Yieldable Commit Blocker 实验结论

[COMPUTED][HIGH] 通用 ORCA priority key 为 `(PortalPriority, LocalPriority)`，按字典序比较。Portal admission 保持第一优先层；SF4 Steering 只派生第二层：Commit=`Committed`，Pursuit/Holding/Reacquire=`Normal`，StableOccupied/ReserveHold=`Yielding`。该输入不跨 boundary 保存，也不新增 Mass fragment。

[COMPUTED][HIGH] pair responsibility 只改变 constraint point 中的责任份额：equal=`0.50/0.50`，higher/lower=`0.25/0.75`。双方 constraint 都生成；combined radius、constraint kind、neighbor set、time horizon、MaxSpeed、LP、量化和 penetration correction 不变。历史 `Sf4RouteMode Active/Yielding` 的删除高优先级 constraint 与0%/100%责任不再由生产 `BuildPairConstraint` 调用。

[COMPUTED][HIGH] CommitGate 的 hard safety 是 Target/Flow/Obstacle 与 assignment validity；Active Commit 和同 boundary selected Commit 继续互斥；Stable/Reserve 相交只形成 `YieldableConflictMask`。DecisionHash 折叠最终 decision、hard reject mask 和 yieldable mask，Joint residual capacity 仍在其后保护剩余可行性。

[COMPUTED][HIGH] 自动化覆盖 equal/asymmetric、双向责任和、输入反序、Portal高于Local、constraint保留、combined radius/kind不变、Commit追Stable速度责任、Stable/Reserve yieldable fixture、hard route conflict及完整回归。Development、Positioning 3/3 与完整SF 33/33通过。

[COMPUTED][HIGH] Static Small Round 1 真实结果：priority hash=`617370832`，asymmetric=`19247`，high25/low75=`19247/19247`，sum violation=`0`；server/client完整hash match=1。live CommitGate yieldable Stable/Reserve=`0/0`，说明该轮没有 ready request 命中这两类动态相交，不能用纯fixture替代live触发事实。

[COMPUTED][HIGH] 末态为`0/1/0/9/10/0`，19个Stable/Reserve实体全部位于各自Position/Holding的30cm物理阈值内，最大位移`29.946/29.783cm`。唯一Agent 6距Holding 501cm，preferred非零、ORCA仍为78cm/s、final为0；本实验把瓶颈从旧的CommitGate/ORCA前段推进到ORCA后的移动安全链，但没有确定具体是ObstacleConstraint、PBD/Reproject还是Finalize。

[COMPUTED][HIGH] Target crossing、双端penetration、severe overlap、deadlock和双端同步均通过，但ORCA stop constraint violation=`427`，所以实验安全硬门失败；物理满足19/20又使能力门失败。没有运行DebugGame、正式两轮或录像。

[INFERRED][HIGH] 后续必须拆成两个只读问题：一是Agent 6的ORCA后速度为何归零，二是大量stop candidate为何违反约束且仍依赖PBD/obstacle保持最终几何安全。没有新fixture前不得修改handoff、ORCA参数、Commit容差或把19/20写成完成。

## 30. 未完成实体最终边界fixture与三路归因

[COMPUTED][HIGH] fixture的未完成口径为非StableOccupied且非ReserveHold；因此当前包含Pursuit/Holding/Commit，未来若出现Reacquire也会被如实保留。每Agent只记录最终量化事实，不记录timeline；Commit拒绝掩码可同时表示readiness、Target/Flow/Obstacle、Stable/Reserve blocker、active/selected route conflict及joint residual capacity。

[COMPUTED][HIGH] 当前8实体可稳定分成三组：Agent 1/2/4/11已经位于Holding容差附近但被CommitGate阻挡；Agent 3/14/13具有非零强preferred而ORCA/final约为零；Agent 10仍为Pursuit且preferred本身为零。该分类由server/client相同fixture直接产生，不是从聚合p95反推。

[INFERRED][HIGH] 设计顺序固定为Admission blocker安全反事实→ORCA责任/优先级fixture→handoff dead-zone设计。只有前一分支的纯几何fixture证明可安全改变行为时才接生产；不同分支不得在同一次修复中混合。

## 29. 联合规划生产接入与动态收敛停点

[COMPUTED][HIGH] 正式Holding Assignment stage现以联合图同时选择固定Position与Holding。求解在局部完整数组中结束后一次发布prepared Position/Holding assignments；Position fragment随后在同一processor pass写入。输入hash未变化时复用结果；非法、溢出或无法保持完整最大基数时保留上一份完整结果。

[COMPUTED][HIGH] 最大基数固定后，合法existing Position+Holding完整组合复用优先于路线代价和稳定ID决胜。当前Small输入可同时强制复用全部合法软组合，因此无需指数枚举；一般冲突fallback仅允许最多12个软复用组合，超过时安全返回invalid而不发布近似结果。

[COMPUTED][HIGH] Commit residual gate把每个候选的Position/Holding锁为Commit，并把Position加入未来Stable blocker集合，再对剩余Agent执行整数最大基数检查。候选按既有Grant稳定顺序串行评估，已接受候选会约束后续候选；这建立了跨同一boundary的剩余容量保护，但不改变route冲突、Target exclusion和Steering guidance。

[COMPUTED][HIGH] 生产Round 1得到联合20/20、hard locked=13、reused=20、Commit residual 7/7；客户端比较joint/residual及完整stage hash均match。补充只读ingress诊断连续两轮Target exclusion crossing=0。安全门通过，但末态只有Stable+Reserve=9/20，故联合规划只解决assignment容量，没有证明动态进入过程完成。

[INFERRED][HIGH] 后续不能继续改匹配器。下一归因对象固定为6个Holding、1个Commit、1个Pursuit及3个尚未稳定Commit的progress/ORCA/arrival事实；在新证据前不修改ORCA、handoff或容差。

## 28. Hall Witness几何验证与分支B联合规划

[COMPUTED][HIGH] 几何事实使用与正式compatibility相同的1cm位置、42cm双方半径和10cm SafetyGap。对每个Holding选择最小clearance margin blocker，再在全部Holding中选择最大worst-margin。Agent 5/P2834最佳结果仍为H2150→P2834，blocker为Agent 6/P2835，margin=-35.922cm、t=1；正式分类与margin分类一致。

[COMPUTED][HIGH] self、引用witness Position、duplicate、stale、半径重复语义和正式分类不一致均为0。endpoint命中75条，但最佳endpoint的中心距离58.078cm小于94cm，故不是仅由闭区间或epsilon造成的相切误判。

[COMPUTED][HIGH] owner释放反事实保留原hard assignment作为可行边，只解除强制owner，因此满足单调性并得到Stable/Reserve/Commit=`18/18/18`。Physical blocker removal保持owner硬锁但分别忽略Stable或Reserve blocker，结果=`20/18`。两类反事实不得再混写。

[COMPUTED][HIGH] 证据满足分支B：全部150条路径真实被封闭且无几何设施错误。Joint Planner图为`Source→Agent→HoldingIn→HoldingOut→Position→Sink`，HoldingIn→Out和Position→Sink容量均为1；硬owner在建图前原子锁定Holding+Position。费用使用整数词典序向量，输入排序后执行确定性Bellman-Ford增广。

[COMPUTED][HIGH] 真实Joint结果为20/20，Agent 5从封闭P2834改为H2843/P2886；15个hard owner不变，18个完整组合复用，唯一性与双端hash通过。Commit residual fixture对每个软候选增加未来Stable blocker、锁定该Holding/Position并重算剩余图，5个候选均保持4/4。

[INFERRED][HIGH] 这些结果证明存在安全的联合静态分配与单次Grant顺序，不证明生产运动状态会自动达到20/20。生产接入仍需原子发布Position/Holding、correction rollback和唯一Steering writer；本轮明确未实施。

## 27. Fixed-Position Holding Hall-deficiency 证书

[COMPUTED][HIGH] 诊断图左侧为当前Position仍合法的Agent，右侧为未被合法硬owner锁定的Holding，边只在该Agent固定PositionId的当前compatibility完整成立时存在。StableOccupied、ReserveHold、Commit合法owner分别进行一次“仅释放该类”的只读最大匹配反事实；其他生产事实保持不变。

[COMPUTED][HIGH] 对最多20个软Agent枚举全部非空子集，先选择Agent数量最少的`|N(S)|<|S|`集合，再按AgentId字典序决胜。fixture按AgentId/PositionId/HoldingId排序，记录record/Flow/Target/Obstacle/revision、Stable/Reserve blocker AgentId集合和最终compatible布尔值；所有字段为整数或布尔量化事实。

[COMPUTED][HIGH] Static Round 1 fixture：`S={5}`、`N(S)={}`、deficiency=1、PositionId=2834、hash=4262261310。150个Holding均有record且Flow/Obstacle/revision成立；Target reject=37、Stable blocker reject=150、Reserve blocker reject=13。计数是多标签统计。

[COMPUTED][HIGH] current/no-Stable/no-Reserve/no-Commit matching=`18/15/18/18`。释放任何单一硬owner类都不能恢复20，Stable释放反而使原本grandfathered的合法硬锁参与当前compatibility重匹配并下降到15。

[INFERRED][HIGH] 下一阶段不能选择owner放宽分支。证据指向固定Position compatibility过滤；需要另行验证Position 2834的Target与blocker过滤是否过严，或Position Assignment与Holding Assignment是否必须联合求解。本节不授权生产行为修改。

## Steering-first Holding/Commit纯内核实现状态

[COMPUTED][HIGH] 当前已完成设计第一门：`ECrowdDemoPursuitSteeringState`及Holding candidate/agent/assignment/summary、compatibility、Commit request/decision/result均为纯C++ POD/数组合同，没有接入Mass processor或修改旧Position fragment。

[COMPUTED][HIGH] Holding不是人工lane或Portal。实现遍历现有Flow free/reachable cell，在目标Position区域外的4个稳定radial bands构造候选；使用膨胀障碍clearance、Target exclusion、Position safety circle和Holding pair spacing过滤，以Target revision、cell和量化位置生成HoldingId。

[COMPUTED][HIGH] Holding→Position第一版是严格直线compatibility，不复用polar route：以50cm或更密采样验证Flow bounds/reachability/clearance/Target exclusion，并精确检测StableOccupied/ReserveHold safety circle。真实Small fixture的20个Position均被compatibility图覆盖；该结论只证明测试几何，不证明正式Mass运动已经完成。

[COMPUTED][HIGH] Holding assignment复用合法owner并以WaitEpoch、量化far-side fill cost、route cost和稳定ID决胜；Commit只占当前短segment，不占Waiting未来路径。Commit gate同时检查readiness、revision、Position、compatibility、Target、Flow/Obstacle、Stable blocker和active/selected segment冲突。

[COMPUTED][HIGH] guidance保持职责单一：Pursuit/Reacquire使用Flow，Holding使用Holding destination，Commit使用Assigned Position，Occupied/Hold使用低增益回正；30cm内停止，输出按1cm/s量化并受MaxSpeed限制。

[COMPUTED][HIGH] 独立自动化在现有Small FlowField/Obstacle facts上完成20/20纯assignment、稳定hash、compatibility、commit set、release/reacquire和guidance验证。旧`FrontAssignedWaiting→RadialStage→AngularAlign→RadialCommit`仍是生产代码，尚未被该状态机替换。

[INFERRED][HIGH] 下一阶段只有在单独授权后才能设计Mass集成：prepared Holding/Compatibility/Commit SoA、最小fragment状态、fixed-step boundary apply、rollback和双端hash必须一起定义；不得把新内核叠在旧Phase Reservation owner之上。

## 当前权威修订：Steering-first Holding/Commit（2026-07-12）

[INFERRED][HIGH] 本修订覆盖本文件下方原始SF4生产设计中的Polar Approach、两阶段Phase Reservation和Route-Aware ORCA扩展方向；下方置于“实验结论/停止点”的内容继续作为实现事实和反证历史，不能删除或写成未发生。

[COMPUTED][HIGH] 当前代码已经实现并验证Candidate/Assignment、Polar Route、Phase Reservation、Wait-For Graph和Reservation-Aware ORCA诊断，但Static能力最终仅`StableOccupied/ReserveHold=1/0`。Target安全、障碍安全、双端hash与correction成立，吞吐不成立；最终边界没有合法最小fixture授权继续修改生产ORCA。

[INFERRED][HIGH] SF4目标架构改为围绕随机地形和开放虫群运动的最小分层：

```text
Shared Navigation / Flow
→ Steering Preferred Velocity
→ Position Candidate / Assignment
→ Holding Position 或 Commit Permit
→ Deterministic ORCA Safe Velocity
→ Obstacle Constraint / Hard PBD / Reproject
→ MovementFinalize
```

[INFERRED][HIGH] Shared Flow负责绕过随机墙体、障碍群和静态可达拓扑；Seek/Arrive/Holding Correction负责连续目标跟随；ORCA负责动态邻居避让；PBD只负责残余penetration。Positioning只决定Position、Holding和Commit三类有限资源，不管理每个实体完整路径。

### 保留

- [INFERRED][HIGH] `TargetFact`、Candidate annulus、Front/Reserve角色、稳定PositionId、批量确定性Assignment、assignment reuse与vacancy promotion。
- [INFERRED][HIGH] Shared Flow、Traffic事实、可选Portal、Movement Intent Compose、Deterministic ORCA、Obstacle Constraint、Hard PBD、Obstacle Reproject和双端fixed-step设施。
- [INFERRED][HIGH] `StableOccupied`、`ReserveHold`、`Reacquire`及未来membership boundary的Acquire/Release语义。

### 替换

- [INFERRED][HIGH] `FrontAssignedWaiting → RadialStage → AngularAlign → RadialCommit`替换为明确`Holding → Commit`；Holding实体拥有稳定WorldLocation，Commit实体只朝Assigned Position移动。
- [INFERRED][HIGH] current/requested polyline reservation替换为轻量Commit许可；第一版只验证Holding到Assigned Position的简单route/corridor是否与同批Commit互斥，不为Radial/Arc/Final段分别加锁。
- [INFERRED][HIGH] Wait-For Graph从生产控制候选降级为历史诊断；Route-Aware ORCA特例不再扩展。若普通ORCA在简化状态机下仍失败，必须用新的最小fixture归因，不能沿用旧Phase事实直接修改责任比例。

### Portal边界

[INFERRED][HIGH] Portal只处理由导航事实识别并经正式运行证明存在容量、对向公平或饥饿问题的硬瓶颈。普通随机地形、障碍组成的墙和同向虫群追逐不要求人工lane或Opened/Closed标识，优先由Flow加Steering/ORCA解决。

### UE官方参考边界

[COMPUTED][HIGH] UE 5.7 MassNavMesh使用NavPath/Corridor/ShortPath推进`FMassMoveTargetFragment`，MassZoneGraph沿lane推进MoveTarget，MassCrowd在明确crossing通过Waiting Area/Waiting Slot执行Acquire/Release。它们没有提供通用攻击距离Candidate或双端fixed-step Position分配。

[INFERRED][HIGH] SF4借鉴其“短MoveTarget＋明确Waiting Slot＋Acquire/Release”思想，不直接接入逐实体同步NavMesh FindPath、StateTree、World DeltaTime或ZoneGraph lane运行时；若未来需要官方导航，必须在独立Spike场景验证20/100/500查询成本、动态目标重规划和双端事实边界。

### 修订状态机

```text
Pursuit
├── 获得合法Holding assignment → Holding
└── 获得直接Commit许可 → Commit

Holding
├── Seek/Arrive到Holding Position并保持
└── 获得Commit许可 → Commit

Commit
├── 到达Front Position → StableOccupied
├── 到达Reserve Position → ReserveHold
└── route/assignment失效 → Reacquire

StableOccupied / ReserveHold
├── 合法assignment保持不变
├── Front vacancy时确定性promotion
└── Target/Candidate/membership失效 → Reacquire

Reacquire
└── fixed-step boundary释放旧资源 → Pursuit
```

[INFERRED][HIGH] Waiting没有合法Holding Position时不得进入目标核心区；它保持Pursuit并受目标外围arrival band约束。Holding不能继续消费指向共享goal中心的Flow velocity，也不能在任意当前位置静默零速。

### 修订Processor目标顺序

```text
RoundPlanApply
→ TargetFactApply
→ SharedFlowFieldBuild
→ CrowdTrafficFieldBuild
→ [证据门控] PortalSchedule
→ PositionCandidateBuild            [dirty]
→ PositionAssignment                [dirty/membership boundary]
→ HoldingCandidateBuild             [dirty]
→ HoldingAssignment / CommitGate    [fixed-step boundary]
→ FlowPreferredVelocity
→ SteeringIntentCompose
→ DeterministicORCA
→ MovementPredict
→ ObstacleConstraint
→ HardSeparationPBD
→ ObstacleReproject
→ MovementFinalize
→ AuthorityCommit / ClientPredictionCommit
```

[INFERRED][HIGH] Compose owner优先级为`Portal未Exited > Commit > Holding/Occupied correction > 普通Flow Pursuit`。ORCA只消费最终preferred velocity，不决定Position归属、Holding归属或Commit顺序。

### 修订实施门

[INFERRED][HIGH] 第一步只允许纯kernel fixture：Holding位置walkable/reachable、彼此不重叠、避开Target exclusion和简单Commit corridor；Commit集合按稳定优先级选择且route互不冲突；输入反序和重复执行hash一致；释放后survivor assignment保持且Reserve promotion稳定。

[INFERRED][HIGH] fixture通过后才接入prepared SoA、最小fragment、rollback和双端hash。Static Small必须满足Target crossing=0、双端penetration=0、hash match、checkpoint/interval p95<1cm、agents=visible=20，以及`Assigned=20`、`StableOccupied+ReserveHold=20`、corridor=20、deadlock=0。失败即停止，不恢复Phase Reservation、不调ORCA/PBD参数、不进入Moving Target。

## Reservation-Aware ORCA fixture定义与反证结果

[COMPUTED][HIGH] 新fixture使用最终fixed-step事实，不从timeline挑选样本。主实体必须是stalled Active，完整half-plane集合加入`dot(v,RouteForward)>=30cm/s`后量化不可行，并能稳定缩减为至多4个OtherAgentId的不可再缩减核心；否则fixture无效且不得选择生产分支。

[COMPUTED][HIGH] containment只证明下一1/30秒：当前点与下一位置必须位于current reservation首个有效segment的4cm capsule内，双方swept segment间距至少为半径和加1cm，下一位置保持Target exclusion clear。

[COMPUTED][HIGH] 唯一RoundResult捕获没有找到满足上述条件的候选，返回`valid=0`。这不证明Active约束无关；它只证明“最终边界的最小不可行核心”与此前跨step聚合的Active主导结论不是同一个事实。

[INFERRED][HIGH] 因此不能实施ActiveRouteDisjoint双解、时间化right-of-way或containment half-plane。若未来重开设计，必须先明确改变采样事实源（例如预声明稳定窗口而非最终边界）；不得在本轮事后改采样点以追逐期望分支。

## Reservation Wait-For Graph结论与后续边界

[COMPUTED][HIGH] wait-for graph只描述已发生的Phase reservation依赖，不替代scheduler：edge精确来自scheduler的requester/blocker pair；SCC用于识别环；stalled/progressing使用当前步ORCA后的route-forward速度和no-progress状态；stale owner要求blocker已不在active membership。

[COMPUTED][HIGH] 当前Static Small为3个cycle、最大9、11个stalled blocker、6个progressing blocker、0个stale owner。cycle内requested paths没有任何pairwise-safe的atomic handoff集合，因此禁止采用“整个SCC原子换位”；也没有ghost owner可清理，且不能归类为无环慢进展。

[COMPUTED][HIGH] 分支C的纯half-plane fixture定义额外约束`dot(v, RouteForward) >= 30cm/s`，分别检查连续解与1cm/s量化解，并通过删除Active、Waiting、Stable、Other来源判断哪类约束恢复可行性。生产诊断样本一致指向Active为主、Waiting次要；该结论是约束归因，不等于Active约束可以安全删除。

[COMPUTED][HIGH] 生产态逐步、1Hz和单点采样均破坏了correction验证设施，故已撤销所有runtime C采样与结果传输；当前保留的只有纯内核API、fixture和自动化。最终安全单轮仍为1/20，不能表述为SF4完成。

[INFERRED][HIGH] 后续若设计Active reservation迁移，必须同时证明：Target exclusion不穿越、current/requested route不冲突、迁移集合稳定、量化route-forward可行、输入乱序hash一致、双端结果一致。没有这些证明，不得通过忽略Active ORCA约束获得吞吐。

[INFERRED][HIGH] 实体membership变化必须在fixed-step boundary按以下生命周期处理：先确认active membership变更，再释放Portal token和Phase reservation，再使assignment失效；保留survivor的稳定assignment，仅对vacancy做确定性promotion；最后清除已离开成员对ORCA/PBD/wait graph的ghost影响，并比较server/client membership与assignment hash。本轮没有死亡系统，也没有实现该生命周期。

## 两阶段Phase Reservation生产验证结论

[COMPUTED][HIGH] 生产实现采用三类事实：Pipeline prepared SoA保存current/requested paths与本boundary decisions；Mass fragment保存已提交phase和最小rollback状态；RoundResult metrics保存累计decision hash与紧凑计数。算法仍只存在于纯kernel，processor负责采集、调用和原子apply。

[COMPUTED][HIGH] current reservation先占用且本boundary不可抢占；requested path按phase、commit step、AgentId稳定扫描。Granted替换自身current占用并提交phase，Held保留current安全终点，Invalid不进入requested path；AdmissionRequeue是唯一明确释放current phase并回Waiting的invalid规则。

[COMPUTED][HIGH] Static Small证明该语义安全且双端确定，但没有证明容量成立：1/20完成、4027次route-conflict Held、held p95=170 steps，Radial forward p50仅3.160cm/s。与保留版0/20相比只多完成1个，与历史最好16/20相比显著回退。

[INFERRED][HIGH] 根因边界不是“没有两阶段提交”，而是当前保守scheduler把所有current reservation视为不可抢占，并对被早期current阻止的请求在同一boundary不重试；大量长期Held再与active/waiting/stable ORCA half-plane叠加，吞吐不足。任何后续改动必须先证明公平迁移不会破坏Target exclusion，本轮禁止继续试验。

## 两阶段Phase Reservation纯内核状态

[COMPUTED][HIGH] 已新增`FCrowdDemoFrontPhaseReservationRequest/Result`与`ScheduleFrontPhaseReservations`纯C++接口。request同时表达current reservation和requested next-phase reservation，避免先切phase、后发现冲突的时序错误。

[COMPUTED][HIGH] 同一boundary的current reservation不可抢占；scheduler按requested phase、原commit step、AgentId稳定选择无冲突请求。Granted用requested path替换自身旧占用，Held保持旧占用，Target exclusion不安全或请求无效则Invalid。该单遍策略是保守的：被其他current reservation阻止的高优先级请求不会因对方稍后迁移而在同一boundary重试。

[COMPUTED][HIGH] 纯fixture已经证明互不冲突的next phase可并发、冲突请求被held、held旧占用不会提前消失、后期phase优先、Target门先决以及输入反序确定性。Development和两项定位测试通过。

[COMPUTED][HIGH] 该段记录接入前状态；上述生产侧设施现已实现，但Static能力门仅1/20，不能表述为SF4站位完成。

## Phase-Local Reservation失败约束

[COMPUTED][HIGH] 直接按当前phase裁剪polyline会提高并发，但phase已经在ApproachRoute阶段切换，Admission只能事后发现冲突。requeue造成反复重启，held则让冲突实体停在目标邻域并放大ORCA约束，最终出现Target exclusion crossing。

[INFERRED][HIGH] 正确模型必须把phase transition改成两阶段提交：`RequestedPhase`先生成next-phase reservation，scheduler在fixed-step boundary决定，获批后才写`FrontApproachPhase`；未获批实体继续保持上一phase的安全终点，不进入下一段。Target exclusion资源必须先于普通route pair决胜。

[COMPUTED][HIGH] 两阶段纯scheduler已经实现并通过定向测试；完整生产状态机尚未实现，失败实验仍保持撤销。

## Phase-Local Admission待设计边界

[COMPUTED][HIGH] constraint来源诊断显示保留版低forward由1个active和1个ReserveCommit约束构成；动态swept预测扩大yielding责任后反而提高全局infeasible并降低grant，实验已撤销。

[INFERRED][HIGH] Full route从当前径向段一直占用到candidate，使后续不同phase实际不同时占用的路径也被判冲突。候选下一版是phase-local reservation：只把当前phase的量化segment集合放入Active route集合，phase transition boundary重新选择最大稳定无冲突集合。

[INFERRED][HIGH] 该方案必须先通过纯fixture证明：同一phase冲突不可并发、不同sector径向段可并发、交叉arc不可并发、phase切换不会让两个已active route进入同一占用段、输入乱序与两轮hash一致。本轮尚未实现。

## Route-Aware ORCA第一版定义与失败边界

[COMPUTED][HIGH] 第一版只覆盖Active Approach与确定性Waiting之间的route占用冲突。Active route由当前位置和当前prepared polar route points组成；若polyline到yielding实体中心的最短距离不超过双方半径加SafetyGap，则active不承担该pair约束，yielding承担完整责任。该规则不扩展到Stable或两个Active route，也不替换Hard PBD/ObstacleReproject。

[COMPUTED][HIGH] `FrontAssignedWaiting`和在Front完成前被固定为零guidance的`ReserveCommit`属于yielding；Portal Admission非None时不启用SF4 route mode。该边界避免把Portal priority误当成SF4 route许可。

[COMPUTED][HIGH] RadialStage、AngularAlign、RadialCommit使用不同误差单位，因此phase变化时重置BestErrorBucket和no-progress timer。Radial→Angular进入门为`RadialTolerance+SafetyGap`，Angular阶段仍持续纠正外圈径向误差。

[COMPUTED][HIGH] 最新单轮仍为0/20；虽然Radial ORCA幅值约800cm/s，沿route forward只有83.6cm/s。第一版route policy只关闭active↔yielding且几何相交的约束，不能保证剩余half-plane集合存在足够的route-forward可行速度。

[INFERRED][HIGH] 下一版设计前置条件是纯C++多route fixture：固定active routes、waiting/stable位置和全部ORCA half-plane，验证每个active agent存在最小正向速度下界，并将失败约束归因到active、stable、yielding或other。没有fixture不得继续扩展生产规则。

## Approach compose滞回与ORCA route契约缺口

[COMPUTED][HIGH] 首次grant的进入条件为agent到assigned candidate距离不超过1200cm；grant前由Flow/Traffic继续负责远端路线。grant后只要状态为`FrontCommitGranted/SlotCommit`且ApproachPhase非None，Approach guidance跨越距离门仍持续拥有compose；Portal状态非None时Portal优先。

[COMPUTED][HIGH] 该规则把原295次compose boundary switch降为22，并恢复800cm/s Radial preferred；但保留版本ORCA/final仅65.765cm/s，0/20完成。直接把active route映射为Portal Reserved priority反而得到0cm/s并产生一次Target exclusion crossing，实验已撤销。

[INFERRED][HIGH] SF4需要独立的route-aware避让语义，而不是借用Portal direction priority。后续设计至少必须定义active swept route、冲突route集合、waiting/stable让行责任、沿route方向的可行速度验证和Target exclusion复验；在纯fixture证明前不得接入正式pipeline。

## Admission并发模型与RadialStage诊断停点

[COMPUTED][HIGH] Admission boundary的输入分为active route集合与waiting route候选。active route先进入占用集合；waiting按`Depth → AngularSector → PositionId → Travel → AgentId`稳定扫描，只有与全部active及本次已选择route均不冲突时才grant。该算法输出稳定的贪心极大无冲突集合，不是全局最大基数集合。

[COMPUTED][HIGH] Route冲突仍使用双方半径加SafetyGap的swept segment检查；Admission并发测试证明不同外圈路径可以共存，交叉路径仍被拆分，输入乱序不改变decision hash。

[COMPUTED][HIGH] RadialStage运行诊断分别采样preferred、ORCA和final speed；同时记录径向误差、相邻step真实下降、量化bucket未变但连续误差下降，以及实体进出compose range的切换次数。

[COMPUTED][HIGH] Static Small单轮得到Radial preferred/ORCA/final p95=`70.000/799.966/799.966cm/s`、radial error p50/p95/max=`480/1009/1009cm`、改善数=1、量化stall=0、compose boundary switches=295；完成状态为0/20。

[INFERRED][HIGH] ORCA压速不是当前RadialStage主因。当前compose-range条件直接由瞬时距离判定且没有滞回/phase锁，295次切换与Radial preferred远低于final共同指向guidance owner反复切换。下一次设计必须先让compose选择成为单调或带滞回的确定性状态，再复测，不应通过调速度或放宽ORCA掩盖。

## Target-Aware Approach Gate实验定义与停止点

[COMPUTED][HIGH] Target exclusion radius=`TargetRadius+AgentRadius+SafetyGap`；OuterGate radius=`max(CandidateRadius+GateClearance, TargetExclusionRadius+GateClearance)`，其中GateClearance=`max(实体直径+SafetyGap, FlowCellSize)`。这只影响SF4 target-near positioning，不修改全局ObstacleSpecs或SF1/SF2/SF3。

[COMPUTED][HIGH] 实验route为`FrontAssignedWaiting → FrontCommitGranted → RadialStage → AngularAlign → RadialCommit → SlotCommit → StableOccupied`。RadialStage沿当前径向到安全外圈，AngularAlign沿合法最短arc对齐candidate sector，RadialCommit只在Target exclusion与occupied blocker均清空时进入。

[COMPUTED][HIGH] No-progress使用量化RouteError bucket、BestError、LastProgressStep与NoProgressSteps；只有误差未改善才累计，不再按grant后的绝对时长requeue。fragment与prepared route已进入既有correction rollback结构。

[COMPUTED][HIGH] 单轮结果为0/20完成，13 Waiting、2 RadialStage、1 RadialCommit，证明当前Admission+Route吞吐在30秒内不足；按硬门停止，不能表述为Approach Gate收敛完成。

## Front Ingress诊断结论与分支A停止点

[COMPUTED][HIGH] 原16/20停点的4个SlotCommit全部被StableOccupied安全圆阻断，14个Stable blocker pairs中有3个进入顺序倒置；Stable来源ORCA约束48条为主导，Target exclusion只覆盖1个，Reserve阻断与PBD/Obstacle推离均为0。minimum fixture hash=`44101239`，只保存Target、一个未完成Agent、Assigned candidate、必要blockers、半径/状态与约束来源ID。

[INFERRED][HIGH] 该证据选择Front Admission分支A：Front表示当前统一Engagement profile的容量，不代表攻击许可；获得Front assignment不等于获得commit admission。

[COMPUTED][HIGH] 实验状态机为`FrontAssignedWaiting → FrontCommitGranted → SlotCommit → StableOccupied`。EntryAxis由Target到cohort SpawnOrigin确定；candidate按轴向深度、AngularSector、PositionId排序，远侧先于入口侧；wave固定2，交叉swept path拆wave，timeout为180 fixed steps并确定性requeue。

[COMPUTED][HIGH] 该实验没有修改ORCA几何/责任、MaxSpeed、ArrivalTolerance、PBD、Flow/Portal/Traffic、30秒轮次或网络参数，但正式结果回退到StableOccupied+ReserveHold=7/20，因此分支A未通过，不能表述为完成的Front admission设计，也不允许进入Moving Target。

[COMPUTED][HIGH] 2026-07-12单Front带验证：纯kernel已把最内侧有效Preferred radial band作为唯一Front，外层合法带作为Reserve；Static Small两轮得到Front/Reserve capacity=`16/134`与StableOccupied/ReserveHold=`12/4`。由于仍有4个SlotCommit且arrival error p95=`488.945cm`，Stage B硬门继续失败，Moving Target未开始。

## 1. 文档职责

[INFERRED][HIGH] 本文件是 `SimRoundPursuitPositioning` 的设计事实源，定义追逐、近目标容量分配、稳定占位、外围等待和动态目标重定位的职责与确定性规则。

[INFERRED][HIGH] 本文件不表示SF4已经实现或通过。当前已验证基线仍是SF1/SF2以及SF3的路线、安全和双端确定性；SF3目标区最终效果尚未通过。

[COMPUTED][HIGH] 本设计依据当前Demo代码与文档，以及原工程以下设计与实现边界整理：

- [KNOWN][HIGH] `25_AttackPointServicePlan.md`：Target registry、位置类型、reservation生命周期和多目标冲突。
- [KNOWN][HIGH] `26_MassEnemyAttackZoneReservationPlan.md`：Allowed/Preferred距离、zone容量、稳定local offset和Reserve晋升。
- [KNOWN][HIGH] `34_MassAISwarmBusinessLogicContract.md`：Corridor、SlotCommit、Attack、ReserveHold阶段。
- [KNOWN][HIGH] `35_MassAIArchitectureBoundaries.md`：Planner、Movement、Replication、Client Visual和业务slot边界。
- [KNOWN][HIGH] `38_MassAIProcessorPipelineContract.md`：WORK POD-only、GT Apply/Commit和last-good规则。

## 2. 当前问题

[COMPUTED][HIGH] 当前SF3为每个cohort提供单一`FlowFieldConfig.GoalLocation`。所有实体最终竞争同一goal附近空间，没有独立位置容量。

[COMPUTED][HIGH] `FlowPreferredVelocity`在140cm goal半径内把preferred velocity置零，但后续`PassingBandGuidance`会从`TrafficAgent.FlowDirection`重新生成速度。因此“到达goal”只是sticky统计，不是稳定运动状态。

[COMPUTED][HIGH] 到达实体仍参加TrafficField、ORCA和PBD，并继续对后续实体产生约束。当前没有candidate、reservation、StableOccupied、ReserveHold、position invalidation或promotion数据。

[COMPUTED][HIGH] 当前Target Actor禁用Tick，保留的waypoint函数没有形成双端fixed-step动态目标事实。

[INFERRED][HIGH] 剩余问题不能通过扩大goal半径、放宽ORCA、隐藏实例、提高速度或增加视觉偏移解决。需要在共享追逐与局部避让之间新增确定性近目标调度层。

## 3. 复用与替换边界

### 3.1 复用原工程概念

[INFERRED][HIGH] SF4复用以下概念，但不直接复制原工程运行时实现：

- [INFERRED][HIGH] 合法距离与偏好距离分离。
- [INFERRED][HIGH] Front与Reserve容量分离。
- [INFERRED][HIGH] reservation acquire、reuse、refresh、release。
- [INFERRED][HIGH] 稳定local offset与target anchor。
- [INFERRED][HIGH] vacancy promotion和assignment hysteresis。
- [INFERRED][HIGH] Pursuit、SlotCommit、StableOccupied、ReserveHold阶段。
- [INFERRED][HIGH] 目标选择、位置分配、运动、安全修正和业务攻击分层。

### 3.2 不直接复用原工程实现

[COMPUTED][HIGH] 原工程AttackPoint服务依赖`UWorld`、Actor、GT导航投影、可变`TMap`和逐消费者请求；原ClusterPlanner还包含大量历史fallback与调参路径。

[INFERRED][HIGH] SF4不得直接引入：

- [INFERRED][HIGH] `UProjectAttackPointSubsystem`作为正式求解器。
- [INFERRED][HIGH] 逐Agent请求并即时修改occupancy。
- [INFERRED][HIGH] 依赖`TMap`迭代顺序的同分选择。
- [INFERRED][HIGH] render-frame Tick驱动的Target运动。
- [INFERRED][HIGH] 旧PlatformTargetSlotCache或legacy fallback。
- [INFERRED][HIGH] 攻击、受击、死亡或业务StateTree。

## 4. 场景隔离

[INFERRED][HIGH] 新增独立场景：

```cpp
SimRoundPursuitPositioning = 3
```

[INFERRED][HIGH] SF1、SF2、SF3必须保持行为与验收不变。SF4复用SF3 Shared Flow、Traffic、Portal、ORCA、Obstacle Constraint、Hard PBD和双端fixed-step设施，但新增独立positioning分支。

[INFERRED][HIGH] 第一批地图建议为：

```text
/Game/Maps/CrowdDemo_SimRoundPursuitPositioningStaticSmall
/Game/Maps/CrowdDemo_SimRoundPursuitPositioningMovingSmall
```

[INFERRED][HIGH] 两张地图均使用20个实体。只有两张Small地图通过后，才另行设计100/500规模。

## 5. 架构分层

```text
Target事实层
├── TargetId、量化位置/速度/半径
├── TargetRevision
└── fixed-step motion input

群体追逐层
├── cohort目标
├── Shared FlowField
├── TrafficField
└── Portal schedule

近目标站位层
├── Position Candidate Build
├── Deterministic Assignment
├── Reservation Lifecycle
├── StableOccupied
└── Reserve Promotion

局部运动层
├── Movement Intent Compose
├── Deterministic ORCA
├── Obstacle Constraint
├── Hard PBD
└── Obstacle Reproject

业务层（本阶段禁止实现）
├── 攻击许可
├── 受击
├── 死亡
└── 技能执行
```

[INFERRED][HIGH] Positioning只产生位置所有权、状态和movement intent。它不产生攻击、伤害或业务slot执行。

## 6. 数据模型

### 6.1 Target事实

```cpp
struct FCrowdDemoPursuitTargetFact
{
    int32 TargetId = INDEX_NONE;
    FVector2f Location = FVector2f::ZeroVector;
    FVector2f Velocity = FVector2f::ZeroVector;
    float RadiusCm = 0.0f;
    int32 Revision = 0;
    int32 StableMotionStep = 0;
};
```

[INFERRED][HIGH] Location和Velocity必须按项目确定性精度量化；Target事实只在fixed-step boundary更新。

### 6.2 Positioning设置

```cpp
struct FCrowdDemoPursuitPositioningSettings
{
    float AllowedDistanceMinCm = 0.0f;
    float AllowedDistanceMaxCm = 0.0f;
    float PreferredDistanceMinCm = 0.0f;
    float PreferredDistanceMaxCm = 0.0f;
    float SafetyGapCm = 10.0f;
    float CommitDistanceCm = 0.0f;
    float ArrivalToleranceCm = 0.0f;
    float ExitToleranceCm = 0.0f;
    int32 StableArrivalSteps = 0;
    int32 ExitGraceSteps = 0;
    int32 MaxAssignmentProposalRounds = 0;
    int32 TargetUpdateIntervalSteps = 0;
    int32 TargetSettleSteps = 0;
};
```

[INFERRED][HIGH] Allowed距离决定合法性；Preferred距离只参与评分。只要旧reservation仍合法，Preferred变化不能单独强制重分配。

### 6.3 Candidate

```cpp
enum class ECrowdDemoPositionRole : uint8
{
    Front,
    Reserve
};

struct FCrowdDemoPositionCandidate
{
    int32 PositionId = INDEX_NONE;
    int32 TargetId = INDEX_NONE;
    ECrowdDemoPositionRole Role = ECrowdDemoPositionRole::Reserve;
    FVector2f LocalOffset = FVector2f::ZeroVector;
    FVector2f WorldLocation = FVector2f::ZeroVector;
    int32 StableCellKey = INDEX_NONE;
    int32 RadialBand = INDEX_NONE;
    int32 AngularSector = INDEX_NONE;
    int32 Capacity = 1;
    bool bReachable = false;
    bool bClearanceValid = false;
};
```

[INFERRED][HIGH] 第一版Candidate capacity固定为1，避免在一个zone内部再次产生未定义的多人站位。

### 6.4 Assignment状态

```cpp
enum class ECrowdDemoPursuitPositionState : uint8
{
    Pursuit,
    SlotCommit,
    StableOccupied,
    ReserveCommit,
    ReserveHold,
    Reacquire
};

struct FCrowdDemoPositionAssignmentFragment : public FMassFragment
{
    int32 TargetId = INDEX_NONE;
    int32 PositionId = INDEX_NONE;
    int32 AssignmentRevision = 0;
    ECrowdDemoPositionRole Role = ECrowdDemoPositionRole::Reserve;
    ECrowdDemoPursuitPositionState State = ECrowdDemoPursuitPositionState::Pursuit;
    FVector LocalOffset = FVector::ZeroVector;
    FVector DesiredLocation = FVector::ZeroVector;
    int32 StableArrivalStepCount = 0;
    int32 ExitGraceStepCount = 0;
    int32 LastReassignmentStep = INDEX_NONE;
};
```

[INFERRED][HIGH] Candidate catalog、assignment proposals和全局结果保存在PipelineSubsystem prepared SoA；每实体fragment只保存被消费的最小assignment状态。

## 7. Candidate生成规则

[INFERRED][HIGH] 第一版在现有FlowField raster上生成候选，不实施NavMesh Bake：

1. [INFERRED][HIGH] 根据Target radius、Agent radius、Allowed距离和SafetyGap构造二维合法annulus。
2. [INFERRED][HIGH] 按稳定cell key遍历目标附近cell。
3. [INFERRED][HIGH] 过滤OutOfBounds、blocked、unreachable和clearance不足cell。
4. [INFERRED][HIGH] Allowed带内生成Front候选。
5. [INFERRED][HIGH] Front带外生成一个或多个Reserve带。
6. [INFERRED][HIGH] Candidate中心间距不得小于两个实体半径加SafetyGap。
7. [INFERRED][HIGH] 候选按`Role → RadialBand → AngularSector → StableCellKey`稳定排序。
8. [INFERRED][HIGH] `PositionId`由TargetId、量化LocalOffset、Role和RadialBand稳定hash生成，不依赖数组插入顺序。

[INFERRED][HIGH] 当前100cm Flow cell与约84cm实体直径接近，因此一个合法cell一个candidate适合作为第一版fixture；它不是最终离线导航资产方案。

## 8. 确定性批量分配

[INFERRED][HIGH] 分配必须批量执行，禁止逐Agent即时占位。

### 8.1 保留优先级

1. [INFERRED][HIGH] 保留仍合法的StableOccupied assignment。
2. [INFERRED][HIGH] 保留仍合法的SlotCommit和ReserveHold assignment。
3. [INFERRED][HIGH] Front vacancy优先给满足commit条件的近端实体。
4. [INFERRED][HIGH] 其余实体分配Reserve。
5. [INFERRED][HIGH] 无Candidate的实体保持Pursuit，不得共享已占位置。

### 8.2 量化评分

```text
IntegerCost =
    QuantizedTravelDistance
  + PreferredRangePenalty
  + ApproachSectorChangePenalty
  + CandidateConflictPenalty
  + ReassignmentPenalty
  - ExistingAssignmentReuseBonus
```

[INFERRED][HIGH] 决胜顺序固定为：

```text
Cost
→ ExistingOwner优先
→ AgentId
→ PositionId
```

### 8.3 Deferred acceptance

[INFERRED][HIGH] 建议采用固定轮数的确定性deferred-acceptance：

1. [INFERRED][HIGH] 每个Agent按稳定cost列表提议。
2. [INFERRED][HIGH] Candidate保留当前最优Agent。
3. [INFERRED][HIGH] 被替换Agent在下一轮提议下一个Candidate。
4. [INFERRED][HIGH] 达到固定最大轮数后停止。
5. [INFERRED][HIGH] 最终assignment按AgentId排序并计算hash。

[INFERRED][HIGH] 该方案避免输入顺序影响，并比500实体全量Hungarian匹配更适合后续扩展。

## 9. 状态机

```text
Pursuit
├── 获得Front且进入commit窗口 → SlotCommit
└── 获得Reserve → ReserveCommit

SlotCommit
└── arrival误差与低速连续满足N步 → StableOccupied

ReserveCommit
└── arrival误差连续满足N步 → ReserveHold

ReserveHold
└── Front vacancy且promotion成立 → SlotCommit

StableOccupied / ReserveHold
└── Candidate失效、Target切换或超出exit门并持续M步 → Reacquire

Reacquire
└── 释放旧reservation → Pursuit
```

[INFERRED][HIGH] ExitTolerance必须大于ArrivalTolerance。Obstacle/PBD短暂推出arrival范围不能立即释放assignment。

## 10. 静态目标行为

[INFERRED][HIGH] 远距离实体继续消费Shared Flow、Traffic和Portal。只有通过最后Portal并进入commit区域后，positioning才能接管最终DesiredLocation。

[INFERRED][HIGH] SlotCommit和ReserveCommit使用指向AssignedPosition的比例guidance并限制MaxSpeed；StableOccupied和ReserveHold在容差内输出零preferred，轻微漂移只做低增益回正。

[INFERRED][HIGH] StableOccupied实体继续作为ORCA邻居存在，代表真实占用空间；Candidate间距必须让这些静止实体不形成虚假penetration。

## 11. 动态目标追逐

[INFERRED][HIGH] 动态目标使用两个anchor：

```text
RouteAnchor      = 有界预测后的共享追逐目标
EngagementAnchor = 当前服务器权威目标位置
```

[INFERRED][HIGH] Shared Flow追逐RouteAnchor；候选位置围绕EngagementAnchor。禁止围绕预测点形成最终站位。

[INFERRED][HIGH] 小幅目标移动时保留PositionId和LocalOffset，只平移WorldLocation并重新验证reachability/clearance；实体通过正常locomotion追随，不能传送。

[INFERRED][HIGH] 大幅移动、Target切换或Candidate失效时增加revision，只重分配失效或明显劣化assignment，不能全群同一帧清空重抢。

[INFERRED][HIGH] 高速目标阶段不强制StableOccupied。目标速度超过实体能力时允许实体落后，不提高基础速度、不传送、不扩大visual correction。

[INFERRED][HIGH] 目标连续低速或停止达到固定步门槛后，才开放稳定Front占位和Reserve promotion。

## 12. Movement Intent合成

[INFERRED][HIGH] 必须新增显式Movement Intent Compose，禁止processor静默覆盖前一阶段速度。

[INFERRED][HIGH] 建议优先级为：

```text
Portal Waiting / Reserved / Inside
> StableOccupied / ReserveHold回正
> SlotCommit / ReserveCommit
> 普通Shared Flow追逐
```

[INFERRED][HIGH] Portal状态未Exited时，远处Position assignment不能绕过Portal guidance。

## 13. Processor顺序

```text
RoundPlanApply
→ TargetFactApply
→ SharedFlowFieldBuild
→ CrowdTrafficFieldBuild
→ PortalSchedule
→ PositionCandidateBuild       [dirty时]
→ PositionAssignment           [dirty/boundary时]
→ FlowPreferredVelocity
→ PassingBandGuidance
→ PursuitPositionGuidance
→ MovementIntentCompose
→ DeterministicORCA
→ MovementPredict
→ ObstacleConstraint
→ HardSeparationPBD
→ ObstacleReproject
→ MovementFinalize
→ AuthorityCommit / ClientPredictionCommit
```

[INFERRED][HIGH] MovementFinalize仍是唯一写`FCrowdDemoRoundSimStateFragment`的位置。Correction继续只在fixed-step boundary应用。

## 14. Facing边界

- [INFERRED][HIGH] Pursuit、SlotCommit、ReserveCommit面向locomotion方向。
- [INFERRED][HIGH] StableOccupied面向真实Target。
- [INFERRED][HIGH] ReserveHold可面向Target或稳定idle yaw。
- [INFERRED][HIGH] ORCA、PBD、Obstacle correction不能成为正常Facing来源。
- [INFERRED][HIGH] 本阶段不生成Attack状态或攻击许可。

## 15. 双端确定性

[INFERRED][HIGH] Server和Client必须消费相同的Target fixed-step事实、Candidate输入、membership hash、TargetRevision、量化cost、AgentId/PositionId顺序和固定proposal轮数。

[INFERRED][HIGH] Target Actor不得在双端各自使用render-frame DeltaSeconds推进。Demo动态目标应由纯C++ fixed-step motion kernel计算，或复制量化TargetFact并在boundary应用。

[INFERRED][HIGH] 每fixed-step输出Candidate hash、Assignment hash和TargetFact hash；RoundResult比较双端hash并在checkpoint应用前报告mismatch。

## 16. 指标

```text
position_candidate_count
position_front_capacity
position_reserve_capacity
position_assigned_count
position_unassigned_count
position_stable_occupied_count
position_reserve_hold_count
position_assignment_reused_count
position_assignment_changed_count
position_assignment_churn_count
position_invalidated_count
position_promotion_count
position_promotion_wait_steps_p95
position_arrival_error_cm_p95
position_exit_after_occupied_count
position_candidate_overlap_count
position_candidate_unreachable_count
target_revision_count
target_reanchor_count
target_follow_error_cm_p95
target_stop_to_settle_seconds
position_candidate_hash
position_assignment_hash
target_fact_hash
```

[INFERRED][HIGH] `flow_goal_reached_count`继续作为路线指标，但不能作为最终站位完成指标。最终完成由assigned candidate arrival和StableOccupied/ReserveHold判断。

## 17. 分阶段验收

### 17.1 A：纯kernel静态fixture

- [x] [COMPUTED][HIGH] Candidate生成重复执行与输入消费乱序不改变结果。
- [x] [COMPUTED][HIGH] CandidateId稳定且无重叠。
- [x] [COMPUTED][HIGH] 20个Agent获得20个唯一assignment。
- [x] [COMPUTED][HIGH] Existing assignment 20/20复用成立。
- [x] [COMPUTED][HIGH] 真实ReserveHold vacancy promotion稳定。
- [x] [COMPUTED][HIGH] 两轮Candidate/Assignment hash一致。

[COMPUTED][HIGH] 阶段A提交为`bac361f7`；Development、DebugGame Editor、完整25项`CrowdDemo.SF`与diff门通过。阶段B/C尚未实施。

### 17.2 B：Static Target Small 20

- [ ] [INFERRED][HIGH] corridor=20、deadlock=0。
- [ ] [INFERRED][HIGH] assigned=20。
- [ ] [INFERRED][HIGH] StableOccupied+ReserveHold=20。
- [ ] [INFERRED][HIGH] Candidate overlap/unreachable=0。
- [ ] [INFERRED][HIGH] settle后assignment churn=0。
- [ ] [INFERRED][HIGH] 双端penetration=0、agents=visible=20。
- [ ] [INFERRED][HIGH] 双端Candidate/Assignment/AgentState hash一致。

[COMPUTED][HIGH] 2026-07-12首次正式两轮停点：corridor=20、deadlock=0、assigned=20、candidate overlap/unreachable=0、penetration=0、agents=visible=20通过；StableOccupied+ReserveHold=13、Traffic/Portal/ORCA双端hash mismatch与精确VIOLATION失败。阶段B未通过，阶段C未开始。

[COMPUTED][HIGH] rollback修复后两轮Traffic/Portal/ORCA/AgentState hash均match=1，correction interval p95=0且无VIOLATION；StableOccupied+ReserveHold仍为13，arrival error p95=266.895cm，因此阶段B仍未通过，阶段C仍未开始。

[COMPUTED][HIGH] SF4连续reproject修复后arrival error p95降为199.057cm，但StableOccupied+ReserveHold仍为13；7个SlotCommit均被19邻居ORCA约束调整到2.828cm/s。当前Front capacity=56且20实体全部为Front，单Front带/外层Reserve规则尚未实现，阶段B继续失败。

### 17.3 C：Moving Target Small 20

[INFERRED][HIGH] 固定步目标路径至少覆盖静止、低速直线、连续转角、较高速移动、减速和停止。

- [ ] [INFERRED][HIGH] 目标移动期间没有全群同步释放reservation。
- [ ] [INFERRED][HIGH] 无同步stop-go、速度膨胀或传送。
- [ ] [INFERRED][HIGH] 目标高速阶段允许物理落后。
- [ ] [INFERRED][HIGH] 目标停止后在有界时间内重新稳定站位。
- [ ] [INFERRED][HIGH] 两轮Target/Candidate/Assignment/AgentState hash一致。
- [ ] [INFERRED][HIGH] 双端penetration=0、agents=visible=20。

### 17.4 D：规模扩展

[INFERRED][HIGH] 只有Static 20与Moving 20全部通过，才允许另行设计100/500门控。本文件不授权自动进入规模扩展。

## 18. 明确非目标

[COMPUTED][HIGH] 本设计阶段不实施：

- [COMPUTED][HIGH] 攻击、受击、死亡、技能或业务StateTree。
- [COMPUTED][HIGH] 通用业务slot服务回迁。
- [COMPUTED][HIGH] NavMesh查询、Recast Bake或Crowd Navigation Field资产。
- [COMPUTED][HIGH] SF1/SF2/SF3参数修改。
- [COMPUTED][HIGH] 网络预算、correction频率、chunk size或NetUpdateFrequency修改。
- [COMPUTED][HIGH] P1、100、500、Medium、Cohort或Crossing运行。
- [COMPUTED][HIGH] timeline trace或通用A/B框架。

## 19. 后续与原工程衔接

[INFERRED][HIGH] SF4验证通过后，可把纯Candidate/Assignment kernels作为原工程Planner输入候选，而不是整体复制Demo Coordinator或RoundSim设施。

[INFERRED][HIGH] 原工程目标选择、攻击能力、受击、死亡和业务状态继续拥有业务权威；Positioning只返回稳定movement target、reservation role和assignment revision。

[INFERRED][HIGH] 更后续应由Editor/Commandlet从明确导航数据生成确定性Crowd Navigation Field，再替换本设计中的FlowField raster候选过滤。该基础设施不属于SF4第一轮。

## 20. Steering-first 正式接入与 Static 单轮停点（2026-07-12）

[COMPUTED][HIGH] 新生产链已按 fixed-step boundary 接入 Holding candidate/compatibility/assignment、Commit Gate、单一 SteeringState writer 与 Steering-first guidance；旧 Polar/Admission/Phase Reservation/WaitGraph 不参与 SF4 生产决策，旧 FrontApproachPhase 不再注入 ORCA route ownership。

[COMPUTED][HIGH] `FCrowdDemoPursuitSteeringStateFragment` 是跨步 steering ownership 的唯一事实；PositionAssignment 继续拥有 PositionId/Role/DesiredLocation。动态候选图和 decision records 留在 prepared SoA，checkpoint rollback 同时恢复 fragment、prepared arrays、summary、metrics 与四类 hash。

[COMPUTED][HIGH] 自动化结果：Development 通过；`CrowdDemo.SF4.Positioning` 3/3、`CrowdDemo.SF4.Integration.SteeringFirstMassContract` 1/1、完整 `CrowdDemo.SF` 28/28 通过。

[COMPUTED][HIGH] Static Small 单轮安全事实：Traffic/Portal/ORCA/AgentState 与 HoldingCandidate/HoldingAssignment/CommitDecision/SteeringState 双端一致；checkpoint p95=0.055cm、interval p95=0.063cm；revision gap=0；最终障碍 penetration=0；client agents=visible=20；Phase Reservation request/grant/held=0。

[COMPUTED][HIGH] 第一硬失败是 corridor exit=10/20。能力末态为 Holding=10、Reacquire=10、Commit=0、StableOccupied=0、ReserveHold=0；HoldingAssigned=20、HoldingAllocationFailure=0，但累计 commit_invalid=9010。该结果只能证明接入与双端传输可运行，不能证明 Static Positioning 能力成立。

[COMPUTED][HIGH] 按门控停止：未运行 DebugGame、正式两轮、录像、Moving Target、P1 或100/500；未修改地图、ORCA/PBD参数、速度、半径、ArrivalTolerance、网络预算或 fixed-step。

## 21. Compatibility proof 与远距离 Holding 接管停点

[COMPUTED][HIGH] 单轮分类证明原10个Reacquire全部来自compatibility rejected；Position invalid、Target revision mismatch和lookup missing均为0。根因是Assignment选择合法边后，CommitRequest再次按非唯一二元键查询graph，可能取到同键的另一条rejected边。

[COMPUTED][HIGH] Assignment现携带selected compatibility hash/valid proof。自动化保持Development、Positioning 3/3和完整SF 28/28通过；正式单轮Invalid=0、Holding=20、双端hash一致、penetration=0。

[COMPUTED][HIGH] 该修复暴露两个后续缺陷：一是8100个重复compatibility key证明HoldingId不唯一；二是Pursuit在出生boundary立即转Holding，使Holding guidance过早覆盖Shared Flow，结果corridor=0、goal=0、Commit grant=0、Held=18020、Stable+Reserve=0/20。

[INFERRED][HIGH] 后续顺序必须是：先建立无碰撞Holding identity regression，再定义基于Flow路线进度/局部handoff envelope的Pursuit→Holding gate。不得恢复旧Phase Reservation，也不得靠放宽Holding tolerance或ORCA参数让远距离直接steering碰巧通过。

## 22. Stable identity与Flow→Holding handoff结果

[COMPUTED][HIGH] HoldingId和PositionId改为各自candidate的StableCellKey；TargetRevision继续负责跨目标生命周期失效。Position/Holding乱序与唯一性自动化通过，正式duplicate compatibility keys=0。

[COMPUTED][HIGH] `ShouldEnterHolding`仅在Flow sample为Reachable且实体距assigned Holding不超过现有FrontAdmissionHoldRangeCm时成立。Pursuit阶段guidance继续返回Flow preferred velocity；远距离Pursuit不生成CommitRequest。

[COMPUTED][HIGH] Development、完整SF 28/28通过。正式单轮corridor=20、deadlock=0、final penetration=0、双端四类新hash一致；checkpoint p95=0.051cm、interval p95=0.061cm。

[COMPUTED][HIGH] 能力门仍失败：Pursuit=5、Holding=4、Commit=0、StableOccupied=9、ReserveHold=2，总完成11/20；Commit累计granted=702、held=3286、invalid=0，ORCA infeasible=781、fallback stop=958。

[INFERRED][HIGH] 下一诊断必须分别解释5个Pursuit为何未进入handoff，以及4个Holding为何未完成ready/grant/arrival；需要分状态距离、forward speed、readiness rejection、commit conflict和ORCA来源聚合。当前证据不授权改变handoff range、Holding tolerance或ORCA参数。

## 23. 11/20终态只读诊断

[COMPUTED][HIGH] 新增每种SteeringState的目标距离、preferred/ORCA/final forward speed p50/p95，以及Pursuit handoff/Flow和Holding readiness终态分类；这些字段只覆盖诊断与RoundResult，不写回模拟。

[COMPUTED][HIGH] Pursuit=5：outside handoff=5、invalid flow=0、holding distance p50/p95=1291.155/1332.901cm、preferred forward p50/p95=800.051/800.236cm/s、ORCA/final forward p50/p95=0。

[COMPUTED][HIGH] Holding=4：holding distance p50/p95=26.348/28.994cm；distance-not-ready=0、speed-not-ready=0、ready-conflict=4、ready-granted=0。

[INFERRED][HIGH] 因此不能用扩大handoff range解决Pursuit：它们已有有效800cm/s preferred但被ORCA压零。也不能放宽Holding tolerance：四者已经满足ready。下一步必须细分ORCA constraint source与Commit geometry/owner conflict source。

## 24. Stable blocker compatibility 缓存分支与最终停点

[COMPUTED][HIGH] Commit Gate终态来源拆分得到`target/flow/obstacle/stable/active/selected=0/0/0/4/0/0`；Pursuit ORCA constraint来源中StableOccupied占主导。该证据支持修复compatibility缓存失效，不支持直接删除Active/Stable ORCA约束。

[COMPUTED][HIGH] compatibility build现在先稳定收集StableOccupied/ReserveHold blocker，并把AgentId、SteeringState、PositionId、10cm量化位置和半径，与Target/Flow/Candidate revision共同折叠为输入hash。hash不变时复用prepared graph；变化时完整确定性重建。该hash已纳入rollback snapshot，correction replay不会沿用未来graph。

[COMPUTED][HIGH] 已完成的StableOccupied/ReserveHold实体保留原Holding ownership，即使其route因自身或新blocker不再可用于其他实体；未完成状态仍必须通过当前compatibility edge。自动化fixture证明Stable blocker可使原边失效并让waiting agent稳定选择alternate Holding。

[COMPUTED][HIGH] Development、Positioning 3/3、完整SF 28/28通过。正式Static Small两轮状态均为`1/2/1/12/2/2`（Pursuit/Holding/Commit/Stable/Reserve/Reacquire），完成14/20；HoldingAssigned=18、allocation failure=2、selected compatibility valid/invalid=18/0，Commit Stable reject=0。

[COMPUTED][HIGH] 两轮HoldingCandidate/HoldingAssignment/Commit/Steering hash均为`1405881816/2939094645/3829879169/1552643084`，AgentState hash均为`630936952`；server/client match=1。corridor=20、deadlock=0、双端penetration=0、checkpoint p95=0.054cm、interval p95=0.064cm、revision gap=0、agents=visible=20、坏日志模式=0。

[COMPUTED][HIGH] 能力硬门要求20/20，实际14/20，因此本分支只能判定“安全且确定，但能力不足”。未运行DebugGame或录像，也未继续另一生产分支；SF4 Static Positioning仍未验收完成。

## 25. Residual Capacity与State Tail诊断

[COMPUTED][HIGH] `FCrowdDemoResidualPositioningAgent/Edge/Summary`是纯诊断POD。Analyzer把Holding route的Flow/Target/Obstacle/revision与Stable/Reserve blocker集合折叠为Agent→Position可行边，使用稳定augmenting-path最大匹配；输入倒序不改变matching或hash。

[COMPUTED][HIGH] Mass侧只在`ShouldBuildRoundResult()`最终boundary收集量化状态、未占Position、可用Holding和blocker，随后调用纯kernel。诊断summary进入rollback和`FCrowdDemoTrafficMetrics`；客户端checkpoint应用前把ResidualCapacityHash与服务器比较。该链不产生per-agent连续日志或运动写回。

[COMPUTED][HIGH] 原参数Static诊断为：unfinished=6、remaining positions=136、compatible edges=61410、current/no-stable/no-reserve matching=6/6/6；Stable/Reserve rejected raw edges=41250/10986，但没有任何单blocker移除提高matching，Target/Geometry反事实增益也为0。大量被拒edge不等于容量不足。

[COMPUTED][HIGH] 最终State Tail：Pursuit=1的Flow valid、preferred forward=799.846cm/s、ORCA/final约0，约束主要来自11个Stable；Holding=2的distance-not-ready=2，距Holding为1120.771/1172.505cm；Commit=1距Position 92.326cm、ORCA/final route-forward=8.675cm/s、no-progress=4 steps、Obstacle/PBD correction=0；Reacquire=2的InvalidReason均为HoldingInvalid。

[COMPUTED][HIGH] Residual matching证明六个实体均存在Position route，但当前greedy Holding assignment只产生18个assignment。该问题不是分支A的容量封死，也不能凭现有数据等同于B的ORCA错误、C的Commit冲突、D的Commit blocker或E的owner/revision/cache生命周期。

[COMPUTED][HIGH] 因归因不唯一，本轮没有生产修复。Development、Residual 1/1、Positioning 3/3、完整SF 29/29通过；诊断运行双端hash、安全与误差门通过。未运行DebugGame、修复后Static、正式两轮或录像。

## 26. Deterministic Holding Matching门控结果

[COMPUTED][HIGH] 新matcher固定Agent的PositionId，只在合法`(HoldingId, PositionId)`edge上做唯一Holding匹配。使用确定性successive shortest augmenting path和整数Bellman-Ford；最大流先决定cardinality，`int64`成本严格编码owner复用、WaitEpoch/PositionIngress优先和量化route cost。

[COMPUTED][HIGH] StableOccupied、ReserveHold和Commit的合法existing Holding先硬锁；Holding/Pursuit为软owner，Reacquire无owner。Target revision、Position、candidate、membership或兼容性硬失效释放owner；重复硬owner使solver invalid而不产生部分结果。

[COMPUTED][HIGH] 纯测试证明旧贪心失败fixture从1/2提升到2/2，并覆盖三种硬锁、软复用、WaitEpoch、公平决胜、输入倒序、revision失效和membership ghost释放。Development与完整`CrowdDemo.SF`30/30通过。

[COMPUTED][HIGH] 真实matcher旁路只在RoundResult最终boundary运行。第一次错误地在每fixed-step执行导致墙钟55秒仅推进约14秒模拟；该调用已删除，最终诊断恢复正常30秒轮次。

[COMPUTED][HIGH] 正式门控Round为greedy=18、fixed-position Holding matching=18、joint feasible=18、position valid=20、matching hash=4043756401。由于没有得到matching=20，生产`AssignHoldingPositions()`保持旧实现；没有原子owner换位进入Mass状态。

[COMPUTED][HIGH] 旧ResidualPosition matching允许改配Position，因此6/6不再作为完整剩余容量证据。下一层应对两个无Holding Agent构造固定Position adjacency与Hall-deficiency witness；本轮不修改Position assignment、ORCA、Commit、handoff或容差。

## 27. Crowd Transit局部联合速度实验结果

[COMPUTED][HIGH] SF4只为本实验提供现有Position/Holding恢复目标、Steering priority和真实Agent 6失败fixture。Joint solver是通用纯C++实验内核，不是新的SF4业务状态机，也没有写入Position/Holding assignment或Commit Gate。

[COMPUTED][HIGH] SF4 priority事实保持不变：Commit=Committed，Stable/Reserve=Yielding，其余=Normal；既有Priority ORCA仍使用25%/75%或50%/50%。新增canonical几何只是抽取原公式，完整SF自动化35/35证明现有约束重构未回退。

[COMPUTED][HIGH] 8328 Round 1仍为physically satisfied=19/20。Agent 6处于Holding，距目标501cm，preferred=`(79,796)`、Priority ORCA=`(0,78)`、final=`(0,0)`cm/s；完整相关约束闭包为11实体/27 pairs，直接约束4个且均来自Stable状态。

[COMPUTED][HIGH] 逐阶段事实把首次归零定位到ObstacleConstraint，而不是ORCA、PBD、ObstacleReproject或MovementFinalize。诊断hash=`3427263131`双端一致；corridor=20、deadlock=0、双端penetration=0、severe p95=0、checkpoint/interval p95=0.059/0.064cm、agents=visible=20。

[COMPUTED][HIGH] 8331最终复测保持相同hash、component和速度链，并把oversize状态修正为`OversizeFallback(5)`；该次有`revision_gap_total=1`与计划内oversize VIOLATION，故不作为完整安全门通过证据。

[COMPUTED][HIGH] 诊断按设计因`fixture_too_large`输出VIOLATION；该记录表示Joint上限门失败，不是穿墙或双端不一致。没有shadow/production接入，没有用PBD掩盖ORCA/Joint stop，也没有修改SF4容差、assignment、Commit Gate、handoff或Obstacle规则。

[INFERRED][HIGH] 下一步若继续，应先解释ObstacleConstraint为何拒绝Agent 6的78cm/s方向；只有证明该方向或替代方向能通过障碍/flow bounds/Target exclusion安全门，才有理由重新设计>8 component或生产Joint apply。

## 28. Obstacle 109 handoff归因与修复

[COMPUTED][HIGH] 8332证明Agent 6位于Obstacle 109膨胀下边界前：当前段从`(100.482,451.568)`到`(100.482,454.168)`，entry t=`0.165992`且终点inside；完整501cm Holding段的entry/exit t=`0.000866/0.594729`，说明现有1200cm距离门在墙前切换成了直线Arrive。双端fixture hash=`3799203711`。

[COMPUTED][HIGH] 新handoff定义为：Flow Reachable、距离进入1200cm、FlowBounds不裁剪、`Current→Holding` swept segment clear、Holding endpoint clearance valid。Holding阶段若该事实后来失效，下一boundary退回Pursuit；不新增绕墙Holding状态机。

[COMPUTED][HIGH] 8334中Agent 6末态为Commit，位置约`(250.859,1923.447)`，当前与Holding段ObstacleId均为-1且direct clear，双端fixture hash=`4077451622`。target crossing、penetration、deadlock、revision gap均为0。

[COMPUTED][HIGH] 整体能力变为physically satisfied=`16/20`，状态`0/2/1/6/11/0`；Agent 5、8、15仍未完成。修复解决了Agent 6的错误handoff，但没有使Static 20/20成立，且物理满足计数低于修复前19/20，因此本阶段在能力门停止。

## 29. Capacity-by-Construction旁路验证与停止点

[COMPUTED][HIGH] 后续完整物理闭合fixture推翻了上一段的三实体列表：状态顺序为`Pursuit/Holding/Commit/Stable/Reserve/Reacquire=0/2/1/6/11/0`，但Reserve Agent 14已偏离目标144cm，所以物理未满足集合是5、8、14、15，合计4，与`20-16`闭合。

[COMPUTED][HIGH] 本阶段没有改变SF4 Commit Gate、handoff、ORCA priority、Obstacle/PBD或状态机；Capacity selection已正式过滤Position/Holding候选，但Joint velocity仍只运行于默认关闭的Shadow旁路。

[COMPUTED][HIGH] P0 `94/188/128/34cm`把Hard safety、普通实体所需入口、两侧yield后的基线间距和soft gap分开。`97/61`是扩展纯fixture的理论容量；正式Static Small从原始`150/150`候选中选择`71/75`，deficit=`0/0`、hash=`1316163284`，并以这些selected IDs完成Position/Holding assignment=`20/20`。

[COMPUTED][HIGH] 纯fixture的2/5/8/12/20 component均通过稳定排序、量化、HardPairDistance和环境复验测试；这只说明接口和有限fixture可工作，不预测真实20实体RoundSim一定可行。

[COMPUTED][HIGH] 8338真实Shadow证明当前Joint构造不可生产：49 solved、703 infeasible、hard/obstacle/target violation=`1317/1/33`、forward ratio Q15=`0`。client另有revision gap=`15`，未完成双端Shadow hash确认。

[COMPUTED][HIGH] 因Shadow硬门失败，Production Apply没有实现，正式SF4仍使用Baseline Priority ORCA。没有运行DebugGame、正式两轮、录像、100/500、Moving Target、玩家、攻击或死亡。

## 30. Capacity正式接入、time-aligned Shadow与单seed停止门

[COMPUTED][HIGH] 正式候选流水线先生成完整Position/Holding池，再由`EvaluateTransitCapacity()`确定稳定selected IDs；过滤后的两个typed prepared SoA在同一boundary供Position assignment、compatibility和joint assignment消费。selection及其summary/hash参与correction snapshot/rollback。

[COMPUTED][HIGH] component构造已从600cm ORCA pair图传递闭包改为两层：直接层只收集与seed time-aligned轨迹相关且在yield budget内的实体；hard closure只加入下一fixed-step内可能触及HardPairDistance的pair。共享实体仍会稳定合并component。

[COMPUTED][HIGH] seed与yielding实体使用相同0.75秒窗口计算相对轨迹最短距离。`Solved`除速度圆、canonical half-plane、HardPairDistance与量化要求外，还必须满足time-aligned clearance deficit不超过1cm，并使active transit forward总量严格高于baseline。

[COMPUTED][HIGH] 当前算法仍是固定迭代的确定性多实体修正器，不是完整加权联合优化器；`PriorityQ8`尚未承担多seed冲突仲裁，环境约束仍是候选生成后的独立复验，34cm soft gap也没有生产movement消费者。

[COMPUTED][HIGH] 8344共得到1780个component-step，最大component=`13`；solved=`529`、IterationLimit=`720`、ClearanceNotAchieved=`228`、NoForwardGain=`303`、HardInfeasible=`0`。candidate hard violation=`129`、baseline hard violation=`928`；candidate obstacle=`0`、flow=`0`、target=`105`。这些分类不得重新合并为单一“Joint violation”。

[COMPUTED][HIGH] 8344双端总hash一致、revision gap=`0`；正式结果仍为`StableOccupied+ReserveHold=17/20`。该运行暴露的ring-entry 20cm固定残差和3-agent canonical 2cm/s残差已在后续硬约束阶段修复。

[INFERRED][HIGH] 该阶段当时的合法后续是先修正单seed ring-entry；后续结果见第31节。

## 31. Hard clearance投影、canonical polish与8347停止点

[COMPUTED][HIGH] ring-entry的固定20cm残差来自恒定目标回拉与25% clearance软修正的平衡点，不是迭代次数不足。求解器现对完整clearance缺口做相对速度投影，MotionWeight只分配修正责任，不能降低最终净空要求。

[COMPUTED][HIGH] 连续候选和量化搜索都复验time-aligned clearance。128次目标迭代后另执行最多32次hard feasibility polish；polish不回拉Desired、不恢复soft spacing，只处理clearance、HardPairDistance与canonical约束。

[COMPUTED][HIGH] 自动化中ring-entry/ring-exit/external均为`Solved/0cm`；由8346真实输入压缩的3-agent canonical fixture也为Solved且无canonical residual。Development、Transit 2/2和完整SF 35/35通过。

[COMPUTED][HIGH] 8347相对8346的Shadow变化为：solved `552→986`、IterationLimit `670→169`、candidate clearance max `93.911→22.099cm`、solver p95 `0.562→0.729ms`。正式运动保持17/20，双端hash匹配、revision gap=0。

[COMPUTED][HIGH] 新最小失败fixture为Agent 7/8/9/16/17/18/19，Agent 17是唯一seed，21个pair；clearance deficit=`8cm`，hard/canonical/environment residual均为0，状态`ClearanceNotAchieved`，hash=`1810564063`。

[INFERRED][HIGH] 该7-agent fixture必须先由纯可行性oracle判定。存在解时才允许改进小规模联合求解；无解时应转向admission/时序，不得降低实体半径、HardGap、Target exclusion或clearance验收线。Production JointApply继续禁止。
## 2026-07-13 Elastic Crowd Shadow 科学复审

[INFERRED][HIGH] SF4 的长期候选运动链仍为`Flow/Position/Holding Preferred → Elastic Spacing/Transit Influence → Priority ORCA → Obstacle → PBD → Finalize`，但Elastic只存在于默认关闭的Shadow；正式MovementIntent和RoundSim state未消费该结果。

[COMPUTED][HIGH] 8351不是有效A/B证据：Baseline与Elastic没有从每步同一snapshot运行相同链，比较阶段也不同。其forward、hard overlap和recovery数字仅保留为历史调试记录，不再作为能力结论。

[COMPUTED][HIGH] 新Twin-Step每个正式boundary从同一snapshot独立运行Baseline与Elastic的八阶段链；Source基础Preferred保持不变，所有HardPair、Target、Obstacle、ORCA和forward指标只做同阶段比较。初始snapshot已有问题被分类为`InheritedAtStepStart`或`SharedByBoth`，只有`ElasticIntroduced/ElasticWorsened`能够固定首失败。

[COMPUTED][HIGH] 8352首失败是step 29的ORCA stop violation，而不是Obstacle或PBD：Agent 1 Baseline ORCA=`(643,462)cm/s`可行，Elastic把Preferred从`(595,535)`微调到`(593,517)cm/s`后，ORCA变为`(0,0)`且infeasible。fixture closure=`20`、hash=`4215325188`，没有截断。

[COMPUTED][HIGH] 独立180步rollout的Elastic completed=`6/6`、Baseline=`5/6`，但Elastic HardPair=`4`高于Baseline=`2`，Target=`365`高于`358`；两支ORCA stop violation分别=`109/147`。恢复指标改善与安全失败同时存在，不能选择性报告为通过。

[INFERRED][HIGH] 下一次算法工作应首先用该fixture检查ORCA可行性与fallback对Preferred扰动的不连续性；本轮不修改正式ORCA、PBD、CommitGate或handoff，也不运行Production Elastic、DebugGame、正式两轮、录像、Moving Target或100/500。

### Step 29精确重放结论

[COMPUTED][HIGH] 两支约束集合逐项完全相同；Baseline `(643,462)cm/s`是Elastic约束下的合法1cm/s量化witness。Elastic连续LP也成功，旧失败唯一发生在连续解附近3×3量化搜索为空之后。

[COMPUTED][HIGH] 新几何量化恢复只枚举同一half-plane/speed-circle可行域的稳定边界候选，不修改preferred、priority、pair责任、epsilon或fallback顺序。新路径单独计为`FormalLpQuantizedGeometryRecovered`，不冒充旧`NeighborhoodRecovered`。

[COMPUTED][HIGH] hash 4215325188回归覆盖约束反序和三组Preferred微扰，均不再从高速解跳为stop violation；Development、ORCA 10/10和完整SF 41/41通过。

[INFERRED][HIGH] 本轮没有进行修复后的真实Static Small单轮，SF4整体与Elastic Parallel能力状态保持未通过。

### 几何量化恢复后的Static Small结果

[COMPUTED][HIGH] 相同P0复跑确认旧step29 ORCA stop不再是首失败；新首失败为step40 Reproject的Agent 8–18 HardPair，Elastic penetration比Baseline多约`0.666cm`，fixture hash=`410502020`。

[COMPUTED][HIGH] Twin最终Source Q15为Baseline/Elastic=`18108/18131`，Elastic只领先23；最终HardPair累计=`189/191`。Parallel ORCA stop=`126/93`较旧运行下降，但HardPair=`3/3`、Target=`541/431`仍非零，Parallel Source=`17857/13205`且Elastic落后。

[COMPUTED][HIGH] 正式goal=`5`、corridor=`20`、deadlock=`0`；双端penetration、revision gap和坏日志为0，误差p95小于1cm。该结果只通过技术安全门，没有通过Static或Elastic能力门。
### Step40 Reproject联合安全polish与新停止点

[COMPUTED][HIGH] step40的直接根因是PBD把equal-mass修正的一半分给靠墙Agent 18，而ObstacleReproject撤销其约4cm墙内位移；随后没有把未实现修正转移给可移动的Agent 8。该结论由Obstacle 101诊断、PBD迭代pair correction和20实体最终位置共同建立。

[COMPUTED][HIGH] Shadow-only polish仅接受重新通过swept obstacle/FlowBounds与Target exclusion的候选，并以全局HardPair安全严格改善为门。固定量化fixture的2个违规降为0；完整P0中Twin最终Reproject累计从`189/191`降为`7/6`，正式AgentState hash保持`1770349576`。

[COMPUTED][HIGH] 新首witness为step81 Obstacle Agent 1–3：Elastic把Agent 1的Y预测从`-2249`变为`-2248`，端点进入Obstacle 101膨胀边界并触发SlideX；但PBD1对1–3施加约10cm修正，之后至Reproject两支均为0。因此该witness只说明差异首次出现在Obstacle阶段，不证明终态安全失败发生于Obstacle。

[COMPUTED][HIGH] 正式能力仍为goal=5、corridor=20、deadlock=0、physically satisfied=18/20、双端obstacle penetration=0；本轮没有修改正式运动行为。Parallel Elastic仍有3次HardPair、431次Target与93次ORCA stop violation，且Source forward Q15=`13205`低于Baseline `17857`，Production Elastic继续禁止。

[INFERRED][HIGH] 后续诊断必须把“预期由下游PBD消除的中间HardPair”与“PBD3/Reproject终态HardPair”分开，否则最早witness会稳定停在合法的安全链中间状态，无法定位仍剩余的终态6次违规。
