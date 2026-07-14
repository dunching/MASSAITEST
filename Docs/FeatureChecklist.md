# MassAI Crowd Demo Feature Checklist

## 当前方向：统一Soft-Pair Pressure（8368已修复，Small在step 155停止）

- [x] [COMPUTED][HIGH] 8368 Agent 4/12完整20实体fixture已固化；环境容量转移与量化感知分配使endpoint/swept达到`94.493cm`，四类安全violation为0。
- [x] [COMPUTED][HIGH] 独立SoftPressure rollback已接入正式correction链；8371连续10次hit、10个replayed step、miss/mismatch=`0/0`，误差p95=`0cm`。
- [x] [COMPUTED][HIGH] candidate/applied hash v2覆盖完整输入合同与MovementFinalize实际RoundSim状态；8371双端hash=`2203959078/1256482314`，fixture hash=`4227770936`，全部匹配。
- [x] [COMPUTED][HIGH] Particle 17/17、完整`CrowdDemo.SF` 42/42、Development与DebugGame Editor通过。
- [ ] [COMPUTED][HIGH] Small在fixed-step 155出现candidate Hard/Swept=`2/2`并停止；首fixture Agent 5/13 endpoint/swept=`93.021cm`，因此20实体能力门未通过。
- [ ] [COMPUTED][HIGH] 因Small安全门失败，连续两轮、独立Round 1、录像、人工审片和SF3/SF4删除均未执行。

- [x] [COMPUTED][HIGH] 参考视频和自然压力传播目标已经登记，且没有把视频效果反推为具体算法。
- [x] [INFERRED][HIGH] 当前数据合同已明确为PhysicalRadius、HardSafetyGap、SoftMargin和Mobility/InverseMass。
- [x] [INFERRED][HIGH] 当前语义明确取消Transit Source、专用穿行状态、Formation Recovery和Shadow/Baseline双路线。
- [x] [INFERRED][HIGH] 概念设计已明确采用少量通用局部规则产生群体现象：共享Flow只提供DesiredVelocity，局部solver统一处理质量响应、粒子/环境Soft压力和Hard安全；靠墙、窄口及穿群只作为fixture，不作为生产分支。
- [x] [INFERRED][HIGH] 环境距离合同已明确为`WallHardDistance=PhysicalRadius+HardSafetyGap`和`WallSoftDistance=WallHardDistance+SoftMargin`；静态环境Mobility为0，法向禁止、切向保留。
- [x] [INFERRED][HIGH] 盒体移除后形成新平衡与开放空间不保证回位的边界已经明确。
- [x] [COMPUTED][HIGH] 粒子pair Particle Constraint Kernel已经实现，P0为30Hz、42cm半径、10cm HardGap、每侧17cm SoftMargin、Mobility=1、主求解8轮、安全闭环8轮、Soft response=`8/s`、soft/hard cap=`8/24cm`、最终1cm/1cm/s量化。
- [x] [COMPUTED][HIGH] Soft按显式fixed-step response施加有限比例压力，允许长期非零且不影响valid；量化后Hard/Swept/Obstacle/Bounds固定闭环不再处理Soft。
- [ ] [COMPUTED][HIGH] SoftMargin尚未作用于Obstacle和FlowBounds；当前环境膨胀仅为`PhysicalRadius+HardSafetyGap`，距墙52–69cm的P0实体没有环境Soft压力。
- [ ] [COMPUTED][HIGH] 正式实体Mobility仍全部为1；1:3质量、零Mobility环境及异构质量压力传播尚未通过真实RoundSim能力门。
- [ ] [COMPUTED][HIGH] Hard pair与环境仍是顺序投影，轴对齐Obstacle只选择SlideX/SlideY；一般二维切向自由度和多pair/环境共同可行闭环尚未实现。
- [ ] [INFERRED][HIGH] 单侧垂直墙、单侧斜墙、双侧墙、窄口和8371完整fixture尚未作为统一粒子—环境合同的失败优先自动化完成。
- [x] [COMPUTED][HIGH] 17项Particle自动化通过，明确包含`Box100InsertRemove`、`OpenInsertRemove`、`RingEntryRollout`、`RingExitRollout`、8368完整`ObstaclePairConflict`、`PostQuantizationSafety`、`InvalidCandidateDoesNotMasqueradeAsApplied`、`HashContract`、`CorrectionReplay`、`SettlingTracker`和95cm Stress Gate。
- [x] [COMPUTED][HIGH] candidate/applied hash、invalid/fallback计数、首失败fixture、连续settling窗口及particle correction rollback已经接入；实际能力指标描述MovementFinalize提交状态，失败candidate只进入诊断。
- [x] [COMPUTED][HIGH] invalid候选会立即发布失败RoundResult并停止本轮；全体Start fallback仅为紧急安全输出，不再继续跑完整30秒。
- [x] [COMPUTED][HIGH] `first_influenced_iteration_max`已替换把solver iteration误称为空间传播层的旧字段；当前不声称已经实现BFS空间传播层。
- [x] [COMPUTED][HIGH] `SimRoundSoftPressure=1`双端生产链与Flow preferred接线正确；Development与完整`CrowdDemo.SF` 42/42通过。
- [x] [COMPUTED][HIGH] Small/Medium/Cohort三张真实地图已生成，实体数为20/100/500，且均包含约定的完整Lighting与预览组件。
- [x] [COMPUTED][HIGH] 正式基础阵型改为128cm附近；95cm初始间距只作为高压Stress Gate，不作为第一能力门。
- [x] [COMPUTED][HIGH] 8368历史失败已由完整20实体fixture复现并关闭；Agent 4/12最终endpoint/swept=`94.493cm`。
- [x] [COMPUTED][HIGH] 8371实际应用fallback状态Hard/Swept/Obstacle/Bounds=`0/0/0/0`；candidate/applied/fixture hash=`2203959078/1256482314/4227770936`且双端一致，checkpoint与correction interval p95=`0cm`、revision gap=`0`、agents=visible=`20/20`。
- [x] [COMPUTED][HIGH] SoftPressure correction rollback同步门通过：hit/replayed=`10/10`、miss/mismatch=`0/0`。
- [ ] [COMPUTED][HIGH] 新首失败pair Agent 5/13在安全闭环后只剩`93.021cm`；沿分离法线的环境容量不足，Small安全门仍失败。
- [ ] [COMPUTED][HIGH] 当前Source仍包含Elastic Transit、Elastic Shadow、Joint Velocity、Priority ORCA和SF4 Position/Holding等重叠路径。
- [ ] [INFERRED][HIGH] 旧兼容路径、metrics、rollback字段、JSON、CLI、自动化和历史场景尚未完成依赖清点与物理删除。
- [x] [COMPUTED][HIGH] DebugGame Editor已通过。
- [ ] [COMPUTED][HIGH] 因Small门失败，100/500、正式两轮、独立Round 1、录像和人工审片未执行；不允许用纯fixture替代真实RoundSim能力证据。

[COMPUTED][HIGH] 下方SF4、Priority ORCA、Joint、Capacity和Elastic项目只保存历史实现与验证事实；其中旧的“当前方向”“下一步”和兼容要求不覆盖本节。

## SF4 Generic Priority ORCA / Yieldable Commit（2026-07-13）

- [x] [COMPUTED][HIGH] PortalPriority 与 LocalPriority 按字典序组合；SF4 Commit/Normal/Yielding 映射已接入 ORCA prepared agents，SF3 LocalPriority 固定为 Normal。
- [x] [COMPUTED][HIGH] 相同优先级 50%/50%，高低优先级 25%/75%；双方 constraint 均保留，历史 route 0%/100% policy 不再进入生产 BuildPair。
- [x] [COMPUTED][HIGH] Stable-only 与 Reserve-only blocker fixture 均 Grant 并写入独立 yieldable mask；Target/Flow/Obstacle、Active/selected Commit 和 joint residual 仍为 hard gate。
- [x] [COMPUTED][HIGH] Priority hash、summary 与物理位移指标支持 rollback，并在 SF4 checkpoint 前执行双端比较。
- [x] [COMPUTED][HIGH] Development、Priority 定向测试、`CrowdDemo.SF4.Positioning` 3/3、完整 `CrowdDemo.SF` 33/33 通过；`git diff --check` 通过。
- [x] [COMPUTED][HIGH] Static 单轮真实产生 asymmetric pairs=`19247`，25%/75% 两侧计数相等，responsibility sum violation=`0`，Priority hash=`617370832` 且双端一致。
- [x] [COMPUTED][HIGH] corridor=`20`、deadlock=`0`、Target crossing=`0`、双端 penetration=`0`、client agents=visible=`20/20`、revision gap=`0`、checkpoint/interval p95=`0.059/0.064cm`。
- [ ] [COMPUTED][HIGH] ORCA safety hard gate 未通过：`stop_violates=427`；最终 PBD/obstacle penetration 为0不能替代 ORCA constraint safety 验收。
- [ ] [COMPUTED][HIGH] 物理能力门未通过：状态及物理满足均为`19/20`，唯一未完成 Agent 6 在 ORCA 后仍有78cm/s速度、final却为0。
- [ ] [COMPUTED][HIGH] live yieldable Stable/Reserve 冲突未触发，计数=`0/0`；当前只能声明 fixture 语义通过，不能声明真实场景已验证该冲突交接。
- [ ] [COMPUTED][HIGH] DebugGame、正式两轮、录像与 contact sheet 未执行；准确原因是 ORCA safety 和物理20/20双门失败。
- [ ] [COMPUTED][HIGH] Agent 6 的 handoff/后处理未修，ORCA 参数、Commit容差、地图和网络设置均未修改。

## SF4未完成实体最终边界诊断（2026-07-13）

- [x] [COMPUTED][HIGH] 只在RoundResult最终boundary捕获未完成实体；不重求ORCA、不写回Mass fragment。
- [x] [COMPUTED][HIGH] 输出AgentId、状态、目标距离、preferred/ORCA/final二维速度、六状态约束来源、Commit拒绝位掩码和no-progress steps。
- [x] [COMPUTED][HIGH] 输入反序hash、厘米量化、完成态排除、per-agent拒绝原因和rollback恢复自动化通过。
- [x] [COMPUTED][HIGH] Development、Positioning 3/3、完整`CrowdDemo.SF`33/33通过。
- [x] [COMPUTED][HIGH] 正式fixture恰为8个Agent，server/client hash=`1989554829`，RoundResult总hash match=1。
- [x] [COMPUTED][HIGH] 归因分组为CommitGate blocker 4、ORCA压速3、Pursuit handoff/guidance dead zone 1。
- [ ] [INFERRED][HIGH] 尚未证明4个CommitGate blocker可安全并发；不得仅凭聚合数量放宽Admission。
- [ ] [COMPUTED][HIGH] 本轮未修改Admission、ORCA优先级、handoff或容差，未运行DebugGame、录像或更大规模。

