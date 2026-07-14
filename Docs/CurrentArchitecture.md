# MassAI Crowd Demo 当前架构

## 2026-07-14 当前事实摘要：8368 已修复，Small 在新的共同可行性失败处停止

[COMPUTED][HIGH] 8368 Agent 4/12 已固化为完整20实体自动化。量化后安全闭环现在先探测双方沿分离法线经过Obstacle/Bounds后可实际实现的容量，再按Mobility目标份额做有界分配；一侧容量被截断时将剩余份额转移给另一侧。连续分配被1cm量化吞掉时，在同一24cm cap内执行稳定离散分配搜索。8368最终endpoint/swept=`94.493cm`，Hard/Swept/Obstacle/Bounds=`0/0/0/0`，正反输入hash一致。

[COMPUTED][HIGH] SoftPressure不再复用SF3 rollback。独立snapshot按AgentId保存AgentId、LifecycleSerial、Location、Velocity、Yaw、Radius、SimulatedServerTime、PlanRevision和Initialized，并保存粒子summary/hash/样本长度/settling/首失败fixture以及Flow路线、低速和penetration累计器。MovementFinalize每个boundary记录snapshot，新Round清空history，只保留最近128步；correction前严格检查数量、AgentId、LifecycleSerial和Radius，缺失或不完整会输出VIOLATION并使用权威状态。

[COMPUTED][HIGH] candidate hash已升级为v2完整输入合同，覆盖fixed-step、全部Particle设置、Flow bounds/goal、排序ObstacleSpecs、排序Agent Start/Predict/radius/gap/margin/mobility、pair和量化后环境投影事实。applied hash在MovementFinalize之后计算，覆盖RoundId、PlanRevision、FixedStepIndex、boundary time以及排序后的AgentId、LifecycleSerial、XYZ位置/速度、Yaw、Radius和Initialized。RoundResult前同时比较candidate/applied hash。

[COMPUTED][HIGH] Particle自动化现为17/17，完整`CrowdDemo.SF`为42/42；Development与DebugGame Editor均编译通过。新增或强化的门包括8368完整fixture、HashContract、CorrectionReplay、SettlingTracker、RingEntry/Exit净进展与局部让行、Box100移除后连续15步settling，以及Open移除后30步无隐式回位。

[COMPUTED][HIGH] 最终P0证据目录为`Saved/CrowdDemo/CrowdDemo_8371_20260714_122140`。连续10个correction均为snapshot hit、每次重放1步、miss/mismatch=`0/0`，位置/速度误差p95=`0cm`；server/client candidate/applied/fixture hash分别为`2203959078/1256482314/4227770936`且全部匹配。Flow hash=`267519150`、rebuild=`1`、unreachable=`0`、双端Obstacle penetration=`0`、agents=visible=`20/20`。

[COMPUTED][HIGH] Small仍未通过。fixed-step 155出现两个Hard和两个Swept candidate violation并立即停止；首个稳定fixture为Agent 5/13，最终endpoint/swept=`93.021cm`，相对94cm硬门缺`0.979cm`。双方沿当前分离法线的量化后Obstacle/Bounds可实现容量不足，固定8轮后正确返回invalid；实际提交全体Start安全fallback，因此applied Hard/Swept/Obstacle/Bounds=`0/0/0/0`。路线只进行到wall=`20`、corridor=`8`、goal=`0`，不能作为30秒能力结果。

[COMPUTED][HIGH] 该安全失败触发预声明停止门：SF3/SF4及其地图、CLI、fragment、processor、kernel和自动化均保留；未运行Small连续两轮、两个独立Round 1、录像、人工审片、100/500、WORK调度、自由游荡或动态目标追逐。当前工程仍是过渡态。

## 已对齐的统一粒子目标与当前实现差距

[INFERRED][HIGH] 当前目标不是为“垂直墙、斜墙、单个穿行者、多个穿行者”分别编写行为，而是让所有实体消费同一组`DesiredVelocity、PhysicalRadius、HardSafetyGap、SoftMargin、Mobility`事实。共享Flow只决定宏观期望运动；局部粒子solver通过质量分配、粒子/环境Soft压力和统一Hard安全约束自然产生让路、压缩、扩散及沿墙滑动。

[INFERRED][HIGH] 对静态环境采用以下双阈值：`WallHardDistance = PhysicalRadius + HardSafetyGap`，`WallSoftDistance = WallHardDistance + SoftMargin`。环境Soft只施加可压缩法向压力并保留切向自由度；环境Hard不可违反。墙体Mobility为0，不能被推入或移动，未能由靠墙粒子实现的修正必须返回局部接触网络重新分配。

[COMPUTED][HIGH] 当前`FCrowdDemoParticleConstraintKernel`的Soft循环只遍历Agent pair；`ConstrainParticleMovement()`和Bounds检查把`AgentInflateCm`设为`PhysicalRadius + HardSafetyGap`，没有加入SoftMargin。因此粒子—环境Soft压力尚未实现，距墙52–69cm的当前P0实体不会因墙体获得Soft响应。

[COMPUTED][HIGH] 当前正式spawn把全部实体Mobility设为1；内核已按Mobility比例分配pair修正，但尚未在真实RoundSim证明异构质量。静态墙体也没有作为显式Mobility=0的统一contact进入solver，而是由后置Obstacle/Bounds投影隐式表达。

[COMPUTED][HIGH] 当前Hard闭环仍按稳定pair顺序沿pair normal修正，然后逐Agent调用Obstacle/Bounds reproject。`ConstrainMovement()`只在轴对齐`SlideX/SlideY`候选中选择进展更大者；它不是任意墙面法向/切向约束，也不是多pair与环境的统一二维共同可行求解。8371的step 155失败正发生在这一未完成边界，但不能仅凭该fixture断言加入环境Soft后一定通过。

[INFERRED][HIGH] 后续实现必须保持生产语义简单：特殊靠墙和穿群情况只增加fixture，不增加运行时场景分支。技术实现需要补齐环境Soft contact，并以8371完整fixture验证一个确定性的局部共同约束闭环；若共同可行域为空则明确invalid，不能放宽94cm硬门或恢复旧ORCA/Portal/Holding兼容路径。

## 2026-07-14 8368 历史停点（已被8371复测取代）

[COMPUTED][HIGH] 场景值1已替换为`SimRoundSoftPressure`；该分支的双端固定步流水线为`RoundPlanApply → SharedFlowFieldBuild → FlowPreferredVelocity → MovementPredict → DeterministicParticleConstraintSolve → MovementFinalize → AuthorityCommit / ClientPredictionCommit`。旧`UCrowdDemoRoundSeparationProcessor`、Hard PBD和ORCA不在该分支执行。

[COMPUTED][HIGH] 新纯C++ `CrowdDemoParticleConstraintKernel`统一消费`PhysicalRadius、HardSafetyGap、SoftMargin、Mobility`。Soft不再把128cm当作必须在本步完全恢复的硬壳，而是按`1-exp(-SoftResponsePerSecond*FixedStepSeconds)`得到显式fixed-step响应；P0的`8/s、30Hz`对应整步约`23.4%`响应，再按8轮和Mobility份额稳定分配。SoftError允许长期非零且不影响`bValid`。

[COMPUTED][HIGH] 主求解按稳定swept-AABB grid与`(MinAgentId,MaxAgentId)`pair顺序执行8轮`Soft endpoint → Hard endpoint → Swept hard → Obstacle/Bounds reproject`。随后量化位置并执行固定8轮只含`Hard → Swept → Quantize → Obstacle/Bounds`的安全闭环；最终只有Hard、Swept、Obstacle和Bounds参与有效性复验，安全闭环不再施加Soft。

[COMPUTED][HIGH] Particle fragments、prepared summary、solver p95和RoundResult已接入。`particle_candidate_hash`描述本boundary被求解的候选，`particle_applied_state_hash`描述MovementFinalize实际提交的完整状态；invalid/fallback分别计数。correction rollback覆盖prepared粒子结果、累计器、solver样本、settling状态和首失败fixture，客户端仍只显示client sim state。

[COMPUTED][HIGH] invalid候选允许全体回到Start作为紧急安全输出，但会立即固定首失败fixture、输出`VIOLATION`、提前构建RoundResult并在结果传输完成后停止该轮；它不再继续运行30秒并伪装成正常能力结果。首失败fixture按`Start/Predict/Soft/Hard/Swept/Obstacle/Quantized/FinalSafety/Applied`保存第一稳定失败pair。

[COMPUTED][HIGH] 14项`CrowdDemo.SoftPressure.Particle`自动化及完整`CrowdDemo.SF` 42/42通过，Development Editor构建通过。测试包括`Box100InsertRemove`、`OpenInsertRemove`、`RingEntryRollout`、`RingExitRollout`、`ObstaclePairConflict`、`PostQuantizationSafety`、`InvalidCandidateDoesNotMasqueradeAsApplied`和95cm Stress Gate。正式基础阵型已改为128cm；95cm只保留为高压fixture，不再是第一能力门。

[COMPUTED][HIGH] 端口8368的Small 20在fixed-step 93出现首个安全失败并立即结束：candidate Hard/Swept/Obstacle/Bounds=`1/1/0/0`；实际应用全体Start安全输出后为`0/0/0/0`，`particle_invalid_step_count=1`、`particle_global_fallback_step_count=1`。candidate/applied/fixture hash分别为`3480157140/1765689391/3809298193`，server/client一致。

[COMPUTED][HIGH] 首失败pair为Agent 4/12，要求HardDistance=`94cm`。Obstacle前pair距离约`95.26cm`；Obstacle reproject把Agent 4的X从`1649`裁到`1647cm`后，最终距离为`93.723cm`，留下约`0.277cm` Hard与Swept缺口。固定安全闭环重复得到同一状态，因此当前第一剩余问题是pair安全与Obstacle reproject的共同可行性/量化闭环，不是Soft响应不足。

[COMPUTED][HIGH] 8368的RoundResult header/chunks/assembly/queue/boundary apply均完成一次；checkpoint p95=`0cm`、revision gap=`0`、双端Obstacle penetration=`0`、agents=visible instances=`20/20`、solver p95=`0.451ms`。correction-interval p95=`30cm`仍未达到`<1cm`门；goal/corridor因step 93主动失败而为`0/0`，不得作为完整30秒路线容量结果。

[COMPUTED][HIGH] 按预声明停止门，SF3/SF4、Elastic、Shadow、Joint、旧PBD/ORCA及其地图、CLI、rollback和自动化尚未删除；Medium/Cohort、DebugGame、正式两轮与录像均未运行。当前工程是过渡态，不得写成“最终只剩SF1与SoftPressure”。

## 2026-07-13 SF4 Generic Priority ORCA 与 Yieldable Commit Blocker

[COMPUTED][HIGH] `FCrowdDemoOrcaAgent` 现在携带只存在于 prepared 输入中的 `LocalAvoidancePriority`；优先级键按 `PortalPriority → LocalPriority` 字典序比较。SF4 将 `Commit` 映射为 `Committed`，将 `StableOccupied/ReserveHold` 映射为 `Yielding`，其余 Steering 状态映射为 `Normal`；SF3 固定使用 `Normal`，没有新增 Mass fragment 或业务状态。

[COMPUTED][HIGH] `BuildPairConstraint` 不再消费历史 `Sf4RouteMode Active/Yielding` 的 0%/100% route policy。相同优先级双方各承担 50%，高优先级侧承担 25%，低优先级侧承担 75%；双方 half-plane 都保留，碰撞尺寸、time horizon、速度圆、LP、量化、fallback 和 PBD 参数均未改变。

[COMPUTED][HIGH] CommitGate 已把冲突拆成三类：Target exclusion、Flow、Obstacle、Position/Revision/Compatibility 继续是 hard safety；Active Commit 与同 boundary 已选 Commit 继续串行化；Stable/Reserve 路径相交只写入 `YieldableConflictMask`，不再单独造成 Held。Joint residual capacity 仍可在其后撤销 Grant。

[COMPUTED][HIGH] Priority round hash 按 fixed-step 折叠 AgentId、双方 priority key、稳定 constraint、量化责任、point/normal 和最终 ORCA velocity，并进入 rollback 与 SF4 checkpoint 双端比较。RoundResult 同时记录 equal/asymmetric pair、25%/75% 两侧计数、责任和违规、yieldable/hard gate 计数及 Stable/Reserve 实际物理位移；“完成”不再只依赖 SteeringState。

[COMPUTED][HIGH] Development、Priority 定向自动化、`CrowdDemo.SF4.Positioning` 3/3 和完整 `CrowdDemo.SF` 33/33 已通过。正式单轮证据目录为 `Saved/CrowdDemo/CrowdDemo_8324_20260713_134501`；server/client traffic、portal、ORCA、Priority ORCA、AgentState 及 SF4 各级 hash 全部匹配，其中 Priority ORCA hash=`617370832`。

[COMPUTED][HIGH] 单轮真实触发 asymmetric pair=`19247`，high-side 25%=`19247`、low-side 75%=`19247`、责任和违规=`0`。该轮没有 ready Commit 与 Stable/Reserve 的实际路径相交，因此 live yieldable stable/reserve=`0/0`；Stable-only/Reserve-only 的 Grant 与 yieldable mask 语义由纯 fixture 验证，不能虚报为真实场景已触发。

[COMPUTED][HIGH] 单轮末态为 `Pursuit/Holding/Commit/StableOccupied/ReserveHold/Reacquire=0/1/0/9/10/0`，状态完成数与物理满足数均为 `19/20`；Stable/Reserve displacement max=`29.946/29.783cm`，没有以保留状态但推离位置制造假完成。唯一未完成 Agent 6 距 Holding=`501cm`，preferred=`(79,796)cm/s`、ORCA=`(0,78)cm/s`、final=`(0,0)cm/s`，所以第一可见速度损失发生在 ORCA 之后，而不是 preferred 生成或 CommitGate。

[COMPUTED][HIGH] 单轮同步与几何安全事实为 corridor=`20`、deadlock=`0`、Target crossing=`0`、双端 obstacle penetration=`0`、severe overlap p95=`0`、client agents=visible instances=`20/20`、revision gap=`0`、checkpoint/interval p95=`0.059/0.064cm`。但是 ORCA `stop_violates=427`，违反本任务预声明的 ORCA safety hard gate；因此本轮不得标记为完整安全通过。

[INFERRED][HIGH] 当前准确停点是：Generic Priority 机制与双端确定性成立，但 SF4 Static 能力仍为 19/20，且既有/剩余 ORCA infeasible-stop 约束违规没有被本轮消除。按边界不继续修 Agent 6 的 handoff、Obstacle/PBD/finalize 链路，也不修改 ORCA 参数或 Commit 容差。

## 2026-07-13 SF4 八实体最终边界只读归因

[COMPUTED][HIGH] 新增最终boundary只读fixture，只消费既有MoveIntent、prepared ORCA results、final RoundSim state、CommitGate decisions和Steering progress；不重求LP、不写回状态。fixture按AgentId排序，距离量化1cm、速度量化1cm/s，ORCA约束来源按Pursuit/Holding/Commit/Stable/Reserve/Reacquire六状态计数，Commit拒绝原因使用可组合位掩码。rollback覆盖fixture，RoundResult复制agent count/hash并在checkpoint前执行双端比较。

