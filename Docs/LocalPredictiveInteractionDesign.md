# Local Predictive Interaction 设计

## 1. 文档职责

本文定义 Shared Guidance 与 Particle Safety 之间的通用局部预测交互层。

它负责在真正发生接触之前，根据邻域位置、速度、实体尺寸和短期轨迹，选择尽量接近 Preferred Movement 的局部可执行速度。

完整链路：

```text
Shared Flow / Target Region / Other Guidance
        ↓
Preferred Velocity
        ↓
Local Predictive Interaction
        ↓
Locally Feasible Velocity
        ↓
Movement Predict
        ↓
Particle Safety
```

本文不负责宏观目标选择，也不替代 Particle 最终安全闭环。

---

## 2. 要解决的问题

如果只使用宏观 Guidance + Particle，可能形成：

```text
A / B 都想进入同一局部区域
        ↓
Movement Predict
        ↓
马上冲突
        ↓
Particle 把双方推开
        ↓
下一步宏观 Guidance 又让双方靠近
        ↓
重复振荡
```

Local Predictive 的目标是把这类“马上会发生的局部冲突”提前处理。

它不是为了给 Cell 建立永久 Owner，也不是给窄口写 admission 特判。

---

## 3. 通用性原则

生产 Kernel 不得依赖：

```text
TestCase
MapName
PortalId
Target 场景标签
Melee / Ranged
Faction
PositionId
永久 SlotId
```

它只消费通用运动与环境事实。

因此相同规则必须同时适用于：

```text
开放交叉
双向交换
窄通道
目标附近拥挤
移动目标
自由游荡
```

不同场景只是输入几何和 Preferred 不同，不应切换另一套局部避让算法。

---

## 4. 输入数据

每个参与实体至少提供：

```text
StableEntityRef / AgentId
Position
Velocity
PreferredVelocity
PhysicalRadius
HardSafetyGap
MaxSpeed
InteractionLayer
BlockedAge / short fairness state
```

环境提供：

```text
Obstacle / Bounds facts
Navigation / Interaction layer
Version / Revision
```

所有输入数组在进入 Kernel 前必须按稳定 Key 排序。

---

## 5. 邻域与 Pair

使用稳定空间索引生成邻域候选。

Pair Key 固定为：

```text
(min(A, B), max(A, B))
```

对每个 Pair，根据：

```text
当前位置
当前速度
PreferredVelocity
双方半径
HardGap
Time Horizon
```

计算最近接近时间与预测最小间距。

只有真正存在短期预测冲突的 Pair 才进入约束图。

高速交换场景必须使用 swept / trajectory-aware 候选生成，不能只按当前点距离判断邻居。

---

## 6. Conflict Component

当前冲突 Pair 形成无向图。

对 Pair 图按 StableEntityRef / AgentId 稳定 BFS 构造 Conflict Component。

例如：

```text
A-B-C-D

E-F
```

形成两个局部求解范围。

Component 是当前 fixed-step 的瞬时求解边界，不是：

```text
Cohort
Faction
Target Region
Portal
永久业务组
```

冲突消失后 Component 自然消失。

---

## 7. 速度约束

局部约束统一表示为二维速度半平面，例如：

```text
dot(v - Point, Normal) >= 0
```

再与：

```text
|v| <= MaxSpeed
```

共同形成可行速度域。

求解目标是：

> 在满足全部局部 Pair / Environment 约束的前提下，找到最接近 PreferredVelocity 的速度。

无冲突实体应尽量精确保留限速后的 PreferredVelocity；局部层不能无理由恢复旧位置、旧阵型或 Cell center。

---

## 8. 连续求解与量化

先求连续速度解，再进行稳定量化。

量化后必须重新验证全部约束。

正确顺序：

```text
Continuous feasible velocity
        ↓
Quantize
        ↓
Local repair / stable candidate selection
        ↓
Pair validation
        ↓
Environment validation
        ↓
Publish
```

不能出现：

```text
连续解合法
→ 量化后非法
→ 仍然提交
```

数值失败必须显式 invalid。

---

## 9. 几何容量不是固定 Cell 容量

Local Predictive 不使用固定：

```text
Cell capacity = 1
```

所谓“当前能通过多少实体”，应该由真实可行速度集合自然决定。

如果三个人可以在当前几何下同时安全前进，就都应该前进。

只有在共同保持期望进展确实不可行时，才需要临时让行。

---

## 10. 有限期公平让行

当 Component 内所有实体无法同时保持合理进展时，可以使用短期、可回滚的公平状态选择临时 grant / yield。

公平排序可以基于：

```text
BlockedAge descending
→ quantized progress deficit descending
→ StableEntityRef / AgentId ascending
```

核心原则：