## Joint Position/Holding生产接入（2026-07-13）

- [x] [COMPUTED][HIGH] 联合求解结果在一个fixed-step boundary原子替换完整Position/Holding prepared SoA；无效结果保留上一份，不部分发布。
- [x] [COMPUTED][HIGH] rollback覆盖完整双assignment、联合结果、Commit residual结果和输入hash；自动化验证correction replay恢复一致。
- [x] [COMPUTED][HIGH] Commit Gate逐候选加入未来Stable blocker并检查剩余联合最大基数；同一boundary后续候选可见此前已接受Grant。
- [x] [COMPUTED][HIGH] Development、联合规划1/1、Positioning 3/3、完整`CrowdDemo.SF`33/33通过。
- [x] [COMPUTED][HIGH] Static Round 1正式assignment=20/20、unmatched=0、Reacquire=0、重复Holding/Position=0；双端joint/residual hash一致。
- [x] [COMPUTED][HIGH] corridor=20、deadlock=0、Target exclusion crossing=0、双端penetration=0、revision gap=0、checkpoint/interval p95最大=0.047/0.066cm、agents=visible=20、ghost owner=0、坏日志=0。
- [ ] [COMPUTED][HIGH] 最终StableOccupied+ReserveHold仅9/20；状态末态为Pursuit/Holding/Commit/Stable/Reserve/Reacquire=`1/6/1/3/9/0`，SF4能力门未通过。
- [ ] [COMPUTED][HIGH] DebugGame、正式两轮、录像和人工审片未运行；按停止门不得在本轮顺手修改ORCA或Commit行为。

## Hall Geometry与Joint Planner（2026-07-13）

- [x] [COMPUTED][HIGH] 正式量化segment-circle fixture覆盖安全、相交、相切、两端点、self、duplicate、stale、反序hash及正式分类一致性。
- [x] [COMPUTED][HIGH] Agent 5/P2834最佳H2150、blocker Agent 6/P2835、required=94cm、actual=58.078cm、margin=-35.922cm；150个Holding均无非负worst-margin。
- [x] [COMPUTED][HIGH] OwnerRelease Stable/Reserve/Commit=`18/18/18`；Physical Stable/Reserve removal=`20/18`；完整Hall deficiency=2。
- [x] [COMPUTED][HIGH] 分支B Joint Planner纯fixture达到20/20，hard locked=15、reused=18、Holding/Position重复=0，Agent 5改配H2843/P2886。
- [x] [COMPUTED][HIGH] Commit residual纯fixture：5个候选全部remaining/residual=4/4，双端hash一致。
- [x] [COMPUTED][HIGH] Development与完整`CrowdDemo.SF`33/33通过；只读Static保持corridor=20、deadlock=0、双端penetration=0、checkpoint/interval p95=0.054/0.064cm、agents=visible=20、坏日志=0。
- [ ] [COMPUTED][HIGH] Joint Planner和Commit residual尚未接入生产；正式能力仍为14/20，不能标记SF4完成。
- [ ] [COMPUTED][HIGH] DebugGame、生产修复后Static、正式两轮、录像和人工审片未运行；停止原因是分支B本轮只允许纯fixture。

## Holding Hall-deficiency 诊断（2026-07-13）

- [x] [COMPUTED][HIGH] 精确最小Hall集合、完整邻接Holding、逐边拒绝原因和稳定hash纯kernel已实现；输入反序与Stable/Reserve/Commit释放反事实测试通过。
- [x] [COMPUTED][HIGH] 诊断只在RoundResult最终boundary运行，completed-round fixture独立快照并落盘JSON；不写回生产assignment或Steering状态。
- [x] [COMPUTED][HIGH] Development、HoldingHall 1/1、Positioning 3/3、完整SF 31/31通过。
- [x] [COMPUTED][HIGH] Static Round 1安全事实：corridor=20、deadlock=0、双端penetration=0、hash match=1、checkpoint/interval p95=0.054/0.065cm、agents=visible=20，未发现Fatal/Assertion/Ensure/LogWindows Error或独立VIOLATION日志。
- [x] [COMPUTED][HIGH] 最小证书为Agent 5、Position 2834、available Holding=0、deficiency=1、hash=4262261310；释放Stable/Reserve/Commit得到15/18/18，均未恢复20。
- [ ] [COMPUTED][HIGH] 生产修复未授权且未实施；当前仅能确定固定Position compatibility为空，尚未证明应修改过滤规则还是进行Position+Holding联合设计。

## SF4 Steering-first Holding/Commit纯内核（2026-07-12）

- [x] [COMPUTED][HIGH] 新增Pursuit/Holding/Commit/StableOccupied/ReserveHold/Reacquire纯状态合同，未改Mass fragment。
- [x] [COMPUTED][HIGH] Holding candidates来自真实Small Flow raster、clearance、Target revision和Position candidates；数量不少于20。
- [x] [COMPUTED][HIGH] Holding候选free/reachable/clearance、Target exclusion、Position安全圆和pair spacing门通过。
- [x] [COMPUTED][HIGH] HoldingId、candidate顺序和hash对Position输入反序稳定。
- [x] [COMPUTED][HIGH] 20个Position均存在合法Holding→Position compatibility，20个Agent获得唯一Holding owner。
- [x] [COMPUTED][HIGH] compatibility拒绝Target crossing、障碍/不可达和Stable/Reserve blocker。
- [x] [COMPUTED][HIGH] existing assignment重复调用20/20复用；Agent/Holding/compatibility倒序不改变assignment hash。
- [x] [COMPUTED][HIGH] Commit短segment两两无冲突；未ready Waiting未来路径不占资源。
- [x] [COMPUTED][HIGH] WaitEpoch稳定优先，输入倒序不改变Commit decision hash。
- [x] [COMPUTED][HIGH] Holding与Commit owner removal释放ownership且无ghost；Position失效和Target revision变化进入Reacquire。
- [x] [COMPUTED][HIGH] Holding guidance不继续指向goal Flow、到位停速；Commit只指向Assigned Position并限制MaxSpeed。
- [x] [COMPUTED][HIGH] Development、3项SF4 Positioning测试、独立Holding/Commit测试和diff门通过。
- [ ] [COMPUTED][HIGH] Mass processor integration、prepared SoA、rollback和双端hash未实现。
- [ ] [COMPUTED][HIGH] Static 20/20未验证；当前正式旧pipeline仍为1/20。
- [ ] [COMPUTED][HIGH] 旧Phase Reservation尚未删除或禁用。

## Steering-first Holding/Commit架构修订（2026-07-12，文档阶段）

- [x] [COMPUTED][HIGH] 最终目标重新确认为随机/非规则地形中的“共享Navigation/Flow＋连续Steering/ORCA＋最小Position容量调度”，而不是人工道路网络或逐实体完整路径规划。
- [x] [COMPUTED][HIGH] 保留边界：Shared Flow、Candidate/Assignment、Deterministic ORCA、Obstacle Constraint、Hard PBD、prepared SoA、fixed-step rollback、双端hash和Client Visual只读消费。
- [x] [COMPUTED][HIGH] 停止扩展边界：Polar Approach、current/requested polyline reservation、Wait-For生产调度和更多Route-Aware ORCA责任特例。
- [x] [COMPUTED][HIGH] Portal已重新定义为证据门控的可选硬瓶颈增强；普通随机障碍和同向追逐不要求人工Opened/Closed通路。
- [x] [INFERRED][HIGH] 目标状态机修订为`Pursuit → Holding → Commit → StableOccupied/ReserveHold → Reacquire`，Waiting必须有明确Holding Position。
- [x] [INFERRED][HIGH] UE官方MassNavMesh/ZoneGraph/MassCrowd只作为MoveTarget、ShortPath、Waiting Slot和Acquire/Release参考，不直接替换双端30Hz正式pipeline。
- [ ] [COMPUTED][HIGH] Holding Candidate/Assignment纯kernel尚未实现。
- [ ] [COMPUTED][HIGH] Commit Gate纯kernel、稳定排序和冲突集合尚未实现。
- [ ] [COMPUTED][HIGH] Holding/Commit prepared SoA、fragment、rollback和双端hash尚未实现。
- [ ] [COMPUTED][HIGH] Static Small `StableOccupied+ReserveHold=20/20`尚未通过；当前代码能力仍为`1/20`。
- [ ] [COMPUTED][HIGH] membership失效、Portal token/Position/Holding释放、survivor preservation和Reserve promotion生产链尚未实现。

## 当前代码保留事实

- [x] [COMPUTED][HIGH] 两阶段Phase Reservation生产接入、安全门、Wait-For Graph和Reservation-Aware ORCA fixture基础设施仍存在于工作区，作为失败基线与诊断证据。
- [x] [COMPUTED][HIGH] 当前文档没有把Steering-first修订写成已实现代码，也没有把Phase Reservation失败结果改写为通过。
- [ ] [INFERRED][HIGH] 代码迁移前必须先冻结可复现基线并保护用户已有SF3 Lighting地图及其他未提交文件。

## SF4 Reservation-Aware ORCA归因（2026-07-12）

- [x] [COMPUTED][HIGH] fixture约束按稳定order/AgentId/量化point-normal排序并生成输入反序一致hash。
- [x] [COMPUTED][HIGH] 实现30cm/s正向half-plane及确定性不可再缩减约束核心。
- [x] [COMPUTED][HIGH] 实现ActiveRouteConflict、ActiveRouteDisjointContained、ActiveRouteDisjointOutsideCorridor分类。
- [x] [COMPUTED][HIGH] 实现1/30秒、4cm corridor、1cm pair安全余量和Target exclusion单步验证。
- [x] [COMPUTED][HIGH] RoundResult边界诊断只执行一次；完整fixture仅计划写入Saved，不复制timeline或20实体全集。
- [x] [COMPUTED][HIGH] Development、两项`CrowdDemo.SF4.Positioning`与`git diff --check`通过。
- [ ] [COMPUTED][HIGH] 正式捕获无合法fixture：`valid=0, too_large=0, primary=-1, agents=0, core=0`，JSON未生成并触发VIOLATION。
- [ ] [COMPUTED][HIGH] 无唯一证据选择分支A/B/C，故三项生产分支均未实施。
- [ ] [COMPUTED][HIGH] 本次诊断安全门因fixture VIOLATION失败，未运行后续Static能力门、DebugGame、完整SF、两轮或录像。

## SF4 Reservation Wait-For Graph诊断（2026-07-12）

