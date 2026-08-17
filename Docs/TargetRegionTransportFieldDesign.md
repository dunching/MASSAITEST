# Target Region Transport Field 设计

[COMPUTED][HIGH] 文档状态：本文件是Target Region Transport算法合同与历史证据，不负责Behavior Source阶段状态。当前生产接入与验收以`CurrentArchitecture.md`、`FeatureChecklist.md`和`TestScenarioMatrix.md`为准。

## 1. 设计职责

[INFERRED][HIGH] 本文件定义 T5 Target-relative Polar Region Transport 的当前权威合同；旧 Polar Density left/current/right guidance 仅为历史实验，不再是生产架构。

[INFERRED][HIGH] 本模块是“围绕目标进行区域人口分布”的可选宏观Guidance Provider，不是普通群体移动、窄口通行或自由游荡的通用必经层。未启用本能力时，实体直接消费其他宏观guidance，并继续使用同一Local Predictive与Particle安全链。

[COMPUTED][HIGH] 纯算法已提取到`MassCrowdCore`的`FCrowdTargetRegionTransportKernel`，Runtime生产WORK已接入Core kernel；旧Demo kernel不再拥有生产权威。Topology、Demand、Plan、validation、quota execution、Guidance和claim replacement结果/hash已有迁移等价fixture。

## 2. 数据分层

[COMPUTED][HIGH] `FCrowdDemoTargetRegionTransportKernel` 提供四个纯函数阶段：`BuildTopology`、`BuildDemand`、`SolveTransport`、`BuildGuidance`。

[COMPUTED][HIGH] PipelineSubsystem 保存 Topology、Demand、FlowPlan、Edge quota、PlanEpoch 与 Agent guidance 的 prepared SoA；不创建 per-cell Mass fragment。

### 2.1 Cell、Region、Anchor与站位语义

[INFERRED][HIGH] Navigation Cell是共享通行区域，Demand Region是人口覆盖统计区域，二者都不是per-agent站位。多个实体可以读取同一Cell方向或在不同时刻通过同一Cell；验收不得用“一Cell一实体”替代真实几何容量。

[COMPUTED][HIGH] 当前`BuildGuidance`对Transport实体使用`NextCellAnchor`点方向；该anchor只应表达下一空间区域的参考方向。若多个source同时指向同一next cell，不能由“都追cell center”隐式决定并发执行。

[INFERRED][HIGH] 正式执行合同应把目标从单点改为目标Cell的可行集合或共享边界：实体进入目标Cell即完成edge handoff，guidance选择当前实体到该集合的稳定最近可行方向，不要求命中统一中心点。

## 3. Polar Topology

[COMPUTED][HIGH] Radial Band 固定 100cm；半径 `<200/<400/<800/其他` 分别使用 8/16/32/64 个扇区。

[COMPUTED][HIGH] Cell 使用稳定 band-prefix key；角区间是半开区间；同环生成 CW/CCW 边，跨环仅在角区间重叠时生成边。

[COMPUTED][HIGH] Cell center 和 edge 受 FlowBounds、`PhysicalRadius + HardSafetyGap = 52cm` 障碍膨胀、Target hard exclusion 与 swept segment 约束。

[COMPUTED][HIGH] 69cm 以内但仍满足硬安全的环境净空只进入软成本，不改变 Particle 的最终硬安全职责。

## 4. Demand

[COMPUTED][HIGH] 验收口径固定为 16 个 Demand Regions；Navigation Cell 通过稳定角区间 overlap link 映射到 Region。

[COMPUTED][HIGH] AvailableCapacity 来自有效距离带内、环境可行的 terminal cell-region links；CurrentPopulation 只统计已经进入有效带且 Cell 可行的实体。

[COMPUTED][HIGH] DesiredPopulation 先公平整除，再按 StableRegionKey 分配余数；容量不足时按稳定顺序继续重分配并保持总人口守恒。

[COMPUTED][HIGH] Polar domain 外实体沿 Shared Flow V2 的 stable next-cell chain 寻找首个安全 source attachment；无 attachment 会使 Demand invalid，不允许静默直线穿障碍。

## 5. Transport 与 Guidance