[COMPUTED][HIGH] Development、`CrowdDemo.SF4.Positioning`3/3和完整`CrowdDemo.SF`33/33通过。正式证据为`Saved/CrowdDemo/CrowdDemo_8322_20260713_124520`；server/client fixture均为8个Agent、hash=`1989554829`，完整RoundResult hash match=1。

[COMPUTED][HIGH] Agent 1/4处于Holding，距Holding=`25/4cm`、preferred/ORCA/final均为0，Commit拒绝为ActiveCommitConflict；Agent 2/11距Holding=`25/26cm`、速度均为0，拒绝为ReserveBlocker。四者已到达Holding容差附近，当前直接阻塞来自Commit admission事实。

[COMPUTED][HIGH] Agent 3/14处于Holding，距离=`58/163cm`，preferred分别为`(197,-121)/(265,-595)cm/s`，ORCA与final均量化为`(-1,-1)cm/s`，Commit拒绝为HoldingDistance。Agent 13处于Commit，距Position=`1038cm`，preferred=`(-83,796)cm/s`，ORCA/final=`(-1,-1)cm/s`。三者的主要即时速度损失发生在ORCA阶段。

[COMPUTED][HIGH] Agent 10处于Pursuit，距assigned Holding=`1226cm`，preferred/ORCA/final均为0，无Commit拒绝；no-progress=901 steps。该实体没有进入CommitGate，问题位于Pursuit到Holding的handoff/guidance边界。

[INFERRED][HIGH] 下一分支顺序应为：先只读复核4个CommitGate blocker的真实route/Reserve安全必要性；若可安全并发，再修Admission。随后单独处理3个ORCA压速实体，最后处理Agent 10的handoff dead zone。当前证据不支持一次同时修改三条路径。

## 2026-07-13 SF4 联合 Position/Holding 生产接入与能力停点

[COMPUTED][HIGH] `PlanJointHoldingPositions` 已接入正式 Holding Assignment processor：每个需要重算的 fixed-step boundary 先在局部数组中完成全图求解，再同时替换 prepared Position/Holding assignments，最后由同一 processor pass 写入 Position fragment。求解无效时保留上一份完整结果，不发布部分 assignment。StableOccupied、ReserveHold、Commit 继续硬锁 Position+Holding；合法软 owner 的完整组合复用是固定最大基数后的第一优化目标。

[COMPUTED][HIGH] correction rollback snapshot 已覆盖 PositionAssignments、HoldingAssignments、JointPositioningResult、JointCommitResidualResult 与 JointAssignmentInputHash；回放测试验证旧 owner、新 owner、联合结果和 hash 一起恢复。

[COMPUTED][HIGH] Commit Gate 已接入 sequential future-Stable residual protection：候选按既有稳定 Grant 顺序逐个试放；每个已接受候选会锁定 Position/Holding，并作为未来 Stable blocker 影响后续候选。只有剩余联合最大基数等于剩余 Agent 数时保留 Grant，否则改为 Held。该门只计算最大基数，不改变 ORCA、Commit route 或 Steering 容差。

[COMPUTED][HIGH] Development、`CrowdDemo.SF4.HoldingHall.JointPositioning` 1/1、`CrowdDemo.SF4.Positioning` 3/3 和完整 `CrowdDemo.SF` 33/33 通过。正式证据目录为 `Saved/CrowdDemo/CrowdDemo_8317_20260713_120042`。

[COMPUTED][HIGH] Round 1 联合 assignment 达到20/20：`valid=1, maximum=20, hard_locked=13, reused=20, unmatched=0`，Holding/Position重复均为0；Commit residual为7/7可行；双端联合 hash=`3114730886`、residual hash=`922829819`，客户端总 hash `match=1`。

[COMPUTED][HIGH] 安全与同步门通过：只读ingress诊断目录`Saved/CrowdDemo/CrowdDemo_8319_20260713_120430`连续两轮均为Target exclusion crossing=0；corridor=20、deadlock=0、server/client obstacle penetration=0、revision gap=0、checkpoint/interval p95最大=`0.047/0.066cm`、agents=visible instances=`20/20`、ghost owner=0，未发现 Fatal、Assertion、Ensure、`LogWindows: Error` 或 VIOLATION。该运行用于补齐安全事实，不因执行了两轮就升级为能力正式两轮验收。

[COMPUTED][HIGH] SF4 能力门仍失败：末态 `Pursuit/Holding/Commit/StableOccupied/ReserveHold/Reacquire=1/6/1/3/9/0`，所以 `StableOccupied+ReserveHold=9/20`。Holding assignment 已从历史18提升到20且 Reacquire=0，但1个Pursuit、6个Holding、1个Commit和3个尚未稳定的Commit证明下一瓶颈是运动/状态收敛，不再是静态联合容量。

[INFERRED][HIGH] 按停止门，本轮不修改 ORCA、Commit guidance、handoff 或容差，也不运行 DebugGame、正式两轮和录像。下一任务必须先用现有末态诊断区分 Holding远距离、Commit低速和Pursuit被ORCA压低的贡献。

## 2026-07-13 SF4 Hall几何验证与联合规划纯fixture停点

[COMPUTED][HIGH] Stage A在Round 1最终boundary对最小Hall witness执行量化segment-circle复核。Agent 5固定Position 2834；150个Holding中最佳为Holding 2150，最严重blocker为Agent 6，RequiredClearance=`42+42+10=94cm`，实际最近距离=`58.078cm`，margin=`-35.922cm`。最近点`t=1`位于Position端点，Agent 6占据相邻Position 2835；该相交是真实圆盘安全距离不足，不是endpoint特殊误判。

[COMPUTED][HIGH] 150个Holding的worst-margin均小于0；self/witness-position/duplicate/stale/radius-semantics/formal-mismatch=`0/0/0/0/0/0`。Target-only=0、Stable-only=104、multi-label=46。Fixture hash=`2240360253`，server/client一致。

[COMPUTED][HIGH] OwnerRelease与PhysicalBlockerRemoval已拆分：仅释放Stable/Reserve/Commit owner均保持18；仅诊断移除Stable物理blocker得到20，移除Reserve得到18。完整Hall deficiency为2，最小witness deficiency为1。旧18→15来自把grandfathered Stable Agent移入当前compatibility图，已停止使用该混合语义。

[INFERRED][HIGH] Stage A唯一选择分支B：Position 2834确实被已站位实体封闭，不修改真实安全半径、Stable约束或compatibility几何。

[COMPUTED][HIGH] 分支B新增只读Joint Planner：`Agent→Holding→Position`使用整数、确定性、词典序最短增广路；Stable/Reserve/Commit同时硬锁Holding和Position。真实fixture得到maximum=20、hard locked=15、reused combinations=18、Holding/Position重复=0、hash=`1940001228`；Agent 5改配为`Holding 2843→Position 2886`。

[COMPUTED][HIGH] 纯Commit residual protection把候选Position临时视为未来Stable blocker后重算剩余联合容量。真实5个软候选均满足`ResidualMatchingAfterGrant=RemainingAgentCountAfterGrant=4`，hash=`2485237134`。该结果未接入CommitGate或生产assignment；正式Static状态仍为14/20。

## 2026-07-13 SF4 Holding Hall-deficiency 最终边界诊断

[COMPUTED][HIGH] `AnalyzeHoldingHallDeficiency`只在`ShouldBuildRoundResult()`最终boundary旁路运行。它在固定PositionId、当前硬owner和当前compatibility事实下精确枚举最多20个Agent的Hall子集；输出最小基数、随后AgentId字典序最小的缺口集合、完整Holding邻接集、逐边拒绝原因及稳定hash。生产`AssignHoldingPositions()`、ORCA、Commit和Steering状态均未修改。

[COMPUTED][HIGH] 原参数Static Small Round 1的最小证书为`AgentIds={5}`、固定`PositionId=2834`、可用Holding集合为空、deficiency=1、fixture hash=`4262261310`。全图current matching=18；仅诊断释放Stable/Reserve/Commit硬owner分别得到`15/18/18`，没有任何一类恢复20。

[COMPUTED][HIGH] 该Agent对150个Holding候选均存在compatibility record，Flow/Obstacle/revision拒绝均为0；37条边同时被Target门拒绝，150条边存在Stable blocker，13条存在Reserve blocker。拒绝计数允许同一edge归入多个原因，不能相加当作独立edge数量。

[INFERRED][HIGH] 证据排除“单纯放宽某类硬owner即可恢复20”的分支，也排除“物理Holding候选总数不足”。当前第一归因是固定Position 2834的Holding compatibility邻接为空；后续应单独判断是修正路径/Target/blocker过滤，还是把Position与Holding改为联合分配。本任务按约束停在诊断层，不实施任一生产修复。

## SF4 Steering-first Holding/Commit纯内核门（2026-07-12）

[COMPUTED][HIGH] `CrowdDemoPursuitPositioningKernel`已新增行为中立、未接入Mass的`Pursuit/Holding/Commit/StableOccupied/ReserveHold/Reacquire`数据合同，以及Holding candidate、Holding assignment、Holding→Position compatibility、Commit Gate和Steering guidance纯函数。Holding/Commit route与动态申请只存在于调用者提供的纯数组，不新增动态数组Mass fragment，也不改变`FCrowdDemoPositionAssignmentFragment`语义。

[COMPUTED][HIGH] Holding candidate从当前Target id/revision、SF1真实Flow raster、blocked/unreachable、按Agent半径膨胀的clearance及现有Position candidates生成。外围annulus从`AllowedDistanceMax+1 cell`开始，使用4个100cm radial bands；稳定顺序为radial band、angular sector、stable cell key、量化location，HoldingId折叠Target、revision、cell和1cm位置。候选与Position及彼此保持`2*AgentRadius+HoldingGap`间距。

[COMPUTED][HIGH] Compatibility对Holding→Position直线按`max(25cm,0.5*FlowCellSize)`采样，要求每点bounds/free/reachable/clearance/Target exclusion成立，并用精确segment-circle检查StableOccupied/ReserveHold blocker；输出量化route cost与稳定hash。Small真实Flow/Obstacle fixture为20个Position全部找到至少一条合法边，并完成20/20唯一Holding assignment。

[COMPUTED][HIGH] Assignment先复用Target revision、Position和compatibility仍合法的既有Holding owner，再按`WaitEpoch降序→稳定Position fill cost→PositionId→AgentId`处理实体，按route cost/HoldingId选择空闲Holding。Position fill cost由Position相对Target在量化EntryAxis上的投影得到，远侧值小于入口侧。membership输入移除后不保留ghost owner，Position失效输出Reacquire。

[COMPUTED][HIGH] Commit Gate只把`bAlreadyCommit`短segment和本boundary已选segment视为资源；未到Holding或速度未达readiness的Waiting不占未来路径。ready request按`WaitEpoch→fill cost→commit cost→PositionId→AgentId`稳定扫描，并复验flow/clearance/Target exclusion/Stable blockers及segment pair冲突。owner移除后相同短segment可重新grant。

[COMPUTED][HIGH] guidance纯函数中Pursuit/Reacquire消费Flow preferred；Holding只Seek/Arrive Holding并在30cm tolerance内停速；Commit只Seek/Arrive Assigned Position；StableOccupied/ReserveHold使用低增益保持。所有输出受MaxSpeed限制并按1cm/s量化。

[COMPUTED][HIGH] Development、`git diff --check`、全部3项`CrowdDemo.SF4.Positioning`和独立`SteeringFirstHoldingCommit`测试通过。当前生产processor仍执行旧Polar Approach/Phase Reservation/Wait Graph/Route-Aware ORCA，正式Static结果仍为1/20；本节不能表述为SF4能力门通过。

## 架构收敛决定：随机地形使用Flow＋Steering，复杂SF4路径预约停止扩展（2026-07-12）

[COMPUTED][HIGH] 当前代码仍实际执行`Polar Approach → Front Admission → Phase Reservation → Boundary Apply → Route-Aware ORCA`，下方各节继续记录其实现、验证和失败证据；本节是后续架构决定，不表示代码已经迁移。

[COMPUTED][HIGH] 现有实现已经证明Candidate/Assignment、Shared Flow、Deterministic ORCA、Obstacle Constraint、Hard PBD、prepared SoA、fixed-step rollback和双端hash可以保留。它也证明两阶段Phase Reservation能够保证Target安全，但Static能力仅`1/20`，大量current polyline长期Held，最终边界没有合法最小fixture授权继续修改Active ORCA。

[INFERRED][HIGH] 后续停止扩展以下生产方向：`RadialStage/AngularAlign/RadialCommit`路径所有权、current/requested polyline锁、Wait-For Graph驱动的生产调度，以及更多Route-Aware ORCA责任特例。现有代码在迁移前保留为可复现失败基线，不得删掉安全门或把失败实验改写成通过能力。

[INFERRED][HIGH] 目标架构改为：

```text
Shared Navigation / Flow
→ Flow Follow + Seek/Arrive + Separation/Obstacle Steering
→ Candidate / Assignment
→ Holding Position 或 Commit Permit
→ Deterministic ORCA
→ Obstacle Constraint / Hard PBD / Reproject
→ MovementFinalize
```

[INFERRED][HIGH] Waiting必须拥有明确Holding Position并由Steering到达后保持；未获Commit许可时不得继续朝共享goal中心推进，也不得在任意当前位置零速冻结。Commit只授予少量稳定排序且简单路线互不冲突的实体；第一版不把路线拆成多个可抢占资源。

[INFERRED][HIGH] Portal调整为证据门控的可选增强：普通随机地形、障碍墙和同向追逐使用Flow＋Steering/ORCA；只有自动导航事实识别出硬瓶颈且正式运行出现对向容量、公平性或饥饿问题时，才启用Portal方向/token调度。该决定不要求人工标记地图通路，也不删除当前SF3实验代码。

[INFERRED][HIGH] UE官方参考边界为：MassNavMesh使用NavPath/Corridor/ShortPath推进MoveTarget，MassZoneGraph沿lane推进MoveTarget，MassCrowd在明确crossing使用Waiting Slot与Opened/Closed lane。Demo只借鉴MoveTarget、Waiting Slot和Acquire/Release概念；不直接把逐实体同步FindPath、StateTree、World DeltaTime或ZoneGraph lane作为当前双端30Hz正式实现。

[INFERRED][HIGH] 下一次代码工作必须先冻结当前失败基线，再在SF4隔离分支实施最小`Pursuit → Holding → Commit → StableOccupied/ReserveHold → Reacquire`状态；不得同时修改SF1/SF2/SF3、ORCA/PBD参数、网络预算、地图或实现死亡/攻击。

## SF4 Reservation-Aware ORCA最终边界归因停止点（2026-07-12）

[COMPUTED][HIGH] 已新增默认关闭的Reservation-Aware ORCA纯诊断：稳定约束排序、30cm/s route-forward half-plane、确定性不可再缩减核心、ActiveRouteConflict/ActiveRouteDisjointContained/ActiveRouteDisjointOutsideCorridor分类、单fixed-step containment与pair swept safety、2–5实体fixture和稳定hash。算法位于纯ORCA/positioning kernel，processor只在RoundResult最终边界采集一次，Coordinator只负责紧凑日志和Saved JSON序列化。

[COMPUTED][HIGH] Development、两项`CrowdDemo.SF4.Positioning`和`git diff --check`通过。纯fixture覆盖冗余约束删除、输入反序hash、disjoint-contained、outside-corridor、真实route冲突、单步containment和pair separation。