- [x] [COMPUTED][HIGH] 精确记录scheduler blocker pair，按AgentId稳定构建wait edge、SCC、reciprocal edge、stalled/progressing/stale分类和最小fixture。
- [x] [COMPUTED][HIGH] wait graph在当前步ORCA telemetry完成后分析，server/client hash一致；correction rollback不重复累计。
- [x] [COMPUTED][HIGH] 自动化覆盖输入反序hash、cycle/SCC、reciprocal、phase计数、stalled/progressing、missing owner、重复分析不累积和atomic-safe集合。
- [x] [COMPUTED][HIGH] 纯ORCA route-forward fixture覆盖连续/量化可行性及Active/Waiting/Stable/Other来源移除归因。
- [x] [COMPUTED][HIGH] 分支A排除：3个cycle中atomic-safe cycle=0；分支B排除：stale owner=0；分支D排除：cycle=3且stalled blocker=11；选择C。
- [x] [COMPUTED][HIGH] 生产态C采样因correction interval缺失/revision gap违反安全门，相关runtime调用、metrics、rollback字段和双端hash字段已撤销；纯fixture保留。
- [x] [COMPUTED][HIGH] Development、两项`CrowdDemo.SF4.Positioning`和`git diff --check`通过。
- [x] [COMPUTED][HIGH] 最终单轮安全门通过：Target crossing=0、双端penetration=0、六类hash match、checkpoint/interval p95=`0.058/0.064cm`、revision gap=0、agents=visible=20、坏日志=0。
- [ ] [COMPUTED][HIGH] Static能力门失败：StableOccupied+ReserveHold=`1/20`、arrival p95=`1165.730cm`。
- [ ] [COMPUTED][HIGH] DebugGame、完整SF、正式两轮和录像未执行；原因是20/20硬门失败。
- [ ] [INFERRED][HIGH] membership lifecycle仅记录未来边界，尚未实现Portal/Phase释放、assignment失效、survivor保留、promotion、ghost cleanup和双端membership hash。

## SF4 两阶段Phase Reservation生产接入（2026-07-12）

- [x] [COMPUTED][HIGH] request-build、admission eligibility、schedule、boundary apply职责分离；request-build不提前写正式phase。
- [x] [COMPUTED][HIGH] 首次RadialStage及后续AngularAlign/RadialCommit只有Granted后提交；Held/Invalid不进入requested segment。
- [x] [COMPUTED][HIGH] Guidance和ORCA Active route只消费committed phase/current reservation。
- [x] [COMPUTED][HIGH] prepared request/result/decision、fragment最小状态、rollback、累计器与phase decision round hash已接入。
- [x] [COMPUTED][HIGH] Development、两项`CrowdDemo.SF4.Positioning`和diff门通过。
- [x] [COMPUTED][HIGH] Static单轮安全门通过：crossing/双端penetration=0、五类hash match、checkpoint/interval p95<1cm、agents=visible=20、坏日志=0。
- [ ] [COMPUTED][HIGH] Static能力门失败：StableOccupied+ReserveHold=`1/20`、arrival p95=`1165.730cm`。
- [ ] [COMPUTED][HIGH] phase request/granted/held/invalid=`4163/39/4027/117`，route conflict=4027，held steps p95=170，未形成足够phase吞吐。
- [ ] [COMPUTED][HIGH] DebugGame、完整SF、正式两轮和录像未执行；原因是Static 20/20硬门已失败。
- [ ] [COMPUTED][HIGH] Moving Target、攻击、多profile、P1、100/500未开始。

## SF4 两阶段Phase Reservation纯内核（2026-07-12）

- [x] [COMPUTED][HIGH] request同时保存current与requested phase/path；current reservation在整个boundary内先作为不可抢占占用。
- [x] [COMPUTED][HIGH] grant稳定顺序为requested phase降序、commit step升序、AgentId升序；非冲突request可并发grant。
- [x] [COMPUTED][HIGH] held request保留旧占用并继续阻止后续冲突请求；Target exclusion失败进入invalid。
- [x] [COMPUTED][HIGH] Decision hash覆盖全部决策输入与1cm量化路径；输入反序不改变结果或hash。
- [x] [COMPUTED][HIGH] Development、两项`CrowdDemo.SF4.Positioning`和`git diff --check`通过。
- [x] [COMPUTED][HIGH] Mass prepared SoA、fragment状态、boundary apply、rollback与紧凑指标已在后续生产接入完成。
- [x] [COMPUTED][HIGH] Static Small已重跑并通过安全门；能力为1/20，仍未通过。

## SF4 Phase-Local Reservation反证（2026-07-12）

- [x] [COMPUTED][HIGH] phase-local requeue版证明grant可从5提升到13，但requeue=10且完成仍为0。
- [x] [COMPUTED][HIGH] reservation-held版证明简单暂停不安全：Target crossing=1、constraint p95=12。
- [x] [COMPUTED][HIGH] phase-local、held fragment及其fixture已撤销，恢复full-route安全版本。
- [x] [COMPUTED][HIGH] 显式next-phase reservation纯scheduler已实现；生产状态机尚未接入。

## SF4 ORCA来源诊断与动态预测反证（2026-07-12）

- [x] [COMPUTED][HIGH] Radial constraint p95及active/waiting/reserve/stable/other来源已进入紧凑日志。
- [x] [COMPUTED][HIGH] 保留版来源为active=1、reserve-commit=1，其余=0。
- [x] [COMPUTED][HIGH] 动态yielding预测与非冲突active约束省略已完成反证并撤销；原因是infeasible=`410→952`、grants=`5→3`。
- [x] [COMPUTED][HIGH] phase-local两阶段纯scheduler已设计并通过测试；当前full-route生产reservation吞吐仍不足，正式接入尚未实施。

## SF4 Route-Aware ORCA实验（2026-07-12）

- [x] [COMPUTED][HIGH] Active route polyline与yielding安全圆真实相交时，active省略该pair约束、yielding承担100%责任；非冲突pair与SF1/SF2/SF3不变。
- [x] [COMPUTED][HIGH] FrontAssignedWaiting与被Front门阻塞的ReserveCommit均进入yielding分类；Portal ownership仍优先。
- [x] [COMPUTED][HIGH] phase变化重置不可比较的progress bucket；Radial进入阈值为Tolerance+SafetyGap，AngularAlign继续径向纠偏。
- [x] [COMPUTED][HIGH] Radial ORCA/final forward投影已加入紧凑聚合，不输出per-agent日志。
- [x] [COMPUTED][HIGH] 最新单轮corridor/turn=20、deadlock=0、crossing=0、penetration=0、hash match、checkpoint/interval p95=`0.050/0.064cm`。
- [ ] [COMPUTED][HIGH] Static能力仍失败：完成0/20、arrival p95=`1531.321cm`、Radial forward仅83.6cm/s。
- [ ] [COMPUTED][HIGH] 正式两轮、完整SF、DebugGame、录像和提交未执行；原因是单轮硬门失败。

## SF4 Approach entry/phase-lock与ORCA停点（2026-07-12）

- [x] [COMPUTED][HIGH] waiting仅在assigned-position 1200cm entry envelope内可grant；active Approach phase越界后仍锁定compose，Portal ownership优先。
- [x] [COMPUTED][HIGH] no-progress把1cm RouteError bucket下降计为进展；1201/1200边界和五种compose ownership自动化通过。
- [x] [COMPUTED][HIGH] 保留版本corridor/turn=20、deadlock=0、penetration=0、hash match、checkpoint/interval p95=`0.066/0.059cm`。
- [ ] [COMPUTED][HIGH] 完成仍为0/20；Radial preferred/ORCA/final=`800/65.765/65.765cm/s`，radial error p95=`927cm`。
- [x] [COMPUTED][HIGH] 失败的Portal priority复用实验已撤销；其ORCA/final=0且target crossing=1，不进入当前架构。
- [ ] [INFERRED][HIGH] route-aware ORCA冲突/让行契约尚未设计；正式两轮、完整SF、DebugGame、录像和提交继续停止。

## SF4 Admission并发与RadialStage诊断（2026-07-12）

- [x] [COMPUTED][HIGH] active route作为占用路径参与冲突检查，不再全局禁止新grant；waiting route按稳定顺序形成确定性贪心极大无冲突集合。
- [x] [COMPUTED][HIGH] 自动化覆盖active/无冲突waiting并发、active/冲突waiting阻止和输入反序hash；Development与两项`CrowdDemo.SF4.Positioning`通过。
- [x] [COMPUTED][HIGH] RadialStage新增preferred/ORCA/final speed、radial error、真实改善、量化stall和compose boundary switch聚合诊断。
- [x] [COMPUTED][HIGH] 单轮安全与基础路线保持：flow hash=`267519150`、unreachable=0、corridor=20、deadlock=0、双端penetration=0、agents=20。
- [ ] [COMPUTED][HIGH] 能力门失败：StableOccupied+ReserveHold=`0/20`，waiting/RadialStage=`14/2`，arrival p95=`1199.163cm`，compose boundary switches=`295`。
- [ ] [COMPUTED][HIGH] 正式两轮、完整SF、DebugGame、录像和提交均未执行；原因是单轮结果低于既有16/20基线。

## SF4 Target-Aware Approach Gate单轮门（2026-07-12）

- [x] [COMPUTED][HIGH] Target exclusion、OuterGate确定性派生、Gate bounds/reachable/clearance、polar arc、radial commit blocker检查已进入纯kernel。
- [x] [COMPUTED][HIGH] Route失效明确输出None，不回退direct-to-slot；blocker输入乱序与route hash稳定。
- [x] [COMPUTED][HIGH] Processor区分FrontAssignedWaiting、RadialStage、AngularAlign、RadialCommit、SlotCommit和StableOccupied；progress按量化RouteError更新，不再使用绝对180-step timeout。
- [x] [COMPUTED][HIGH] 单轮Target crossing=0、Stable blocked radial commit=0、order inversion=0、corridor=20、deadlock=0、penetration=0、agents=visible=20、坏日志=0。
- [ ] [COMPUTED][HIGH] 单轮StableOccupied+ReserveHold=`0/20`，waiting/radial/angular/radial-commit=`13/2/0/1`，arrival p95=`1197.644cm`，能力门显著回退。
- [ ] [COMPUTED][HIGH] 单轮回退后按规则停止；未运行正式两轮、完整SF、DebugGame或录像，未提交为通过。

## SF4 Static Front Ingress诊断与分支A（2026-07-12）

