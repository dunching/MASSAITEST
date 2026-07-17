# MassAI Crowd Demo 测试场景矩阵

## 1. 文档职责与三级验收

[INFERRED][HIGH] 本文件定义典型行为的正式尝试关卡、输入合同、能力门和升级顺序；纯kernel fixture、关卡运行、FFmpeg证据和人工审片必须分别记录，不能互相替代。

```text
纯kernel/自动化
→ 20实体真实尝试关卡
→ FFmpeg视觉证据
→ 人工审片
→ 100实体
→ 500实体与WORK/复制性能
```

[INFERRED][HIGH] 前一级失败立即停止同场景升级；不得通过删除实体、隐藏实例、视觉偏移、放宽Hard门或恢复旧ORCA/Slot路径制造通过。

## 2. 尝试关卡资产合同

[INFERRED][HIGH] 下一阶段新增以下真实package；名称固定，避免同一行为被多个临时地图重复表达：

| ID | Package | 首轮规模 | 主要目的 |
|---|---|---:|---|
| T1 | `/Game/Maps/CrowdDemo_SimRoundSoftPressureT1OpenSpawnRelaxationSmall` | 20 | 开放空间生成、压力传播和新平衡 |
| T2 | `/Game/Maps/CrowdDemo_SimRoundSoftPressureT2OpenCohortMovementSmall` | 20 | 无障碍群体整体移动、Polar handoff与自然落位 |
| T3 | `/Game/Maps/CrowdDemo_SimRoundSoftPressureT3OpenBidirectionalSwapSmall` | 20 | 两个cohort双向交换 |
| T4 | `/Game/Maps/CrowdDemo_SimRoundSoftPressureT4ValidCorridorTransitSmall` | 20 | 窄口、转角和出口展开 |
| T5S | `/Game/Maps/CrowdDemo_SimRoundSoftPressureTargetStaticSmall` | 20 | 静态Target包围；现有package复用 |
| T5M | `/Game/Maps/CrowdDemo_SimRoundSoftPressureTargetMovingSmall` | 20 | 移动Target跟随；现有package复用 |
| T6A | `/Game/Maps/CrowdDemo_SimRoundSoftPressureT6HeterogeneousTransitSmall` | 20 | 混合尺寸/质量共享通道 |
| T6S | `/Game/Maps/CrowdDemo_SimRoundSoftPressureT6HeterogeneousTargetStaticSmall` | 20 | 混合尺寸/质量/攻击距离静态分层 |
| T6M | `/Game/Maps/CrowdDemo_SimRoundSoftPressureT6HeterogeneousTargetMovingSmall` | 20 | 混合群体动态分层跟随 |
| T7 | `/Game/Maps/CrowdDemo_MultiStateVatHitResponseSmall` | 20 | 多状态VAT、击退、击飞、落地与命中改色 |
| T8 | `/Game/Maps/CrowdDemo_RangedProjectileCombatSmall` | 20 | 远程windup、Mass投射物、swept命中与统一受击响应 |
| T9 | 尚未设计 | 20 | T7/T8通过后的混合战斗集成 |

[INFERRED][HIGH] 新地图必须使用Unreal Python执行`delete_asset → new_level → save_current_level`生成真实package；不得复制、移动或改名现有`.umap`文件。

[COMPUTED][HIGH] T5S/T5M已经存在，不重新生成，不修改用户Lighting。

[INFERRED][HIGH] 所有新地图统一包含：DirectionalLight、SkyLight、SkyAtmosphere、ExponentialHeightFog、VolumetricCloud、SM_SkySphere、PreviewFloor、PlayerStart、固定俯视相机和场景配置Actor。

[INFERRED][HIGH] 初始阵型必须按参与实体中最大的PairHardDistance构造无Hard overlap稳定布局；若场景需要同点连续生成，必须由受控spawn序列产生并把初始Hard冲突计入fixture，不能把全体初始重叠作为默认状态。

## 2.1 通用局部交互验收合同

[INFERRED][HIGH] T3双向交换、T4窄口、T5同目标和T6M异构移动目标必须使用同一个Local Predictive Interaction kernel。测试地图只改变位置、速度、半径、障碍和Preferred等输入事实，不得授权生产processor读取TestCase、地图名、Portal或“窄口/Target”标签。

[INFERRED][HIGH] Navigation/Flow Cell可以被多个实体同时或先后使用。验收不要求每Cell只能进入一个实体；应检查预测冲突pair、局部component、可行正向速度、让行公平、长期零吞吐、Particle反向修正和最终稳定窗口。

[INFERRED][HIGH] 几何允许多人并行时，局部层不得人为串行；共同前进不可行时，有限期grant必须避免饥饿，未获grant实体仍应选择安全让行速度而不是永久归零。

[INFERRED][HIGH] 每个专项场景除原能力门外还需满足：LocalPredictive invalid/quantization failure=0、blocked age有界、server/client local hash一致、rollback replay不改变grant顺序，以及Hard/Swept/Obstacle/Bounds=0。

## 3. T1 Open Spawn Relaxation

[INFERRED][HIGH] 开放地图按稳定AgentId顺序逐批生成实体，验证Hard安全、Soft压力传播、停止生成后的settling和移除实体后的新平衡。

[INFERRED][HIGH] 能力门包括：Hard/Swept/Obstacle/Bounds=0、压力至少传播到第三层、连续15步settling成立、移除后不恢复旧矩阵、agents=visible。

## 4. T2 Open Cohort Movement

