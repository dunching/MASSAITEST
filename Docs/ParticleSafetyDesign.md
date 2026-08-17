# Particle Safety 设计

## 1. 文档职责

本文定义 MassCrowdSimulation 的局部粒子安全层：实体之间以及实体与环境之间的最终 Soft / Hard / Swept / Obstacle / Bounds 安全约束。

本文不负责宏观导航、目标区域人口分布、攻击距离、Behavior 生命周期或业务优先级。

相关文档：

- `TargetRegionTransportFieldDesign.md` — 目标附近宏观区域运输。
- `LocalPredictiveInteractionDesign.md` — 碰撞前局部可执行速度选择。
- `CurrentArchitecture.md` — 当前接入与真实并行边界。
- `LegacyCodeInventory.md` — Demo Particle 重复实现与迁移消费者。
- `PhasePlan.md` / `FeatureChecklist.md` — 大型单 Island 等 OPEN 项。

---

## 2. 在运动链中的位置

```text
Behavior / Objective
        ↓
Macro Guidance
   ├── Shared Flow
   └── Target Region Transport
        ↓
Movement Planning
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

它回答：

> 上层已经给出候选运动以后，最终状态是否满足实体与环境安全；不满足时如何确定性修正。

它不回答：

```text
目标是谁
去哪个 Region
哪个 Cell 缺人
哪个职业优先
是否应该攻击
某张测试地图应该如何特判
```

---

## 3. 通用物理输入

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
InteractionLayer / NavLayer relevant facts
Environment facts
```

不同体型/重量继续走同一个通用 kernel，差异来自真实物理参数，不通过 Melee/Ranged/职业/地图名选择另一套碰撞规则。

`Mobility` 表达修正责任权重，不等于业务优先级或穿透权限。

---

## 4. 距离合同

实体对：

```text
PairHardDistance
= RadiusA + RadiusB + max(HardGapA, HardGapB)

PairSoftDistance
= PairHardDistance + SoftMarginA + SoftMarginB
```

环境：

```text
WallHardDistance = RadiusA + HardGapA
WallSoftDistance = WallHardDistance + SoftMarginA
```

### Hard

不可放宽的最终安全边界。

不能因为“赶路”“追目标”“抢位置”突破 Hard。

### Soft

可压缩的舒适间距/压力层。允许 SoftError 非零；Soft 不是站位 Slot。

### Swept / Environment

只看终点不足以防止高速交换/穿越，因此最终还要验证 swept pair、obstacle、bounds 和环境几何。

---

## 5. Fixed-Step 安全链

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

Candidate 失败必须形成可诊断结果。

无法找到安全进展时可以安全静止，但必须区分：

```text
Safety valid
vs
Progress valid
```

---

## 6. 与 Local Predictive 的边界

```text
Local Predictive
= 尽量在撞上前选择可共同执行速度

Particle
= 即使上层判断不完美，也不能真的穿透
```

如果长期出现：

```text
靠近
→ Particle 推开
→ Guidance 再要求靠近
→ 再次推开
```

优先检查 Guidance / Local Predictive 控制闭环，不应通过无限增强 Particle soft force 或放宽 HardDistance 掩盖。

---

## 7. 与 Target Region 的边界

Target Region 只产生宏观区域运输与 Preferred Guidance。

它可以利用 PhysicalRadius / HardGap 判断 Polar Cell / Edge 的保守可行性，但实时 pair 安全仍由 Particle 计算。

Transport 不得把：

```text
Melee
Ranged
Heavy
Region Owner
Cell Owner
```

转换成 Particle 特权。

---

## 8. Pair 与稳定顺序

逻辑 Pair 使用稳定身份规范化：

```text
(min(StableRefA, StableRefB), max(StableRefA, StableRefB))
```

候选、Pair、Contact、Correction、merge 必须稳定排序，不依赖 TMap/TSet 迭代顺序产生模拟语义。

跨 Interaction/Nav layer 的实体是否形成 pair 必须由显式通用碰撞/层合同决定，而不是 XY 重叠就默认交互。

---

## 9. 当前 Interaction Island 实现

当前 `FCrowdMassParticleWork::Solve()` 已实现**约束闭包分解**。

大致流程：

```text
Agent stable sort
        ↓
根据本 fixed-step 最大修正预算扩大 closure reach
        ↓
BuildCandidatePairs(ClosureAgents)
        ↓
Union-Find connected components
        ↓
Interaction Islands
```