- [x] [COMPUTED][HIGH] 默认关闭的Ingress diagnostic、量化segment-circle、Target exclusion、Stable/Reserve/Commit blocker分类、alternative Front、ORCA来源与最小fixture已实现。
- [x] [COMPUTED][HIGH] 纯测试覆盖输入乱序、fixture hash、远侧优先、wave稳定、交叉路径拆wave与timeout/requeue。
- [x] [COMPUTED][HIGH] 原行为两轮诊断稳定：Stable阻断4/4、blocker pairs=14、order inversion=3、ORCA来源Stable/Reserve/Commit=`48/9/12`、fixture=`44101239/2`。
- [x] [INFERRED][HIGH] 依据覆盖率选择分支A；B仅1/4、C不满足无几何阻挡、D无局部不可达证据。
- [x] [COMPUTED][HIGH] Front assignment与commit admission已分离为`FrontAssignedWaiting → FrontCommitGranted → SlotCommit → StableOccupied`；固定wave=2并按远侧优先。
- [x] [COMPUTED][HIGH] promotion改为真实Reserve→Front边沿计数；本轮transition/agent=`0/0`，旧7216重复累计已消失。
- [ ] [COMPUTED][HIGH] 分支A正式两轮仅StableOccupied/ReserveHold=`4/3`，SlotCommit=2、ReserveCommit=1，arrival p95=`1196.852cm`，Static能力硬门失败。
- [x] [COMPUTED][HIGH] 双端/两轮AgentState hash=`1583783642`，checkpoint/interval p95=`0.056/0.061cm`，corridor=20、deadlock=0、penetration=0、agents=visible=20、坏日志=0。
- [ ] [COMPUTED][HIGH] 硬门失败后按规则停止；失败分支后未运行完整SF/DebugGame、未录像、未进入Moving/P1/100/500。

## SF4 单Front带/外层Reserve验证（2026-07-12）

- [x] [COMPUTED][HIGH] 失败优先fixture在旧实现上精确失败：Front跨radial band 1–4，且存在Front内侧Reserve。
- [x] [COMPUTED][HIGH] Candidate纯kernel现只把最内侧有效Preferred radial band标为Front，外侧合法带标为Reserve；单项与完整25项`CrowdDemo.SF`通过。
- [x] [COMPUTED][HIGH] Development与DebugGame Editor编译通过，`git diff --check`通过。
- [x] [COMPUTED][HIGH] 两轮candidates/front/reserve=`150/16/134`、assigned=20、candidate overlap/unreachable=0，Candidate/Assignment hash稳定。
- [x] [COMPUTED][HIGH] 两轮双端四类hash一致，checkpoint/interval p95=`0.063/0.066cm`，cross-round growth=0，corridor=20、deadlock=0、penetration=0、agents=visible=20。
- [ ] [COMPUTED][HIGH] StableOccupied+ReserveHold=`12+4=16/20`，剩余4个SlotCommit，arrival error p95=`488.945cm`；Static硬门失败。
- [ ] [COMPUTED][HIGH] 按停止条件未进入Moving Target，未运行P1/100/500，未录像，Stage B未提交为通过。

## SF4 Static Small首次集成失败（2026-07-12）

- [x] [COMPUTED][HIGH] 只在SF4保留PBD后连续reproject位置，关闭低速0.348cm/step被旧整厘米路径清零；SF1/SF2/SF3行为分支未改。
- [x] [COMPUTED][HIGH] 修复后双端hash继续match=1，sim/checkpoint/interval error p95=`0.049/0.049/0.063cm`且不跨轮扩散。
- [ ] [COMPUTED][HIGH] StableOccupied+ReserveHold仍为13/20；7个SlotCommit的ORCA constraint p95=19、速度p95=2.828cm/s，arrival error p95=199.057cm。
- [ ] [INFERRED][HIGH] Front capacity=56且ReserveHold=0，下一步需要单Front带/外层Reserve的纯kernel失败优先设计；尚未实施。

- [x] [COMPUTED][HIGH] SF4 correction rollback已覆盖Portal/Admission/Band/FlowSample、PositionAssignment/PursuitGuidance、TargetFact、prepared SoA与revision；两轮Traffic/Portal/ORCA/AgentState hash一致。
- [x] [COMPUTED][HIGH] correction interval p95从173.220/173.072cm降为0/0cm，checkpoint/cross-round error=0且无VIOLATION。
- [ ] [COMPUTED][HIGH] 连续reproject修复后StableOccupied+ReserveHold仍为13/20，arrival error p95为199.057cm；Static能力门继续失败。

- [x] [COMPUTED][HIGH] scenario=3、最小position fragments、prepared Candidate/Assignment SoA和五个显式processor已形成首版并通过Development/DebugGame编译。
- [x] [COMPUTED][HIGH] Static真实package由非NullRHI Unreal Python创建，六类用户指定Lighting、SkySphere、PreviewFloor和相机均存在。
- [x] [COMPUTED][HIGH] 完整25项`CrowdDemo.SF`通过；SF1/SF2/SF3自动化没有测试失败。
- [x] [COMPUTED][HIGH] 两轮corridor=20、deadlock=0、assigned=20、candidate overlap/unreachable=0、双端penetration=0、agents=visible=20、AgentState跨轮hash一致。
- [ ] [COMPUTED][HIGH] StableOccupied+ReserveHold=`13+0`，未达到20；连续reproject修复后的arrival error p95=199.057cm。
- [x] [COMPUTED][HIGH] 原Traffic/Portal/ORCA双端hash与correction rollback失败已关闭。
- [ ] [COMPUTED][HIGH] Static硬门失败，未录像、未进入Moving、未提交阶段B为通过。

## SF4 Pursuit Positioning 阶段A（2026-07-12）

- [x] [COMPUTED][HIGH] 纯C++ TargetFact、Settings、Candidate、PositioningAgent、Assignment与Summary数据边界已建立。
- [x] [COMPUTED][HIGH] Candidate按Flow raster稳定生成，过滤bounds/blocked/unreachable/clearance，capacity=1，PositionId不依赖输入顺序。
- [x] [COMPUTED][HIGH] 输出Candidate最小中心间距为实体直径+SafetyGap，自动化验证无重叠和PositionId唯一。
- [x] [COMPUTED][HIGH] 固定轮数deferred acceptance使用量化cost与`Cost→ExistingOwner→AgentId→PositionId`决胜，最终按AgentId排序。
- [x] [COMPUTED][HIGH] 20实体获得20个唯一assignment；Front不足转Reserve、20/20 reuse、ReserveHold vacancy promotion和candidate失效release通过。
- [x] [COMPUTED][HIGH] Agent/Candidate输入倒序、同分决胜及两轮Candidate/Assignment hash测试通过。
- [x] [COMPUTED][HIGH] Development、DebugGame Editor、完整25项`CrowdDemo.SF`与`git diff --check`通过；阶段A提交为`bac361f7`。
- [ ] [COMPUTED][HIGH] Static Target Small、scenario=3、processors、fragments、地图、双端metrics和人工审片尚未实施。
- [ ] [COMPUTED][HIGH] Moving Target尚未实施；攻击、NavMesh Bake、P1与100/500不属于当前已通过范围。

## SF3 ORCA/LP Closeout（2026-07-12）

- [x] [COMPUTED][HIGH] 建立统一continuous solve输入、统一half-plane不等式和共享连续结果验证；constraint builder仍只生成一次稳定约束。
- [x] [COMPUTED][HIGH] 固定官方RVO2 commit `b577921d2bc1281a6b721c2d4778f397d37da97d`，Apache-2.0 LICENSE、来源、SPDX和改写边界随测试专用adapter入库。
- [x] [COMPUTED][HIGH] reference adapter仅包含LP1/LP2语义，不引入Simulator、Agent/KdTree、OpenMP或时间推进，不参与正式AgentState输出。
- [x] [COMPUTED][HIGH] 单约束方向、epsilon窄条、平行/近平行、微小残差、真矛盾、速度圆切线、1–24约束矩阵、输入乱序和continuous-only quantization failure差分测试通过。
- [x] [COMPUTED][HIGH] Small差分1052样本：current/reference exact=637/637，双向miss=0，both miss/oracle hit=0；选择保留当前continuous solver。
- [x] [COMPUTED][HIGH] 正式Solve已完全移除Oracle调用与witness写回；自动化断言Oracle invocation=0、witness used=0，reference diagnostic默认关闭。
- [x] [COMPUTED][HIGH] Exact、3×3 quantized recovery、best-effort、StopFeasible和StopViolation口径分离；3×3恢复每轮8962，continuous可行但quantized empty=19。
- [x] [COMPUTED][HIGH] Development、DebugGame Editor、完整24项`CrowdDemo.SF`通过，`git diff --check`通过。
- [x] [COMPUTED][HIGH] 正式Small P0三轮flow unreachable=0、corridor=20、deadlock=0、双端penetration=0、agents=visible=20、11-stage mismatch=0、三轮AgentState hash=`1121342212`、误差与跨轮增长=0。
- [x] [COMPUTED][HIGH] 三轮正式路径Oracle invocation/witness used=0，日志无Fatal/Assertion/Ensure/`LogWindows: Error`/精确VIOLATION。
- [ ] [COMPUTED][HIGH] goal仅10/20；每轮exact无严格可行输出=742、StopViolation=742、target-near non-reached=10。LP收尾技术门通过，但最终目标区群体效果未通过。
- [ ] [INFERRED][HIGH] 下一阶段稳定站位/目标容量仅完成设计登记，尚未实现或验收。
- [ ] [INFERRED][HIGH] 后续离线Crowd Navigation Field仅完成架构登记，尚未执行NavMesh查询、Bake或地图迁移。

## SF3 Flow Reachability Boundary Diagnosis and Recovery（2026-07-12）

- [x] [COMPUTED][HIGH] Flow sample已区分Reachable、OutOfBounds、BlockedRasterCell、UnreachableFreeCell，并携带cell index/stable key/blocked/unreachable/cost/direction。
- [x] [COMPUTED][HIGH] 默认关闭的六阶段reachability诊断、三类有界witness、continuous-legal分类、invalid preferred/final/deadlock和correction rollback已接入；不输出AgentId或per-agent timeline。
- [x] [COMPUTED][HIGH] 纯测试覆盖continuous合法但raster blocked、PBD进入blocked cell、OutOfBounds、free-cell island、nearest reachable稳定选择和SF1 golden hash。
- [x] [COMPUTED][HIGH] 正式修复前两轮均证明7 deadlock=7 final invalid=7 OutOfBounds；7次首次失效全部发生在MovementPredict，blocked/unreachable-free/PBD/reproject均为0。
- [x] [COMPUTED][HIGH] 只采用分支B：SF3 bounds clamp阻止越界分量并保留切向移动；最大navigation-domain reproject=23.267cm，不snap到cell center。
- [x] [COMPUTED][HIGH] 修复后两轮final Reachable=20、invalid=0、flow unreachable=0、corridor=20、deadlock=0、wall/turn=20、双端obstacle penetration=0。
- [x] [COMPUTED][HIGH] Flow build hash=267519150、Portal count=3、Portal geometry regression、全部ORCA/Portal/Holding/Band与完整23项SF自动化通过；Development/DebugGame Editor通过。
- [x] [COMPUTED][HIGH] ORCA口径拆为continuous failure=415、quantization failure=637、Oracle witness used=607、3×3 recovery=8538、Oracle no witness=422；true-no-witness reachable/invalid Flow=`445/0`。
- [x] [COMPUTED][HIGH] 双端Traffic/Portal/ORCA/AgentState hash=`1032547618/2199333646/2957536827/2466489683`，两轮一致；checkpoint/interval/cross-round error=0，agents=visible=20，坏日志模式=0。
- [ ] [COMPUTED][HIGH] goal仍为13/20；7个尾部已集中到目标141.662–286.575cm，当前只具备进入稳定攻击位置独立设计阶段的条件，尚未验收目标容量。
- [ ] [COMPUTED][HIGH] ORCA infeasible/stop violation=445、Oracle quantized witness used=607、reservation timeout=1，均未关闭。
- [x] [COMPUTED][HIGH] diagnostic-only录像/contact sheet生成于`Saved/CrowdDemoCapture/CrowdDemoCapture_8291_20260712_010614`；用户Lighting使亮度正常，可见原Corridor滞留消失且未见穿墙/snap，但目标区构图不完整，不计为站位人工验收。
- [x] [COMPUTED][HIGH] 未修改或提交四张用户光照地图；未修改Flow bounds/build输入、ORCA pair/LP、Portal/Holding/Band、goal/reached、PBD参数、网络、NavMesh或站位系统，未运行P1/Medium/Cohort/Crossing。