[INFERRED][HIGH] 无障碍整体移动，验证Shared Flow宏观方向、自然间距、速度上限、队形不依赖PositionId，以及进入Target Influence范围后向Target Region Transport的稳定handoff。

[INFERRED][HIGH] T2的终态不再是“20个实体触发同一140cm点目标”：Polar domain外由Shared Flow负责接近，Polar domain内由Target Region Transport负责群体运输，满足Demand的终端实体使用Distance Band自然落位。`flow_goal_reached_count`只能作为宏观接近/handoff里程碑，不得作为T2最终能力门。

[INFERRED][HIGH] T2最终能力门以8.5稳定性验收V1为准：20/20持续进入有效距离带、持续覆盖全部环境可行Demand Region、merge/terminal稳定窗口成立，同时Plan/Guidance unrouted=0、Transport validation failure=0、Hard/Swept/Obstacle/Bounds=0、invalid/fallback=0、agents=visible、双端hash与rollback通过。

## 5. T3 Open Bidirectional Swap

[INFERRED][HIGH] 两侧各10个实体交换目标，验证相向群体能在统一Particle规则下穿过、无交换穿透、无长期deadlock且双方throughput接近。

[INFERRED][HIGH] T3的完成条件必须是两侧独立的宽交换区或越过完成平面，不得复用140cm点目标；T3验证通过与交换，不引入Target Region Transport终态站位。

[INFERRED][HIGH] 本场景不引入Portal、场景专用priority或passing band；Local Predictive层接入后仍使用与T4/T5/T6相同的通用轨迹规则。若20实体失败，按统一Shared Guidance/Local Predictive/Particle链归因，不得暗中恢复旧SF3。

[COMPUTED][HIGH] 8507原P0使用通用LocalPredictive生产链达到cohort agents=`10/10`、center crossed=`10/10`、completed=`10/10`、throughput difference=`0`、final deadlock=`0`；LocalPredictive samples=`901/901`、invalid=`0/0`、hash=`1161166200`双端一致，Particle四类安全违规为0，rollback=`53/0/0`、replayed=`56`，误差p95=`0cm`。

## 6. T4 Valid Corridor Transit

[INFERRED][HIGH] 复用当前SF1/SoftPressure障碍几何，明确保留满足最大参与实体Hard clearance的有效窄口，验证Shared Flow V2、全体通过、Soft压缩、转角和出口展开。

[INFERRED][HIGH] T4的最终能力门是20/20越过窄口后的出口平面/区域并完成展开，不使用点目标或Target Region Transport代替通道吞吐验收。

[INFERRED][HIGH] T4只用于验证通用局部预测规则在受限几何中的表现，不存在“检测到窄口后切换调度器”的生产合同；相同规则必须能从环境与邻域轨迹事实自然得到可并行或需让行的结果。

[INFERRED][HIGH] T4 Small通过后，T6A才允许将Large/Heavy实体加入同一通道；若最大实体物理上不可通过，必须修改测试几何或声明该profile不适用，不能让Particle放宽HardDistance。

[COMPUTED][HIGH] T4现由`FCrowdDemoValidCorridorTransitKernel`固定20实体、10列、128cm间距布局，并以墙体通过平面`Y=-1950cm`、通道出口平面`Y=-650cm`和完成平面`Y=750cm`定义sticky进度；能力合同不读取140cm点目标结果。

[COMPUTED][HIGH] 8460原P0单轮达到wall/corridor/completed=`20/20/20`、final deadlock=`0`、unreachable=`0`、completion max step=`272`。T4 layout/flow/progress hash=`1371747839/520862038/704754322`双端一致；Particle四类安全违规与invalid/fallback均为0，rollback hit/miss/mismatch=`54/0/0`，checkpoint/interval error p95=`0cm`，agents/visible=`20/20`。

[COMPUTED][HIGH] 8461录像与contact sheet显示20实体从下方阵型进入下部窄口、完成连续转向并从中部墙体左端进入上部出口；未观察到整群死锁、明显穿墙、实例隐藏、错误visual owner或明显fixed-step跳变。远景录像不能替代厘米级pair安全指标，细粒度安全结论仍以8460技术门为准。

[COMPUTED][HIGH] 8509在同一通用LocalPredictive生产链下再次达到wall/corridor/completed=`20/20/20`、completion max step=`327`、final deadlock=`0`；LocalPredictive samples=`901/901`、invalid=`0/0`、hash=`3029136817`双端一致，Particle安全为0，rollback=`53/0/0`、replayed=`60`，误差p95=`0cm`。

## 7. T5 Target Influence and Surround

[COMPUTED][HIGH] T5S 8417与T5M 8418已通过20实体技术门：inside band=20/20、Plan/Guidance unrouted=0、validation failure=0、Particle安全、rollback和五类双端hash成立。

[INFERRED][HIGH] T5仍需补录FFmpeg和人工审片，检查Far Flow进入Target Region Transport、环境可行Region覆盖、目标移动时相对跟随、无旧world coordinate追逐、无隐藏实例和无fixed-step视觉跳变。

[COMPUTED][HIGH] T5尚未验证异构能力；当前20实体使用同一Particle和距离配置。

[COMPUTED][HIGH] 8515 Static的Particle安全、rollback、inside band=`20/20`、unrouted/transport invalid/validation failure=`0`，但feasible Region coverage仍为`14/16`。Region 4/5的Demand与完整quota路径存在，Agent 8/16消费首段Guidance后Local速度仅约13.45/9.06cm/s，低于30Hz、1cm位置量化的15cm/s执行门，Applied为0。

