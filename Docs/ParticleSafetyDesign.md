# Particle Safety 设计

## 1. 文档职责

本文定义 MassCrowdSimulation 的局部粒子安全层：它负责实体之间以及实体与环境之间的最终 Soft / Hard / Swept / Obstacle / Bounds 安全约束。

本文不负责宏观导航、目标区域人口分布、攻击距离、Behavior Source 生命周期或业务优先级。

相关文档：

- `TargetRegionTransportFieldDesign.md`：目标附近的宏观区域运输与人口分布。
- `LocalPredictiveInteractionDesign.md`：碰撞发生前的局部可执行速度选择与公平让行。
- `CurrentArchitecture.md`：当前生产接入状态。
- `PhasePlan.md` / `FeatureChecklist.md`：大型单 Interaction Island 等当前未关闭项。

---

## 2. 在完整运动链中的位置

```text
Behavior / Objective
        ↓
Macro Guidance
   ├── Shared Flow
   └── Target Region Transport
        ↓
Preferred Movement
        ↓
Local Predictive Interaction
        ↓
Movement Predict
        ↓
Particle Safety
        ↓
Facing / Finalize
```

Particle 是最终 Safety Layer。

它回答的是：

> 在已经得到期望运动和局部协调结果之后，最终候选状态是否仍满足实体与环境的安全约束；若不满足，应该如何以确定性方式修正。

Particle 不回答：

- 目标应该是谁；
- 应该去哪个 Region；
- 哪个 Cell 缺人；
- 哪个职业优先；
- 当前是否应该攻击；
- 某个测试场景应该如何特判。

---

## 3. 统一实体输入

Particle 只消费通用物理事实：

```text
StableEntityRef / AgentId
StartPosition
PredictedPosition
Velocity
PhysicalRadius
HardSafetyGap
SoftMargin
Mobility
InteractionLayer
Environment facts
```

异构实体继续使用同一个 kernel。

Small / Standard / Large、Light / Heavy 的差异来自真实半径、HardGap、SoftMargin 和 Mobility，而不是职业专用碰撞规则。

`Mobility` 表达修正责任权重，不得额外引入“靠墙者”“穿行者”“近战”“远程”“Slot owner”等第二套安全优先级。

---

## 4. 距离合同

实体对的核心距离定义：

```text
PairHardDistance =
    RadiusA + RadiusB + max(HardGapA, HardGapB)

PairSoftDistance =
    PairHardDistance + SoftMarginA + SoftMarginB
```

环境边界：

```text
WallHardDistance = RadiusA + HardGapA
WallSoftDistance = WallHardDistance + SoftMarginA
```

### Hard

Hard 是不可放宽的最终安全约束。

任何正常提交结果都不得以“为了通行”“为了达到目标”“为了追上队伍”为理由突破 Hard 安全距离。

### Soft

Soft 是可压缩的舒适间距和压力响应。

SoftError 可以长期非零；系统不要求所有实体最终精确达到 SoftDistance。Soft 不得被误用成永久站位或目标环线。

### Swept / Environment

只验证终点距离不足以防止高速穿越，因此最终安全必须同时考虑运动过程中的 Swept 约束、障碍、Bounds 和环境几何。

---

## 5. Fixed-Step 安全链

统一流程：

```text
Resolved / Preferred Velocity
        ↓
Local Predictive Result
        ↓
Movement Predict
        ↓
Pair Soft Response
        ↓
Pair Hard / Swept
        ↓
Obstacle / Bounds / Environment
        ↓
Quantize
        ↓
Applied-State Validation
        ↓
Final Safe State
```

Candidate 失败必须形成可诊断结果；不得静默把非法状态当成功提交。

在无法找到合法移动时，可以产生保守的安全静止或受限运动，但必须把“没有进展”与“安全成立”区分开来。

---

## 6. 与 Local Predictive 的边界

Local Predictive 的职责是尽量在冲突真正发生之前找到可共同执行的速度。

Particle 的职责是最终守住安全边界。

因此：

```text
Local Predictive
= 尽量别撞上

Particle
= 即使上层判断不完美，也不能真的穿透
```

如果多个实体持续争抢同一个局部目标，出现：

```text
靠近
→ Particle 推开
→ 上层再次要求靠近
→ 再次推开
```

这属于上层 Guidance / Local Predictive 合同问题，不能通过不断增强 Particle Soft 力、修改 Mobility 或放宽 HardDistance 来掩盖。