[COMPUTED][HIGH] 唯一正式诊断目录为`Saved/CrowdDemo/CrowdDemo_8391_20260712_184754`。Round 1最终边界得到`valid=0, too_large=0, primary=-1, agents=0, core=0`：没有stalled Active候选同时满足“完整约束下30cm/s量化不可行、核心可缩为2–5实体、分类唯一”的fixture条件，因此未生成JSON，并按预声明输出VIOLATION。

[INFERRED][HIGH] 该结果否定了在本轮直接从历史聚合归因进入任一生产分支：历史样本只能说明Active来源经常主导，不能证明RoundResult最终状态属于安全disjoint省略、真实route冲突或corridor偏离。分支A/B/C均未选择，生产ORCA、PBD、Target exclusion和pipeline顺序均未修改。

[COMPUTED][HIGH] 诊断运行本身保持原行为指标`StableOccupied/ReserveHold=1/0`、双端penetration=0、checkpoint/interval p95=`0.058/0.064cm`、revision gap=0、agents=visible=20；但fixture无效VIOLATION使本次安全硬门失败，禁止把该运行列为通过证据。

[COMPUTED][HIGH] 未运行分支后Development、DebugGame、完整SF、正式两轮或录像；未进入Moving Target、攻击、死亡、100/500或其他分支。

## SF4 Reservation Wait-For Graph诊断与停止点（2026-07-12）

[COMPUTED][HIGH] Phase scheduler现会在纯C++内核中保留精确的`requester -> blocker`关系；Pipeline在DeterministicORCA之后，用同一步已量化的route-forward速度和no-progress状态构造wait-for graph。节点、边、SCC、fixture及hash均按AgentId和稳定pair顺序生成，correction rollback会恢复对应prepared SoA与累计器。

[COMPUTED][HIGH] wait graph诊断固定定义为：`NoProgressSteps >= 30`或route-forward速度`<=10cm/s`为stalled blocker；持有合法active membership但不满足该条件为progressing blocker；blocker没有active membership为stale owner。SCC大小大于1或自环为cycle；只有cycle内全部requested paths两两无冲突、request有效且Target exclusion清空时，才计为atomic-safe handoff set。

[COMPUTED][HIGH] 完整单轮证据为`Saved/CrowdDemo/CrowdDemo_8391_20260712_182256`：unique requests/blockers/edges=`9/12/45`，reciprocal=`7`，cycles=`3`，max cycle=`9`，stalled/progressing/stale=`11/6/0`，blocker phase radial/angular/radial-commit=`11/2/1`，atomic-safe cycles/max set=`0/0`。server/client wait graph hash=`3546673066`且match，phase hash=`2741940762`且match。

[COMPUTED][HIGH] 分支判定为C：A被`atomic-safe cycle=0`排除；B被`stale owner=0`排除；D被存在3个cycle且11个stalled blocker排除。纯ORCA half-plane fixture已实现并通过测试，可固定最小route-forward约束、验证连续/量化可行性，并按Active/Waiting/Stable/Other移除来源进行归因；它没有进入生产fixed-step路径。

[COMPUTED][HIGH] 曾进行的生产态C采样显示Active约束占主导：重采样证据中blocked=`1725`，移除Active/Waiting/Stable/Other后恢复=`1522/146/1/0`；1Hz证据为`64`个blocked中`56/3/0/0`；单次证据为`5`个blocked中`4/1/0/0`。这些采样导致correction interval结果缺失或revision gap，违反安全门，故运行时采样、传输字段和hash比较已全部撤销，只保留纯fixture与自动化。

[COMPUTED][HIGH] 撤销后最终单轮保持安全：Target crossing=0，server/client obstacle penetration=0，Traffic/Portal/ORCA/AgentState/Phase/WaitGraph hash全部一致，checkpoint/interval p95=`0.058/0.064cm`，revision gap=0，agents=visible=`20/20`，corridor=20，deadlock=0，无Fatal/Assertion/Ensure/LogWindows Error/VIOLATION。

[COMPUTED][HIGH] 能力门仍失败：StableOccupied/ReserveHold=`1/0`，arrival p95=`1165.730cm`，完成状态仅`1/20`。因此没有运行DebugGame、完整`CrowdDemo.SF`、正式两轮或录像，也没有修改ORCA/PBD、Target exclusion、地图、round时长或网络参数。

[INFERRED][HIGH] 下一次生产设计若处理Active约束，必须先在纯fixture证明“释放或迁移active reservation”仍保持Target exclusion和route safety；当前证据不授权直接忽略Active half-plane或放宽安全间距。

[INFERRED][HIGH] 未来membership lifecycle边界固定为：`Active membership boundary -> 释放Portal token -> 释放Phase reservation -> assignment失效 -> 保留survivor assignment -> 确定性promotion -> 清除ORCA/PBD/blocker ghost -> 双端membership/assignment hash比较`。本轮没有实现死亡、移除或成员变更系统。

## SF4 两阶段Phase Reservation生产接入与能力停止点（2026-07-12）

[COMPUTED][HIGH] SF4正式fixed-step顺序已接入`PositionApproachRouteRequestBuild → FrontAdmissionEligibility → FrontPhaseReservationSchedule → FrontPhaseReservationBoundaryApply`。request-build不再写正式phase；首次`None→RadialStage`及后续`RadialStage→AngularAlign→RadialCommit`均只有在scheduler返回Granted后由boundary apply原子提交。Held保持current phase/current reservation，Invalid仅按明确reason保持安全状态或执行AdmissionRequeue，不存在direct-to-slot fallback。

[COMPUTED][HIGH] PipelineSubsystem持有prepared requests/result/decision records/admission result，动态route points不进入Mass fragment。`FCrowdDemoPositionAssignmentFragment`只保存requested phase、decision、revision、held steps与invalid reason；现有`FrontApproachPhase`仍是唯一已提交phase。Guidance按committed phase生成速度，ORCA Active route只读取current reservation。

[COMPUTED][HIGH] rollback snapshot覆盖prepared request/result/decisions、admission result、完整PositionAssignment fragment、phase指标与held-step采样长度。phase decision round hash随RoundResult复制，客户端在checkpoint应用前与Traffic/Portal/ORCA/AgentState hash一并比较。

[COMPUTED][HIGH] Development、两项`CrowdDemo.SF4.Positioning`和`git diff --check`通过。原参数Static Small单轮证据为`Saved/CrowdDemo/CrowdDemo_8391_20260712_173009`：Target crossing=0、server/client obstacle penetration=0、Phase/Traffic/Portal/ORCA/AgentState hash match、checkpoint/interval p95=`0.058/0.064cm`、agents=visible=20、无Fatal/Assertion/Ensure/LogWindows Error/VIOLATION，故生产接入安全门通过。

[COMPUTED][HIGH] Static能力门失败：assigned=20、candidate overlap/unreachable=0/0、corridor=20、deadlock=0，但StableOccupied/ReserveHold=`1/0`、arrival p95=`1165.730cm`。Front末态聚合为Waiting/RadialStage/AngularAlign/RadialCommit=`3/10/2/1`；phase request/granted/held/invalid=`4163/39/4027/117`、route conflict=`4027`、held steps p95=`170`、transitions=39。Radial forward p50/min=`3.160/-9.832cm/s`，constraint来源active/waiting/stable=`72/24/6`。

[INFERRED][HIGH] 两阶段原子性和双端安全已经成立，但“全部current reservation不可抢占、单遍不重试”的保守调度在当前几何中形成大量长期Held；同时active/waiting/stable ORCA约束把径向有效前进压到接近零。该证据只定位下一设计问题，不授权继续放宽Target exclusion、修改ORCA/PBD或调参。本轮按能力门停止。

[COMPUTED][HIGH] 未运行DebugGame Editor、完整`CrowdDemo.SF`、正式两轮或录像；未进入Moving Target、攻击、多profile、P1、100/500或NavMesh Bake。

## SF4 两阶段Phase Reservation纯内核（2026-07-12）

[COMPUTED][HIGH] 新增纯C++ `ScheduleFrontPhaseReservations`，输入同时携带`CurrentPhase/CurrentReservationPoints`与`RequestedPhase/RequestedReservationPoints`。scheduler先把全部current reservation作为本boundary不可抢占占用，再按`RequestedPhase降序 → CommitGrantedStep升序 → AgentId升序`稳定扫描；next-phase路径只有通过Target exclusion门且不与其他current或本boundary已批准路径冲突时才grant。

[COMPUTED][HIGH] 获批请求只在内核临时占用集中以requested path替换自己的current path；held请求继续保留current path并阻止后续冲突请求，invalid请求不会进入next phase。Decision hash折叠SafetyGap、AgentId、grant step、半径、current/requested phase、Target门和1cm量化的两组路径点。

[COMPUTED][HIGH] 自动化覆盖无冲突并发、current冲突held、held旧占用继续生效、Target门失败、phase优先级与输入反序hash；Development和两项`CrowdDemo.SF4.Positioning`通过，`git diff --check`通过。

[COMPUTED][HIGH] 该段记录纯内核门通过时的历史状态；其后已完成生产接入，当前状态与能力停止点以上一节为准。

## SF4 Phase-Local Reservation反证与撤销（2026-07-12）

[COMPUTED][HIGH] 实验分支将RadialStage/AngularAlign/RadialCommit分别缩为当前径向段、当前arc和最终径向段，并在active局部段冲突时尝试确定性requeue，随后改为reservation-held以避免重启。

[COMPUTED][HIGH] Phase-local requeue版把grants提高到13、arrival p95降到1176.595cm，但requeues升到10且仍0/20。reservation-held版把requeues降到1，却产生Target exclusion crossing=1，Radial constraint p95升到12，来源active/waiting/reserve=`10/14/8`，违反硬安全门。

[COMPUTED][HIGH] Phase-local与held代码、fragment和fixture已撤销；当前恢复Target crossing=0的full-route静态route-aware版本，仅保留ORCA约束来源诊断。Development与定向自动化在撤销后重新执行。

[INFERRED][HIGH] Phase-local不能只靠“缩短占用段+冲突时停住”实现；它需要独立的phase transition reservation状态机，在进入下一phase前申请而不是先切phase后补救，并且必须把Target exclusion作为不可抢占资源。

## SF4 ORCA约束来源诊断与动态预测反证（2026-07-12）

[COMPUTED][HIGH] RadialStage新增constraint p95及active/waiting/reserve-commit/stable/other来源聚合。保留版单轮来源为constraint p95=2、active=1、reserve-commit=1、waiting/stable/other=0，证明侧向转速不是大量Waiting约束造成。

[COMPUTED][HIGH] 实验性扩展曾让非冲突active polylines互相省略约束，并用ORCA horizon预测yielding swept segment；纯fixture通过，但正式单轮ORCA infeasible从410升至952、grants从5降至3，故该动态扩展和fixture已撤销，未进入当前架构。

[COMPUTED][HIGH] 动态实验末态Radial constraint来源变为active=0、waiting=2、reserve-commit=1；唯一Radial agent forward=81.633cm/s、error=41cm，说明其自身已正向接近phase门，但整体吞吐仍只有3 grants和0/20完成。

[INFERRED][HIGH] 当前主要问题已经转到Admission的全route占用过于长寿命：完成一个多phase route之前，新候选持续被完整未来polyline阻止。下一设计应比较full-route admission与phase-local reservation，而不是继续扩大ORCA yielding责任。

## SF4 Route-Aware ORCA实验停点（2026-07-12）

[COMPUTED][HIGH] 新增SF4专用`ECrowdDemoOrcaRouteMode`与稳定route pair policy。Active Approach route以`当前位置→RadialStage→arc→candidate`量化polyline表示；只有该polyline穿过Waiting/被Front门阻塞的ReserveCommit安全圆时才覆盖默认pair规则：active侧省略该waiting约束，yielding侧承担100%责任。不相交pair、active↔active、active↔stable和所有SF1/SF2/SF3 pair保持原ORCA规则，后续仍执行PBD和ObstacleReproject。

[COMPUTED][HIGH] 纯fixture覆盖active/yielding双向责任、非冲突pair不覆盖、BuildPairConstraint active侧省略与yielding侧责任=1；Development和两项`CrowdDemo.SF4.Positioning`通过。

[COMPUTED][HIGH] 新增Radial ORCA/final沿preferred方向投影指标。它证明旧`speed≈800cm/s`主要是侧向速度：首轮route-aware版本的ORCA/final speed p95=`799.991`，但forward p50/min仅`70.406/0cm/s`。将等待中的ReserveCommit纳入yielding后，曾得到Radial forward=`800/503.641cm/s`并有2个RadialCommit，但仍0/20完成。

[COMPUTED][HIGH] no-progress已按ApproachPhase分别重置BestErrorBucket，禁止把厘米RadialStage与毫弧度AngularAlign bucket跨单位比较；requeue由5降到1。诊断随后发现Radial error=`31/35cm`停在30cm阈值外，故进入阈值使用既有`RadialTolerance+SafetyGap=40cm`，AngularAlign仍保留径向纠偏。

[COMPUTED][HIGH] 最新单轮证据`Saved/CrowdDemo/CrowdDemo_8391_20260712_150501`仍失败：Stable/ReserveHold=`0/0`、waiting/radial/angular/radial-commit=`13/2/0/1`、SlotCommit=1、grant/requeue=`5/2`、arrival p95=`1531.321cm`。Radial preferred/ORCA/final p95=`800/799.991/799.991cm/s`，forward p50/min=`83.6/83.6cm/s`，radial error p50/p95=`42/2194cm`。

[COMPUTED][HIGH] 最新单轮corridor/turn=`20/20`、deadlock=0、Target crossing=0、双端penetration=0、server/client hash match、checkpoint/interval p95=`0.050/0.064cm`，无精确坏日志。技术安全与确定性成立，但Static能力0/20未成立。

[INFERRED][HIGH] 现有几何route无冲突不等于速度half-plane可并发；active routes仍可通过active↔active、stable或非route waiting约束被转向。下一阶段必须先建立多route速度可行性fixture和route conflict与ORCA half-plane的一致性证明，禁止继续靠阈值或priority调参。

## SF4 Approach entry/phase-lock与ORCA停点（2026-07-12）

[COMPUTED][HIGH] 当前保留的Approach ownership规则为确定性滞回：`FrontAssignedWaiting`只有进入assigned position 1200cm envelope后才可首次grant；一旦进入`FrontCommitGranted/SlotCommit`且ApproachPhase有效，即使短暂越出1200cm仍由Approach guidance持续拥有compose；Portal ownership仍优先。no-progress现在把任意1cm RouteError bucket下降计为真实进展。

[COMPUTED][HIGH] entry gate与phase-lock纯测试覆盖1201cm不grant、1200cm可grant、active phase在距离门外仍compose、waiting仅在门内compose以及Portal ownership优先。Development和两项`CrowdDemo.SF4.Positioning`通过。

[COMPUTED][HIGH] 保留版本单轮证据为`Saved/CrowdDemo/CrowdDemo_8391_20260712_144038`：corridor/turn=`20/20`、deadlock=0、penetration=0、双端hash match、checkpoint/interval p95=`0.066/0.059cm`；但完成仍为0/20，waiting/RadialStage=`13/3`、grant/requeue=`10/7`、arrival p95=`1218.161cm`。