[COMPUTED][HIGH] 8515双端稳定诊断报告`sub_quantum_supply=2/2`、首Agent=`8/8`、hash=`2690604116`一致；完整`CrowdDemo.SF`仍为62/62。

[INFERRED][HIGH] 当前下一门是通用Local constraint component的可执行进展下界，不是增加Region容量或T5专用Cell admission。没有纯fixture证明前，不得直接调速度、绕过Particle量化或运行Moving掩盖失败。

## 8. T6 Heterogeneous Cohorts

### 8.1 首版尺寸/质量profile

[INFERRED][MED] 第一版建议锁定三档P0，仅用于20实体能力验证；实现前必须在纯kernel fixture中确认所有距离和量化合同：

| Profile | PhysicalRadius | HardGap | SoftMargin | Mobility | 含义 |
|---|---:|---:|---:|---:|---|
| SmallLight | 30cm | 10cm | 17cm | 2.0 | 小型、较易被压力移动 |
| Standard | 42cm | 10cm | 17cm | 1.0 | 当前基线 |
| LargeHeavy | 60cm | 10cm | 17cm | 0.5 | 大型、较难被压力移动 |

[INFERRED][HIGH] Mobility继续是Particle唯一逆质量权重；不得再增加HeavyPriority、MeleePriority或另一套pair responsibility。

[INFERRED][HIGH] 异构Pair继续使用：

```text
HardDistance = RadiusA + RadiusB + max(HardGapA, HardGapB)
SoftDistance = HardDistance + SoftMarginA + SoftMarginB
CorrectionShareA = MobilityA / (MobilityA + MobilityB)
```

### 8.2 首版攻击距离profile

[INFERRED][MED] 第一版建议将攻击范围表达为Target中心距离带，不在Particle内表达攻击语义：

| Capability | MinimumCenterDistance | MaximumCenterDistance | 预期层次 |
|---|---:|---:|---|
| Melee | `max(TargetHardDistance,170cm)` | 300cm | 靠近Target的内层 |
| MidRange | 400cm | 600cm | 中间层 |
| Ranged | 700cm | 850cm | 外层 |

[INFERRED][HIGH] `TargetHardDistance = TargetRadius + AgentRadius + max(TargetHardGap,AgentHardGap)`始终优先；配置Minimum低于Hard门时必须规范化并计入summary。

[INFERRED][HIGH] 攻击距离只决定Target Region terminal band，不直接生成攻击、射击、伤害或永久Slot。

### 8.3 联合输入

[INFERRED][HIGH] T6S/T6M首轮20实体使用稳定组合，而不是把尺寸和攻击范围绑定成单一职业：

```text
SmallLight × Melee
SmallLight × Ranged
Standard × Melee
Standard × MidRange
Standard × Ranged
LargeHeavy × Melee
LargeHeavy × Ranged
```

[INFERRED][HIGH] 组合数量按AgentId稳定分配并保证每个profile至少2个实体；具体20实体配比在实现计划中固定后不得在失败运行中临时改变。

[INFERRED][HIGH] Navigation/Demand按量化CapabilityProfileKey稳定分cohort；同profile共享Topology、Demand和Transport Plan，不为每个实体独立建图。

[INFERRED][HIGH] CapabilityProfileKey至少折叠PhysicalRadius、HardGap、SoftMargin、Mobility、Minimum/MaximumCenterDistance和Target hard profile；Server/Client必须得到相同cohort membership和hash。

### 8.4 T6验收

[INFERRED][HIGH] T6A验证LargeHeavy不会被SmallLight穿透、不同Mobility修正份额成立、最大实体可通过声明的通道且全体无deadlock。

[INFERRED][HIGH] T6S验证Melee/MidRange/Ranged形成内/中/外自然分层，每个cohort覆盖`min(CohortAgentCount, FeasibleRegionCount)`个可行Region，Plan/Guidance unrouted=0；这些条件必须在稳定窗口内持续成立，不能只取RoundResult单帧。

[INFERRED][HIGH] T6M验证目标移动时三层保持Target-relative距离，慢/重实体允许在MaxSpeed约束下出现可解释的瞬时落后，但最终稳定窗口仍必须达到各cohort完整覆盖；不得通过速度膨胀、隐藏实例或旧Slot坐标追赶。

[INFERRED][HIGH] 所有T6场景还必须满足跨profile Hard/Swept/Obstacle/Bounds=0、Particle candidate/applied hash、Capability/Cohort/Transport hash双端一致、rollback miss/mismatch=0和agents=visible=20。

### 8.5 Target类场景稳定性验收V1

[INFERRED][HIGH] 本节适用于T2终态、T5S/T5M与T6S/T6M。T6A和T4按通道完成平面验收，不要求静态终态窗口。

#### A. 到达与分布

- [INFERRED][HIGH] `inside_effective_band_count`必须在最后连续90个fixed steps（30Hz下3秒）保持等于实体数，而不是仅在某一step达到过。
- [INFERRED][HIGH] 每个Capability cohort的Region coverage必须在同一90-step窗口内持续等于`min(CohortAgentCount, FeasibleRegionCount)`；不可行Region变化必须带明确TargetRevision/graph原因。
- [INFERRED][HIGH] Region人口上限继续使用`ceil(CohortAgentCount / CoveredFeasibleRegionCount) + 1`；不得用单个拥挤Region满足inside-band总数。
- [INFERRED][HIGH] Plan/Guidance unrouted、Transport invalid/validation failure和final deadlock均为0。

