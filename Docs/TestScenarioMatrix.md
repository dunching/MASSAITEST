# MassAI Crowd Demo 测试场景矩阵

## 1. 三级验收

[INFERRED][HIGH] 每个场景依次通过代码门、FFmpeg视觉证据和人工审片。前一级失败立即停止；视觉不能覆盖Hard安全和双端确定性。

## 2. T1 Open Spawn Relaxation

[INFERRED][HIGH] 开放地图同点连续生成实体，验证Hard安全、Soft压力传播、散开和停止生成后的新平衡。

## 3. T2 Open Cohort Movement

[INFERRED][HIGH] 无障碍整体移动，验证宏观方向、自然间距、速度限制和完整显示。

## 4. T3 Open Bidirectional Swap

[INFERRED][HIGH] 两侧cohort交换目标，验证互相穿过、throughput、deadlock和Hard/Swept安全。

## 5. T4 Valid Corridor Transit

[INFERRED][HIGH] 明确存在硬净空的窄道、转角和出口展开，验证Shared Flow V2、全体通过、Soft压缩和无穿墙。

## 6. T5 Target Influence and Surround

[INFERRED][HIGH] Static与Moving Target分别验证：

- [INFERRED][HIGH] Far Flow平滑进入Target Influence。
- [INFERRED][HIGH] 100/850两种有效距离带自然分层。
- [INFERRED][HIGH] 不追逐单点、精确环线或旧world coordinate。
- [COMPUTED][HIGH] 同方向进入后，Polar Region Transport把过密区域的群体沿可行Cell图输送到欠占用Demand Regions，Particle负责局部压力与Hard安全；Polar Density只保留为历史失败基线。
- [INFERRED][HIGH] Moving Target下相对跟随且MaxSpeed不膨胀。
- [INFERRED][HIGH] Hard/Swept/Obstacle/Bounds、双端hash、rollback和agents/visible全部通过。

[INFERRED][HIGH] Polar Region Transport纯kernel门必须覆盖：自适应外环分辨率、环向/跨环连接、环境可行Cell/Edge、区域容量和人口守恒、过密到欠占用运输、平衡时无主动流动、AgentId稳定分流、乱序hash和无永久owner。

[INFERRED][HIGH] 正式P0必须同时记录原始16-sector覆盖和环境可行sector覆盖，以及Topology/Demand/Transport/Guidance hash、deficit/surplus、transported agents、unrouted supply、edge quota、sector transition和Particle applied速度。覆盖不足不得临时加入Slot、方向锁或场景fallback。

## 7. T6 Heterogeneous Shared Region

[INFERRED][HIGH] 不同尺寸、速度或攻击距离cohort在同一区域交叉、穿过或共同包围Target，验证共享Distance Band函数、按量化capability profile分组的Polar Region Field和同一Particle安全规则。

## 8. 规模

[INFERRED][HIGH] 每项先20，再100，最后500。500额外验证solver p95、WORK/GT边界、复制预算和完整实例显示。

## 9. 当前状态

[COMPUTED][HIGH] Shared Flow V2、Particle和T5 Distance Band + Target Region Transport生产链已有Small证据。8417 Static达到inside band=`20/20`、可行Region覆盖=`16/16`、最大Region人口=`2`。

[COMPUTED][HIGH] 8416固定了Moving step331完整fixture；FeasibleGraphHash、Plan validator、Guidance consumption和rollback合同已接入。

[COMPUTED][HIGH] 8418 Moving达到inside band=`20/20`、当时可行Region覆盖=`12/12`、Plan/Guidance unrouted=0及Particle安全；FFmpeg、人工审片、T1/T3正式入口和100/500仍未运行。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