## SF3 Half-Plane LP Parallel Numerical Stability Repair（2026-07-12）

- [x] [COMPUTED][HIGH] `36d7dbbc` 中 failure-first 回归在旧 strict-parallel 实现上准确失败；生产精确 fixture 与显式 `+1.82e-7cm/s` 残差均已保留。
- [x] [COMPUTED][HIGH] BehaviorEpsilon=0.1cm/s 未改；Angular（无量纲）、Residual（cm/s）和 Parameter（line parameter）容差已独立定义并由 float epsilon 与速度尺度推导。
- [x] [COMPUTED][HIGH] dot、判别式、Numerator/Denominator 和 interval bound 使用 double 中间值；1cm/s速度量化、Q15 normal、pair几何、最终hash输入未改。
- [x] [COMPUTED][HIGH] 正负微残差、2x/10x容差外冲突、0.01/0.1cm/s真实冲突、近平行角度、输入反序和1–24 constraint Formal-vs-Oracle矩阵通过。
- [x] [COMPUTED][HIGH] Development、DebugGame Editor、ORCA 9/9、完整 `CrowdDemo.SF` 22/22 与 git diff check通过；Portal/Holding/Band自动化未回退，Small Portal count=3。
- [x] [COMPUTED][HIGH] P0 Small两轮 Formal missed→Zero=0、missed→Oracle=0、continuous-only quantized empty=0；parallel 导致的 Formal 连续漏解 recovery 已清零。
- [ ] [COMPUTED][HIGH] Oracle invocation=915；现有 `FormalLpQuantizedRecovered` 聚合没有区分固定3×3恢复与 Oracle quantized witness，且代码仍存在采用后者的路径，因此不能声称正式速度完全不依赖 Oracle。
- [ ] [COMPUTED][HIGH] Formal整体硬门失败：true-no-witness=380、stop-violation=380、ORCA infeasible=380，目标均为0。
- [ ] [COMPUTED][HIGH] 路线门失败：goal=12/20、corridor=12/20、deadlock=7、flow unreachable=7；8个实体位于Corridor/goal 1200cm以上。
- [x] [COMPUTED][HIGH] 两轮及双端Traffic/Portal/ORCA/AgentState/diagnostic hash一致，11-stage mismatch=0，checkpoint/interval/cross-round error=0，overlap/severe p95=0，双端penetration=0，agents=visible=20，坏日志模式=0。
- [x] [COMPUTED][HIGH] diagnostic-only录像/contact sheet已生成于 `Saved/CrowdDemoCapture/CrowdDemoCapture_8291_20260712_002822`；可见Corridor长期停滞，未见穿墙或隐藏实例，不计为人工验收通过。
- [ ] [COMPUTED][HIGH] 不具备进入稳定攻击位置阶段的条件；按门控未运行P1/Medium/Cohort/Crossing，未实现站位。
- [x] [COMPUTED][HIGH] 未修改ORCA行为epsilon、pair几何、责任比例、Portal/Holding/Band/density、goal/reached、参数、地图、网络、NavMesh、PBD或顶层pipeline。

## SF3 Half-Plane LP Missed-Feasible Repair Attempt（2026-07-11）

- [x] [COMPUTED][HIGH] 旧Formal LP失败优先fixture已在修改前失败并独立提交。
- [x] [COMPUTED][HIGH] active boundary epsilon一致性、line-circle interval、line-half-plane clipping、parallel/contradictory和每次更新后的全约束复验已实现。
- [x] [COMPUTED][HIGH] 1cm/s中心量化、固定3×3恢复、连续可行但网格空条带测试通过；未增加epsilon。
- [x] [COMPUTED][HIGH] recovery分类、oracle invocation/耗时、solver p50/p95/max和六种admission state统一agent-step口径已输出。
- [x] [COMPUTED][HIGH] Development、DebugGame Editor和完整19项`CrowdDemo.SF`通过；Portal/Holding/Band回归未在自动化中失败。
- [ ] [COMPUTED][HIGH] Formal LP硬门失败：missed-zero=4087、missed-oracle=3791，目标均为0。
- [ ] [COMPUTED][HIGH] true-no-witness/stop-violation/infeasible=380，目标均为0。
- [ ] [COMPUTED][HIGH] Small能力门失败：goal=12、corridor=12、deadlock=7、flow unreachable=7。
- [x] [COMPUTED][HIGH] 两轮server/client hash、AgentState replay、checkpoint/interval p95=0cm、agents=visible=20、penetration=0、日志坏模式=0。
- [ ] [COMPUTED][HIGH] 情况D停止：Formal LP仍需zero/oracle recovery，不能宣称修复完成，不能进入稳定站位阶段。
- [x] [COMPUTED][HIGH] diagnostic-only录像/contact sheet已生成；约12个实体到达goal approach，仍有8个实体在Corridor停滞，不计为人工验收通过。
- [x] [COMPUTED][HIGH] 未修改goal、Portal/Holding/Band、参数、地图、网络、NavMesh或顶层pipeline，未运行P1/Medium/Cohort/Crossing。

## SF3 Small Goal Congestion / ORCA Feasibility Diagnosis（2026-07-11）

- [x] [COMPUTED][HIGH] goal reached精确事实已审计：二维中心距离<=140cm、轮内sticky；reached实体继续参与TrafficField/ORCA/PBD。
- [x] [COMPUTED][HIGH] 默认关闭`-CrowdDemoSf3GoalCongestionDiagnostic`；flag关闭时完整SF hash/determinism回归通过，flag开启不写回模拟状态。
- [x] [COMPUTED][HIGH] reference oracle覆盖zero、preferred、line projection、line-line与line-circle候选，且不参与正式速度输出。
- [x] [COMPUTED][HIGH] distance bucket、Flow region、zero-feasible、intersection witness、矛盾half-plane、输入乱序和reached仍在ORCA grid测试通过。
- [x] [COMPUTED][HIGH] P0 Small两轮diagnostic hash=`1767999127`；server/client结果一致，原四类hash、AgentState hash和0cm correction不变。
- [x] [COMPUTED][HIGH] 15个non-reached最终为100–200cm:1、200–400cm:6、400–800cm:8、800cm外:0；GoalNear 7、PostCorridor 8。
- [x] [COMPUTED][HIGH] reached仍在ORCA=5，non-reached具有reached neighbor=14/15，reached→non-reached constraints=27430。
- [ ] [COMPUTED][HIGH] 正式LP正确性未通过：zero feasible=7206、oracle feasible=1068、no witness=205；旧multi-empty分类包含大量假空集。
- [ ] [COMPUTED][HIGH] quantization诊断仍有continuous-feasible/quantized-failure=80；本轮未修改量化恢复。
- [ ] [COMPUTED][HIGH] 严格goal容量根因C未成立：只有7/15 non-reached在400cm内，non-reached相互约束占主导。
- [x] [COMPUTED][HIGH] diagnostic-only录像和agent-boost contact sheet已生成；不计为SF3人工验收通过。
- [x] [COMPUTED][HIGH] 本轮未修改行为、参数、地图、网络、Portal/Holding/Band或goal规则，未运行P1/Medium/Cohort/Crossing。

## SF3 Deterministic ORCA Constraint Correctness Repair（2026-07-11）

- [x] [COMPUTED][HIGH] 失败优先 `PairConstraintGeometry` 在旧径向实现上准确失败，随后由标准 cutoff-circle/left-leg/right-leg/penetration 构造修复。
- [x] [COMPUTED][HIGH] pair 坐标、统一 half-plane 方向、50/50 与 25/75 reciprocal responsibility、同位 pair 稳定反向法线和 penetration correction cap 已由纯 kernel 测试覆盖。
- [x] [COMPUTED][HIGH] LP 速度圆、矛盾半平面、单约束排除速度圆、量化后全约束复验、input-order/hash determinism 与既有 canonical scenarios 通过。
- [x] [COMPUTED][HIGH] ORCA constraint kinds、LP 根因、fallback/stop 和 admission-state infeasible 已加入每轮聚合指标；无 per-agent 日志。
- [x] [COMPUTED][HIGH] Development、DebugGame Editor、完整 16 项 `CrowdDemo.SF` 与全部 `CrowdDemo.SF3` 通过；SF1 hash=`267519150`，Portal count=3，geometry hash=`1962319733`。
- [x] [COMPUTED][HIGH] 正式 Small 两轮四类 hash 与 AgentState hash完全一致；checkpoint/interval p95=0cm，cross-round growth=0，双端 penetration=0，agents=visible=20，精确 `VIOLATION`=0。
- [ ] [COMPUTED][HIGH] Small goal 硬门失败：5/20，目标 20/20；corridor=20/20、deadlock=0。
- [ ] [COMPUTED][HIGH] ORCA 能力硬门失败：multi-empty=8479、quantization-destroyed=80、infeasible=8559、fallback-stop=8480、stop-violation=1273。
- [ ] [COMPUTED][HIGH] 人工录像/审片未执行；原因是 Small 自动化能力硬门失败。
- [ ] [COMPUTED][HIGH] P1、Medium、Cohort、Crossing 未运行；按预声明停止条件禁止继续。
- [x] [COMPUTED][HIGH] 本轮未修改 Portal extraction/binding、Holding、PassingBand、density、PBD、ObstacleConstraint、fixed-step、网络参数、地图、NavMesh 或顶层 pipeline 架构。

[INFERRED][HIGH] 最终目的和目标效果见 `DemoPurposeAndTargetEffect.md`；本清单中的 `[x]` 只表示对应代码/测试项通过，不自动表示最终群体效果完成。