#### B. 通用局部预测与吞吐

- [INFERRED][HIGH] 相同next cell请求允许出现；验收对象是局部预测速度是否满足pair/environment约束并保持可实现吞吐，而不是强制“一Cell一实体”。`local_predictive_infeasible_count=0`。
- [INFERRED][HIGH] `merge_blocked_agent_count=0`：同一实体不得连续15步保持edge-forward requested≥30cm/s、applied≤10cm/s，且主要反向修正来自同一next-cell merge component。
- [INFERRED][MED] Small 20的临时门设为yield wait p95≤2秒、max≤4秒、单个局部冲突component连续零吞吐≤1秒、starvation=0；这些数值必须在首个纯fixture与真实诊断运行前冻结，运行失败后不得临时放宽。
- [INFERRED][HIGH] correction rollback后BlockedAge、grant epoch、throughput样本和hash必须恢复并重放，不得重复授予或清零等待公平性。

#### C. 稳定落位与无拉扯

- [INFERRED][HIGH] Static使用世界位置/速度，Moving使用减去Target平移后的Target-relative位置/速度。
- [INFERRED][MED] 在同一最终90-step窗口内，settled实体Target-relative speed p95≤10cm/s、max≤20cm/s；30-step滑动窗口的位置峰峰值p95≤5cm、max≤10cm。
- [INFERRED][HIGH] `terminal_chatter_count=0`：没有TargetRevision、graph失效或明确surplus释放原因时，同一实体不得在一个15-step Plan寿命内发生`Terminal→Supply/Transport→Terminal`往返。
- [INFERRED][HIGH] `attraction_rejection_cycle_count=0`：同一anchor/component不得连续15步重复出现宏观requested指向内侧、Particle realized correction指向外侧且净进展不成立。
- [INFERRED][HIGH] Particle settling tracker必须至少出现一个连续15步窗口：最大实际修正≤1cm且SoftError p95相邻样本变化≤1cm。SoftError本身允许长期非零。

[INFERRED][MED] V1的速度、位置、等待与零吞吐数值是Small 20的预声明门；必须先由确定性纯fixture证明可计算、由一次只读真实诊断确认量纲，再冻结用于生产修复验收。诊断结果可以否定阈值设计，但不得在同一次失败运行后为获得通过而放宽。

#### D. 安全、双端与视觉

- [INFERRED][HIGH] Hard/Swept/Obstacle/Bounds、Particle invalid/fallback、revision gap和rollback miss/mismatch均为0；checkpoint/interval position error p95<1cm；server/client所有Transport、LocalPredictive、stability与applied hash一致；agents=visible=20。
- [INFERRED][HIGH] FFmpeg与人工审片必须检查：无共享anchor持续争抢、无局部高频往返、无terminal实体被周期性重新拉走、无隐藏实例、无fixed-step跳变和错误visual owner。

[COMPUTED][HIGH] 现有8426、8417/8418和8479没有输出上述局部冲突/yield/chatter/稳定窗口全部指标；它们保留为旧口径到达、安全和同步证据，但在V1复验前不得单独标记“稳定落位完整通过”。

## 9. 规模升级

[INFERRED][HIGH] 当前优先级改为先补齐T1-T6的20实体真实尝试关卡；只有对应Small三级验收通过，才为同一场景建立100/500 package。

[INFERRED][HIGH] 500额外验证solver p95、真实WORK/GT边界、复制预算、correction成本和完整实例显示；Small结果不得直接外推。

## 10. 当前状态

[COMPUTED][HIGH] T1、T2 与 T5S/T5M 已有对应真实 Small 运行证据；T1 已完成自动化、双端技术门、FFmpeg 与人工审片。T5S 8521已完成8.5稳定性V1自动化、双端技术与能力门，仍待人工审片；T2与T5M尚未按8.5复验。

[COMPUTED][HIGH] T1/T2/T3/T4/T6 的新尝试关卡和异构 Particle/CapabilityProfile 基础已创建；T1、T3与T4已完成各自现行能力运行，T2保留8426旧口径证据并等待稳定性V1复验。T6A 8464通过，T6S 8479等待稳定性V1复验，T6M 8492为19/20失败。

[INFERRED][HIGH] 下一阶段应先建立Target类场景的merge容量、terminal迟滞和稳定窗口诊断/纯fixture，再修复T6M并按8.5复验；旧单帧inside-band结论不得外推为稳定自然落位。

## 11. 2026-07-15 Package 与运行矩阵

[COMPUTED][HIGH] T1/T2/T3/T4/T6A/T6S/T6M 的 Small package 均已创建；T5S/T5M 继续复用原 package。

| 顺序 | 场景 | Package | 本轮状态 |
|---:|---|---|---|
| 1 | T1 Open Spawn Relaxation | `CrowdDemo_SimRoundSoftPressureT1OpenSpawnRelaxationSmall` | 自动化、技术门、能力门、录像与人工审片通过；停止 |
| 2 | T2 Open Cohort Movement | `CrowdDemo_SimRoundSoftPressureT2OpenCohortMovementSmall` | 8426旧口径技术/能力门通过；稳定性V1待复验 |
| 3 | T3 Bidirectional Swap | `CrowdDemo_SimRoundSoftPressureT3OpenBidirectionalSwapSmall` | 8455自动化、双端技术门与20/20能力门通过；8456录像完成 |
| 4 | T4 Valid Corridor Transit | `CrowdDemo_SimRoundSoftPressureT4ValidCorridorTransitSmall` | 8460自动化、双端技术门与20/20能力门通过；8461录像完成 |
| 5 | T5 Static/Moving | 现有两张 Target map | 8521 Static自动化、双端技术与稳定性V1能力门通过：20/20、coverage=16/16、最终速度/位置抖动为0；人工审片与Moving未运行 |
| 6 | T6 Transit/Static/Moving | 三张 Heterogeneous map | T6A通过；T6S旧口径通过待V1复验；T6M 19/20失败 |