[COMPUTED][HIGH] 整数 min-cost flow 依次优化最大运输量、物理整数成本、旧 quota 复用和 stable key 决胜。

[COMPUTED][HIGH] 物理成本包含厘米几何距离、69cm 以下软净空缺口、目标 Cell 人口乘 94，以及离开有效距离带的径向厘米。

[COMPUTED][HIGH] 同 Cell 实体按 AgentId 消费按 ToCellKey 排序的出口 quota；未被分配的实体 Stay；不存在永久 Slot、PositionId 或 Region owner。

[COMPUTED][HIGH] Polar domain 外继续使用 Shared Flow；运输实体使用 `TargetVelocity + Normalize(NextCellAnchor - Location) × 300cm/s`；满足 Demand 的 terminal 实体使用 Distance Band settle。

### 5.1 Transport 与通用局部预测交互边界（目标设计尚未实现）

[INFERRED][HIGH] Edge quota只表达宏观运输需求，不等同于同一fixed-step的per-agent通行许可。Transport不应从“相同next cell请求数”直接生成固定Cell容量或独占admission。

[INFERRED][HIGH] 同一next cell的多个实体是否可以并行，应由通用Local Predictive Interaction基于当前位置、速度、Preferred、半径、环境和短期轨迹共同求解；该层不读取Target、Portal、窄口或TestCase语义。

[INFERRED][HIGH] 几何上存在共同可行速度时，全部实体可并行前进；共同前进不可行时，局部层才按可回滚的BlockedAge、量化进展和AgentId选择有限期让行。该决策不是永久Slot、Region owner或Cell入口锁。

[INFERRED][HIGH] Transport继续根据Region人口与Edge quota生成宏观Preferred，并可消费长期吞吐/阻塞汇总决定下一Plan；它不直接持有局部pair约束、grant owner或碰撞责任。

[INFERRED][HIGH] 若requested forward持续为正而Local/Particle applied progress持续接近零，应记录为宏观请求与局部可行性的反馈事实；不得继续重复同一anchor请求，也不得通过Particle调参解决。完整局部设计见`LocalPredictiveInteractionDesign.md`。

### 5.2 Terminal迟滞与稳定终态（设计已对齐，尚未实现）

[COMPUTED][HIGH] 当前`bTerminalStay`由每次Demand中的当期人口重新派生；T2的`TerminalSettledAgentIds`实际等于当前`GuidanceMode::TerminalSettle`集合，不包含连续低速或位置稳定证明。

[INFERRED][HIGH] 后续终态合同应使用可rollback的短期迟滞状态：实体连续满足有效Band、可行Region和低Target-relative速度后进入stable terminal；只有超出较宽退出阈值、Region/路径硬失效或TargetRevision使原终态不可行时退出。

[INFERRED][HIGH] Demand人口再平衡可以稳定选择确需释放的surplus实体，但不得因15-step Plan重建让仍然合法的terminal实体发生无因`TerminalStay↔Supply/Transport`往返。

[INFERRED][HIGH] Particle仍允许SoftError长期非零；稳定终态要求实际修正与SoftError变化收敛，而不是强制所有pair达到SoftDistance。Hard/Swept/Obstacle/Bounds仍是不可放宽的安全门。

## 6. Plan、Hash 与 Rollback

[COMPUTED][HIGH] Plan寿命为15 fixed steps；TargetRevision、FeasibleGraphHash、membership hash、Demand满足和validator失败均可触发同一boundary重建并重新验证一次。

[COMPUTED][HIGH] Topology、Demand、Transport、Guidance与Validation各自生成step hash，并按fixed-step顺序折叠为round hash供双端比较。

[COMPUTED][HIGH] FeasibleGraphHash折叠全部Cell的key、feasible、terminal、PrimaryDemandRegion，以及排序后Edge的from/to、geometry、实际soft-clearance缺口、radial deviation和cross-band；不直接折叠Target世界坐标。

[COMPUTED][HIGH] Plan validator要求Edge严格排序唯一且quota为正，验证Edge存在和可行、Supply outgoing quota、正quota子图到Deficit的可达性及中间Cell流量守恒；有效Plan必须产生Guidance unrouted=0。

