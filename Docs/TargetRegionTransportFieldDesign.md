# Target Region Transport Field 设计

## 1. 设计职责

[INFERRED][HIGH] 本文件定义 T5 Target-relative Polar Region Transport 的当前权威合同；旧 Polar Density left/current/right guidance 仅为历史实验，不再是生产架构。

## 2. 数据分层

[COMPUTED][HIGH] `FCrowdDemoTargetRegionTransportKernel` 提供四个纯函数阶段：`BuildTopology`、`BuildDemand`、`SolveTransport`、`BuildGuidance`。

[COMPUTED][HIGH] PipelineSubsystem 保存 Topology、Demand、FlowPlan、Edge quota、PlanEpoch 与 Agent guidance 的 prepared SoA；不创建 per-cell Mass fragment。

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

## 6. Plan、Hash 与 Rollback

[COMPUTED][HIGH] Plan寿命为15 fixed steps；TargetRevision、FeasibleGraphHash、membership hash、Demand满足和validator失败均可触发同一boundary重建并重新验证一次。

[COMPUTED][HIGH] Topology、Demand、Transport、Guidance与Validation各自生成step hash，并按fixed-step顺序折叠为round hash供双端比较。

[COMPUTED][HIGH] FeasibleGraphHash折叠全部Cell的key、feasible、terminal、PrimaryDemandRegion，以及排序后Edge的from/to、geometry、实际soft-clearance缺口、radial deviation和cross-band；不直接折叠Target世界坐标。

[COMPUTED][HIGH] Plan validator要求Edge严格排序唯一且quota为正，验证Edge存在和可行、Supply outgoing quota、正quota子图到Deficit的可达性及中间Cell流量守恒；有效Plan必须产生Guidance unrouted=0。

[COMPUTED][HIGH] rollback 原子恢复 prepared arrays、quota、PlanEpoch、四类 hash、重建计数和 solver samples，再执行 fixed-step replay。

## 7. 已知能力边界

[COMPUTED][HIGH] 8414 Static P0 已通过 20/20 有效带、16/16 可行 Region、unrouted=0 和全部 Particle/同步门。

[COMPUTED][HIGH] 8416 fixture证明step331并非参数不足：实际非terminal的Agent 15附着到terminal Cell 176，旧source→cell边允许sink直接吸收且没有outgoing quota。修复后terminal-anchor Supply必须先消费真实Topology Edge。

[COMPUTED][HIGH] 8417 Static与8418 Moving均通过Plan/Guidance unrouted=0、invalid/validation failure=0、Particle安全、rollback及五类双端hash门；PlanLifetime、TransportSpeed和Region数量未修改。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