[COMPUTED][HIGH] T1 的 `first_influenced_iteration_max=1` 只表示求解迭代，不是空间传播层；门控不得引用该字段证明“传播到第三层”。

[COMPUTED][HIGH] T1 空间传播使用实际 realized Soft correction pair 的累计无向图；inserted source 为 layer 0，稳定 BFS 实测 layer max=3。T1 不动态创建或销毁 Mass 实体，所有 20 个实例始终可见；active 集合变化只属于测试参与合同。

[COMPUTED][HIGH] 8424 视频为 `Saved/CrowdDemoCapture/CrowdDemoCapture_8424_20260715_190228/crowd_demo_phase_f.mp4`，activation contact sheet 为同目录 `capture_contact_sheet_activation.jpg`。

## 11.1 T3 双向交换诊断与最终验收

[COMPUTED][HIGH] 8452加载T3真实package与配置Actor，20实体运行完整30秒。Particle Hard/Swept/Obstacle/Bounds、invalid/fallback均为0；candidate/applied hash双端一致；rollback hit/miss/mismatch=`54/0/0`、replayed=`63`；误差p95=`0cm`；agents/visible=`20/20`；无禁止错误。

[COMPUTED][HIGH] 8452不能通过T3能力门：Source中`BidirectionalSwap`没有布局、cohort目标、相反guidance或交换完成指标的生产消费者；实际结果仍是通用单目标Flow，`flow_goal_reached=17/20`。因此本运行只证明基础设施，没有证明任何双向交换能力。

[COMPUTED][HIGH] 随后已接入稳定`10+10` cohort布局、两套相反Shared Flow、中心穿越与完成平面、独立T3 progress/hash/rollback指标。纯自动化包含布局与相反Flow、完成平面、final-deadlock语义和900步正式Particle rollout；最终T3定向自动化4/4、完整`CrowdDemo.SF` 50/50、Development Editor和`git diff --check`通过。

[COMPUTED][HIGH] 8454使用同一横向目标中心时复现18/20：Agent 102与111在中心附近形成约100.7cm的安全Soft压力平衡。对共享cohort目标中心执行纯kernel固定输入扫描后，最小通过配置为双方分别偏移`±400cm`；该修正只改变cohort级Shared Flow目标中心，没有增加per-agent lane、passing band或Particle参数。

[COMPUTED][HIGH] 8455原P0 Round 1最终得到cohort agents=`10/10`、center crossed=`10/10`、completed=`10/10`、throughput difference=`0`、final deadlock=`0`、unreachable samples=`0`、completion step max=`213`。T3 layout/flow/progress hash=`2106229224 / 1511622317,3339167804 / 1026748592`双端完全一致；Particle Hard/Swept/Obstacle/Bounds与invalid/fallback均为0，candidate/applied hash=`3365230133/2988060962`双端一致，rollback hit/miss/mismatch=`53/0/0`、replayed=`57`，checkpoint/interval误差p95=`0cm`，client agents/visible=`20/20`。

[COMPUTED][HIGH] 8456录像为`Saved/CrowdDemoCapture/CrowdDemoCapture_8456_20260717_011432/crowd_demo_phase_f.mp4`，contact sheet为同目录`capture_contact_sheet.jpg`，亮度QA均值为`144.052`。人工审片确认两群相向进入、中心交换后分离，未见整群死锁、隐藏实例、明显fixed-step跳变或错误visual owner；相机距离较远，逐实体接触净空仍以8455的Hard/Swept技术指标为证据，而不是由录像外推。

[INFERRED][HIGH] T3能力门使用中心穿越、双侧完成平面和cohort吞吐对称性；通用`flow_goal_reached_count=12`只保留为点目标旧诊断，不得覆盖T3的20/20交换结论。

## 12. T2 Open Cohort Movement 8425旧合同运行与重新解释

[COMPUTED][HIGH] T2 已从 SF1 障碍配置中隔离：`MakeOpenFlowConfig()` 保留 Shared Flow V2 与原目标，但 `ObstacleSpecs=0`；Arena 双端均输出 `visuals=0`。20 实体使用按 FormationIndex 排序的 `10×2`、128cm 间距阵型，layout hash=`1263469201`。

[COMPUTED][HIGH] Development Editor、T2 定向自动化 1/1、Particle 23/23 和完整 `CrowdDemo.SF` 44/44 通过。

[COMPUTED][HIGH] 8425 Round 1 的安全与同步门通过：Hard/Swept/Obstacle/Bounds=`0/0/0/0`，invalid/fallback=`0/0`，corridor=`20/20`，final deadlock=0，candidate/applied hash=`590396294/2922157754` 双端一致，route hash=`2516988440` 双端一致，rollback hit/miss/mismatch=`53/0/0`、replayed steps=55，误差 p95=0cm，client agents/visible=`20/20`。

[COMPUTED][HIGH] 8425在旧合同下记录 `flow_goal_reached_count=16/20`、never reached=4、reached-then-left=12、Flow contract violation=68；失败均为 mask 8。Round末SoftError p95=`33.745cm`，未发生Particle安全失败。这些数值保留为旧点目标合同的历史诊断，不再用于判定T2最终能力通过或失败。

