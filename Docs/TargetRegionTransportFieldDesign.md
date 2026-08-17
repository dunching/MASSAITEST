# Target Region Transport Field 设计

## 1. 文档职责

本文定义大量 Agent 接近目标后，在目标参考系内进行区域人口分布与宏观运输的通用设计。

可以把它理解为：

> 一个以 Target 为原点、使用极坐标拓扑组织空间的局部 Transport / Guidance Field。

它不是普通移动的固定必经层，也不是永久站位 Slot 系统。

相关文档：

- `CurrentArchitecture.md`：当前生产接入。
- `TargetArchitecture.md`：最终全局运动架构。
- `LocalPredictiveInteractionDesign.md`：局部轨迹冲突与公平让行。
- `ParticleSafetyDesign.md`：最终 Hard / Swept / Environment 安全。
- `FeatureChecklist.md` / `TestScenarioMatrix.md`：当前完成状态与测试证据。

---

## 2. 什么时候启用

远离目标时，大范围路线主要由 Shared Flow 或其他宏观 Guidance 提供：

```text
远离 Target
    ↓
Shared Flow / Navigation Guidance
    ↓
接近 Target influence area
    ↓
Target Region Transport
```

Target Region 只在业务确实需要“围绕目标进行区域分布”时启用，例如：

- 大量单位围绕 Boss 展开；
- 近战与远程需要进入不同目标中心距离带；
- 移动目标周围需要维持总体人口分布；
- 目标周围某些区域被障碍阻塞，需要把人口运输到其他可行区域。

普通开放移动、自由游荡、双向交换、窄通道通过不应为了兼容本模块而强制建立 Target Region。

---

## 3. Target-relative Polar Topology

以 Target 为原点建立极坐标局部拓扑：

```text
r = 距离 Target 的中心距离
θ = 位于 Target 的方向
```

当前拓扑使用固定径向 Band，并根据半径扩大角向分辨率：

```text
Radial Band = 100 cm

r < 200 cm   → 8 sectors
r < 400 cm   → 16 sectors
r < 800 cm   → 32 sectors
其他         → 64 sectors
```

每个 Polar Cell 由稳定的 Band / Sector Key 标识。

同环建立 CW / CCW 邻接边；跨环只有角区间实际重叠时才能建立边。

Cell 和 Edge 的可行性必须考虑：

```text
Flow / Navigation bounds
Environment obstacle clearance
Agent physical radius
Hard safety gap
Target hard exclusion
Swept segment feasibility
```

Polar topology 是共享宏观空间结构，不是 per-agent 导航图。

---

## 4. Navigation Cell 不是站位 Slot

Navigation Cell 表示可共享的空间区域。

它不是：

```text
Agent17 → Cell17
```

也不是：

```text
一个 Cell 同时只能容纳一个 Agent
```

多个实体可以：

- 读取同一 Cell 的宏观方向；
- 在不同时间通过同一 Cell；
- 在几何允许时同时进入同一区域。

Cell anchor 只能作为方向参考，不能被解释为多个实体必须共同命中的精确坐标。

实体一旦进入目标 Cell 的可行空间集合，即可完成该次 edge handoff；不要求所有实体追逐 Cell center。

---

## 5. Distance Band

Target Region 同时使用目标中心距离带表达终端区域要求：

```text
MinimumCenterDistance
MaximumCenterDistance
InfluenceBlendWidth
```

Target 与 Agent 的硬安全下限为：

```text
TargetHardDistance =
    TargetRadius
  + AgentRadius
  + max(TargetHardGap, AgentHardGap)
```

配置的最小距离不得低于 TargetHardDistance。

Distance Band 内部是允许区域，不是一条必须精确贴住的圆环。

因此：

```text
D > Maximum → 向内修正
Minimum <= D <= Maximum → 不强制径向修正
D < Minimum → 向外修正
```

不同 Combat / Capability Profile 可以使用不同终端距离带，但这只影响宏观终端区域，不改变 Particle 的碰撞优先级。

---

## 6. Demand Region

Polar Navigation Cell 用于路径运输；Demand Region 用于人口统计。

当前设计固定使用 16 个 Demand Regions。

每个 Region 维护：

```text
AvailableCapacity
CurrentPopulation
DesiredPopulation
Deficit
Surplus
```

`CurrentPopulation` 只统计已经进入有效距离带并位于可行空间的实体。

`DesiredPopulation` 先在可行 Region 之间公平分配，再按 StableRegionKey 处理余数和容量不足时的重分配。

Demand 必须满足人口守恒；不能因为某些 Region 不可行就静默丢失实体需求。

---

## 7. Polar Domain 外的 Attachment

实体不一定一开始就在 Polar Domain 内。

Polar Domain 外实体继续使用 Shared Flow；系统沿 Shared Flow 的稳定 next-cell chain 寻找首个安全的 Polar attachment。

```text
Far Shared Flow
      ↓
first feasible polar attachment
      ↓
Polar Transport
```

若没有合法 attachment，必须显式报告 Demand / Guidance 无法建立，不能退化成直线穿过障碍物连接目标。

---

## 8. Transport Plan

Transport Plan 根据当前人口和目标人口计算 Cell 间宏观运输。

核心输入：

```text
Polar feasible graph
Supply / Surplus
Demand / Deficit
Old quota / previous plan
Physical transport cost
```

当前求解模型使用整数 min-cost flow，并按以下优先级稳定优化：