[COMPUTED][HIGH] rollback 原子恢复 prepared arrays、quota、PlanEpoch、Topology/Demand/Transport/Guidance/Validation五类hash、重建计数和solver samples，再执行fixed-step replay。

## 7. 已知能力边界

[COMPUTED][HIGH] 8414 Static P0 已通过 20/20 有效带、16/16 可行 Region、unrouted=0 和全部 Particle/同步门。

[COMPUTED][HIGH] 8416 fixture证明step331并非参数不足：实际非terminal的Agent 15附着到terminal Cell 176，旧source→cell边允许sink直接吸收且没有outgoing quota。修复后terminal-anchor Supply必须先消费真实Topology Edge。

[COMPUTED][HIGH] 8417 Static与8418 Moving均通过Plan/Guidance unrouted=0、invalid/validation failure=0、Particle安全、rollback及五类双端hash门；PlanLifetime、TransportSpeed和Region数量未修改。

## 8. 异构 Capability Cohort 合同与实现边界

[COMPUTED][HIGH] T5生产链继续使用同质配置；T6生产链已按CapabilityProfileKey构建多份Topology/Demand/Transport。T6A 8464与T6S 8479已运行，T6M 8492仍为19/20；所有既有Target终态运行均未覆盖新版merge/stability V1全部指标。

[COMPUTED][HIGH] 实体已按量化`CapabilityProfileKey`稳定分cohort；key折叠PhysicalRadius、HardSafetyGap、SoftMargin、Mobility、MinimumCenterDistance、MaximumCenterDistance和Target hard事实。

[INFERRED][HIGH] 同一Capability cohort共享一份Topology、Demand、Transport Plan和Edge quota；不同cohort可因实体硬半径与终端距离带不同而拥有不同可行图，但不得为每个实体独立构建导航图。

[INFERRED][HIGH] 尺寸与质量继续由Particle逐实体消费：异构pair使用双方真实Radius、双方HardGap最大值和Mobility份额；Transport不得额外引入Heavy、Melee或Ranged优先级。

[INFERRED][HIGH] 攻击范围在本层只表示Target terminal band。Melee、MidRange和Ranged实体到达各自终端Region后停止宏观运输；攻击、伤害、朝向和业务状态不属于本设计的当前实现范围。

[INFERRED][HIGH] 后续攻击链只能把“处于有效terminal Region”作为输入前提，不得把AttackPhase、Projectile owner、HitFact、Knockback/KnockUp或HitFlash写入Cell、Region、quota或Transport owner。详细边界见`RangedCombatVatAndHitResponseDesign.md`。

[COMPUTED][HIGH] prepared SoA、Plan validator、RoundResult和rollback已增加Capability membership、per-cohort五类hash、routed/unrouted、coverage和population；Server/Client cohort划分不一致时判定失败。

[COMPUTED][HIGH] 本合同已由T6A/T6S/T6M的20实体真实尝试关卡产生证据；T6M现以8670的30+15秒连续两轮关闭能力门。T2/T5等Target终态场景仍须按`TestScenarioMatrix.md`稳定性V1复验，代码接入和旧口径inside-band仍不是完整能力通过证据。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 2026-07-18 AcquireThenHold资格合同

[COMPUTED][HIGH] 2026-07-19修正：`AcquireThenHold`不是永久世界坐标零速。目标向实体靠近的径向分量被抑制，避免远程实体主动后退；目标切向运动以及目标远离时会扩大距离的分量继续被跟随。该规则只依赖目标相对几何和速度，不依赖T6、AgentId或Region特判。

[COMPUTED][HIGH] 8790证明该修正可把T6M Round末inside/coverage恢复为20/20，但最后90步最低仍为18/17；因此它关闭了“全量零速Hold造成不必要漂移”的实现偏差，没有关闭移动目标持续Region稳定性。

[COMPUTED][HIGH] 用户随后确认：AcquireThenHold的验收目标不是让已接战实体随移动目标持续重排极区，而是首次取得正确Terminal后，在交互资格有效期间保持既有选择；资格失效后才重新进入追逐/Transport。基于该合同，8790的18/17窗口值降为诊断，Round末20/20与安全/同步/性能共同构成T6M技术放行证据。