[COMPUTED][HIGH] 当前T2生产配置只启用开放Shared Flow和Particle，没有启用Target Region Transport，因此8425并未执行现行目标站位架构。

[INFERRED][HIGH] 8425证明开放群体宏观移动、Particle安全、双端同步和完整显示成立；T2能力状态应重置为“未按现行终态合同验证”，不是“已证明失败”或“已通过”。下一次运行必须先接入Shared Flow→Target Region Transport→Distance Band终态合同，再以inside-band、Region coverage、unrouted和settling判定能力。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 19. T6M 8490–8493 归因结果

[COMPUTED][HIGH] 8490复现8487的`17/20`并输出每cohort可行Region：phase=5与phase=10在旧规则下都选择`11,12,13`，证明边界可行图使绝对Region phase发生折叠。

[COMPUTED][HIGH] phase修复后的8491/8492达到inside-band=`19/20`、coverage=`19`、max region population=`1`、unrouted/invalid/validation failure=`0`，安全、rollback、误差和双端hash门保持通过。

[COMPUTED][HIGH] 8492唯一带外实体为Agent15：LargeHeavy Melee，distance=`308.3cm`，上界=`300cm`，attachment成功，当前cell32与next cell16均可行；同cohort Agent14也从cell31指向cell16。

[COMPUTED][HIGH] 8493临时next-cell单准入实验仍为`19/20`，误差降到`4.5cm`但coverage降为`18`；该分支未达到能力门并已撤销。8493只保留为失败实验记录，不代表当前生产结果。

[COMPUTED][HIGH] 当前矩阵中T6M仍为失败；不得启动架构删除、T9/T10、100/500或Projectile插件迁移。

[INFERRED][HIGH] 8492还说明旧能力门缺少merge与稳定窗口事实：安全为0只能证明Particle阻止了穿透，不能证明Transport没有持续吸引与反向修正。下一次T6M运行必须同时使用8.5的到达、merge、稳定和视觉四层门。

## 20. T6M 8496稳定窗口归因

[COMPUTED][HIGH] 8496使用原P0、20实体、30秒、server/client与correction replay，只新增默认关闭的Target稳定窗口诊断。诊断最后90步hash=`4081220932`，双端一致；客户端rollback hit/miss/mismatch=`53/0/0`、replayed steps=`66`；Particle Hard/Swept/Obstacle/Bounds、invalid/fallback与日志错误均为0。

[COMPUTED][HIGH] 同next-cell请求在90/90步存在，但诊断没有把“共享cell”本身判错；只有2个实体同时满足连续90步requested≥30cm/s、applied≤10cm/s且Particle沿请求方向反向修正，才被计为`merge_blocked`。首witness为step 825、Agent14、next cell16。

[COMPUTED][HIGH] Terminal chatter=0；因此当前证据排除Terminal/Supply无因反复切换作为第一原因。Particle settled window=0，但同期inside-band最低17、coverage 16/20，仍有实体处于Transport，故它不是独立首因。Primary cause=`MergeCapacity`。

[INFERRED][HIGH] T6M下一门必须验证通用Local Predictive Interaction：不降低Region coverage、等待有界、无starvation、同next-cell可由几何决定安全串并行、最终90步满足稳定性V1。现有8496仍是失败归因运行，不是T6M通过结果。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 18. T6M Heterogeneous Target Moving：8487 最终状态

[COMPUTED][HIGH] 8487使用原P0、20实体、30Hz、30秒、server/client与correction replay运行连续两轮；地图为`/Game/Maps/CrowdDemo_SimRoundSoftPressureT6HeterogeneousTargetMovingSmall`，没有修改地图、Lighting、Particle或网络参数。

[COMPUTED][HIGH] 两轮稳定输入证据相同：round input=`4290856233`、initial state=`3920329045`、dynamic Flow round=`905043919`；Topology=`4029698379`、anchor cell=`2706`、Integration=`338742383`、semantic rebuild=`25`。Transport Topology/Demand/Plan/Guidance/Validation分别为`327980743/3161459931/3091774693/4129074629/210412695`，Particle candidate=`490909087`。

[COMPUTED][HIGH] 两轮均满足Hard/Swept/Obstacle/Bounds=`0/0/0/0`、Particle invalid/fallback=`0/0`、Plan/Guidance unrouted=`0/0`、Transport invalid/validation failure=`0/0`、rollback miss/mismatch=`0/0`、位置误差p95=`0cm`、agents/visible=`20/20`，且日志无Fatal、Assertion、Ensure、`LogWindows: Error`或VIOLATION。

[COMPUTED][HIGH] 能力门失败：两轮aggregate inside-band都为`17/20`，可行Region coverage为`16`，没有达到计划要求的每个CapabilityProfile及aggregate=`20/20`。因此8487证明Round与动态Flow技术合同成立，但不证明T6M终态能力成立。

[COMPUTED][HIGH] 因首个能力门失败，独立Round 1双进程、DebugGame、录像和架构删除均未执行。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## T6 真实运行矩阵（2026-07-17）