---

## 7. 与 Target Region Transport 的边界

Target Region Transport 只产生宏观区域运输需求和 Preferred Guidance。

它可以基于实体的 `PhysicalRadius + HardSafetyGap` 构造保守可行的 Polar Cell / Edge，但最终实体对安全仍由 Particle 使用实时几何事实计算。

Transport 不得把：

```text
Melee
Ranged
Heavy
Region Owner
Cell Owner
```

转换成 Particle 特权。

攻击距离决定 Target terminal band；物理尺寸和 Mobility 决定局部安全响应。两者必须分离。

---

## 8. Interaction Pair 与稳定顺序

实体对使用稳定 Pair Key：

```text
(min(StableRefA, StableRefB), max(StableRefA, StableRefB))
```

同一个 pair 不能因为查询方向不同而出现两次不同语义。

邻域候选、Pair、Contact、Correction 和最终 merge 都必须使用稳定排序，禁止依赖 `TMap` / `TSet` 的非稳定迭代顺序决定结果。

跨 `InteractionLayer` 的实体默认不形成普通地面 Pair；需要跨层交互时必须由显式能力/碰撞合同开启。

---

## 9. Interaction Island

Particle 的并行边界不是简单的固定 Entity ID 分片。

如果约束图为：

```text
A-B-C-D

E-F

G-H-I
```

且三组在当前安全闭包内互不影响，可以形成三个独立 Interaction Island，各自求解。

```text
Island 1 → Solve
Island 2 → Solve
Island 3 → Solve
        ↓
Deterministic Merge
        ↓
Global Applied-State Validation
```

多个闭合 Island 可以独立执行，但最终仍必须通过全局 Applied-State Validation。

如果分解或局部求解无法证明最终状态安全，应 fail-closed；允许使用明确记录的 monolithic fallback，而不是提交无法证明正确的局部结果。

---

## 10. 大型单 Island 的最终并行方向

当大量高密度实体形成一个巨大的闭合 Interaction Island 时，不能仅按 64 个 Entity 硬切 Shard，因为跨 Shard pair 会共享约束。

最终扩展方向是：

```text
Large Interaction Island
        ↓
Stable Spatial Cells
        ↓
Stable Cell-Pair Ownership
        ↓
Per-round Local Work
        ↓
Barrier Merge
        ↓
下一传播轮
        ↓
Global Safety Validation
```

必须保证：

1. 每个 pair 只有唯一 Owner；
2. 同一传播轮的 shard 只写自己的局部输出；
3. 轮间状态只能在 Barrier 后可见；
4. Merge 顺序稳定；
5. 不因线程完成顺序改变最终结果；
6. 安全验证失败时整轮拒绝或进入明确 fallback。

当前是否已经完成大型单 Island 分片，以 `FeatureChecklist.md` 和 `PhasePlan.md` 为准；本文只冻结设计原则。

---

## 11. Reactive Motion

Knockback、Impulse 等水平受击运动不能在 Particle 之后直接修改 Transform。

正确链路是：

```text
Hit / Reactive Fact
      ↓
Movement / Constraint Contribution
      ↓
Movement Predict
      ↓
Particle Safety
      ↓
Final Apply
```

击飞的 Z 轴可使用独立确定性 ballistic 状态，但 XY footprint 仍需遵守统一安全链，除非未来显式引入真正的 3D 空中穿越能力。

---

## 12. 确定性与 Fail-Closed

Particle 必须满足：

- StableEntityRef / Pair Key 稳定；
- 输入物理顺序改变不影响结果；
- Shard 完成顺序改变不影响结果；
- hash 可用于 replay / correction / regression；
- 缺失实体、重复 pair、非法半径、NaN、开放安全闭包、容量溢出或 applied-state 不合法时 fail-closed；
- 不能通过静默丢 pair、静默截断实体或隐藏视觉实例制造“安全通过”。

---

## 13. 验收边界

Particle 能力验收至少需要覆盖：

```text
不同半径 pair
不同 Mobility pair
靠墙传播
高速 swept crossing
Obstacle / Bounds
InteractionLayer
多独立 Island
单大型高密度 Island
Knockback / Reactive Motion
确定性 replay
不同输入顺序
```

Particle “零 Hard violation”只证明最终安全成立，不证明宏观 Target 分布合理、吞吐量足够或终态稳定；这些能力必须由对应 Target / Local Predictive / 场景验收分别证明。