[COMPUTED][HIGH] Phase-lock把compose switches从旧295降到22，并让Radial preferred保持800cm/s；随后ORCA/final仅为65.765cm/s，radial error p50/p95/max=`610/927/927cm`且末step改善数=0。因此当前瓶颈已定位到Approach preferred进入ORCA后的可行速度压缩。

[COMPUTED][HIGH] 曾只在SF4把active approach映射为Portal Reserved优先级进行单轮反证；结果ORCA/final进一步降为0并新增target exclusion crossing=1，故该实验代码已撤销，不作为当前架构。Portal通行priority不能直接冒充环形Approach route许可。

[INFERRED][HIGH] 下一设计必须显式定义route-aware局部避让契约：active route的swept corridor、与waiting/stable route的时空冲突以及可行速度方向，而不是继续复用Portal枚举或修改ORCA参数。

## SF4 Admission并发与RadialStage诊断停点（2026-07-12）

[COMPUTED][HIGH] `ScheduleFrontAdmission`不再因存在任意active route而全局停止grant。每个fixed-step boundary先把`FrontCommitGranted/SlotCommit` route作为已占路径，再按`Depth → AngularSector → PositionId → Travel → AgentId`扫描waiting route；候选必须同时不与active集合和本boundary已选集合冲突才grant。结果是稳定贪心极大无冲突集合，不声称是全局最大基数集合。

[COMPUTED][HIGH] 自动化新增active route并发fixture：无冲突waiting route可同时grant、冲突waiting route被阻止、输入反序不改变decision hash。Development Editor编译和两项`CrowdDemo.SF4.Positioning`通过。

[COMPUTED][HIGH] RadialStage诊断新增preferred/ORCA/final speed p95、radial error p50/p95/max、真实改善数、量化stall和compose-range边界切换数，并进入correction rollback聚合口径，不输出per-agent日志。

[COMPUTED][HIGH] 原参数Static Small单轮仍失败：StableOccupied/ReserveHold=`0/0`、waiting/RadialStage/AngularAlign/RadialCommit=`14/2/0/0`、grants/requeues=`7/5`、arrival p95=`1199.163cm`。两个RadialStage的preferred/ORCA/final speed p95=`70.000/799.966/799.966cm/s`，radial error p50/p95/max=`480/1009/1009cm`，真实改善实体=1、量化stall=0、compose boundary switches=295。

[INFERRED][HIGH] 本轮证据反驳“ORCA把RadialStage速度压成零”：ORCA/final速度显著高于Radial preferred。主要新瓶颈是compose-range边界在一轮内反复切换，使Radial guidance不能持续主导最终速度；Admission并发修复成立，但没有转化为站位完成。

[COMPUTED][HIGH] 单轮flow hash=`267519150`、flow unreachable=0、corridor=20、deadlock=0、双端最终obstacle penetration=0、agents=20；按能力门停止，未运行正式两轮、完整SF、DebugGame Editor或录像。证据目录为`Saved/CrowdDemo/CrowdDemo_8391_20260712_142103`。

## SF4 Target-Aware Approach Gate单轮回退停点（2026-07-12）

[COMPUTED][HIGH] 分支A回退根因是grant后仍使用direct-to-slot guidance；新实验新增量化Target exclusion、同sector OuterGate、RadialStage、AngularAlign、RadialCommit和no-progress计数。Gate clearance固定为`max(实体直径+SafetyGap, FlowCellSize)`，Gate/arc/radial commit必须同时满足Flow bounds、reachable、clearance、Target exclusion和Stable/Reserve blocker检查，失效时不回退直线。

[COMPUTED][HIGH] 极坐标route纯测试通过旧直线穿Target/Stable失败、新Gate+arc+radial路径成功、Gate失效、blocker乱序、route hash、远侧优先、交叉route拆分与no-progress requeue。

[COMPUTED][HIGH] 原参数单轮能力门显著回退：StableOccupied/ReserveHold=`0/0`、FrontAssignedWaiting/RadialStage/AngularAlign/RadialCommit=`13/2/0/1`、SlotCommit=1、front grants/requeues=`7/4`、arrival p95=`1197.644cm`。Target crossing=0、Stable blocked radial commit=0、order inversion=0、penetration=0、corridor=20、deadlock=0、agents=visible=20。

[INFERRED][HIGH] 该结果符合失败情况2：route合法但30秒内未完成，瓶颈位于Admission吞吐与RadialStage路径进度；由于单轮完成状态明显低于原16/20，按门控停止，不运行正式两轮、不延长round、不提高MaxSpeed，也不叠加occupied-aware reassignment。

## SF4 Front Ingress诊断与分支A失败停点（2026-07-12）

[COMPUTED][HIGH] 默认关闭的`-CrowdDemoSf4IngressDiagnostic`通过纯C++量化几何helper，在MovementFinalize后只读评估SlotCommit路径、Target exclusion、Stable/Reserve/Commit安全圆、alternative Front、ORCA constraint来源、四阶段速度、连续低速、PBD/Obstacle推离和最小稳定fixture；输入按状态优先级与AgentId稳定排序，不输出per-agent日志。

[COMPUTED][HIGH] 原行为诊断两轮完全一致：4/4 SlotCommit直线路径被StableOccupied阻断，Stable blocker pairs=14，3/4存在ingress order inversion；ORCA来源Stable/Reserve/Commit=`48/9/12`，Target exclusion crossing=1/4，Reserve几何阻断=0，PBD/Obstacle push-away=`0/0`，unblocked alternative Front=0，same-side occupied Front=3，minimum fixture hash/constraints=`44101239/2`。

[INFERRED][HIGH] 证据选择分支A：StableOccupied阻断覆盖全部失败实体，且3个顺序倒置证明可调整进入顺序；分支B只覆盖1/4，分支C不满足“几何无阻挡”，分支D没有candidate/local reachability证据。

[COMPUTED][HIGH] 分支A增加`FrontAssignedWaiting → FrontCommitGranted → SlotCommit → StableOccupied`，按Target→Spawn EntryAxis将远侧candidate排在入口侧candidate之前，固定wave size=2，交叉路径拆wave，180 steps timeout确定性requeue。等待中的Front在进入1200cm compose范围后停止继续向内，ORCA/PBD/Obstacle参数均未修改。

[COMPUTED][HIGH] 分支A原参数两轮结果完全一致但硬门失败并回退：StableOccupied/ReserveHold=`4/3`，SlotCommit=2、ReserveCommit=1，front admission grants/requeues=`10/4`，arrival error p95=`1196.852cm`。Ingress终点仍为Stable blocked=2、Target crossing=2、order inversion=1；AgentState hash=`1583783642`且双端/两轮一致，checkpoint/interval p95=`0.056/0.061cm`，corridor=20、deadlock=0、penetration=0、坏日志=0。

[COMPUTED][HIGH] promotion指标已改为真实Reserve→Front边沿：`promotion_transition_count`与`promotion_agent_count`本轮均为0，不再逐fixed-step重复累加Assignment summary。相关metrics位于correction rollback快照中。

[INFERRED][HIGH] 分支A单独不足以在30秒内完成Static；当前禁止叠加Approach Gate、放宽ORCA、延长轮次或调大容差。Stage B继续失败，Moving Target未开始。

## SF4 单Front带/外层Reserve验证停点（2026-07-12）

[COMPUTED][HIGH] 失败优先fixture证明旧Candidate catalog把radial band 1–4同时标为Front，并保留了Front内侧Reserve。纯kernel现改为：选择最内侧、确实命中PreferredDistance的单一radial band作为Front，丢弃其内侧过近带，其余外侧合法带作为Reserve；不修改ORCA、MaxSpeed、ArrivalTolerance、round duration或网络参数。

[COMPUTED][HIGH] 单项fixture、Development、DebugGame Editor和完整25项`CrowdDemo.SF`通过。原参数Static Small两轮均为candidates/front/reserve=`150/16/134`、assigned=`20`、StableOccupied/ReserveHold=`12/4`、剩余SlotCommit=`4`、arrival error p95=`488.945cm`；因此完成状态仅`16/20`，Stage B硬门仍失败。

[COMPUTED][HIGH] 两轮Traffic/Portal/ORCA/AgentState hash=`1663271679/2373768712/367271209/3320977328`且双端一致；checkpoint/interval p95=`0.063/0.066cm`、cross-round growth=0、corridor=20、deadlock=0、双端penetration=0、agents=visible=20，精确坏日志模式=0。

[INFERRED][HIGH] Reserve角色划分已成立，但4个未settle Front实体仍被19邻居ORCA约束压到约4.123cm/s且误差均大于300cm。当前证据不支持继续扩大容差或放宽ORCA；按硬门停止，不进入Moving Target。

## SF4阶段B Static Small首次集成与停止点（2026-07-12）

[COMPUTED][HIGH] 未settle诊断进一步定位：修复前7个实体均为SlotCommit、guidance active，Portal/compose-range/arrival-speed门均未阻塞；误差分布为101–300cm六个、>300cm一个，guidance speed p95=800cm/s、ORCA output=10.440cm/s，但旧ObstacleReproject整厘米位置量化把0.348cm/step舍入为0。

[COMPUTED][HIGH] 仅SF4改为PBD后保留连续reproject位置，SF1/SF2/SF3仍保持原取整行为；回归测试证明10.44cm/s在30Hz的0.348cm位移会被旧整厘米路径清零。修复后Obstacle output p95恢复为2.828cm/s，arrival error p95从266.895降为199.057cm，双端hash和误差门保持通过。

[COMPUTED][HIGH] Static能力仍未通过：StableOccupied/ReserveHold仍为13/0。剩余7个均为SlotCommit且guidance active，误差31–100cm两个、101–300cm四个、>300cm一个；ORCA adjusted=7、constraint p95=19、ORCA/Obstacle/final speed p95=2.828cm/s，无fallback/infeasible、无obstacle hit，上一step PBD corrected=2。

[INFERRED][HIGH] 现有Candidate catalog的Front capacity=56，跨多个径向带，导致20实体全部获得Front、ReserveHold=0。13个先到StableOccupied实体继续作为ORCA邻居后，剩余7个向内层Front位置的速度被联合约束压到极低。下一次若继续，应先用fixture证明“单一Front带+外层Reserve带”角色划分，而不是调ORCA、速度或arrival容差。

[COMPUTED][HIGH] 后续rollback修复已把SF4纳入Traffic场景快照，并保存/恢复Portal runtime、Admission、Band、FlowSample、PositionAssignment、PursuitGuidance、TargetFact、Candidate/Assignment prepared SoA、revision与positioning summary。原参数重跑两轮后Traffic/Portal/ORCA/AgentState local/server hash全部一致，correction interval p95从`173.220/173.072cm`降为`0/0cm`，无VIOLATION。

[COMPUTED][HIGH] 确定性恢复且连续reproject修复后Static能力门仍失败：两轮StableOccupied/ReserveHold均为`13/0`，7个实体未settle，arrival error p95为`199.057cm`。Candidate/Assignment/Target hash为`1845459626/372776512/479230708`，说明结果稳定复现，不是correction伪差异。

[INFERRED][HIGH] 下一次若继续Static，应先增加紧凑的未settle空间/状态分类，区分仍在SlotCommit、到位速度门失败、ORCA/PBD持续推出和candidate几何误差；在证据前不得扩大ArrivalTolerance、提高速度或放宽ORCA。

[COMPUTED][HIGH] Static真实package已通过非NullRHI Unreal Python `delete_asset→new_level→save_current_level`创建，包含DirectionalLight、ExponentialHeightFog、SkyAtmosphere、SkyLight、`SM_SkySphere`、VolumetricCloud、PreviewFloor与俯视PlayerStart。NullRHI编辑路径曾在ActorFactory阶段触发`EXCEPTION_INT_DIVIDE_BY_ZERO`；非NullRHI重建成功。

[COMPUTED][HIGH] 首版scenario=3集成在Development、DebugGame Editor与完整25项`CrowdDemo.SF`中通过编译/自动化，但正式Static两轮硬门失败。两轮均为candidates/front/reserve=`154/56/98`、assigned/unassigned=`20/0`、StableOccupied/ReserveHold=`13/0`、arrival error p95=`266.895cm`、candidate overlap/unreachable=`0/0`、corridor=`20`、deadlock=0、双端obstacle penetration=0、agents=visible=20、AgentState hash=`484091323`且跨轮一致。

[COMPUTED][HIGH] 双端确定性硬门失败：round1客户端local/server Traffic hash=`3604966057/1183689225`、Portal=`958722397/902594370`、ORCA=`2518695377/104969352`；round2分别为`2549368363/1183689225`、`4052251115/902594370`、`3685793051/104969352`。两轮均输出精确`CrowdDemoSf3Hash ... match=0 VIOLATION`，correction interval p95=`173.220/173.072cm`。

[COMPUTED][HIGH] 原结构缺口已关闭；仍有7个已分配实体在30秒内未进入StableOccupied，属于独立settle能力失败。

[COMPUTED][HIGH] 按Static硬门停止；阶段B代码、脚本和地图保持未提交，不进入Moving Target、录像、P1或100/500。

## SF4 阶段A：确定性Candidate与Assignment kernels（2026-07-12）

[COMPUTED][HIGH] `FCrowdDemoPursuitPositioningKernel`是纯C++数据内核，不访问UWorld、Actor、Mass fragment或导航系统。Candidate读取现有Shared FlowField raster，按bounds、blocked、unreachable与按Agent半径扩张的obstacle clearance过滤；capacity固定为1，输出按`Role→RadialBand→AngularSector→StableCellKey`排序，PositionId由TargetId、量化LocalOffset、Role与RadialBand做稳定FNV折叠。

[COMPUTED][HIGH] Assignment先为每个Agent生成量化整数cost proposal，再以固定轮数deferred acceptance批量求解；决胜为cost、existing owner、AgentId、PositionId。完成匹配后，对稳定PositionId顺序的空缺Front执行第二个确定性批量promotion pass，只允许已分配Reserve的实体晋升，不在proposal遍历中即时修改occupancy。

[COMPUTED][HIGH] 阶段A自动化覆盖20唯一assignment、Candidate spacing、reachable/clearance、Front不足转Reserve、existing assignment 20/20复用、真实ReserveHold vacancy promotion、candidate invalidation/release、Agent/Candidate乱序、同分AgentId/PositionId决胜与两轮hash。Development、DebugGame Editor、完整25项`CrowdDemo.SF`与`git diff --check`通过，提交为`bac361f7`。

[COMPUTED][HIGH] 阶段A尚未新增场景、fragment、processor、地图或运行时metrics；Static Target集成、状态机、双端技术门与录像均未开始。

## SF3 ORCA/LP 收尾与成熟参考实现对照（2026-07-12）

[COMPUTED][HIGH] 当前 Formal LP 与测试专用 RVO2 参考实现现在消费同一份 `FCrowdDemoOrcaContinuousSolveInput`：相同 preferred velocity、max speed、稳定排序 half-plane 与 behavior epsilon；统一合法域为 `dot(Velocity-Point,Normal)>=-BehaviorEpsilon`。连续求解不执行 1cm/s 量化，连续结果随后共用正式量化与全约束复验。