| 场景 | 证据 | 技术门 | 能力门 | 当前结论 |
|---|---:|---|---|---|
| T6A Heterogeneous Transit | 8464 | [COMPUTED][HIGH] 安全/hash/rollback/20实例通过 | [COMPUTED][HIGH] wall/corridor/completed=`20/20/20` | [COMPUTED][HIGH] 通过 |
| T6S Heterogeneous Target Static | 8479 | [COMPUTED][HIGH] 安全/hash/rollback/20实例通过 | [COMPUTED][HIGH] 旧口径7 cohort、inside-band/coverage=`20/20` | [COMPUTED][HIGH] 旧口径通过；稳定性V1待复验 |
| T6M Heterogeneous Target Moving | 8481 | [COMPUTED][HIGH] 安全/hash/rollback/20实例通过 | [COMPUTED][HIGH] inside-band/coverage=`10/20` | [COMPUTED][HIGH] 失败并停止 |

[COMPUTED][HIGH] 8481 各 capability profile 的 coverage/agent 分别为：StandardRanged `2/3`、LargeMelee `0/3`、SmallRanged `3/3`、LargeRanged `2/3`、StandardMelee `0/3`、StandardMid `2/2`、SmallMelee `1/3`。

[COMPUTED][HIGH] 8481 没有以旧 140cm 点目标、隐藏实例或客户端偏移判定能力；失败来自实体未进入各自 Target-relative terminal band/Region 覆盖。

[INFERRED][HIGH] T6M 下一次运行前必须先建立移动 Target 与 Shared Flow 空间锚点的独立合同；T6A/T6S 的通过结果不得外推为 T6M、100/500 或插件迁移已通过。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 12.1 T2 Open Cohort Movement 8426 Polar handoff验收

[COMPUTED][HIGH] T2 只在 `OpenCohortMovement` testcase 启用 Target Region Transport；T1/T3/T4 隔离测试通过。双端 owner 顺序为 Shared Flow远程接近→Transport guidance→Terminal Distance Band settle，同一boundary每个实体只产生一份最终guidance。

[COMPUTED][HIGH] Development Editor、T2定向自动化3/3、Target Region Transport 5/5、Particle 23/23和完整`CrowdDemo.SF` 46/46通过。

[COMPUTED][HIGH] 8426 Round 1旧口径能力指标为：flow approach entered=`20`、transport handoff=`20`、inside effective band=`20`、feasible Region coverage=`16/16`、max Region population=`2`（门限3）、Plan/Guidance unrouted=`0/0`、Transport validation failure=`0`、当帧terminal mode=`20/20`、首次全体terminal mode step=`435`、final deadlock=`0`。

[COMPUTED][HIGH] 8426安全与同步指标为：Hard/Swept/Obstacle/Bounds=`0/0/0/0`、Particle invalid/fallback=`0/0`、candidate/applied hash=`1532319504/2504526196`双端一致；Topology/Demand/Transport/Guidance/Validation hash=`2876421261/3579462314/4142750777/1462919974/2878556849`双端一致；rollback hit/miss/mismatch=`53/0/0`、replayed steps=`55`；checkpoint/interval error p95=`0cm`；client agents/visible=`20/20`。

[COMPUTED][HIGH] 8426无Fatal、Assertion、Ensure、`LogWindows: Error`或VIOLATION。旧`flow_goal_reached_count`、never-reached、reached-then-left及mask-8仍只作为点目标/Flow owner对照诊断，不覆盖Polar终态能力门。

[INFERRED][HIGH] 按当时口径T2 Small单轮通过并停止；在8.5稳定性V1生效后，该结果降级为旧口径证据，需复验后才能宣称稳定自然落位。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 13. T7 Multi-State VAT and Hit Response Small

[INFERRED][HIGH] T7是状态、真实VAT和直接受击响应的独立归因场景，不生成投射物。20个实体按稳定AgentId分组进入Idle、Move、Attack、HitReact和Death，并通过测试HitFact分别触发Knockback、KnockUp、LandingRecovery和HitFlash。

[INFERRED][HIGH] T7纯自动化必须覆盖状态转换合法性、唯一writer、VisualRevision、clip mapping、HitEventId去重、多事件稳定合成、水平impulse进入Particle、垂直ballistic落地、rollback/replay、输入反序与双端hash。

[INFERRED][HIGH] T7真实运行门为：agents=visible=20；全部指定VisualState有可见样本；knockback requested/realized可核对；knock-up apex和landing各一次；Hard/Swept/Obstacle/Bounds=0；hit flash revision与可见改色一致；correction后不重复受击、不重启错误clip。

[INFERRED][HIGH] T7必须使用真实虫子AnimToTexture mesh/material并通过FFmpeg人工审片；Engine Cube、静态换色或仅输出VatClipIndex日志均不能判VAT通过。

[COMPUTED][HIGH] 8447 已通过 T7 完整单轮技术门：20/20 VAT instances、稳定终态 Idle/Move/Attack/HitReact/Death=`8/4/4/0/4`、HitFlash active=`2/2/4`、Particle Hard/Swept/Obstacle/Bounds=`0/0/0/0`、双端 candidate/applied hash=`961313407/3034185224`、rollback hit/miss/mismatch=`54/0/0`、checkpoint/interval position error p95=`0cm`，且未出现 VIOLATION/Fatal/Assertion/Ensure/LogWindows Error。

[COMPUTED][HIGH] 8445 使用T7专用近景完成有效录像；逐帧与contact sheet检查确认`2/2/4`三次红色HitFlash、水平击退、击飞上升/顶点/落地、Attack和四个Death姿态，20个实例全程可见，未观察到隐藏实例、错误visual owner或明显fixed-step跳变。T7人工视觉门已通过。

## 14. T8 Ranged Projectile Combat Small

