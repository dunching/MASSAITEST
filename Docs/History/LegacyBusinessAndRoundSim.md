# Legacy Business and RoundSim History

[COMPUTED][HIGH] 本文归档已从当前工程删除的实验方向：Static/Moving/ObstacleSideSwap、CohortMove/FlowObstacle/Density/ApproachBand、SimRoundMove/Separation/HeadOn、PA1 Predictive，以及 R4–R6 业务 crowd/visual 路径。

[COMPUTED][HIGH] 这些实验曾用于定位 movement、flow、density、approach band、visual interpolation 和 sampled RVO，但不再是当前 SF1/SF2 验证器的可执行入口。

[INFERRED][HIGH] 历史指标只能解释归档当时的实现，不能作为当前Shared Flow、SoftPressure或Target Influence Distance Band的验收结果。

[COMPUTED][HIGH] 详细原始记录可从 Git 提交 `88a0d511` 的旧文档查阅。

## Polar Density 历史实验

[COMPUTED][HIGH] 8412 使用 Distance Band + 固定 16-sector Polar Density；安全、双端 hash 和 rollback 通过，但最终只有 16/20 实体处于有效距离带，Angular coverage 仅 3/16，最大单 Sector 人口 11，因此 Static 分布能力失败。

[COMPUTED][HIGH] 8413 的有效客户端诊断记录显示 requested tangential p95=`119.854cm/s`、Particle applied p95=`54.016cm/s`、lost p95=`100.185cm/s`、direction flips=`641`、sector transitions=`593`、band transitions=`1050`；该证据只描述旧 Density 请求到执行的衰减，不证明某种具体根因。

[COMPUTED][HIGH] Polar Density 的正式 guidance processor 已在 Target Region Transport Static 门通过后删除；旧纯测试与诊断结构只用于历史证据，不构成生产 fallback。