[COMPUTED][HIGH] 当前资格失效由`ResolveTargetEngagement()`实现为：旧Region成为Supply、DistanceResponsePolicy不再是AcquireThenHold，或距离超过`MaximumCenterDistance + AcquireThenHoldReleaseHysteresis`。Demo尚未覆盖目标Actor销毁或业务Target引用失效；该项属于未来业务事件合同，不得由距离释放自动外推。

[COMPUTED][HIGH] Ranged Profile可使用`AcquireThenHold`，但“进入有效距离带”本身不等于取得保持资格。只有上一boundary已由Demand判为`TerminalStay=true`且`Supply=false`的实体才可取得；因此Transport仍先完成Region人口分配，不会把多个早到实体永久冻结在同一Region。

[COMPUTED][HIGH] 已取得保持的实体输出零世界自主速度，目标向其靠近并进入Minimum以内时不产生径向后退。保持在两类情况下失效：目标距离超过`Maximum+100cm`；或当前Region人口变化后该实体被稳定Demand排序判为超额Supply。后者只触发Region重分流，不修改Particle硬安全或Capability距离参数。

[COMPUTED][HIGH] 8716旧资格诊断为coverage 18/20，Profile `2372223050`的Region 9人口/需求=`3/1`，Agent 4/5已是Supply但仍保持。加入Supply失效规则后，8719达到aggregate coverage=`20/20`、max Region population=`1`，Transport五类hash双端一致，Plan/Guidance unrouted与validation failure均为0。

## 2026-07-18 Quota执行态实施与复测

[COMPUTED][HIGH] `FCrowdDemoTargetRegionFlowPlan::EdgeFlows`现在只表示生成时的不可变短期聚合路线；`FCrowdDemoTargetRegionQuotaExecutionState`独立保存各edge的initial/consumed quota和短期active claim。

[COMPUTED][HIGH] claim在实体仍位于FromCell时保持，不重复消费；进入ToCell后只消费一次，并允许该实体领取同一Plan的下一段。终端完成或Plan替换后不保留永久owner。运行中validation检查执行态与当前Supply的下一段容量，新Plan仍先经过完整flow-conservation结构验证。

[COMPUTED][HIGH] 执行态已经进入prepared SoA、guidance/execution hash与SoftPressure rollback snapshot；新增`0→1→2`多edge fixture证明重复boundary不重复消费、跨cell继续下一段、rollback replay hash一致、终点claim清零。

[COMPUTED][HIGH] 取消ExternalPopulationHash失效但不增加执行态的8644实验退化到inside19/coverage18，已撤回；这证明单纯放宽Plan validation不能替代消费进度模型。

[COMPUTED][HIGH] 接入后的T5 Static 8653保持inside20与coverage16/16，安全/同步错误0；T6 Moving 8652也保持安全与同步门，但coverage仍为19/20。该结果反驳“只要增加剩余quota执行态就能关闭T6M”的充分归因。

[COMPUTED][HIGH] 8652只证明聚合Plan rebuild为2010，未记录异构cohort的原因分解。后续8654确认移动Target的位置每步变化但`TargetRevision=1`保持不变，TargetRevision重建为0；此前把2010次重建归因于TargetRevision的结论已撤销。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 2026-07-18 Plan Lifecycle只读归因

[COMPUTED][HIGH] 新增默认关闭的`-CrowdDemoTargetRegionPlanLifecycleDiagnostic`。开关在Plan激活时锁定；关闭时不保留诊断历史，且不改变正式Plan、Guidance、Particle结果或既有hash。分类和fixture选择位于纯C++ kernel，Coordinator只复制紧凑字段并序列化Server JSON。

[COMPUTED][HIGH] 诊断把FeasibleGraph拆成Cell可行性/Terminal/Region映射、Edge集合和Edge成本三类hash；把Execution invalid拆成state mismatch、claim off edge、quota exceeded、Supply无可用出口和other。SoftPressure rollback保存每个cohort诊断runtime、累计器、fixture pin和hash，correction replay不会重复累计。