[INFERRED][HIGH] T8首轮由10个射手和10个静止目标从合法、无重叠且已满足射程的稳定位置直接开始，不启用Target Region Transport；随后执行AcquireTarget→Windup→Fire→Recovery，投射物命中后只能通过T7已验证的统一HitFact/HitResponse链施加伤害、击退/击飞和命中改色。

[INFERRED][HIGH] T8纯自动化必须覆盖windup目标锁定、Lifecycle失效、单次发射、批量spawn稳定排序、高速swept命中、最早hit决胜、过期销毁、一次命中/伤害、rollback不重复开火和事件hash。

[INFERRED][HIGH] T8真实运行门至少包括：`completed_windup_count=projectile_spawned_count`；`spawned=active+impacted+expired`；duplicate fire/hit/damage=0；服务端gameplay命中与客户端visual event边界成立；Attack VAT、fire phase、impact、hit flash和受击响应在录像中可辨识。

[INFERRED][HIGH] T8不得使用每发一个持续复制Actor、当前点半径查询或客户端本地伤害来制造通过；首版直射弹必须使用fixed-step previous→proposed swept collision。

[COMPUTED][HIGH] T8 package 已通过 Unreal Python 的 `delete_asset → new_level → save_current_level` 生成，并包含统一 Lighting、PreviewFloor、PlayerStart、固定相机和配置 Actor；被 NullRHI 中断的首次不完整 package 已被完整 Editor 生成结果替换。

[COMPUTED][HIGH] T8 最终自动化为 `CrowdDemo.Combat.T8` 7/7，完整 `CrowdDemo.SF` 为46/46，Development Editor 编译通过。7项覆盖单次windup/fire、Lifecycle失效、最早swept hit、expiry守恒、重复请求、真实10-lane 50发回合和rollback replay。

[COMPUTED][HIGH] 8451 原P0双端单轮满足 acquired/windup/spawned/impacted/damage=`50/50/50/50/50`、active/expired/duplicate fire/duplicate hit/invalid projectile=`0/0/0/0/0`；client projectile visual 为 spawn/impact/expire/active=`50/50/0/0`，三类战斗hash完全一致。

[COMPUTED][HIGH] 8451 Particle四类安全违规、invalid/fallback、rollback miss/mismatch和位置误差p95均为0，agents/visible=`20/20`。8450录像人工确认两排20实例、10发齐射、Attack、impact/HitFlash和Death可见；T8 Small能力门关闭。

## 15. T9 Mixed Combat Integration

[INFERRED][HIGH] T9只在T7和T8分别通过后设计，用于混合Melee/MidRange/Ranged、移动Target、死亡和群体重新运输。T9不能替代T7/T8的独立归因关卡，也不能作为首版实现时顺带加入的综合场景。

[INFERRED][HIGH] T7/T8 Small通过前不建立其100/500 package；Small结果也不得外推为真实WORK预算、大规模投射物复制或完整业务AI已经成立。

## 16. 插件迁移门与T10 Gameplay Swarm Sandbox

[INFERRED][HIGH] T9/T10开始前必须先满足T1-T8独立Small场景无已知硬失败，并完成插件提取、最小宿主自动化与Demo插件版回归；综合场景不得成为绕过T3/T4/T6/T7/T8独立归因门的替代品。

[INFERRED][HIGH] T10为类游戏持续场景：玩家控制Pawn；Server将输入量化为带CommandId和ApplyFixedStep的GameplayCommand；地图按稳定、有上限的规则持续生成敌群；敌群根据玩家位置和世界事实更新cohort目标；线形Capsule和圆形Cylinder命中经稳定spatial grid与精确测试生成统一HitFact；受击继续复用伤害、HitFlash、击退、击飞、落地和死亡合同。

[INFERRED][HIGH] T10导航必须使用NavMesh派生的稳定Surface Graph和分层共享Flow；独立地图至少覆盖连续上下坡、桥上桥下XY重叠、双路线高台、窄坡/窄桥、不可通行落差、墙体转角和不同Nav层刷怪区。

[INFERRED][HIGH] T10按四个独立门实施：平面GameplayCommand/HitFact；动态spawn/despawn和玩家追逐；无战斗NavMesh分层Flow；完整业务集成。每门先运行20实体，失败时停止在首失败阶段，不直接进入100/500。

[COMPUTED][HIGH] T10 package、Pawn、GameplayDirector、NavSurfaceGraph、DamageShape和高低差地图当前均不存在；本轮仅记录需求，不能列入通过矩阵。详细设计见`GameplaySwarmSandboxAndPluginMigrationPlan.md`。

## 17. Mass Projectile插件与大量远程敌人追加门

[COMPUTED][HIGH] T8 8451/8450继续作为10射手+10静止目标回归基线；它不覆盖移动目标、环境阻挡、高低差layer、空间Broadphase或100/500远程群体。

[INFERRED][HIGH] 插件版T8必须追加四类独立fixture：移动目标相对sweep、墙体先于目标、Team/CollisionLayer过滤、rollback后Mass projectile pool复用且Lifecycle不串弹。

[INFERRED][HIGH] 规模升级必须记录active Projectile、broadphase candidates、narrowphase tests、grid build/query/solve p95、HitFact backlog、duplicate hit/damage、environment/layer/team reject、server/client visual event守恒和GT commit数量；不得只用FPS或最终死亡数证明通过。

[INFERRED][HIGH] T10线形Capsule与圆形Cylinder使用同一spatial query和HitFact出口，但不通过生成伪Projectile实现范围伤害。完整门控见`MassProjectileHitFrameworkDesign.md`。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