## SF3 Portal、Holding 与 ORCA LP Repair

- [x] [COMPUTED][HIGH] Small Portal 从23收敛为3；geometry hash=`1962319733`，合成/真实几何 regression 通过。
- [x] [COMPUTED][HIGH] stable binding、entry-side/span 检查、上游 holding overflow、中心 transit lane、target 无重叠与输入乱序测试通过。
- [x] [COMPUTED][HIGH] PassingBand lateral error feedback、中心零横速、Reserved/Inside 50% clearing minimum 测试通过。
- [x] [COMPUTED][HIGH] ORCA incremental half-plane LP、速度圆、量化格点验证、矛盾约束、grid/brute-force 与场景测试通过。
- [x] [COMPUTED][HIGH] Development、DebugGame Editor、`CrowdDemo.SF` 前缀14项和 `CrowdDemo.SF3` 9项通过。
- [x] [COMPUTED][HIGH] P0 Small 两轮 server/client 四类 hash、AgentState replay、checkpoint/interval p95=0cm、penetration=0、agents=visible=20。
- [x] [COMPUTED][HIGH] corridor=20、deadlock=0、severe p95=0、rebind=0、reservation/transit timeout=0、capacity violation=0。
- [ ] [COMPUTED][HIGH] Holding 最终可见效果未验收：Small 能力门失败后没有录像，不能仅凭target生成单元测试判定完成。
- [ ] [COMPUTED][HIGH] Small goal 门失败：18/20，目标为20/20。
- [ ] [COMPUTED][HIGH] ORCA 能力门失败：infeasible=6017、fallback stop=5904、stop constraint violation=5666。
- [ ] [COMPUTED][HIGH] 人工录像未执行；Small 自动化能力硬门未完全通过。
- [ ] [COMPUTED][HIGH] Medium/Cohort/Crossing/P1 未运行；按停止条件禁止继续。
- [ ] [COMPUTED][HIGH] NavMesh offline Crowd Navigation Field Asset 仅记录为后续架构，本轮未实现。

## SF3 Determinism and Portal Repair 历史基线

- [x] [COMPUTED][HIGH] 11-stage FNV-1a32 diagnostic，默认关闭，只在 correction boundary 输出。
- [x] [COMPUTED][HIGH] correction rollback 恢复 raw state、PortalAdmission、Band、FlowSample、Portal runtime 和 round hash accumulator。
- [x] [COMPUTED][HIGH] persistent token、capacity、60/120-step timeout、清空后换向、DirectionEpoch 与 band 稳定性自动化通过。
- [x] [COMPUTED][HIGH] overlap grid 与 brute-force pair set 一致，输入乱序不改变 pair 顺序。
- [x] [COMPUTED][HIGH] `CrowdDemo.SF` 11/11、`CrowdDemo.SF3` 6/6、Development、DebugGame Editor 通过。
- [x] [COMPUTED][HIGH] P0 Small 两轮 server/client stage hashes、Traffic/Portal/ORCA/AgentState hashes 全部一致。
- [x] [COMPUTED][HIGH] Round 1/2 AgentState hash 都为 `327320670`；checkpoint/interval p95=0cm；penetration=0；agents=visible instances=20。
- [x] [COMPUTED][HIGH] initial overlap=0、overlap p95=1/max=3、severe p95=0/max=0、residual PBD penetration pairs max=10、final obstacle penetration=0。
- [ ] [COMPUTED][HIGH] Small 能力门失败：goal=9/20、corridor=11/20、deadlock=13、ORCA infeasible=5344、fallback stop=4821。
- [ ] [COMPUTED][HIGH] starvation 没有在能力门失败前形成独立有效验收值；不得把默认 0 当作通过。
- [ ] [COMPUTED][HIGH] Medium/Cohort/Crossing 与 SF3 视频未运行；Small 硬门已触发停止。

## SF3 Shared Crowd Traffic Scheduling 基础集成历史记录

- [x] [COMPUTED][HIGH] 独立 `SimRoundCrowdTraffic=2`、parser 0/1/2、SF1/SF2 分支隔离。
- [x] [COMPUTED][HIGH] TrafficField、Portal、PassingBand、ORCA 纯 kernels 与双端 Mass pipeline 已接入；Hard PBD 与 obstacle reproject 复用 SF2。
- [x] [COMPUTED][HIGH] SF3 内核与既有 SF 自动化已扩展为当前的 `CrowdDemo.SF` 11/11 和 `CrowdDemo.SF3` 6/6。
- [x] [COMPUTED][HIGH] 四张 SF3 地图由独立 Unreal Python 脚本创建为真实 package。
- [ ] [COMPUTED][HIGH] Medium、Cohort、Crossing 未运行；原因是 Small 硬门控已失败。
- [ ] [COMPUTED][HIGH] SF3 人工录像未生成；原因是只有四档全通过才允许录像。

## SF1 Shared FlowField

- [x] [COMPUTED][HIGH] `SimRoundObstacle=0`，双端同 processors/kernels。
- [x] [COMPUTED][HIGH] flow hash `267519150`、rebuild 1、整数 Dijkstra、障碍膨胀和 swept constraint 保持不变。
- [x] [COMPUTED][HIGH] 本轮未修改 SF1 算法、correction 频率、复制预算或 NetUpdateFrequency。

## SF2 Separation + Hard PBD

- [x] [COMPUTED][HIGH] 独立 `SimRoundFlowSeparation=1`。
- [x] [COMPUTED][HIGH] SoftSeparation、稳定 PBD pair、3 次迭代、24cm cap、equal-mass half correction、PBD 后 obstacle reproject。
- [x] [COMPUTED][HIGH] spatial-grid pair set 与 brute-force 一致，输入乱序不改变结果。
- [x] [COMPUTED][HIGH] 20/100/500 第一轮 initial overlap 均为 0，双端 obstacle penetration 均为 0。
- [x] [COMPUTED][HIGH] 20 severe p95=0，checkpoint p95=0，不跨轮扩散。
- [ ] [COMPUTED][HIGH] 100 容量未完全通过：severe p95=33，goal=93/96，corridor=98，deadlock=1。
- [ ] [COMPUTED][HIGH] 500 容量失败：severe p95=186/103，goal=305/198，corridor=351，deadlock=93。

## RoundResult 传输

- [x] [COMPUTED][HIGH] RoundResultHeader 不含 AgentState；round end 只采集一份 checkpoint AgentState。
- [x] [COMPUTED][HIGH] checkpoint 复用 correction chunks，chunk size=100；500 每轮 5/5。
- [x] [COMPUTED][HIGH] 支持 header-first、chunks-first、交错乱序与重复 chunk；revision mismatch/missing chunk 自动化覆盖。
- [x] [COMPUTED][HIGH] checkpoint 不作为普通 correction 提前应用，boundary 顺序为 compare→apply→next plan。
- [x] [COMPUTED][HIGH] 三档连续两轮 header/chunks/assembly/queued/applied 完整，无 timeout、mismatch、revision gap 或 VIOLATION。
- [x] [COMPUTED][HIGH] checkpoint 前真实误差不再固定为 0；应用后 p95=0 且不跨轮扩散。

## Readiness、视觉与录像

- [x] [COMPUTED][HIGH] readiness 要求 agents=visible instances=expected 连续稳定 0.5 秒，并检查 Arena/Coordinator/visual owner。
- [x] [COMPUTED][HIGH] ready 后 3 秒开始 round 1；60 秒 timeout 输出 VIOLATION；无 flag 时保持默认行为。
- [x] [COMPUTED][HIGH] client visual 只读取 RoundSim state；pre-plan 只显示静止 bootstrap，不计算 gameplay movement。
- [x] [COMPUTED][HIGH] future plan 不提前占用静止 boundary；Small/Medium 第一轮立即开始运动。
- [x] [COMPUTED][HIGH] 五张地图保留用户放置的照明，固定俯视相机覆盖完整 FlowField 与 500 初始阵型。
- [x] [COMPUTED][HIGH] SF1 500、SF2 20/100/500 最终录像均通过亮度/contact-sheet QA，并覆盖第一轮前和第一轮运动。
- [ ] [COMPUTED][HIGH] FFmpeg 未直接使用 `hwnd=` 输入：该路径捕获 Unreal swapchain 为空白；当前通过 MainWindowHandle 固定窗口后捕获对应 desktop region。

## 构建与仓库边界

- [x] [COMPUTED][HIGH] Development 与 DebugGame Editor 编译通过。
- [x] [COMPUTED][HIGH] 5 项 `CrowdDemo.SF` 自动化通过。
- [x] [COMPUTED][HIGH] SF3 的 ORCA、density speed scaling 与 approach/passing band 保持在隔离场景内；未引入 slot、攻击、timeline trace 或 A/B 框架。

## 长期目标缺口

- [ ] [COMPUTED][HIGH] 自由游荡尚未实现。
- [ ] [COMPUTED][HIGH] 动态目标追逐尚未实现。
- [ ] [COMPUTED][HIGH] 真实 off-thread WORK processor、预算 continuation 与 GT apply/commit 验收尚未完成。
- [ ] [COMPUTED][HIGH] 静态目标附近不聚向单一 goal cell 的最终群体效果尚未完成。
- [ ] [COMPUTED][HIGH] 100/500容量和完整显示的最终联合验收尚未完成。

## SF4 Steering-first Mass Integration（2026-07-12）

- [x] [COMPUTED][HIGH] Steering-first Holding/Commit 纯 kernel 与真实 Small flow fixture 通过。
- [x] [COMPUTED][HIGH] SF4 正式 pipeline 已旁路旧 Approach/Admission/PhaseReservation/WaitGraph 与旧 route-aware ORCA ownership。
- [x] [COMPUTED][HIGH] Steering state、Holding/Commit prepared SoA、四类 hash 与聚合指标已纳入 rollback/结果比较。
- [x] [COMPUTED][HIGH] Development、SF4 integration contract 与完整 `CrowdDemo.SF` 28/28 通过。
- [x] [COMPUTED][HIGH] 单轮双端 hash match=1、checkpoint p95=0.055cm、interval p95=0.063cm、revision gap=0、penetration=0、client agents=visible=20。
- [ ] [COMPUTED][HIGH] 技术安全门失败：corridor=10/20；这是本轮第一硬失败。
- [ ] [COMPUTED][HIGH] 能力门失败：StableOccupied+ReserveHold=0/20，Holding=10，Reacquire=10，commit_invalid=9010。
- [ ] [COMPUTED][HIGH] DebugGame、正式两轮与录像未执行；原因是单轮技术门失败。

### Compatibility proof 后续结果