[COMPUTED][HIGH] 原P0端口8654得到：inside-band=`20/20`、coverage=`19/20`，最终缺Profile `2729413899`的Region 3；生命周期Server/Client hash均为`3232819302`，样本6307，重建2010。原因计数为Lifetime 229、TargetRevision 0、FeasibleGraph 1559、Membership 0、DemandSatisfied 31、ExecutionInvalid 184、InitialInvalid 7，精确闭合。

[COMPUTED][HIGH] 图变化计数为cost-only 1208、Cell可行性351、Edge集合351；Plan年龄p50/p95/max=`1/15/15`，提前重建1773。active/geometry eligible/new-plan eligible/dropped-still-feasible claim=`1080/1033/851/1033`。Execution invalid子类=`7/41/0/392/3`。

[COMPUTED][HIGH] 唯一fixture为step 323、Profile `2729413899`、最终缺失Region 3、reason=ExecutionInvalid、condition mask=64、Plan age=11、fixture hash=`2766809658`。旧/新图三类hash完全相同，Agent 8的`77→46` claim在几何和新Plan中均可继承，但旧执行态同时报告一个Supply无可用出口。

[INFERRED][HIGH] 该fixture证明至少存在“仍有效claim随重建丢失”和“剩余执行态无出口”同时发生；全轮又存在大量cost-only刷新和351次真实Topology变化。因此没有唯一单分支归因。本阶段不修改生产行为，下一版必须显式决定短Plan的成本刷新、claim迁移和剩余quota重算如何组合，而不是恢复永久Agent owner。

[COMPUTED][HIGH] 证据目录：`Saved/CrowdDemo/CrowdDemo_8654_20260718_114613`；完整fixture为`target_region_plan_lifecycle_fixture.json`。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 2026-07-18 短Plan替换与claim迁移生产合同

[COMPUTED][HIGH] `FeasibleGraphHash`现在只折叠Cell可行性、Terminal/Region映射和Edge集合；几何距离、软净空与径向成本仍进入Topology/Transport hash和下一Plan求解，但不再单独废弃当前15步不可变Plan。

[COMPUTED][HIGH] `ReplacePlanPreservingClaims()`按AgentId稳定处理旧执行态。claim只有在Agent仍处于FromCell、仍为Supply且Topology仍包含该edge时才具备迁移资格；已到ToCell的claim在替换边界计为完成，硬失效claim释放。

[COMPUTED][HIGH] 迁移claim先把一单位Supply从FromCell变换到ToCell，再在当前Demand上求剩余流，随后把固定edge quota加回新Plan。这样既保留正在执行的短段，也维持原始流量守恒；Plan与QuotaExecution在同一boundary一次替换。

[COMPUTED][HIGH] 完整冻结集合若不可行，内核按稳定的AgentId降序逐个释放并重求，直到得到合法剩余流或明确失败；没有永久Agent owner，也没有在Guidance阶段部分替换。

[COMPUTED][HIGH] 纯测试覆盖cost-only保持Plan有效、真实Edge删除使Plan失效、claim迁移、原子validation、输入反序hash一致、跨cell一次消费与rollback replay。Development、DebugGame、完整`CrowdDemo.SF`31/31、生命周期5/5和Transport 6/6通过。

[COMPUTED][HIGH] 原P0端口8658结果：inside-band=`20/20`、coverage=`19/20`、rebuilds=`883`；原因=`238/0/351/0/51/236/7`，Plan age p50/p95/max=`5/15/15`，claim active/geometry/supply/new-plan/migrated/completed/dropped=`520/480/346/346/346/115/0`，execution invalid=`7/36/0/401/5`。相较8654，重建减少1127次，且全部346个仍为Supply的有效claim被迁移。

[COMPUTED][HIGH] 8658最终缺Profile `2217961739`的Region 13。修正“到达ToCell”和“非Supply释放”口径后，不再存在有效claim丢失fixture；当前选择的最早premature fixture为step 1、reason=ExecutionInvalid、condition mask=64、Plan age=1、hash=`4171107798`，记录3个Supply无可用出口。