[COMPUTED][HIGH] 参考实现只适配上游 RVO2 的 `linearProgram1/linearProgram2`，固定到 `snape/RVO2@b577921d2bc1281a6b721c2d4778f397d37da97d`，许可证为 Apache-2.0。代码位于 `ThirdParty/Reference/RVO2`，仅在 `WITH_DEV_AUTOMATION_TESTS` 下编译；没有接入 Simulator、Agent container、KdTree、OpenMP、时间推进或正式 Mass pipeline。

[COMPUTED][HIGH] 单轮 Small P0 有界差分样本为1052：current/reference exact=`637/637`、current miss/reference hit=`0`、current hit/reference miss=`0`、both miss/oracle hit=`0`、all exact miss=`415`。637个连续可行样本均没有1cm/s量化格点或3×3邻域解；Oracle有630个连续witness，但没有证明当前continuous solver漏解。按替换门选择分支B：保留当前 Formal LP，不用参考实现替换正式连续求解器。

[COMPUTED][HIGH] 正式 `Solve` 已删除 LP失败后的 Oracle调用、Oracle量化witness写回和对应隐藏恢复层。Oracle与RVO2 reference只允许测试/显式Development诊断调用；正式结果中Oracle invocation/witness used均为0。`FormalLpFeasible`与`FormalLpQuantizedRecovered`仍是严格可行输出，fallback是best-effort且不计入Exact；Stop只有满足全部约束才记为StopFeasible，否则保留StopViolation。

[COMPUTED][HIGH] reference diagnostic关闭后的Small P0连续三轮结果完全相同：AgentState hash=`1121342212`，Traffic/Portal/ORCA hash=`1737467741/902594370/3496938511`；flow unreachable=0、corridor=20/20、deadlock=0、wall/turn=20/20、双端obstacle penetration=0、11-stage mismatch=0、checkpoint/interval/cross-round error=0、agents=visible instances=20。日志没有Fatal、Assertion、Ensure、`LogWindows: Error`或精确VIOLATION。

[COMPUTED][HIGH] 每轮18020个agent-step中：preferred exact=3701、Formal LP exact=4596、3×3 quantized recovery=8962、continuous-feasible/quantized-empty=19、exact无严格可行输出并最终StopViolation=742、Portal best-effort=2、StopFeasible best-effort=17、总fallback-stop=759。goal=10/20，但10个未到达实体最终距goal仅167.836–265.957cm；reached→non-reached constraints=47363，说明剩余效果同时包含严格约束冲突、离散量化冲突和单目标邻域容量竞争，现有聚合证据不能把10个goal失败逐个因果分摊给某一层。

[INFERRED][HIGH] LP收尾技术门已通过，Small路线与确定性没有回退；goal未到20不是LP漏解证据，也不是最终群体效果通过。下一阶段应独立设计稳定站位/目标容量：cohort生成可通行候选位，按AgentId与量化代价稳定分配，已占位实体进入StableOccupied，后续实体进入外围等待/补位。本轮没有实现站位、攻击或业务slot。

[INFERRED][HIGH] 更后续的基础设施方向是Editor/Commandlet从明确导航数据离线生成带walkable、height、clearance、area flags、connectivity与版本hash的Crowd Navigation Field；运行时只按cohort goal重建integer integration/shared flow，并把动态障碍作为确定性overlay。本轮没有执行NavMesh查询、Bake或地图迁移。

## SF3 Flow reachability 边界诊断与恢复（2026-07-12）

[COMPUTED][HIGH] 初步“raster/continuous不一致”判断只被部分证实：纯测试确实证明连续AABB合法位置可能落在blocked raster cell，但正式Small的7个invalid并非BlockedRasterCell或UnreachableFreeCell，而是OutOfBounds。两轮诊断均为最终Reachable/OutOfBounds/BlockedRaster/UnreachableFree=`13/7/0/0`，final invalid=7、invalid deadlock=7、flow unreachable=7，三者完全对应。

[COMPUTED][HIGH] 默认关闭的`-CrowdDemoSf3FlowReachabilityDiagnostic`比较StepStart、MovementPredict、ObstacleConstraint、HardPBD、ObstacleReproject与MovementFinalize。修复前所有7次`Reachable→OutOfBounds`首次发生在MovementPredict；Obstacle/PBD/Reproject均为0。首个有界witness为cell1247→out-of-bounds、world delta=`(26.133,5.300)cm`、连续penetration=0、最近reachable cell距离50.413cm。

[COMPUTED][HIGH] 因正式证据唯一指向分支B，SF3在现有两次continuous movement constraint调用中启用Flow bounds clamp：只裁掉越界X/Y分量并保留切向分量；不移动到cell center、不搜索全局最近点。SF1/SF2调用保持默认关闭。PBD后仍执行同一ObstacleReproject。Flow bounds最大reproject delta=`23.267cm`。

[COMPUTED][HIGH] 修复后两轮最终Flow状态均为Reachable/OutOfBounds/BlockedRaster/UnreachableFree=`20/0/0/0`；每轮仍观察到2次Predict候选越界，但ObstacleConstraint立即恢复，invalid→reachable=2，final invalid/preferred-zero/final-zero/invalid-deadlock均为0。flow unreachable=`7→0`、corridor=`12→20`、deadlock=`7→0`、wall/turn=`20/20`，证明远端Corridor停滞主要由Flow bounds丢失造成。

[COMPUTED][HIGH] goal由12提升为13但未到20。7个non-reached从修复前3721–4117cm远端Corridor转为目标附近：nearest/p50/p95=`141.662/207.993/286.575cm`，stopped near/far=`1/0`，19个实体仍在移动。该分布满足“Flow关闭后只剩目标附近容量/稳定位置问题”的后续设计条件，但本轮未实现arrival、slot、攻击位置或reached行为改造。

[COMPUTED][HIGH] ORCA恢复口径已拆分：oracle after continuous failure=415、after quantization failure=637、oracle quantized witness used=607、fixed 3×3 recovered=8538、oracle no witness=422；true-no-witness按正式FailureReason为reachable Flow=445、invalid Flow=0、goal-near=222、Corridor=117。ORCA infeasible/stop violation仍为445，说明Flow域已不是剩余true-no-witness的拥有者；本轮不加入LP3。

[COMPUTED][HIGH] 修复后Traffic/Portal/ORCA/AgentState hash=`1032547618/2199333646/2957536827/2466489683`，diagnostic hash=`1395484196`；两轮与双端一致，11-stage mismatch=0，checkpoint/interval/cross-round error=0，agents=visible=20。overlap/severe p95=0，双端obstacle penetration=0，capacity violation=0；reservation timeout=1是未通过项，不能写成0。日志Fatal/Assertion/Ensure/LogWindows Error/精确VIOLATION均为0。

[COMPUTED][HIGH] Development、DebugGame Editor、完整23项`CrowdDemo.SF`通过；SF1 build hash仍为`267519150`，Portal count=3且geometry regression未回退。四张SF3地图及其Lighting组件是用户现有改动，本轮没有修改、重建、暂存或提交地图。

[COMPUTED][HIGH] diagnostic-only录像位于`Saved/CrowdDemoCapture/CrowdDemoCapture_8291_20260712_010614`。用户补齐Lighting后画面亮度明显正常，contact sheet可见20个实例通过窄口并离开原Corridor停滞区，未见穿墙、bounds snap或贴墙振荡；后段目标区不完整处于相机构图内，因此不能凭该录像验收goal分布，仍以双端日志判定agents=visible=20。本录像不是最终站位acceptance evidence。

## SF3 Half-Plane LP parallel 数值稳定性修复与停止点（2026-07-12）

[COMPUTED][HIGH] 已关闭同向等价 half-plane 的 parallel 数值残差误判：旧 `ClipLineIntervalAgainstHalfPlane` 在 `abs(Denominator)<=1e-6` 时用严格 `Numerator<=0`，会把约 `+1.82e-7cm/s` 的浮点残差判成真实矛盾。提交 `36d7dbbc` 先以显式残差证明旧实现红测失败；提交 `9f01b31c` 完成修复。

[COMPUTED][HIGH] `BehaviorEpsilonCmps=0.1cm/s` 保持不变，只定义 ORCA 行为边界。数值层独立使用 `ParallelAngularTolerance=64*FLT_EPSILON`（无量纲）、`ResidualToleranceCmps=max(1e-6,16*FLT_EPSILON*RelevantVelocityScale)` 和 `ParameterTolerance=max(1e-6,8*FLT_EPSILON*RelevantVelocityScale)`；800cm/s 尺度下分别约为 `7.629e-6`、`0.001526cm/s` 和 `0.000763` 个 line parameter。RelevantVelocityScale 覆盖 MaxSpeed、line/constraint point 与 interval endpoint 量级。

[COMPUTED][HIGH] Formal LP 的 dot product、line-circle 判别式、Numerator/Denominator 与 interval bound 改用 double 中间值；输入、最终 `FVector2f`、1cm/s 速度量化、Q15 normal 和 hash 合同未变。非 parallel 仍按 denominator 符号裁剪；parallel/near-parallel 仅当 `Numerator>ResidualToleranceCmps` 才判矛盾；区间空判断使用独立 ParameterTolerance。Formal 连续解复核允许 BehaviorEpsilon 之外的派生数值残差，正式量化输出与 fallback 仍按原行为边界复验。

[COMPUTED][HIGH] 自动化覆盖生产精确 fixture、`±1e-8/±1e-7/±1e-6cm/s` 残差、`2x/10x` 容差与 `0.01/0.1cm/s` 真实冲突、`0/±1e-8/±1e-7/±1e-6/±1e-5rad` 近平行角度、输入反序和 1–24 constraint 固定 Formal-vs-Oracle 矩阵。ORCA 定向 9/9、完整 `CrowdDemo.SF` 22/22、Development 与 DebugGame Editor 均通过；SF1/Portal/Holding/Band 自动化未回退，Small 仍提取 3 个 Portal。

[COMPUTED][HIGH] 原 P0 Small 20 双端两轮完全一致：processed=18020、preferred feasible=7678、Formal feasible=2942、Formal quantized recovery=6668、missed→Zero=0、missed→Oracle=0、continuous-only quantized empty=0、true no witness/stop violation/infeasible=380、Oracle invocation=915。constraint p50/p95/max=`10/14/19`；parallel/near-parallel/redundant/stricter/true-contradiction/numerical-acceptance=`58/0/56/0/2/0`。solver p50约`0.095–0.098ms`、p95约`0.177–0.179ms`、max约`0.787–0.922ms`。

[COMPUTED][HIGH] 数值漏解安全网依赖已清零，但整体 Formal 硬门仍失败：每轮仍有 380 次 oracle 也找不到 witness 的真实空可行域分类，随后 stop 仍违反约束。路线仍为 goal=12/20、corridor=12/20、deadlock=7、flow unreachable=7；8 个 non-reached 全在 `1200_plus` 距离桶和 Corridor 区域，nearest/p50/p95=`3721.295/3917.934/4116.642cm`，stopped near/far=`2/7`。

[COMPUTED][HIGH] 这里的“依赖清零”只指 Formal 连续求解的 missed→Zero/missed→Oracle 分类。当前仍有 Oracle invocation=915；代码在 Formal 连续解成功但固定3×3量化失败时可以采用 Oracle quantized witness，而聚合的 `FormalLpQuantizedRecovered` 没有区分两种恢复来源。因此本轮不能证明正式速度完全不依赖 Oracle，这也是未通过项，不用分类名掩盖。

[COMPUTED][HIGH] 安全与双端设施通过：Traffic/Portal/ORCA/AgentState hash=`1831652660/2254112183/390261822/1715680406`，diagnostic hash=`4117294620`，两轮与双端一致；11-stage mismatch=0，checkpoint/interval p95与cross-round growth均为0，overlap/severe p95=0，双端 obstacle penetration=0，agents=visible instances=20，精确坏日志模式=0。Portal count=3，admission granted/denied=`32/1`，Reserved→Inside→Exited=`32/32`，timeout/capacity/holding failure/holding overlap均为0。

[INFERRED][HIGH] 当前剩余失败不再是 parallel/near-parallel 数值漏解，而是已有 ORCA constraints 在部分 agent-step 上形成真实空交集，并使 8 个实体停在 Corridor。该问题需要另行审计 constraint compatibility、preferred/traffic 状态或上游路线，不能通过继续放宽数值容差解决；当前不具备进入稳定攻击位置阶段的条件。

[COMPUTED][HIGH] diagnostic-only 录像位于 `Saved/CrowdDemoCapture/CrowdDemoCapture_8291_20260712_002822`。contact sheet 可见约 8 个实例长期滞留 Corridor，未见穿墙或隐藏实例；画面偏暗且硬门失败，不是 acceptance evidence。本轮没有修改 pair 几何、责任比例、Portal/Holding/Band/density、goal、PBD、地图、网络、NavMesh、P0/P1 或顶层 pipeline，也没有运行 P1/Medium/Cohort/Crossing或实现站位。

## SF3 Deterministic Half-Plane LP missed-feasible修复尝试与停止点（2026-07-11）

[COMPUTED][HIGH] 失败优先fixture确认旧Formal LP在epsilon语义下漏解：candidate合法性使用`dot(v-Point,Normal)>=-epsilon`，但旧active boundary仍在`dot=0`上求解。两条反向近似平行constraint形成`0<=dot(v,n)<=0.05`可行条带且zero可行，旧solver返回false。

[COMPUTED][HIGH] interval solver已把active line改为`BoundaryPoint=Point-Normal*epsilon`，公开并测试line-circle interval与line-half-plane clipping；每次更新candidate后重验当前及全部前序constraints。量化继续保持1cm/s中心复验和固定3×3恢复，没有增大epsilon。

[COMPUTED][HIGH] 新分类区分`FormalLpFeasible`、`FormalLpQuantizedRecovered`、`FormalLpMissedZeroRecovered`、`FormalLpMissedOracleRecovered`、`ContinuousFeasibleQuantizedEmpty`、`TrueNoFeasibleWitness`和`StopViolation`。只有最终stop仍违反约束时`bInfeasible=true`；oracle只在Formal/量化失败时调用。

[COMPUTED][HIGH] 纯kernel红测转绿，Development、DebugGame Editor与完整19项`CrowdDemo.SF`通过。但正式P0 Small两轮仍完全一致地需要大量recovery：processed=18020、preferred=7678、formal LP feasible=621、formal quantized recovered=1111、formal missed zero recovered=4087、formal missed oracle recovered=3791、true no witness/stop violation/infeasible=380、oracle invocation=8292。Formal LP硬门失败。

[COMPUTED][HIGH] 新的未关闭fixture类别是同向平行、几何等价half-plane：`Normal=(0.989176510,-0.146730474)`，一条line point为`(0,0)`，另一条为`(-58.692189782,-395.670603986)`；两者法向常数差约`1.82e-7cm/s`。当前parallel分支用严格`Numerator<=0`判断冗余，把纯浮点残差误判为矛盾。该残差远小于行为epsilon，但本轮按情况D停止，没有继续修改parallel数值容差。

[COMPUTED][HIGH] 修复尝试后的能力结果为goal=12/20、corridor=12/20、deadlock=7、flow unreachable=7、severe p95=0、双端obstacle penetration=0。8个non-reached停在goal 1200cm以上/Corridor，nearest non-reached=`3721.295cm`；与修复前“15个全部在800cm内”相比，行为回退到路线中段。