- [x] [COMPUTED][HIGH] Commit invalid 已从9010降为0；selected compatibility proof valid/invalid=20/0。
- [ ] [COMPUTED][HIGH] Holding identity 未通过唯一性检查：22500条边中存在8100个重复 `(HoldingId,PositionId)` 键。
- [ ] [COMPUTED][HIGH] Flow→Holding handoff 未成立：末态Holding=20，Commit=0，全部18020次请求Held。
- [ ] [COMPUTED][HIGH] 技术门进一步失败为corridor=0/20、goal=0/20；不得将Invalid清零写成Static能力通过。

### Stable identity与handoff gate

- [x] [COMPUTED][HIGH] HoldingId/PositionId唯一性回归通过；正式duplicate compatibility keys=0。
- [x] [COMPUTED][HIGH] 远距离Pursuit继续使用Shared Flow，corridor恢复20/20，deadlock=0。
- [x] [COMPUTED][HIGH] 双端hash、checkpoint/interval误差、penetration、agents/visible技术项通过。
- [ ] [COMPUTED][HIGH] Static能力仅11/20：Pursuit=5、Holding=4、StableOccupied=9、ReserveHold=2。
- [ ] [COMPUTED][HIGH] 分状态route-forward speed、Holding readiness和Held冲突来源尚未形成可信聚合，不能继续调handoff或ORCA。

### Steering终态归因

- [x] [COMPUTED][HIGH] 六种SteeringState的distance与preferred/ORCA/final forward p50/p95已进入RoundResult紧凑指标。
- [x] [COMPUTED][HIGH] Pursuit 5个均Flow有效，preferred forward约800cm/s但ORCA/final为0。
- [x] [COMPUTED][HIGH] Holding 4个均在30cm tolerance内且速度ready，但全部因Commit冲突Held。
- [ ] [COMPUTED][HIGH] Pursuit ORCA约束的对方状态来源尚未细分。
- [ ] [COMPUTED][HIGH] Commit conflict尚未拆分为几何拒绝、Stable blocker、Active Commit和同boundary Selected冲突。

### Stable blocker compatibility 缓存分支

- [x] [COMPUTED][HIGH] Commit Gate已拆分target/flow/obstacle/Stable blocker/Active Commit/Selected冲突，ORCA约束已按双方SteeringState聚合。
- [x] [COMPUTED][HIGH] compatibility输入hash包含Stable/Reserve blocker事实；变化时重建graph，已完成实体保留自身Holding ownership。
- [x] [COMPUTED][HIGH] Stable blocker使旧边失效并选择alternate Holding的纯测试通过；Development、Positioning 3/3、完整`CrowdDemo.SF` 28/28通过。
- [x] [COMPUTED][HIGH] Static两轮安全门通过：corridor=20、deadlock=0、penetration=0、双端/跨轮hash一致、checkpoint/interval p95=0.054/0.064cm、revision gap=0、agents=visible=20、坏日志=0。
- [ ] [COMPUTED][HIGH] Static能力门仍失败：StableOccupied+ReserveHold=14/20；剩余Pursuit=1、Holding=2、Commit=1、Reacquire=2。
- [ ] [COMPUTED][HIGH] DebugGame与录像未执行；原因是能力门未达到20/20。未进入另一生产修复分支。

### Residual Capacity + State Tail Diagnosis

- [x] [COMPUTED][HIGH] 确定性Residual maximum matching、输入倒序hash、Stable关键blocker与非关键blocker测试通过。
- [x] [COMPUTED][HIGH] 诊断仅在RoundResult最终boundary运行，不写运动状态；ResidualCapacityHash进入server/client比较与rollback。
- [x] [COMPUTED][HIGH] Static结果：unfinished=6、remaining positions=136、compatible edges=61410、matching/no-stable/no-reserve=6/6/6、best single gain=0、critical blockers=0。
- [x] [COMPUTED][HIGH] 分支A已排除：已完成实体没有降低Residual maximum matching。
- [x] [COMPUTED][HIGH] Reacquire=2均归因为HoldingInvalid；Commit arrival=92.326cm、no-progress=4 steps、Obstacle/PBD correction=0。
- [ ] [COMPUTED][HIGH] B/C/D/E无法唯一选择：Pursuit ORCA压零、Holding远目标、Commit低速推进、Holding assignment 18/20同时存在。
- [ ] [COMPUTED][HIGH] 未实施生产修复；能力仍为StableOccupied+ReserveHold=14/20。
- [x] [COMPUTED][HIGH] Development、Residual 1/1、Positioning 3/3、完整SF 29/29通过；单轮安全与双端hash门通过。
- [ ] [COMPUTED][HIGH] DebugGame、修复后单轮、正式两轮和录像未运行；原因是唯一归因门失败。

### Deterministic Holding Matching

- [x] [COMPUTED][HIGH] 整数min-cost max-flow纯kernel通过A/B贪心失败、输入反序和稳定hash测试。
- [x] [COMPUTED][HIGH] Stable/Reserve/Commit硬owner、Holding软复用、WaitEpoch、revision失效与membership释放测试通过。
- [x] [COMPUTED][HIGH] matcher只在RoundResult最终boundary旁路执行；rollback与ResidualHoldingMatchingHash已接入。
- [x] [COMPUTED][HIGH] 新Residual语义拆为PositionValid、HoldingMatching、JointFeasible、Greedy和MatchingHash。
- [ ] [COMPUTED][HIGH] 真实生产替换门失败：greedy/matching/joint=`18/18/18`，没有证明新solver可恢复20个Holding owner。
- [ ] [COMPUTED][HIGH] 未替换正式`AssignHoldingPositions()`，能力基线仍为14/20。
- [ ] [COMPUTED][HIGH] 量化完整JSON fixture未生成；本轮有效证据保存在8307双端日志和RoundResult指标中。
- [ ] [COMPUTED][HIGH] DebugGame、正式两轮与录像未运行；生产接入前置门失败。

### Crowd Transit + Adaptive Spacing

- [x] [COMPUTED][HIGH] 四层距离语义已拆分为PhysicalRadius、HardSafetyGap、PreferredSpacingGap、AssignedSpacing与量化ContextScale；Preferred不得低于Hard。
- [x] [COMPUTED][HIGH] Joint component内pair由JointSolver唯一拥有spacing，component外才允许SoftSeparation；double owner测试拒绝并计数。
- [x] [COMPUTED][HIGH] canonical pair geometry是Priority ORCA与Joint relative constraint的唯一几何来源；point/normal/kind/量化重构测试通过。
- [x] [COMPUTED][HIGH] AdaptiveSpacing 1/1、JointVelocity 1/1、SF4 Positioning 3/3、完整SF 35/35和Development通过。
- [x] [COMPUTED][HIGH] 纯fixture覆盖追赶、五实体环进入/离开、外部固定速度、大半径、多人协同、合并component、oversize、退化normal、反序、量化修复/失败和Assigned恢复。
- [x] [COMPUTED][HIGH] Agent 6最终boundary速度链已定位：ORCA/Predict=78/78cm/s，Obstacle/PBD/Reproject/Final=0/0/0/0cm/s，首次归零为ObstacleConstraint。
- [x] [COMPUTED][HIGH] 诊断双端hash一致：`3427263131`；JSON紧凑summary已生成，未截断11实体闭包。
- [x] [COMPUTED][HIGH] 8331语义复测将oversize明确报告为`OversizeFallback(5)`，但该运行有`revision_gap_total=1`和计划内oversize VIOLATION，只作为归因证据。
- [ ] [COMPUTED][HIGH] Local component门失败：真实闭包=`11 agents/27 pairs`，超过第一版最大8；未求真实Joint velocity。
- [ ] [COMPUTED][HIGH] Shadow未运行；原因是oversize且下游ObstacleConstraint归零，前置门不成立。
- [x] [COMPUTED][HIGH] 默认关闭的SF4 ObstacleConstraint最终boundary诊断记录ObstacleId、Start/Proposed、inflated bounds、entry/exit t、端点inside、SlideX/SlideY、FlowBounds delta和稳定hash。
- [x] [COMPUTED][HIGH] 8332双端确认Agent 6当前段与完整Holding段命中Obstacle 109，fixture hash=`3799203711`，排除FlowBounds归零。
- [x] [COMPUTED][HIGH] Pursuit→Holding现要求reachable、handoff range、swept-clear和endpoint-clear；Holding路径失效时退回Pursuit，未放宽ObstacleConstraint。
- [x] [COMPUTED][HIGH] 8334安全门通过：target crossing=0、双端penetration=0、severe p95=0、corridor=20、deadlock=0、revision gap=0、误差<1cm、agents=visible=20、双端hash一致。
- [ ] [COMPUTED][HIGH] Static能力仍失败：physically satisfied=`16/20`，状态顺序`Pursuit/Holding/Commit/Stable/Reserve/Reacquire=0/2/1/6/11/0`，完整未满足Agent为5、8、14、15；旧三实体列表已废止。
- [ ] [COMPUTED][HIGH] Production JointApply未接入；正式processor顺序与运动行为保持不变。
- [ ] [COMPUTED][HIGH] Static能力仍为physically satisfied=19/20、goal=4、corridor=20、deadlock=0；穿行能力未通过。
- [ ] [COMPUTED][HIGH] DebugGame、正式两轮、录像、100/500、Moving Target、玩家、攻击和死亡均未执行。

### Capacity-by-Construction与Joint Clearance Shadow

- [x] [COMPUTED][HIGH] 最终boundary物理未满足fixture覆盖全部20实体，得到satisfied/unsatisfied=`16/4`、count closed=`1`、hash=`1861497071`，包含displaced Reserve Agent 14。
- [x] [COMPUTED][HIGH] P0 Hard/Required/Baseline/PreferredGap=`94/188/128/34cm`，公式、不同半径、不同transit radius、yield不足与输入反序测试通过。
- [x] [COMPUTED][HIGH] 扩展纯fixture按128cm结构间距得到Position/Holding理论构造容量=`97/61`、hash=`4238641562`；该数字不是正式Static Small的实际候选数。
- [x] [COMPUTED][HIGH] Transit swept capsule、component 2/5/8/12/20、二维让行、Obstacle/FlowBounds/Target复验、量化与反序纯测试通过。
- [x] [COMPUTED][HIGH] 默认关闭的`-CrowdDemoTransitCapacityShadow`已接入，且不消费Joint结果；rollback覆盖prepared SoA、summary、hash和solver样本长度。
- [x] [COMPUTED][HIGH] Development、Transit 2/2、Positioning 3/3、完整SF 35/35通过；默认关闭路径未发生自动化回退。
- [x] [COMPUTED][HIGH] 8338的混合violation与revision gap只保留为历史诊断；8344已拆分candidate/baseline、恢复双端总hash一致并达到revision gap=`0`。
- [x] [COMPUTED][HIGH] max yield=`26.669cm`未超过30cm；oversize、numerical、quantized、flow-bounds与double-owner failure均为0。
- [ ] [COMPUTED][HIGH] Production JointApply未实现、未暴露运行脚本入口、未改变正式processor输出；当前阻塞项已推进为8347七实体clearance可行性，而不是旧ring-entry固定点。
- [ ] [COMPUTED][HIGH] DebugGame、正式两轮、录像、100/500、Moving Target、玩家、攻击和死亡未执行，符合失败即停规则。