[INFERRED][HIGH] 新证据否定“只要停止cost-only重建并迁移claim即可得到20/20”的充分归因，也否定“仍有有效claim被清空”是当前剩余原因。下一步只能把最终Region 13缺口与对应Supply无出口或最后路线进度直接关联，不得用最早step 1 fixture作事后因果，也不得恢复永久Slot、Region owner或场景专用通行规则。

[COMPUTED][HIGH] 证据目录：`Saved/CrowdDemo/CrowdDemo_8658_20260718_130308`。

## 2026-07-18 最终缺失Region直接fixture

[COMPUTED][HIGH] fixture选择器现在只在某Region当时`feasible && deficit>0`时保留候选，优先选择该Region最后一次Supply无可用出口，其次选择最后一次确有Supply可沿Plan到达该Region的路线进度；只有不存在直接候选时才退回claim-drop或premature fixture。选择、乱序和rollback已由第6项生命周期自动化覆盖。

[COMPUTED][HIGH] 原P0端口8659连续完成两轮。两轮均为inside-band=`20/20`、coverage=`19/20`，最终缺Profile `2217961739`的Region 13；生命周期hash=`2238624409`、fixture hash=`4114446767`，两轮结果一致。Particle硬安全、Transport validation、双端hash和correction rollback门通过。

[COMPUTED][HIGH] fixture为step 883、selection=`FinalRegionSupplyWithoutOutgoing`、reason=`FeasibleGraphChanged`、condition mask=`68`、Plan age=`1`。Region 13可行且`current=0, desired=1, deficit=1`；Region 12为`current=2, desired=1, surplus=1`。旧、新Plan都可从当前Supply路由到Region 13，Agent 5的`168→225` claim在替换后继续存在，实际有效claim丢失为0。

[COMPUTED][HIGH] 该boundary的旧执行态在当前Demand/Topology上有1个Supply无出口；新Plan随即加入`134→135→167`，并保留`168→225→226→227→228`。因此该计数描述的是旧Plan跨Topology变化时的失配，不等同于新Plan无路或quota solver失败。

[COMPUTED][HIGH] 最后90步的直接覆盖诊断为Demand gap 0、Plan gap 4、Guidance gap 81、retention gap 51、进入40、离开38。现有证据把剩余问题缩到“跨图attachment/Guidance连续性”与“动态terminal保留或路线实际耗时”，但不能在两者间唯一选择。

[COMPUTED][HIGH] 8659提出的两个只读反事实已经在8661完成：一是继承仍落在新图可行端点上的source attachment/短段；二是只在同Region、无surplus且无双占时冻结已满足terminal。两个反事实都没有恢复Region 13，因此没有进入对应生产修改。

[COMPUTED][HIGH] 证据目录：`Saved/CrowdDemo/CrowdDemo_8659_20260718_132242`。

## 2026-07-18 attachment/terminal只读反事实

[COMPUTED][HIGH] Target Stability纯kernel新增观察型反事实。Agent样本显式携带CapabilityProfileKey；Plan edge按cohort稳定BFS判断多段路线是否仍到达最终缺失Region。该诊断不改Plan、QuotaExecution、Guidance、位置或速度，并复用现有Target Stability rollback和双端hash。

[COMPUTED][HIGH] attachment反事实只在图变化后、同一Agent上一step为Transport、当前仍处在原current/next cell且当前Plan仍有路但Guidance中断时计为可恢复。terminal反事实只在实体仍属于同一可行Region、无surplus、未占据其他Region时保留观察membership；跨Region占用被拒绝，不允许制造虚假coverage。

[COMPUTED][HIGH] 8661原P0结果：Region 13最后90步baseline missing 73步，合法多段in-flight也是73步；attachment recovered guidance=0、final changes guidance=0。terminal hold transition/recovered/final held=`0/0/0`，population violation=0，outcome=`Neither`，双端counterfactual hash=`3886489997`。

[COMPUTED][HIGH] 最终Agent 5位于cell 226、next 227，Plan继续为`226→227→228`，还剩2条edge；窗口内剩余edge最小/最大=`2/9`，下降/增加/保持=`7/1/64`，最终目标相对速度=`301.496cm/s`。因此原`guidance_gap_steps=81`主要混入“下一段尚未直接进入terminal cell”的正常多段在途时间，不能单独证明Guidance失效。