1. 等得越久的人更有机会获得进展；
2. 决胜稳定；
3. grant 有固定租期；
4. 参与者或 revision 变化时重新计算；
5. grant 只调整避让责任，不删除 Pair safety constraint；
6. winner 仍然不能穿人、穿墙。

让行状态必须能进入 rollback / correction，避免 replay 后公平顺序变化。

---

## 11. BlockedAge

`BlockedAge` 只在实体确实具有正向请求、但局部实际进展长期不足时增长。

有正常进展后应确定性衰减或清零。

BlockedAge 不是业务优先级，也不能由职业、Faction 或 Agent class 直接覆盖。

---

## 12. Environment Constraint

Local Predictive 不能只处理 Agent-Agent Pair。

如果它选择一个侧移速度直接指向墙体，下一层 Particle 又会把实体推回，从而制造新的振荡。

因此局部速度还应考虑：

```text
Obstacle
Flow / World Bounds
Interaction Layer
短期 swept endpoint
```

环境约束仍然只是局部速度可行性判断；最终位置级安全由 Particle Safety 再次验证。

---

## 13. 与 Target Region Transport 的边界

Target Region 产生：

```text
Region population demand
Transport Plan
Cell Edge Quota
Preferred Guidance
```

Local Predictive 不读取 Region 业务语义，只看到最终 Preferred。

多个实体请求同一 NextCell 时：

```text
Transport
= 宏观上这些人口应该往那边走

Local Predictive
= 这一小段时间内大家怎么走才互不冲突
```

不得把局部 grant 变成永久 Region / Cell Owner。

如果长期 actual progress 接近零，应把该事实反馈给宏观层，而不是无限重复同一 Preferred。

---

## 14. 与 Particle Safety 的边界

```text
Local Predictive
= 预测并减少未来冲突

Particle Safety
= 最终保证 Hard / Swept / Obstacle / Bounds
```

Local Predictive 结果仍然必须进入 Particle Safety。

它没有权力绕过：

```text
HardDistance
Swept collision
Environment
Bounds
```

Particle 也不应承担公平调度或 Target 宏观人口分布。

---

## 15. Mobility 边界

`Mobility` 主要属于 Particle 修正责任权重。

Local Predictive 的公平让行不应该因为 Heavy / Light、Melee / Ranged 等职业身份直接产生“不可让行特权”。

如果未来确需不同局部责任模型，也必须通过明确、通用、可验证的物理 / movement profile 合同引入，而不是测试场景特判。

---

## 16. Deterministic Merge

不同 Conflict Component 可以并行求解，但最终输出必须按稳定顺序合并。

Task 完成先后不能决定：

```text
谁获得 grant
谁 yield
最终 Velocity
Stable Hash
```

所有 Pair、Constraint、Component、Result 和 fairness state 都必须使用稳定 Key。

---

## 17. Failure 语义

以下情况必须 fail-closed：

```text
非法输入 / NaN
重复 Pair
开放或不完整 Component
连续可行域求解失败
量化后无法修复
Environment validation 失败
Joint pair validation 失败
容量溢出
```

紧急情况下可以输出安全静止，但“安全静止”不能被统计为“局部求解成功”。

Candidate Result 与 Applied Result / validation status 必须分开记录。

不得静默恢复旧 ORCA / Portal / Admission fallback。

---

## 18. 状态与 Hash

需要跨 fixed-step 保存的状态应保持最小化，例如：

```text
BlockedAge
Grant expiry / epoch
必要的短期 component fairness state
```

不建立永久业务 Owner Fragment。

Step Hash 至少覆盖：

```text
Settings
Stable inputs
Pair / Constraint keys
Component membership
Grant decisions
Continuous result
Quantized result
Fairness state
```

同样输入和同样历史状态必须产生同样结果。

---

## 19. 验收边界

Local Predictive 的专项验收至少覆盖：

```text
无冲突 Preferred 精确保留
双向交换
多人交叉
窄通道
同目标接近
移动目标
高速交换 Pair
Environment side-step
InteractionLayer
BlockedAge fairness
Grant expiry / switch
输入反序等价
rollback / replay
不同 Task 完成顺序
```

“Particle violation = 0”不能替代 Local Predictive 验收，因为 Particle 可以在最后强行保证安全，但仍可能存在长期振荡和零进展。

---

## 20. 当前状态不属于本文

本文只定义长期 Local Predictive 设计。

旧 ORCA、PortalAdmission、具体端口、T6M 历史运行号、processor 迁移阶段和历史 Hash 不再保存在正文。

当前实现状态与证据统一查看：

```text
CurrentArchitecture.md
FeatureChecklist.md
TestScenarioMatrix.md
```

历史过程使用 Git 历史追溯。