[COMPUTED][HIGH] 双端仍确定：Traffic/Portal/ORCA/AgentState hash=`1505634695/2254112183/3309940941/3199391860`，两轮diagnostic hash=`4117294620`，checkpoint/interval p95与cross-round growth均为0，agents=visible=20，日志坏模式为0。

[INFERRED][HIGH] 当前只能判定为情况D：Formal LP仍有Bug，不能宣称interval solver修复完成，也不具备进入稳定站位/攻击位置阶段的条件。oracle recovery改变了正式速度并改善部分实体到达，但同时使8个实体停在Corridor；这不是可接受的安全网结果。

[COMPUTED][HIGH] diagnostic-only录像位于`Saved/CrowdDemoCapture/CrowdDemoCapture_8291_20260711_233818`；boost contact sheet显示约12个实体聚到goal approach，余下实体未完成路线，未见穿墙，但不能判定为人工验收通过。

## SF3 Small Goal Congestion / ORCA Feasibility 根因诊断（2026-07-11）

[COMPUTED][HIGH] `flow_goal_reached_count` 在 `UCrowdDemoRoundSimPipelineSubsystem::RecordFlowAgentSamples` 中以实体位置到 cohort goal 的二维中心距离 `<=140cm` 判定。达到过的 AgentId 写入轮内 sticky set，round reset；不是单帧采样、goal cell 或 integration-cost 判定。

[COMPUTED][HIGH] 到达实体不会销毁或退出模拟：它继续进入 TrafficField、ORCA neighbor grid、constraint construction、PBD 和最终提交。FlowPreferredVelocity 在140cm内先写零速度，但后续 PassingBandGuidance 会依据 `TrafficAgent.FlowDirection` 重新生成 preferred；最新正式诊断中5个reached实体只有1个在ORCA输入端仍为零preferred。因此“到达后停止”不是当前完整流水线事实。

[COMPUTED][HIGH] 新增默认关闭的 `-CrowdDemoSf3GoalCongestionDiagnostic`。它只聚合round-end距离桶、既有Flow区域、stopped持续时间、reached/non-reached约束来源和独立feasibility oracle；不写回AgentState、ORCA velocity、processor顺序或hash输入。correction rollback会恢复诊断累计器，避免客户端重放重复计数。

[COMPUTED][HIGH] 独立oracle枚举zero、clamped preferred、每条line上的preferred投影、line-line交点和line-circle交点，并对每个候选重新验证finite、MaxSpeed圆和全部half-plane。它只提供可行witness证据，不替代正式LP。

[COMPUTED][HIGH] P0 Small两轮诊断完全相同：diagnostic hash=`1767999127`；15个non-reached最终分布为100–200cm 1个、200–400cm 6个、400–800cm 8个，800cm外为0。nearest non-reached=172.800cm，non-reached radial p50/p95=`414.441/749.652cm`。Flow区域为GoalNear 7个non-reached、PostCorridor 8个，Corridor/PreCorridor/FarRoute均为0。

[COMPUTED][HIGH] reached=5，均仍进入ORCA并生成constraints；14/15 non-reached曾有reached neighbor，reached→non-reached constraints=`27430`。但200–800cm两档已记录的non-reached来源constraints中，against reached=`24942`、against non-reached=`118972`，说明外层停滞主要不是由5个reached实体单独造成。

[COMPUTED][HIGH] 正式LP连续失败的独立诊断为：`lp_failed_zero_feasible=7206`、`lp_failed_oracle_feasible=1068`、`lp_failed_oracle_no_witness=205`、`continuous_feasible_quantized_failure=80`、`genuine_speed_circle_empty=0`。因此旧 `multi-constraint empty=8479` 中至少8274次存在明确可行witness，不能继续解释为真实空交集；`stop feasible=7207` 与旧empty分类的逻辑冲突得到复现和定位。

[INFERRED][HIGH] 根因规则A成立：正式增量LP漏解或失败分类错误是主因。严格规则C不成立，因为只有7/15 non-reached位于400cm内、stopped near/far=`10/8`，且non-reached相互约束显著多于reached造成的约束。目标附近占位是次要贡献；当前证据不支持把问题简化为单一goal容量不足。

[COMPUTED][HIGH] diagnostic-only录像位于 `Saved/CrowdDemoCapture/CrowdDemoCapture_8291_20260711_230638`。原始画面过暗；另生成仅作观察的agent-boost副本和contact sheet。画面可见实体通过窄口后沿PostCorridor到goal approach形成长链并出现停滞/方向摆动，不是全部集中在140cm goal圈。该录像不是SF3 acceptance evidence。

## SF3 标准 reciprocal ORCA 约束修复与最新停点（2026-07-11）

[COMPUTED][HIGH] 旧 pair constraint 只沿两实体中心连线使用 `(combinedRadius-distance)/TimeHorizon` 生成径向速度约束；该表达式没有 cutoff circle 与左右 collision-cone leg，不能完整表达非重叠实体的 velocity obstacle，因此会构造错误的联合可行域。

[COMPUTED][HIGH] 当前纯 C++ kernel 统一采用 `relativePosition=Other.Position-Agent.Position`、`relativeVelocity=Agent.Velocity-Other.Velocity`，合法半平面统一为 `dot(Velocity-Point,Normal)>=-Epsilon`。非重叠 pair 按标准时间域投影选择 `CutoffCircle/LeftLeg/RightLeg`；重叠 pair 使用 fixed-step `Penetration` 投影。同优先级责任为 50%/50%，高/低优先级为 25%/75%，两侧责任和为 1。

[COMPUTED][HIGH] position 与 velocity 先量化，constraint point 量化为 1cm/s，normal 量化为 Q15 后重新归一化；增量二维 half-plane LP 在量化约束上求连续解，输出中心量化破坏可行性时才搜索稳定 3×3 邻域并重新验证全部约束。fallback 顺序仍为 preferred、50% flow、35% portal axis、stop；stop 不满足约束时保留 `StopViolation`，不伪装成安全解。

[COMPUTED][HIGH] ORCA 已拆分为可测试的 `BuildNeighbors`、`BuildPairConstraint`、`BuildAgentConstraints`、`SolveVelocityHalfPlanes`、`QuantizeAndValidateVelocity` 与 `SelectFallback`。每轮聚合 constraint kind、LP 根因、fallback 结果和 admission-state infeasible，不输出 per-agent 日志。

[COMPUTED][HIGH] 新增 correctness 测试先在旧径向实现上失败，随后在标准几何上通过；最终 Development、DebugGame Editor 与完整 16 项 `CrowdDemo.SF` 自动化通过，包含全部 `CrowdDemo.SF3`、Portal/Holding/Band 回归。SF1 flow hash 仍为 `267519150`，Small Portal 仍为 3 个，geometry hash 仍为 `1962319733`。

[COMPUTED][HIGH] 最新正式 P0 Small 20 两轮完全一致：Traffic/Portal/ORCA/AgentState hash=`1792415928/1263292735/1087088246/342420002`；goal=5/20，corridor=20/20，deadlock=0，overlap p50/p95/max=0/0/1，severe=0/0/0，reservation/transit timeout=0，capacity violation=0，holding allocation failure/overlap=0，双端 obstacle penetration=0，agents=visible instances=20，checkpoint/interval p95=0cm，cross-round growth=0。

[COMPUTED][HIGH] 每轮 ORCA 分类相同：cutoff/left/right/penetration constraints=`184898/7558/7594/132`，preferred feasible=3608，LP feasible=5853，single-outside-speed-circle=0，multi-constraint empty=8479，quantization destroyed=80，fallback flow/portal=`38/41`，stop feasible=7207，stop violation=1273；总 ORCA infeasible=8559，fallback stop=8480。infeasible admission 分布为 Waiting/Approach/Reserved/Inside=`4/0/53/29`（聚合的状态计数与 agent-step 总数口径不同，前者沿用既有按状态累计口径）。

[INFERRED][HIGH] 标准 pair 几何错误已修复，但 Small 的主要剩余失败是多约束空交集；它不是 Portal 数量、Holding 分配、双端确定性、checkpoint、障碍安全或实例显示问题。本轮按门控停止，不修改 Portal/Holding/Band、P0/P1、地图、网络、PBD 或顶层 pipeline。

[INFERRED][HIGH] Demo 的长期目的、目标效果和架构原则以 `DemoPurposeAndTargetEffect.md` 为稳定事实源；本文只描述当前代码与验证状态。

## SF3 Portal/Holding/ORCA 实验实现与门控结论（2026-07-11）

[COMPUTED][HIGH] Portal extraction 现在只在 cohort spawn→goal 主交通轴上提取严格局部 clearance 最小值；三格窗口合并 plateau 和 flow-edge 空档，排除场地边界与横向墙端。Small 固定得到 3 个 Portal，ID 为 `322861801/344220896/416677724`，geometry hash 为 `1962319733`。

[COMPUTED][HIGH] Approach/Waiting 锁定 Portal；候选检查 entry side、轴向 approach、横向 span 和 Flow/Portal axis 正投影。正式两轮 portal rebind/release、reservation/transit timeout 和 capacity violation 均为 0。

[COMPUTED][HIGH] 当前代码包含实验性 holding target、entry-side 上游扩行、PassingBand lateral-error 反馈和 Reserved/Inside Portal-axis guidance；相关纯内核测试通过，但最新 Small 能力门失败且没有通过后的人工录像，因此不能表述为最终 Holding/Traffic 效果已经完成。

[COMPUTED][HIGH] Deterministic ORCA 已由两遍投影替换为增量二维 half-plane LP；每个约束在速度圆边界区间上求解，输出量化后在 3×3 格点重新验证。Fallback stop 不再无条件接受，而是分别记录 satisfies/violates。

[COMPUTED][HIGH] 正式 P0 Small 两轮的 traffic/portal/ORCA/state hash 分别为 `2531374191/1430184422/1802002571/3324292024`，双端一致且两轮 AgentState hash 一致；checkpoint 与 correction interval p95 均为 0cm，双端 obstacle penetration=0。

[COMPUTED][HIGH] Small 能力门仍失败：两轮均为 goal=18/20、corridor=20/20、deadlock=0、ORCA infeasible=6017、fallback stop=5904、stop constraint violations=5666。第一硬失败指标为 goal，不启用 P1，不运行 Medium/Cohort/Crossing，不录制视频。

[INFERRED][HIGH] 后续若重新立项，应首先检查 ORCA constraint construction 的联合可行性与速度障碍模型；不能通过把 stop 再次标记为无条件安全来掩盖失败。

[INFERRED][HIGH] 后续导航架构使用 Editor/Commandlet 从 NavMesh 离线烘焙 deterministic Crowd Navigation Field Asset，Server/Client 加载同一资产并比较 BuildHash；本轮没有实现 NavMesh 扫描或 bake。

## SF3 Determinism/Portal 修复历史基线（已被上方最新结果替代）

[COMPUTED][HIGH] 本节保留后续 Portal/Holding/ORCA 改造前的可复现基线；其中 hash 和能力指标不是当前最新值。

[COMPUTED][HIGH] SF3 correction rollback 现在按 frame 对应 fixed-step 恢复 raw RoundSim state、PortalAdmission、PassingBand、上一 step FlowSample、Portal runtime 及 round hash accumulator，然后重放后续 fixed steps；没有增加复制字段或修改 correction 频率。

[COMPUTED][HIGH] 默认关闭的 `-CrowdDemoSf3DeterminismDiagnostic` 在 correction boundary 输出 11-stage 稳定 FNV-1a32 hash。正式 P0 Small 两轮共对齐 132 个 server/client boundaries，mismatch=0。

[COMPUTED][HIGH] Portal admission 状态机为 `None → Approach → Waiting → Reserved → Inside → Exited → None`。Reserved/Inside 不重复申请 token；capacity 从持久状态重算；reservation/transit timeout 为 60/120 steps；portal 未清空不得换向。

[COMPUTED][HIGH] 新增 `ObstacleReproject → OverlapSample → MovementFinalize`，overlap/severe/residual PBD pairs 使用稳定 spatial grid 并按 `(MinAgentId, MaxAgentId)` 排序。

[COMPUTED][HIGH] 正式 P0 Small 两轮的 Traffic/Portal/ORCA/AgentState hash 双端一致；两轮 AgentState hash 都为 `327320670`；checkpoint 与 correction-interval p95 都为 0cm；双端 obstacle penetration=0。

[COMPUTED][HIGH] Small 通行能力仍失败且两轮完全复现：goal=9/20、corridor=11/20、deadlock=13、ORCA infeasible=5344、fallback stop=4821、overlap p95=1、severe p95=0。该结果证明剩余问题是 Server 交通调度能力，不是双端确定性或传输设施。

[COMPUTED][HIGH] 因 Small 硬门失败，未运行 Medium/Cohort/Crossing，也未录制 SF3 视频。

## SF3 场景架构（2026-07-11）

[COMPUTED][HIGH] 工程现有第三个隔离场景 `SimRoundCrowdTraffic=2`。SF1 与 SF2 的规则和 processor 分支未修改；SF3 固定 30Hz、30 秒，每轮重置稳定阵型。

[COMPUTED][HIGH] SF3 双端流水线为：`RoundPlanApply → SharedFlowFieldBuild → CrowdTrafficFieldBuild → PortalSchedule → FlowPreferredVelocity → PassingBandGuidance → DeterministicORCA → MovementPredict → ObstacleConstraint → HardSeparationPBD → ObstacleReproject → MovementFinalize → AuthorityCommit / ClientPredictionCommit`。

[COMPUTED][HIGH] Traffic/Portal/ORCA 算法位于纯 C++ kernels，processor 通过 PipelineSubsystem prepared SoA 交换；Coordinator 仍只负责 RoundPlan、RoundResultHeader、checkpoint chunks、readiness 与聚合指标。

[COMPUTED][HIGH] 新增四张真实 package：SF3 Small 20、Medium 100、Cohort 500、Crossing 200。它们由独立 Unreal Python 脚本创建，没有读取或重建既有五张 SF1/SF2 地图。

## 工程边界

[COMPUTED][HIGH] 工程保留三个隔离的双端确定性 RoundSim 场景：`SimRoundObstacle=0`（SF1 Shared FlowField）、`SimRoundFlowSeparation=1`（SF2 FlowField + Soft Separation + Hard PBD）和 `SimRoundCrowdTraffic=2`（SF3 Traffic/Portal/PassingBand/ORCA + Hard PBD）。

[COMPUTED][HIGH] SF3 存在实验性的 density speed scaling、portal approach/holding、passing band 与 deterministic ORCA 代码路径，但 Small 能力门仍失败，不能将它们写成最终架构验收完成。未实现 slot、攻击或其他业务系统；客户端视觉只读取 `FCrowdDemoRoundSimStateFragment`，衰减 correction display offset 并提交 ISM，不生成 gameplay movement。

## SF1/SF2 双端流水线

```text
RoundPlanApply
→ SharedFlowFieldBuild
→ FlowPreferredVelocity
→ [SF2] SoftSeparation
→ MovementPredict
→ ObstacleConstraint
→ [SF2] HardSeparationPBD
→ [SF2] ObstacleReproject
→ MovementFinalize
→ AuthorityCommit / ClientPredictionCommit
```