[INFERRED][HIGH] 现有证据不支持attachment迁移或terminal冻结生产修复。T6M下一设计对象应是测试时间合同：折叠edge几何长度、当前目标相对进度和完成时间下界，并以预声明宽限判断“正常在途”与“永久缺口”。不得用永久Region owner或直接追目标缩短测试。

## 2026-07-18 T6M Round时间合同与有界移动目标

[COMPUTED][HIGH] Round Plan已显式区分`NominalDurationSeconds`和`CompletionGraceSeconds`。T6M固定为30秒名义窗口加15秒完成宽限；其他SoftPressure场景保持30秒且无宽限。宽限不修改Transport、Guidance、Particle、30Hz、目标名义速度或实体速度。

[COMPUTED][HIGH] 8662在旧单向目标下于step 934产生Bounds violation；8665 fixture确认目标代理中心从`-2488cm`预测到`-2491cm`，而半径100cm加HardGap 10cm要求中心不小于`-2490cm`。Mobility为0时共同Hard closure正确判定无解，因此不能把测试时间延长与无界目标轨迹混用。

[COMPUTED][HIGH] T6M目标现采用通用有界反射直线运动：运动范围由FlowBounds减去目标半径、HardGap和10cm安全余量得到；前30秒轨迹与旧规则一致，触及安全边界后确定性反向。Shared Flow仍在世界坐标按当前Target anchor重建Integration，环境不随目标平移。

[COMPUTED][HIGH] 8670连续两轮均为inside-band=`20/20`、aggregate coverage=`20/20`、max region population=1；每个Capability Profile的验收覆盖为`min(AgentCount, FeasibleRegionCount)`，从而不再错误要求3个实体覆盖10或16个可行Region。Plan/Guidance unrouted、Transport invalid/validation和Particle安全错误均为0。

[COMPUTED][HIGH] 8671两轮Mass AgentState hash均为`1956985324`、dynamic-flow round hash均为`2219477909`，Transport topology/demand/plan/guidance/validation hash均为`3522103338/756980852/987067237/1474826255/2422635450`，生命周期hash均为`2949210181`；Server/Client逐轮一致。日志目录为`Saved/CrowdDemo/CrowdDemo_8671_20260718_143438`。

[INFERRED][HIGH] 该证据关闭T6M 20实体45秒能力门，但不否定旧30秒19/20结果，也不替代T5稳定窗口、人工审片或100/500规模验收。

[COMPUTED][HIGH] 证据目录：`Saved/CrowdDemo/CrowdDemo_8661_20260718_134713`。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 2026-07-17 Feasible-Ordinal Phase 合同

[COMPUTED][HIGH] `DemandRegionPhaseOffset`表示完整16区角域中的归一化相位，不是过滤后仍可直接使用的Region数组下标。当前映射为`floor(NormalizedPhase * FeasibleRegionCount / DemandRegionCount)`，随后从按StableRegionKey排序的可行Region集合循环分配人口。

[COMPUTED][HIGH] 该规则在16区全部可行时与旧行为一致；当Bounds/Obstacle使可行集合稀疏时，不再让不同phase跳过相同不可行前缀后落到同一连续Region集合。

[COMPUTED][HIGH] 8492表明phase修复能够把T6M提高到`19/20`，但不能解决两个source cell同时向同一next-cell发送quota时的执行冲突。当前edge quota仍是聚合流，不是per-agent Region owner或时间化reservation。

[COMPUTED][HIGH] 8496只读稳定窗口已把该假设提升为实测归因：最后90步同next-cell争抢持续存在，2个实体连续90步出现高请求、低实际前进和Particle反向修正；首witness为Agent14从聚合source请求next cell16。Terminal chatter为0，双端诊断hash一致。

[INFERRED][HIGH] 下一版不把EdgeFlow quota改造成Transport内部的Cell admission。应在所有Shared Guidance之后增加通用Local Predictive Interaction；多个不冲突轨迹可并行，共同可行域不足时才使用BlockedAge/进展/AgentId的有限期让行，并报告throughput和starvation。