### Capacity正式候选与time-aligned Shadow复审（2026-07-13）

- [x] [COMPUTED][HIGH] 正式顺序已调整为`PositionBuild → HoldingBuild/CapacitySelection → PositionAssign → Compatibility → JointAssign`；Static Small原始`150/150`候选被稳定selected IDs过滤为`71/75`，deficit=`0/0`、hash=`1316163284`、applied=`1`。
- [x] [COMPUTED][HIGH] selection、过滤后的prepared SoA、summary和hash已纳入correction snapshot/rollback；正式Position/Holding assignment得到`20/20`。
- [x] [COMPUTED][HIGH] local component只沿time-aligned transit relevance和下一fixed-step可达的hard-safety closure扩展，不再沿全部600cm ORCA邻居图传递闭包。
- [x] [COMPUTED][HIGH] seed与yielding实体统一使用0.75秒相对轨迹最短距离；`Solved`现在要求clearance deficit不超过1cm且transit forward总量严格高于baseline。
- [x] [COMPUTED][HIGH] 8344 Shadow共1780个component-step：solved=`529`、IterationLimit=`720`、ClearanceNotAchieved=`228`、NoForwardGain=`303`、HardInfeasible=`0`；最大component=`13`。
- [x] [COMPUTED][HIGH] 8344正式结果保持`StableOccupied+ReserveHold=17/20`，server/client总hash一致、revision gap=`0`、selection hash一致；Shadow未写入MovementPredict或Mass movement fragment。
- [x] [COMPUTED][HIGH] ring-entry、ring-exit和external fixture均为`Solved`且time-aligned clearance=`0cm`；clearance完整投影不再被Preferred回拉稳定在20cm残差。
- [ ] [COMPUTED][HIGH] `PreferredSpacingGapCm=34`仍只由Shadow消费；PriorityQ8多seed仲裁、环境可行域内求解和Production JointApply均未实现。

### Hard clearance与canonical polish复审（2026-07-13）

- [x] [COMPUTED][HIGH] clearance修正量不再乘以`Relaxation×Weight`；MotionWeight只分配seed/yielding双方责任，连续与量化候选均复验time-aligned clearance。
- [x] [COMPUTED][HIGH] 新增32次hard feasibility polish，不执行Desired回拉或soft spacing；8346三实体fixture的2cm/s canonical残差已由紧凑回归覆盖并消失。
- [x] [COMPUTED][HIGH] Development Editor、`CrowdDemo.SF4.Transit` 2/2、完整`CrowdDemo.SF` 35/35通过。
- [x] [COMPUTED][HIGH] 8347双端Shadow hash一致、revision gap=`0`，正式末态仍为17/20，确认Shadow没有改变正式运动。
- [ ] [COMPUTED][HIGH] 8347仍有794个失败component-step；新最小fixture为7 agents/21 pairs、clearance deficit=`8cm`、hash=`1810564063`，其硬可行性尚未证明。
- [ ] [COMPUTED][HIGH] 因七实体fixture未归因，不运行DebugGame、正式两轮、录像、多seed、Production Apply、100/500或Moving Target。
## SF4 Elastic Crowd Shadow科学复审（2026-07-13）

- [x] [COMPUTED][HIGH] 8351已标记为历史无效实验：它没有从每步同一snapshot执行同一安全链，且比较了不同stage；旧forward/hard/recovery数字不再作为能力门。
- [x] [COMPUTED][HIGH] 新纯Shadow helper统一执行八阶段`Preferred/ORCA/Predict/Obstacle/PBD1/PBD2/PBD3/Reproject`，Baseline与Elastic唯一差异是ORCA前的非Source Preferred调整。
- [x] [COMPUTED][HIGH] PBD可选三轮诊断与无诊断结果完全等价；Obstacle诊断、统一Target/HardPair半径、八阶段hash和同阶段指标已接入。
- [x] [COMPUTED][HIGH] Twin-Step不跨正式步携带Shadow位置/速度；Parallel Rollout冻结正式输入后维护两个独立世界180步，前90步启用Transit Influence，恢复窗口为90–179。
- [x] [COMPUTED][HIGH] 初始约`90cm`间距低于`94cm` HardPairDistance形成的step 0 overlap已归类为`InheritedAtStepStart/SharedByBoth`，不会抢占后续Elastic因果首失败。
- [x] [COMPUTED][HIGH] rollback恢复累计指标、zero-progress、首失败pin、fixture hash/样本长度及Parallel世界，避免correction replay重复累计。
- [x] [COMPUTED][HIGH] Development Editor、Shadow定向自动化4/4、完整`CrowdDemo.SF` 40/40通过。
- [x] [COMPUTED][HIGH] 8352双端stage/rollout/fixture比较match，agents=visible=`20`、obstacle penetration=`0`、revision gap=`0`、correction p95约`0.06cm`，无Demo运行期坏日志或VIOLATION。
- [x] [COMPUTED][HIGH] 首失败fixture有效：step=`29`、stage=`ORCA`、kind=`OrcaStopViolation`、attribution=`ElasticWorsened`、closure=`20`、hash=`4215325188`；JSON包含八阶段、ORCA constraints、Obstacle/Reproject和三轮PBD诊断。
- [ ] [COMPUTED][HIGH] ORCA安全门失败：Agent 1 Baseline ORCA=`(643,462)cm/s`可行，Elastic Preferred从`(595,535)`变为`(593,517)cm/s`后，Elastic ORCA=`(0,0)`且infeasible。
- [ ] [COMPUTED][HIGH] Parallel安全门失败：Baseline/Elastic HardPair=`2/4`、Target=`358/365`、ORCA stop violation=`147/109`；不得用completed=`5/6`和end-error p95=`116.013/29.706cm`宣称Elastic通过。
- [ ] [COMPUTED][HIGH] Production Elastic、DebugGame正式两轮、录像/contact sheet、Moving Target、100/500、攻击和死亡未运行；原因是首失败已在ORCA阶段成立，任务按计划停止。
- [x] [COMPUTED][HIGH] 正式SF4 processor输出、ORCA/PBD参数、地图/Lighting、网络和实体数均未改变；Joint Velocity继续只读诊断。

### Step 29 ORCA量化重放

- [x] [COMPUTED][HIGH] hash=`4215325188`重捕获后保持不变，fixture新增MaxSpeed、epsilon、quantum、Q15法线和分阶段replay诊断。
- [x] [COMPUTED][HIGH] Baseline/Elastic 14条constraint逐项完全相同；Baseline `(643,462)cm/s`满足Elastic全部constraint和800cm/s速度圆。
- [x] [COMPUTED][HIGH] Elastic连续LP为`ExactFeasible`，约`(632,455)cm/s`；第一失败为连续解附近3×3量化`NoSolution`，不是continuous LP或speed-circle失败。
- [x] [COMPUTED][HIGH] 新确定性几何量化恢复覆盖half-plane投影、half-plane交点、line-circle交点及其3×3格点，单独计为`FormalLpQuantizedGeometryRecovered`。
- [x] [COMPUTED][HIGH] constraint反序和Preferred三组微扰均返回相同非零可行结果，不再坍缩为stop violation。
- [x] [COMPUTED][HIGH] 真正无1cm/s格点的sub-centimeter strip继续为`NoSolution`；没有降低epsilon或安全距离。
- [x] [COMPUTED][HIGH] Development、ORCA 10/10、完整SF 41/41及`git diff --check`通过。
- [ ] [COMPUTED][HIGH] 修复后原P0 Static Small真实单轮尚未运行；未评价新的首失败、Parallel Rollout或Production Elastic。

### 几何量化恢复后P0复跑

- [x] [COMPUTED][HIGH] 使用完全相同P0、Static Small 20、30Hz/30秒和readiness完成单轮复跑。
- [x] [COMPUTED][HIGH] 旧step29 ORCA stop首失败消失；新首失败为step40 Reproject HardPair，Agent 8–18，fixture hash=`410502020`。
- [ ] [COMPUTED][HIGH] HardPair仍失败：step40 Baseline/Elastic penetration=`3.523/4.189cm`；Twin最终累计=`189/191`。
- [x] [COMPUTED][HIGH] Twin最终Source Q15绝对值由旧`12515/12538`升至`18108/18131`，Elastic同轮领先23。
- [ ] [COMPUTED][HIGH] Parallel Source未改善为Elastic优势：Baseline/Elastic=`17857/13205`。
- [x] [COMPUTED][HIGH] Parallel ORCA stop由`147/109`降至`126/93`；Elastic和Baseline均下降。
- [ ] [COMPUTED][HIGH] Parallel HardPair=`3/3`仍非零；Target=`541/431`较旧`358/365`绝对恶化。
- [x] [COMPUTED][HIGH] 技术安全门通过：agents=visible=`20`、corridor=`20`、deadlock=`0`、双端penetration=`0`、revision gap=`0`、checkpoint/interval p95=`0.057/0.063cm`、hash match、坏日志=0。
- [ ] [COMPUTED][HIGH] 正式goal=`5`，低于旧运行`6`；SF4 Static与Elastic能力均未通过，Production继续禁止。
- [x] [COMPUTED][HIGH] step40 `410502020` Reproject residual已固化为20实体量化fixture；障碍可实现的稳定单侧修正使量化HardPair违规2→0，且Obstacle penetration保持0。
- [x] [COMPUTED][HIGH] Shadow Baseline/Elastic共用相同Reproject safety polish；正式SF4运动链和Mass fragments未消费该结果。
- [x] [COMPUTED][HIGH] Development Editor、Elastic 7/7与完整SF 42/42通过。
- [x] [COMPUTED][HIGH] 相同P0单轮完成RoundResult与客户端boundary apply：stage/fixture/rollout hash双端一致，agents=visible=20，双端penetration=0，revision gap=0，checkpoint/interval p95=`0.057/0.065cm`，坏日志=0。
- [x] [COMPUTED][HIGH] 旧step40首失败消失；新首witness为step81 Obstacle Agent 1–3，hash=`3917118627`，且该步PBD1至Reproject均已清零。
- [ ] [COMPUTED][HIGH] Twin终态仍有Baseline/Elastic Reproject HardPair=`7/6`；Parallel Elastic HardPair=`3`，Target=`431`，ORCA stop violation=`93`，安全门未通过。
- [ ] [COMPUTED][HIGH] Production Elastic、DebugGame、正式两轮与录像未执行。