[COMPUTED][HIGH] Server/Client 使用同一组 Mass processors 和纯 C++ kernels。顶层 fixed-step 与 client visual processor 在 World BeginPlay 后显式注册，并设为 `EMassQueryBasedPruning::Never`，避免客户端实体晚于 processing graph 出现时被永久裁剪。

[COMPUTED][HIGH] inactive 状态只有在 plan 已到期时才允许 claim PlanApply boundary；future plan 不再提前占用静止 boundary。result 到达后可重新开放 round-end boundary，按“比较旧状态 → 应用 checkpoint → 激活下一轮”处理。

## RoundResult 传输

[COMPUTED][HIGH] replicated `RoundResultHeader` 只含 round/checkpoint/state-frame revision 与聚合指标，不含 AgentState。round end 只生成一份稳定 AgentId 排序的 `RoundResultCheckpoint` frame，并复用 correction chunk 通道按 100 agents 分块。

[COMPUTED][HIGH] 客户端支持 header-first、chunks-first、交错和重复 chunk；header 与 chunks 齐备后才组装内部 RoundResult。checkpoint 不作为普通 correction 提前应用；header/chunk 等待超过 5 秒输出 VIOLATION。

[COMPUTED][HIGH] 500 agents 每轮为 5×100 chunks；correction interval、chunk size、历史 revision 数、复制预算和 NetUpdateFrequency 未改变。

## Readiness 与视觉

[COMPUTED][HIGH] `-CrowdDemoRequireClientReady` 启用测试门：客户端必须连续 0.5 秒满足 `tracked agents == visible instances == expected`，且 Arena、Coordinator、replicated visual owner 均存在；服务器接受后将 round 1 安排在 3 秒后，60 秒超时输出 VIOLATION。默认启动行为不变。

[COMPUTED][HIGH] 复制实体第一次到达时初始化 client RoundSim state，仅用于 bootstrap 显示；visual processor 可在 plan 前提交静止实例，plan 到期后才开始 gameplay simulation。

## 算法与指标

[COMPUTED][HIGH] SF1 继续使用整数 Dijkstra、相同障碍膨胀和 swept constraint；flow hash 为 `267519150`，rebuild count 为 1。

[COMPUTED][HIGH] SF2 PBD 使用稳定 spatial grid candidate pairs，最终按 `(MinAgentId, MaxAgentId)` 排序；固定 3 次迭代、24cm pair/iteration cap、equal-mass half correction，并在 PBD 后 obstacle reproject。

[COMPUTED][HIGH] 指标区分 checkpoint 后误差、correction interval 应用前误差、跨轮增长，以及 pair correction、agent total correction、obstacle reproject delta 和 final safety delta。

## 2026-07-11 验证结果

[COMPUTED][HIGH] Development、DebugGame Editor 和 5 项 `CrowdDemo.SF` 自动化通过：parser、PBD pair set/乱序、PlanApply boundary/formation/future-plan、跨轮误差、500→5×100 checkpoint 重建。

[COMPUTED][HIGH] SF2 连续两轮有效结果：

| 规模 | Round | overlap p95 | severe p95 | goal | corridor | deadlock | correction interval p95 | checkpoint p95 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 20 | 1 | 8 | 0 | 19 | 19 | 1 | 29.000cm | 0cm |
| 20 | 2 | 6 | 0 | 19 | 19 | 1 | 13.601cm | 0cm |
| 100 | 1 | 166 | 33 | 93 | 98 | 1 | 33.106cm | 0cm |
| 100 | 2 | 167 | 33 | 96 | 98 | 1 | 32.249cm | 0cm |
| 500 | 1 | 870 | 186 | 305 | 351 | 93 | 142.225cm | 0cm |
| 500 | 2 | 511 | 103 | 198 | 351 | 93 | 85.376cm | 0cm |

[COMPUTED][HIGH] 三档第一轮 initial overlap 均为 0；readiness 为 20/100/500 agents 与 visible instances 精确匹配；两轮 server/client obstacle penetration 均为 0；checkpoint error 不跨轮扩散。500 的传输每轮满足 header=1、chunks=5/5、assembly=1、queued=1、applied=1，无 mismatch、timeout、revision gap 或 VIOLATION。

[COMPUTED][HIGH] SF2 500 容量验收失败，100 也存在明显 severe overlap；这是 reactive separation/PBD 的能力边界，不由 checkpoint 修复掩盖。

## 录像结论

[COMPUTED][HIGH] 最终录像目录：`Saved/CrowdDemoCapture/CrowdDemoCapture_8281_20260711_164509`（SF1 500）、`...8282_20260711_164623`（SF2 20）、`...8283_20260711_164731`（SF2 100）、`...8284_20260711_164838`（SF2 500）。

[COMPUTED][HIGH] 四段录像均从 round 1 前开始，亮度均约 151，contact sheet 可见完整阵型与实际运动；无桌面误捕、近黑或隐藏实例。人工可见 20 基本通过，100 有局部堆积，500 有大面积拥塞与滞留；未见穿墙。

[COMPUTED][HIGH] FFmpeg 的 `gdigrab hwnd=` 对当前 Unreal 硬件 swapchain 输出空白，因此实现改为：用 `MainWindowHandle` 重新获取、定位并置顶 Unreal client，再捕获该固定 desktop region。此项没有按原计划使用 HWND 作为 FFmpeg 输入，但避免了桌面/旧窗口误捕。

## 2026-07-12 SF4 Steering-first Mass 接入停点

[COMPUTED][HIGH] `SimRoundPursuitPositioning` 正式 fixed-step 链已切换为 `PositionCandidateBuild → PositionAssignment → HoldingCandidateBuild → HoldingCompatibilityBuild → HoldingAssignment → CommitRequestBuild → CommitGateSchedule → SteeringStateBoundaryApply → FlowPreferredVelocity → SteeringFirstPositionGuidance → MovementIntentCompose → DeterministicORCA`。旧 Polar Approach、Front Admission、Phase Reservation、WaitGraph 与旧 PursuitPositionGuidance 源码保留，但不再由 SF4 正式链调用；旧 FrontApproachPhase 不再生成 SF4 route-aware ORCA ownership。

[COMPUTED][HIGH] 新增最小 `FCrowdDemoPursuitSteeringStateFragment` 保存跨 fixed-step 的 state、Holding owner、Position 引用、revision、WaitEpoch 与进度事实；Holding candidates、compatibility、assignments、commit requests/decisions 和 steering guidance 保存在 PipelineSubsystem prepared SoA，并纳入 correction rollback。

[COMPUTED][HIGH] Development 与完整 `CrowdDemo.SF` 28/28 通过。Static Small 单轮双端四类新 hash 一致，checkpoint p95=0.055cm、interval p95=0.063cm、revision gap=0、最终障碍 penetration=0、客户端 agents=visible=20，旧 Phase Reservation request/grant/held 均为0。

[COMPUTED][HIGH] Static 技术安全门未通过：corridor exit=10/20，首个硬失败为 corridor。能力结果为 HoldingAssigned=20、Holding=10、Reacquire=10、Commit=0、StableOccupied+ReserveHold=0/20、commit_invalid=9010。SF4 未完成；未运行 DebugGame、正式两轮或录像。

### Compatibility proof 修复后的新停点

[COMPUTED][HIGH] 9010次 Reacquire 已唯一归因为 CommitRequest 对 compatibility graph 的二次按键查询返回 rejected；Position invalid、Target revision mismatch 和 key missing 均为0。HoldingAssignment 现携带其实际选择的 compatibility proof/hash，CommitRequest 不再重新选择边。

[COMPUTED][HIGH] 修复后 selected compatibility valid=20、invalid=0、Commit invalid=0、末态 Holding=20，但 corridor=0/20、Commit granted=0，18020次请求全部 Held。技术门仍失败，且当前链从出生点立即进入 Holding、直接 steering 到目标附近 holding point，没有先沿共享 Flow 完成 corridor 路线。

[COMPUTED][HIGH] compatibility graph 同时检测到8100个重复 `(HoldingId, PositionId)` 键；这证明当前 hashed HoldingId 不是唯一身份。selected proof 避免了错误边重取，但没有修复 HoldingId 碰撞。

### 无碰撞 identity 与 Flow→Holding handoff 复测

[COMPUTED][HIGH] HoldingId与PositionId现直接使用各自Shared Flow candidate的StableCellKey，并由TargetRevision隔离生命周期；正式单轮duplicate compatibility keys从8100降为0。

[COMPUTED][HIGH] `ShouldEnterHolding`纯函数使用现有FrontAdmissionHoldRangeCm作为局部handoff envelope：远距离实体保持Pursuit/Shared Flow，只有Flow状态为Reachable且进入局部范围才转Holding。CommitRequest只为Holding/Commit状态生成。

[COMPUTED][HIGH] 复测corridor=20/20、deadlock=0、penetration=0、双端hash match、checkpoint p95=0.051cm、interval p95=0.061cm；末态Pursuit=5、Holding=4、StableOccupied=9、ReserveHold=2，即完成11/20，能力门仍失败。

### 11/20终态只读归因

[COMPUTED][HIGH] 5个Pursuit全部Flow有效且仍在handoff范围外，距assigned Holding为p50=1291.155cm、p95=1332.901cm；preferred forward p50/p95约800.051/800.236cm/s，但ORCA与final forward p50/p95均为0。它们不是因handoff范围判定失效，而是在范围外被ORCA压停。

[COMPUTED][HIGH] 4个Holding距holding target为p50=26.348cm、p95=28.994cm，终态distance-not-ready=0、speed-not-ready=0；四者均已ready但被Commit冲突Held，ready granted=0。

[INFERRED][HIGH] 下一诊断应把Pursuit ORCA约束按对方SteeringState及infeasible/fallback分类，并把Commit冲突拆为target/flow/obstacle/stable blocker/active commit/本轮selected。当前证据不支持扩大handoff range。

### Stable blocker compatibility 缓存修复与 14/20 停点

[COMPUTED][HIGH] Commit Gate 来源拆分证明上一轮4个 ready Holding 全部被 Stable blocker 拒绝；ORCA 来源矩阵同时证明 Pursuit 的主要约束来自 StableOccupied，而不是 handoff 或 Flow 失效。该证据选择了唯一生产分支：让 Holding compatibility 缓存把 StableOccupied/ReserveHold 的稳定身份、位置、半径与 PositionId 纳入输入 hash；输入改变时确定性重建 graph。

[COMPUTED][HIGH] `StableOccupied` 与 `ReserveHold` 继续保留其已经完成的 Holding ownership，不因自身成为后续实体的 blocker 而被错误转入 Reacquire。纯测试覆盖 Stable blocker 使原 selected edge 失效并稳定选择 alternate Holding；Development、`CrowdDemo.SF4.Positioning` 3/3 与完整 `CrowdDemo.SF` 28/28 通过。

[COMPUTED][HIGH] Static Small 两轮完全复现：末态 `Pursuit/Holding/Commit/StableOccupied/ReserveHold/Reacquire=1/2/1/12/2/2`，即能力完成 `14/20`；Commit Stable blocker reject 已由4降为0，但仍有6个实体未进入完成态。两轮 Steering hashes 均为 `1405881816/2939094645/3829879169/1552643084`，AgentState hash均为`630936952`。

[COMPUTED][HIGH] 安全门通过：corridor=20、deadlock=0、双端 obstacle penetration=0、server/client hash match=1、checkpoint p95=0.054cm、correction interval p95=0.064cm、revision gap=0、agents=visible instances=20、坏日志模式=0。能力门 `StableOccupied+ReserveHold=20/20` 失败，实际为14/20，因此SF4 Static能力仍未通过。

[COMPUTED][HIGH] 按停止规则未运行DebugGame或录像，也未进入第二生产修复分支；补证进程因外层命令超时未执行脚本清理，确认两轮结果落盘后已只终止该次8303的server/client进程。

### Residual Capacity 与 State Tail 只读诊断停点

[COMPUTED][HIGH] 新增纯C++ `AnalyzeResidualPositioning`：按AgentId/PositionId/HoldingId稳定排序，在RoundResult最终boundary对未完成Agent与未占Position执行确定性二分图最大匹配，并计算NoStable、NoReserve、逐blocker移除、Target/Obstacle/Flow反事实。结果只进入prepared summary、rollback和RoundResult hash，不写回运动状态。

[COMPUTED][HIGH] Static单轮结果为unfinished=6、remaining positions=136、compatible edges=61410、current/no-stable/no-reserve matching=`6/6/6`、best single blocker gain=0、critical blockers=0、target/geometry limited=`0/0`。前14个Stable/Reserve没有封死剩余容量，因此排除Residual look-ahead分支A。

[COMPUTED][HIGH] 尾状态同时存在四种事实：Pursuit=1，Flow有效且preferred forward=799.846cm/s，但ORCA/final约0；Holding=2，distance-not-ready=2且距Holding p50/p95=1120.771/1172.505cm；Commit=1，arrival error=92.326cm、route-forward ORCA=8.675cm/s、no-progress仅4 steps、Obstacle/PBD correction=0；Reacquire=2，真实原因均为HoldingInvalid。Holding assignment仍只有18，尽管Residual maximum matching为6/6。

[COMPUTED][HIGH] 这些事实不能唯一选择B/C/D/E，也暴露了提示词未列出的“最大匹配有解但当前greedy Holding assignment留下2个无Holding”问题。按唯一分支规则未修改Commit、ORCA、handoff、readiness、assignment或状态转换，未运行DebugGame、修复后单轮、正式两轮或录像。

[COMPUTED][HIGH] Development、Residual Capacity 1/1、Positioning 3/3和完整`CrowdDemo.SF`29/29通过；诊断单轮corridor=20、deadlock=0、双端penetration=0、hash match=1、checkpoint/interval p95=0.054/0.064cm、agents=visible=20、坏日志=0。

### Deterministic Holding Matching真实门控

[COMPUTED][HIGH] 新增独立纯C++整数min-cost max-flow Holding matcher。图保持每Agent固定PositionId，StableOccupied/ReserveHold/Commit合法owner硬锁，Holding/Pursuit为软复用；目标顺序为最大cardinality、最多复用、WaitEpoch公平、最小量化route cost与稳定AgentId/HoldingId决胜。

[COMPUTED][HIGH] 失败fixture`A:{H1,H2}, B:{H1}`由旧贪心1/2修为2/2；输入反序、Commit/Stable/Reserve硬锁、相同cardinality软owner复用、WaitEpoch、Target revision失效和membership owner释放测试通过。solver结果及summary纳入rollback和双端RoundResult hash。

[COMPUTED][HIGH] matcher只在RoundResult最终boundary旁路运行，生产仍使用旧`AssignHoldingPositions()`。首次误接到每fixed-step曾使模拟明显降速，已移除该调用；最终代码没有保留`#if 0`残块。

[COMPUTED][HIGH] 真实Static Round结果为`position_valid=20、greedy=18、fixed-position holding matching=18、joint=18`，matching hash=`4043756401`且双端总hash match=1。生产接入要求18→20，实际未成立，因此没有替换正式assignment。

[COMPUTED][HIGH] 该结果修正了旧Residual语义：允许Agent改配136个未占Position所得的6/6，不能证明“固定Position不变时”有20/20 Holding容量。当前证据指向固定Position↔Holding兼容图本身只有18容量，而不是贪心遗漏增广路径。

### Crowd Transit纯内核与只读诊断边界