[INFERRED][HIGH] 后续合同必须保持“无永久Agent owner”边界。简单每cell只放一个实体已由8493证明不足；目标Cell也不应拥有固定硬编码容量，真实并发由局部几何与速度可行域决定。

[INFERRED][HIGH] 新验收还必须证明merge执行和terminal终态在连续窗口内稳定；`inside-band=20/20`、coverage完成或Hard violation为0都不能覆盖持续的anchor争抢、状态chatter或Target-relative位置抖动。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 2026-07-17 动态 Shared Flow Anchor 合同与 T6M 结果

[COMPUTED][HIGH] 动态目标的远场接近不再只依赖`SharedFlowPreferred + TargetVelocity`。Shared Flow V2现缓存世界坐标Topology，并把目标位置解析为稳定free-cell anchor；anchor cell变化时只重建Integration，Obstacle与Bounds不随目标平移。

[COMPUTED][HIGH] 8487两轮均产生25次语义Integration rebuild，Topology hash=`4029698379`、最终anchor cell=`2706`、Integration hash=`338742383`、dynamic round hash=`905043919`，全部双端一致且两轮相同。Transport五类hash也逐轮一致。

[COMPUTED][HIGH] 动态Flow修复后T6M由8481的inside-band=`10/20`提升为8487的`17/20`；这是实测差值，不构成因果充分证明。三实体仍未进入各自有效距离带，可行Region coverage仍为`16`，所以Capability Cohort移动目标合同仍未通过。

[COMPUTED][HIGH] 剩余实体的profile、source attachment、终端Region与settle事实已经完成只读归因；8661进一步证明最终缺口是Agent 5仍沿合法多段路线在途。当前仍不得修改Particle、直接追Target、增加第二guidance owner或场景专用fallback。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 2026-07-17 Capability Cohort 真实验收与移动目标缺口

[COMPUTED][HIGH] T6A 8464 与 T6S 8479 已分别证明异构 Particle 通道安全和静态 Target 的 7-cohort Topology/Demand/Transport/Guidance 合同；其中 T6S aggregate inside-band/coverage=`20/20`。

[COMPUTED][HIGH] `ExternalPopulationHash` 已进入 Plan revision/validation；否则其他同距离带 cohort 的动态占用改变时旧 quota 会继续被复用。8479 是该合同接入后的首个T6S旧口径到达/安全证据，不包含稳定性V1。

[COMPUTED][HIGH] Moving FarFlow 已采用 `SharedFlowPreferredVelocity + TargetVelocity` 并按 MaxSpeed 截断；纯 Transport `5/5` 与完整 SF `54/54` 通过，但 T6M 8481 仍只有 `10/20` 进入对应有效距离带。

[INFERRED][HIGH] Transport guidance 中加入 TargetVelocity 只保证进入 Polar domain 后的速度参考系一致；Polar domain 外的 Shared Flow cell、integration cost 与目标吸引盆仍锚定静态 Flow goal。对一轮平移约2400cm的 Target，单纯速度叠加不能把空间势场同步平移。

[INFERRED][HIGH] 后续设计必须在“平移坐标采样既有 Flow”与“按量化 Target revision 重建/缓存移动 Flow”之间建立独立、可测试的唯一合同；在证据完成前，不得加入直接追目标、第二 owner guidance 或场景专用 fallback。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 2026-07-15 Capability Cohort 扩展

[COMPUTED][HIGH] Target Region Transport 四阶段现可按稳定 `CapabilityProfileKey` cohort 独立构建 Topology、Demand、Plan、Guidance 与 Validation，并在 RoundResult 中逐 cohort 输出五类 hash。

[COMPUTED][HIGH] correction snapshot 已覆盖 cohort membership、prepared arrays、PlanEpoch、quota、round hash、solver samples 与累计计数。

[COMPUTED][HIGH] 该扩展已通过编译与纯自动化，随后T6S/T6M已运行；T6S只有旧口径通过，T6M仍为19/20，不能由接口接入推断三层Target稳定落位已经成立。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