1. 尽可能最大化成功路由的人口；
2. 最小化物理运输成本；
3. 尽量复用上一 Plan 的合法 quota，减少无意义抖动；
4. 成本相同时使用 Stable Key 决胜。

Transport 管理的是群体流量，而不是永久 Entity Owner。

---

## 9. Edge Quota

Transport Plan 产生短期 Cell Edge Quota：

```text
FromCell
→ ToCell
→ Quota
```

Quota 表示宏观上“有多少实体应该沿这条边移动”。

它不等价于：

- 永久 Region Owner；
- 单个 Agent 的长期 Slot；
- 固定 Cell 容量；
- 当前 fixed-step 的唯一通行许可证。

同一 Cell 中实体可以按 StableEntityRef / AgentId 稳定消费出口 quota；没有分配到运输 quota 的实体 Stay 或继续消费其他合法 Guidance。

---

## 10. Guidance

Transport 最终为实体产生宏观 Preferred Guidance。

典型情况：

```text
Polar Domain 外
→ Shared Flow

有 Transport Quota
→ 朝下一可行 Cell / 共享边界移动

已满足终端 Demand
→ Distance Band settle / terminal guidance
```

Guidance 只负责回答“宏观上应该往哪里走”。

它不直接解决：

```text
A 和 B 下一秒会不会撞
这个窄口能不能两个人并排过
谁应该临时让一步
最终 HardDistance 是否被突破
```

这些问题分别属于 Local Predictive 和 Particle Safety。

---

## 11. 与 Local Predictive 的边界

多个实体请求相同 NextCell，不代表必须在 Transport 层建立固定 admission lock。

Local Predictive 根据：

```text
当前位置
速度
Preferred
半径
环境
短期轨迹
BlockedAge / progress
```

判断这些实体是否可以共同前进。

如果存在共同可行速度，应允许并行推进；只有共同前进不可行时，才进行有限期、可回滚的公平让行。

Transport 不拥有 Pair constraint，也不保存永久 grant owner。

如果宏观 Guidance 持续要求实体进入局部不可行区域，应把低实际进展反馈为宏观规划事实，而不是通过 Particle 强推。

---

## 12. 与 Particle Safety 的边界

Target Region 可以基于 cohort 的尺寸和 HardGap 构造保守可行 Cell / Edge，但最终安全仍使用每个实体实时物理输入：

```text
PhysicalRadius
HardSafetyGap
SoftMargin
Mobility
```

Transport 不能为 Heavy、Melee、Ranged 等业务标签建立额外碰撞优先级。

宏观分布正确与 Particle 安全是两件不同的事情：

> Particle violation = 0 不能证明 Target Region 分布已经稳定。

---

## 13. Capability Cohort

共享同一宏观运动事实的实体可以组成 Target Cohort。

一个 Target Cohort 可以由以下事实形成：

```text
Objective / Target
Navigation Layer
Physical feasibility profile
Movement Profile
Capability Profile
Terminal Distance Band
Environment Revision
```

同一 Cohort 共享一份：

```text
Topology
Demand
Transport Plan
Edge Quota
```

不同 Cohort 可以因为实体尺寸或终端距离不同拥有不同可行图。

不得为每个实体独立建立完整 Polar graph。

Cohort 不等于 Faction。

---

## 14. Moving Target

Moving Target 必须使用 Target-relative 事实，而不是简单比较世界坐标。

Guidance 可以叠加目标速度，使群体随目标参考系移动；终态验收也应关注：

```text
Target-relative distance
Target-relative velocity
Region validity
Population stability
```

不能只用某一帧 inside-band 数量判断稳定。

业务如果采用 Acquire-Then-Hold 等策略，应由明确的 Engagement / Behavior 合同决定“何时继续重排、何时保持已有选择”，不能把该业务规则硬编码进 Polar Cell 或 Particle。

---

## 15. Plan 生命周期与重建

Plan 是短期宏观事实，不是永久绑定。

当前 Plan lifetime 为 15 fixed steps。

以下变化可以触发重建：

```text
Target Revision
Feasible Graph Hash
Cohort Membership Hash
Resource / Environment Revision
Demand 状态变化
Plan validator 失败
旧 quota 已不再可执行
```

静态 Objective 不应因为纯 Clock 推进而无条件重建全部 Cohort。

只有受影响的 Cohort 应被依赖传播唤醒。

---

## 16. Validation 与稳定 Hash

Topology、Demand、Plan、Quota Execution、Guidance 都必须有稳定可复核的 Hash / Revision。

Plan validator 至少验证：

```text
Edge 唯一且稳定排序
Quota > 0
Edge 确实存在且可行
Supply outgoing quota 合法
正 quota 子图可以到达 Deficit
中间 Cell 流量守恒
最终 unrouted / invalid 情况显式可见
```

输入顺序变化不能改变合法结果。

失败必须 fail-closed，不允许部分非法 quota 进入生产 Guidance。

---

## 17. 当前能力状态不属于本文

本文只冻结 Target Region Transport 的长期算法与边界。

当前是否已经关闭：

- T5 长窗口；
- step 886 Demand defect；
- 10k 双 Cohort；
- moving target 稳定窗口；
- WA9 性能门；

统一查看：

```text
FeatureChecklist.md
TestScenarioMatrix.md
PhasePlan.md
```

历史端口、旧 T5/T6 运行号和迁移证据不再保存在本文正文；需要追溯时使用 Git 历史。
