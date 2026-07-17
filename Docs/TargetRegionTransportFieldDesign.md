# Target Region Transport Field 设计

## 1. 设计职责

[INFERRED][HIGH] 本文件定义 T5 Target-relative Polar Region Transport 的当前权威合同；旧 Polar Density left/current/right guidance 仅为历史实验，不再是生产架构。

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

[INFERRED][HIGH] 本合同已由T6A/T6S/T6M的20实体真实尝试关卡产生部分证据；当前T6M仍失败，且所有Target终态场景仍须按`TestScenarioMatrix.md`稳定性V1复验。代码接入和旧口径inside-band均不是完整能力通过证据。

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

[INFERRED][HIGH] 下一步只能对剩余三个实体的profile、source attachment、终端Region与settle事实做只读归因。没有证据前不得修改Particle、直接追Target、增加第二guidance owner或场景专用fallback。

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