[COMPUTED][HIGH] 当前新增`FCrowdDemoJointVelocityKernel`和责任中立`FCrowdDemoOrcaCanonicalPairGeometry`。既有Priority ORCA仍从同一canonical correction派生25%/75%或50%单方约束；纯Joint kernel使用双方耦合relative velocity约束，固定32轮投影、1cm/1cm/s量化、最大8实体、整分量回退和pair唯一spacing owner。

[COMPUTED][HIGH] 该Joint kernel没有进入正式RoundSim processor链。正式SF4仍为`Preferred Velocity → Baseline Priority ORCA → MovementPredict → ObstacleConstraint → Hard PBD → ObstacleReproject → MovementFinalize`；没有Joint velocity替换或额外push。

[COMPUTED][HIGH] 默认关闭的`-CrowdDemoTransitJointDiagnostic`只在RoundResult最终boundary读取prepared ORCA与Movement/Obstacle/PBD/Finalize事实，生成紧凑双端hash和server JSON，不写Mass fragment或运动状态。

[COMPUTED][HIGH] 8328诊断得到完整component=`11 agents/27 pairs`、hash=`3427263131`双端一致；Agent 6速度在ObstacleConstraint从78cm/s降为0。8331最终语义复测保持同一hash和速度链，并明确报告`joint_status=OversizeFallback(5)`；该复测有`revision_gap_total=1`且包含预期oversize VIOLATION，只能作为归因证据。因component超出8且失败位于ORCA下游，shadow与production均未接入，CurrentArchitecture正式pipeline保持不变。

### Obstacle-safe Holding handoff（2026-07-13）

[COMPUTED][HIGH] `-CrowdDemoSf4ObstacleConstraintDiagnostic`默认关闭，只在Round 1最终boundary输出Agent 6的ObstacleId、Start/Proposed、inflated bounds、segment entry/exit t、端点inside、SlideX/SlideY合法性、FlowBounds delta和稳定hash。8332确认现有Holding直线路径命中Obstacle 109，双端hash=`3799203711`。

[COMPUTED][HIGH] 当前Pursuit→Holding转换不再只依赖距离：还必须满足Flow Reachable、目标在FlowBounds内、`Current→Holding` swept clear及endpoint clearance。Holding路径后来失效时退回Pursuit；正式运动安全链和唯一MovementFinalize writer均未改变。

[COMPUTED][HIGH] 8334中Agent 6已进入Commit且109不再阻塞，但整轮physically satisfied仅`16/20`，状态为`0/2/1/6/11/0`。安全门已通过，能力门未通过，因此当前架构不能写成SF4 Static完成。

## Capacity-by-Construction与Joint Clearance Shadow当前边界

[COMPUTED][HIGH] 正式SF4运动链没有变化，仍为`Position/Holding Guidance → Baseline Priority ORCA → MovementPredict → ObstacleConstraint → Hard PBD → ObstacleReproject → MovementFinalize`。`FCrowdDemoJointVelocityKernel`和`-CrowdDemoTransitCapacityShadow`只形成旁路prepared SoA和汇总指标，Joint结果不被MovementPredict消费。

[COMPUTED][HIGH] P0量化间距事实为HardPairDistance=`94cm`、RequiredTransitAperture=`188cm`、BaselinePairDistance=`128cm`、PreferredSpacingGap=`34cm`。Moving实体的soft spacing和Position/Holding的结构间距是两个消费边界。`97/61`仅是扩展纯fixture的理论构造容量；正式Static Small在两个原始候选池生成后消费稳定selected IDs，实际从`150/150`过滤为`71/75`，capacity hash=`1316163284`。

[COMPUTED][HIGH] Shadow每个fixed-step从Commit transit seed派生position、preferred和0.75秒预测窗口。component先收集与同时间轴轨迹直接相关的实体，再仅沿下一fixed-step可能触及硬距离的pair形成hard-safety closure；不再沿完整600cm ORCA pair图传播。多个seed只有共享直接相关或hard-closure实体时才合并。

[COMPUTED][HIGH] Shadow rollback保存完整agents/pairs/components/results、summary、累计hash和solver样本长度。RoundResult复制紧凑component、deficit、violation、yield、solver与hash指标；Coordinator不执行Joint算法。

[COMPUTED][HIGH] 最新完整物理fixture为agents=`4`、total=`20`、satisfied=`16`、count_closed=`1`、hash=`1861497071`：Agent 5 Holding距目标45cm，Agent 8 Commit距目标1370cm，Agent 14 ReserveHold距目标144cm，Agent 15 Holding距目标28cm。旧三实体日志遗漏Agent 14，已废止。

[COMPUTED][HIGH] 8338是已被指标修复取代的历史样本：其violation混合了Joint候选和baseline fallback，且客户端未完成RoundResult。不得再用`1317/1/33`直接归因Joint候选。

[COMPUTED][HIGH] Production Apply不存在；运行脚本只暴露`-TransitCapacityShadow`。Shadow硬失败后没有新增Joint velocity替换、额外push或生产状态，SF1/SF2/SF3、地图/Lighting、网络、fixed-step、PBD、Target exclusion和client visual owner均未修改。

### 2026-07-13 Capacity正式候选接入与time-aligned Shadow收敛

[COMPUTED][HIGH] 正式SF4候选顺序现为`Position Candidate Build → Holding Candidate Build → Transit Capacity Selection → Position Assignment → Compatibility → Joint Position/Holding Assignment`。Capacity selection在完整prepared SoA上一次生成稳定ID子集，再原子过滤两个候选池；容量不足或结果非法时Position assignment不使用原始候选静默回退。

[COMPUTED][HIGH] selection与candidate arrays、assignments一起进入correction rollback snapshot；RoundResult复制position/holding capacity、deficit、applied和selection hash，客户端把它们纳入SF4双端hash比较。8344正式运行得到`71/75`、deficit=`0/0`、applied=`1`、hash=`1316163284`、assignment=`20/20`。

[COMPUTED][HIGH] Joint Shadow仍是确定性多实体迭代修正器，不是完整加权优化器。`PriorityQ8`尚未形成多Transit intent仲裁，34cm preferred gap也仍只有Shadow消费者；不得描述为正式移动持续维护34cm。

[COMPUTED][HIGH] clearance现使用同一0.75秒窗口内的相对轨迹最短距离，不再比较yielding实体1/30秒位置与seed整条0.75秒capsule。`Solved`额外要求time-aligned clearance不超过1cm且transit forward总量严格高于baseline；失败分别记录`ClearanceNotAchieved`和`NoForwardGain`。

[COMPUTED][HIGH] 8344 Shadow为components=`1780`、max=`13`、13–20实体样本=`16`、solved=`529`、IterationLimit=`720`、ClearanceNotAchieved=`228`、NoForwardGain=`303`、solver p95=`0.568ms`。最小失败fixture为3 agents/3 pairs，clearance=`0`，第一残差是2cm/s canonical violation，fixture hash=`785957084`。

[COMPUTED][HIGH] 8344时正式SF4结果保持`StableOccupied+ReserveHold=17/20`，server/client总hash一致、revision gap=0；Shadow没有写MovementPredict或Mass movement fragment。该运行的ring-entry 20cm固定残差和3-agent canonical 2cm/s残差已由后续硬约束修复取代。

### 2026-07-13 Hard clearance与canonical feasibility polish

[COMPUTED][HIGH] time-aligned clearance在求解阶段现为硬相对速度约束：完整缺口按`1/MotionWeight`的mobility比例分配给seed与yielding实体，`TransitClearanceWeightQ8`只控制该约束是否启用，不再缩小必须满足的修正量。连续解与量化修复都复验clearance。

[COMPUTED][HIGH] 每个component完成128次目标/soft-spacing迭代后，追加最多32次确定性feasibility polish；polish不执行Desired回拉或soft spacing，只重复完整clearance、HardPairDistance和canonical half-plane投影。这使后续pair不再永久留下前序canonical残差。

[COMPUTED][HIGH] ring-entry自动化从`ClearanceNotAchieved/20cm`变为`Solved/0cm`；ring-exit与external fixture保持`Solved/0cm`。由8346真实3-agent/3-pair fixture压缩的canonical回归也通过。

[COMPUTED][HIGH] 8347真实Shadow为components=`1780`、max=`13`、solved=`986`、IterationLimit=`169`、ClearanceNotAchieved=`301`、NoForwardGain=`324`、solver p95=`0.729ms`。相对8346，solved从552上升、IterationLimit从670下降，joint candidate clearance max从`93.911cm`下降到`22.099cm`。

[COMPUTED][HIGH] 8347的新最小失败fixture为7 agents/21 pairs，单seed Agent 17，direct relevant=`3`、hard closure=`4`，状态`ClearanceNotAchieved`、fixture clearance deficit=`8cm`、hard/environment/canonical residual均为0、hash=`1810564063`。正式SF4仍为17/20，双端Shadow hash match=`1`、revision gap=`0`。

[INFERRED][HIGH] 当前停止点不是旧ring固定点或3-agent canonical残差，而是必须先判断该7-agent约束集是否存在同时满足速度圆、time-aligned clearance、HardPairDistance与canonical约束的解。完成该fixture归因前不接入Production JointApply。
## 2026-07-13：Elastic Crowd Shadow 当前边界

[COMPUTED][HIGH] `FCrowdDemoElasticCrowdKernel`只负责确定性Preferred调整；`FCrowdDemoElasticShadowKernel`负责科学实验的POD/SoA数据合同、同链Twin-Step、首失败fixture与独立Parallel Rollout。两者均不访问UWorld、UObject或Mass fragment。

[COMPUTED][HIGH] SF4正式处理顺序保持为`FlowPreferredVelocity → PassingBandGuidance → SteeringFirstPositionGuidance → MovementIntentCompose → DeterministicORCA → MovementPredict → ObstacleConstraint → HardSeparationPBD → ObstacleReproject → MovementFinalize`，Production Elastic不存在。

[COMPUTED][HIGH] 仅在`-CrowdDemoElasticCrowdShadow`下，Shadow processor读取同一正式snapshot并调用纯helper生成Baseline/Elastic临时分支；两支都执行`Preferred → ORCA → Predict → Obstacle → PBD1 → PBD2 → PBD3 → Reproject`。Elastic只在ORCA前调整非Source Preferred，临时结果不写正式intent、ORCA fragment、Mass transform或RoundSim state。

[COMPUTED][HIGH] Hard PBD的可选诊断在同一次Solve内暴露三轮中间结果；未开启时的正式调用不变。Shadow对Obstacle同时保存constraint诊断与正式裁剪结果，并为八阶段位置、速度、HardPair、Target、Source forward生成稳定hash。

[COMPUTED][HIGH] Twin-Step把正式snapshot已有的违规分类为`InheritedAtStepStart/SharedByBoth`；只有`ElasticIntroduced/ElasticWorsened`能固定首失败。当前Static Small的约`90cm`初始间距低于`94cm` HardPairDistance，因此step 0 hard overlap不会被误归因给Elastic。

[COMPUTED][HIGH] rollback snapshot覆盖八阶段累计器、连续zero-progress、首失败pin、fixture hash/样本长度与Parallel Rollout世界。RoundResult只复制紧凑metrics和hash；完整fixture仅server写入`Saved/CrowdDemo/<RunId>/elastic_crowd_failure_fixture.json`。

[COMPUTED][HIGH] 8352双端stage、fixture元数据和rollout hash一致；首失败为step 29的Elastic-worsened ORCA stop violation，完整fixture hash=`4215325188`。8351因不同snapshot/阶段比较失效，不再作为当前架构能力证据。

[INFERRED][HIGH] 当前架构结论是实验设施已可信，但Elastic候选未通过生产门；下一算法问题位于ORCA对微小Preferred变化的可行性/回退不连续，而不是继续调Elastic权重。

### Step 29量化漏解修复

[COMPUTED][HIGH] ORCA连续LP之后先执行原有连续解附近3×3量化；仅当该局部搜索为空时，执行确定性几何量化恢复。候选中心来自half-plane上的Preferred投影、half-plane交点和half-plane与MaxSpeed圆交点，每个中心只检查相邻3×3个1cm/s格点。

[COMPUTED][HIGH] 全局候选仍必须满足原速度圆和全部`dot(v-Point,Normal)>=-epsilon`约束；选择顺序固定为距连续解、距Preferred、X、Y。新结果类型为`GeometryRecovered`，Round指标单独累计`FormalLpQuantizedGeometryRecovered`。

[COMPUTED][HIGH] hash 4215325188重放证明连续LP=`ExactFeasible`，旧局部量化=`NoSolution`，几何量化恢复得到非零可行witness；fallback stop不再发生。该修复位于通用Deterministic ORCA纯内核，未修改Elastic kernel、PBD、Obstacle或正式processor顺序。

### 几何量化恢复后的当前运行事实

[COMPUTED][HIGH] 原P0单轮中旧step29 ORCA失败已消失，首失败后移为step40 Reproject HardPair，Agent 8–18的Baseline/Elastic penetration=`3.523/4.189cm`，fixture hash=`410502020`。

[COMPUTED][HIGH] Twin最终Source forward Q15=`18108/18131`，HardPair累计=`189/191`；Parallel Source Q15=`17857/13205`、HardPair=`3/3`、Target=`541/431`、ORCA stop violation=`126/93`。这些指标由同一新运行双端一致复制。

[INFERRED][HIGH] 当前架构停止点已从ORCA量化漏解推进到Reproject之后仍有Elastic-worsened HardPair；Production Elastic继续不存在。
[COMPUTED][HIGH] 2026-07-13 新增的 Shadow-only `PolishReprojectHardPairs()` 位于八阶段链的 Reproject 内部末端。它不修改正式 SF4 processor：对 Reproject 后仍穿透的稳定 AgentId pair，依次测试 equal-mass、A 单侧和 B 单侧修正；每个候选必须重新通过从本 fixed-step 初始位置出发的 swept obstacle/FlowBounds constraint 与 Target exclusion，再按全局 HardPair 违规数、最大 penetration、总 penetration 的整数化字典序选择严格改善结果。无改善候选时保持原结果。

[COMPUTED][HIGH] hash=`410502020` 的 step40 fixture 已固化为 `CrowdDemo.SF4.Elastic.Step40ReprojectHardPairReplay`。1cm JSON 坐标还把 Agent 6–16 的约 `0.61cm` 边缘量化残差显式暴露出来；polish 后量化 fixture 的 HardPair 违规由2降为0，Agent 18保持在 Obstacle 101外，主要剩余修正转移给无墙约束的Agent 8，输入反序保持同一hash。

[COMPUTED][HIGH] 相同P0真实复跑目录为`Saved/CrowdDemo/CrowdDemo_8352_20260713_223511`。正式Mass运动与AgentState hash=`1770349576`保持不变；Shadow Twin最终Reproject累计HardPair从旧`189/191`降为`7/6`，但Parallel Baseline/Elastic仍为`0/3`。因此该polish只修正Shadow实验安全链，不构成Production Elastic或SF4能力通过。

[INFERRED][HIGH] 新首witness为step81、Obstacle阶段、Agent 1–3、hash=`3917118627`，但同一步PBD1至Reproject的HardPair均为0。当前“按最早stage钉住任意HardPair”的诊断会优先选择可被后续设计阶段消除的暂态penetration；它不能被表述为终态安全根因，后续必须把“暂态stage witness”和“最终安全失败”分开。