例如：

```text
A-B-C-D

E-F

G-H-I
```

可形成三个互不交换约束的闭合 Island。

当前多 Island 路径会：

```text
for Island 1: Solve
for Island 2: Solve
for Island 3: Solve
        ↓
stable Pair / Result merge
        ↓
Global Applied-State Validation
```

必须强调：

> **当前这是 Island decomposition / independent sub-solve，不是 Island-level Task parallelism。**

虽然源码指标名有 `bUsedIslandSharding`，当前 Island Solve 实际仍在一个 `FCrowdMassParticleWork::Solve()` 调用内顺序执行。

Worker Particle Domain 当前也从一个 `MovementControl` Resource Work 进入，而不是“一个 Island 一个 WorkItem”。

因此当前状态：

```text
Island decomposition             = DONE
Independent sub-solve            = DONE
Stable merge + global validation = DONE
Island-level UE Task parallelism = OPEN
```

---

## 10. Monolithic fallback

多 Island 结果 merge 后必须重新执行 Applied-State Safety Validation。

若 decomposition 结果无法证明全局安全，当前实现允许明确记录并退回 monolithic solve。

这属于 fail-closed safety fallback，不代表长期性能架构目标是永久依赖 monolithic。

Trace capture 或只有一个 component 时当前也直接走 monolithic solve。

---

## 11. 大型单 Island 的最终扩展方向

一个高密度大群可能形成：

```text
A-B-C-D-E-F-G-... 数千实体
```

这时 connected-component decomposition 无法拆开。

最终并行方向：

```text
Large Interaction Island
        ↓
Stable Spatial Cells
        ↓
Stable Cell-Pair Ownership
        ↓
Per-round local work
        ↓
Deterministic barrier merge
        ↓
下一轮约束传播
        ↓
Global exact validation
```

关键点：

- 不能简单每 64 Agent 硬切；
- 跨 Shard Pair 必须有唯一 owner；
- round barrier 必须保证下一轮看到稳定上一轮结果；
- merge 顺序不能由 Task completion 决定；
- 最终 Applied-State safety 仍不可省略。

---

## 12. Island-level Task 并行与单 Island 分片是两个问题

后续性能工作不要把两者混成一件事。

### A. 多 Island 并行

当前已经有安全 decomposition，下一步可以研究：

```text
Island A → Task
Island B → Task
Island C → Task
→ stable merge
```

这是相对低风险的并行扩展，但仍需处理 bounded task/shard 数量和 deterministic merge。

### B. 单大 Island 内并行

这是更难的问题，需要 Cell-Pair Owner / per-round barrier。

即使完成 A，也不能说明 10k 高密度单 Island 已解决。

---

## 13. Reactive Motion

Knockback / KnockUp 等 Reactive Motion 不能在 Particle 之后直接写 Transform 绕过 Safety。

正确模型：

```text
Combat / Reactive state
→ Movement Planning / Movement Predict
→ Particle Safety
→ Finalize
```

若安全层无法完全实现 requested impulse，应记录 requested / realized 差异，而不是突破 Hard safety。

---

## 14. Determinism / Rollback

任何影响结果的下列事实都必须可确定性恢复：

```text
Particle settings
输入 Agent 顺序规范化结果
Environment revision
Candidate/closure facts
Applied result
必要的 iterative state
```

故障诊断必须尽量重放同一个 immutable work input；不能稍后在 GT 重新 gather 一个已经变化的世界再声称复现同一步。

---

## 15. 禁止项

生产 Particle 不允许：

- 按 AgentId 写特殊分支；
- 按地图名/TestCase 调另一套 solver；
- 永久 Agent→Cell owner；
- 为了通过 corridor 放宽 Hard safety；
- 把 visual footprint 当碰撞半径；
- 在 Particle 后直接应用未复验的位移；
- 把 `bUsedIslandSharding` 当成“已经多线程并行”的证明。

---

## 16. 当前与最终边界

当前已经成立：

```text
通用 Core Particle kernel
Worker Particle domain
conservative Interaction Island decomposition
独立子 Solve
stable merge
Global Applied-State validation
monolithic safety fallback
```

当前未完成：

```text
Island-level UE Task parallelism
Large single-island internal parallelism
Cell-Pair Owner / per-round barrier
完整 10k high-density acceptance
```

当前阶段状态以 `FeatureChecklist.md` / `PhasePlan.md` 为准。
