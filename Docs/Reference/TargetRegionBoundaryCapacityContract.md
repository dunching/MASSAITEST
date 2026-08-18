# Target Region Boundary / Capacity Contract

## 1. 文档职责

本文细化 `TargetArchitecture.md` 中 **Target-relative Polar Transport Field** 的边界语义，定义目标接近 NavMesh / Environment 边缘、角落或局部可行空间收缩时，Target Region 的正确行为。

本文是长期设计合同，不代表当前 `main` 已完整实现。当前实现状态看 `../FeatureChecklist.md`，阶段顺序看 `../PhasePlan.md`，验收证据看 `../TestScenarioMatrix.md`。

本合同解决的核心问题是：

```text
Moving Target 接近地图边缘 / 角落
→ 完整极坐标环不再全部落在有效导航空间
→ Target Region 应如何生成有效 Cell
→ 有限有效容量被占满后，其余 Agent 应如何处理
```

---

## 2. 核心设计结论

### 2.1 Target 不因 Target Region 完整性被强制反弹

Target 的业务运动合同与 Target Region 的空间可行性分离。

Target 可以正常移动到：

```text
NavMesh 边缘
Environment 边缘
障碍物附近
角落
局部狭窄区域
```

不能仅为了保持完整圆形 Polar Topology，给 canonical Moving Target 强制启用 reflected motion bounds。

Target Region 必须适应当前有效导航空间，而不是要求目标永远处于完整圆环可生成的位置。

### 2.2 Target Region 是 Environment / NavMesh clipped topology

Target Region 仍以 Target 为原点生成：

```text
Radial Band × Angular Sector
```

但 Cell 是否进入有效 Topology / Demand Capacity，必须服从当前真实导航与安全空间。

概念链：

```text
Polar candidate cell
        ↓
Navigation / Environment validity
        ↓
Reachability
        ↓
Hard clearance / target clearance
        ↓
Capability / Physical profile compatibility
        ↓
Feasible Target Cell
```

落在无效 NavMesh、不可达区域、障碍物硬清距内或其它明确非法空间中的 Cell：

```text
不计入有效 Target capacity
不生成可消费 Demand
不允许成为 Plan / Execution 的合法占用目标
```

目标处于地图中央时，可得到接近完整的极坐标域；目标靠边时，域自然被裁剪；目标处于角落时，只保留真实可行的那一部分。

### 2.3 “Region 不完整”本身不是 Demand failure

例如：

```text
理论 Demand Regions = 16
当前 Feasible Regions = 3
```

只要这 3 个 Region / 其中的 Cell 在真实导航空间内有效，就不能仅因为其余 Region 被边界裁掉而判定整个 Target Demand invalid。

需要区分：

```text
A. clipped but valid / capacity-limited
B. genuinely invalid / unreachable / inconsistent
```

A 是正常运行状态；B 才属于 fail-closed correctness error。

---

## 3. Cell 具有有限容量，但不是永久 Agent Slot

### 3.1 保持“无永久 Slot”原则

本合同不引入：

```text
永久 Agent → Cell 绑定
永久 Region Owner
Demo-specific Slot manager
第二套 GT placement authority
```

Target Cell 仍是 Macro Guidance / Population Transport 的空间单元。

### 3.2 Cell Capacity 必须有限

“Navigation Cell 可共享”不等于“无限共享”。

每个可行 Cell 必须有确定性的有限 Occupancy Capacity。Capacity 应由真实可用空间与 Cohort / Physical Profile 合同决定，例如可依赖：

```text
Cell usable geometry
PhysicalRadius
HardSafetyGap
SoftMargin / spacing policy
Capability / profile
```

具体公式属于实现设计，但必须满足：

```text
Capacity >= 0
Deterministic
Environment-valid
Profile-aware where required
不可因 Agent 数增加而无限扩张
```

某些 Cell 的容量可以是 1，也可以大于 1；本合同不强制“一个极坐标 Cell 永远只允许一个 Agent”。真正硬约束是：**任何 Cell 都不能被无限过量占用。**

### 3.3 Total Target Capacity

对一个 Cohort：

```text
TotalFeasibleCapacity
= Σ Capacity(feasible cell)
```

Demand 的“希望接近目标的人数”与“当前物理上能合法接纳的人数”必须分开：

```text
DesiredPopulation
AssignablePopulation = min(DesiredPopulation, TotalFeasibleCapacity)
OverflowPopulation   = max(0, DesiredPopulation - TotalFeasibleCapacity)
```

`DesiredPopulation > TotalFeasibleCapacity` 是容量饱和，不自动等于算法失败。

---

## 4. Saturation / Overflow 是合法模拟状态

### 4.1 有效区域占满后不得继续往里挤

当目标位于角落，只剩约 1/4 有效区域，且这些 Cell 的容量已经被合法占用后：

> **其它 Agent 不得继续争抢、占用或持续向已饱和 Target Region 内部施压。**

系统不能把“大家继续朝目标中心跑，最后交给 ORCA / Particle 顶住”当作容量控制方案。

### 4.2 Overflow 必须与真正的 Unrouted Failure 区分

没有拿到 Target Region 容量的 Agent，语义上属于正常的 Capacity Saturation / Overflow 状态，而不是“找不到路”的 correctness failure。

实现名称可以最终选择，例如：

```text
CapacityHold
OverflowHold
WaitingForCapacity
```

但验收语义必须能区分：

```text
CapacityHold / Overflow
= 有路、有有效 Target topology，但当前合法容量已满

UnroutedFailure
= 本应存在合法路由/占用，却因拓扑、计划、引用或其它错误无法得到有效结果
```

### 4.3 Overflow Agent 的行为要求

未获容量的 Agent：

- 不得继续 consume 已满 Cell 的占用权。
- 不得持续生成指向饱和 Target interior 的强制 guidance。
- 必须保持在合法、安全的外部位置或外围 holding 区域。
- 可以继续遵守 Shared Flow / Local Predictive / Particle 的通用安全约束。
- 当新容量出现时，可以按稳定规则重新参与分配。

具体外围 holding 速度/位置策略可以后续实现，但不能通过继续向内挤压来“等待”。

---

## 5. Plan / Execution 拥有容量分配语义

### 5.1 谁决定“能不能占”

Target Plan / Execution 负责宏观容量和占用资格。

```text
Topology
→ Feasible Cell + Capacity
→ Demand
→ Plan
→ Execution / Claim
→ Target Guidance
```

Local Predictive / ORCA / Particle 只负责局部可执行性与安全最终裁决，不负责把过量人口塞进有限 Target capacity。

### 5.2 Occupancy / Claim 不能超卖

Plan / Execution 必须保证：

```text
Occupied(cell) + ActiveClaims(cell) <= Capacity(cell)
```

需要避免：

- 多个 Agent 同时认为自己拥有同一份最后容量。
- plan reuse 后旧 claim 与新 claim 重复占用。
- target 移动后失效 Cell 的 claim 继续计入有效占用。
- quota/claim 使用顺序受 Task 完成顺序影响。

所有分配、释放、迁移必须遵守 Stable Key / deterministic merge 原则。

---

## 6. Moving Target 下的动态 Cell 生命周期

Target 移动时，Polar Topology 会相对 Environment 变化。

### 6.1 Cell 从 valid 变 invalid

当原来可行的 Cell 因目标移动而被 NavMesh / Environment 裁掉：

```text
Cell invalidated
→ 该 Cell 不再提供 capacity
→ 相关 claim / occupancy 需要释放或迁移
→ Agent 尝试进入其它 feasible capacity
→ 若无容量则进入 CapacityHold / Overflow
```

不能：

- 保留对 invalid Cell 的永久 claim。
- 为保持旧 Plan 强行把 invalid Cell 当可行。
- 把 Agent 推入无效导航区域。

### 6.2 Cell 从 invalid 变 valid

当目标离开边缘/角落，新的可行 Cell 重新出现：

```text
New feasible capacity
→ deterministic demand/plan refresh
→ Overflow Agent 可重新参与
→ 按稳定顺序补充占用
```

不应出现大规模抢占往返振荡。

---

## 7. 与 Shared Flow / Local Predictive / Particle 的职责边界

### Shared Flow

负责目标区域之外和大尺度世界空间的可达性、导航方向及 Environment resource。

### Target Region

负责目标附近 clipped Polar Topology、有限容量、供需、计划与宏观占用资格。

### Local Predictive / ORCA

负责短时间尺度的速度冲突、让行和局部可执行性。

### Particle

负责 Hard / Swept / Obstacle / Bounds / Environment Safety。

正确关系：

```text
Environment-valid Target Capacity
        ↓
Target Plan decides admission
        ↓
Movement / Local Predictive executes
        ↓
Particle enforces safety
```

错误关系：

```text
Target Plan 无限 admit
        ↓
所有 Agent 往里挤
        ↓
依赖 ORCA / Particle 被动顶住
```

---

## 8. Fail-Closed 边界

以下可以是正常状态：

```text
feasible regions < theoretical regions
DesiredPopulation > TotalFeasibleCapacity
OverflowPopulation > 0
Target 位于 NavMesh 边缘/角落
部分 Cell 动态失效/恢复
```

以下仍应 fail-closed：

```text
同一 capacity 被重复 claim
Occupied > Capacity
Plan / Execution revision 不一致
invalid Cell 被成功分配
本应 reachable 的 Cell 因 stale resource / clock domain 错误整体失联
非法 topology / stale lifecycle / corrupted hash
```

---

## 9. 当前 Moving T5 证据与本合同的来源

当前 `main` 已包含 Moving objective orchestration 修复：

```text
Objective effective tick
→ Worker persistent absolute tick domain

Production moving objective
→ Runtime-owned dynamic SharedFlow refresh
→ versioned Environment resource
→ Worker Target
```

这关闭了历史 Moving `fixed_step=398` 的：

```text
clock-domain mismatch
+ stale dynamic SharedFlow
+ source_attachment_failures=20/20
```

但 canonical Moving 长窗口随后在目标接近边缘时暴露新的容量语义缺口：

```text
absolute fixed_step = 1460
target = (-3171, 1900)
feasible_regions = 3 / 16
desired = 19
source_attachment_failures = 0
```

该失败说明 SourceAttachment 已正常，而现有 Demand 合同仍把“可行域被边界裁剪、容量不足”作为拒绝条件。

本文正式选择的方向是：

> **实现 clipped Target topology + finite capacity + overflow holding；不通过限制 Target 必须反弹来规避边界情况。**

---

## 10. 实施原则

实现时：

- 不按 map / step / AgentId / region 写特判。
- 不恢复 Host Prepared Target transaction。
- 不增加第二个 Target / SharedFlow Production owner。
- 不把 CapacityHold 伪装成成功的 `UnroutedFailure`。
- 不让 ORCA / Particle 代替 Target admission control。
- 不要求完整 16 Region 才能运行。
- 不为了通过 T5 简单删除 Demand validity gate；必须建立真实容量合同。
- 不把 temporary diagnostic state 变成第二套 simulation state。

---

## 11. 最低验收矩阵

至少覆盖：

### A. Center / full topology

- 完整或近完整 polar region。
- 与现有 TargetRegionTransport baseline 一致。
- 无不必要 Overflow。

### B. Edge clipped topology

- Target 靠一侧边界。
- 只使用真实有效 Cell。
- 被裁剪区域不产生 capacity / claim。
- Demand 不因理论 Region 缺失直接失败。

### C. Corner / quarter topology

- Target 进入角落，仅剩约 1/4 可行域。
- 可行 Cell 被正常填充。
- 超额 Agent 进入 CapacityHold / Overflow。
- 无 Agent 持续向已满区域内挤压。

### D. Saturation / release

- 容量满时不超卖。
- Agent 离开/死亡/Target 移动导致容量释放后，等待 Agent 可稳定补位。

### E. Moving edge-in / edge-out

- Target 从中央移动到边缘/角落，再离开。
- Cell invalidation / reactivation、claim release / reassignment 正确。
- 无 stale plan / stale claim。

### F. Determinism

- 相同输入重复运行 Stable Hash 一致。
- Task 完成顺序不影响 capacity allocation / overflow population。

### G. Existing regression invariants

- Default Unity / DisableUnity build PASS。
- TargetRegionTransport automation PASS。
- RuntimeV2 Target PASS。
- Target affected cohort 10k scoped invalidation PASS。
- Static T5 >=1000 Tick 不回归。
- Moving T5 >=1000 Tick，包含边缘/角落时不因合法容量饱和 fail。

---

## 12. 完成定义

该合同实现完成时，应能稳定表达：

```text
Target freely moves within business motion contract
        ↓
Polar topology clipped by real navigation space
        ↓
Finite feasible capacity
        ↓
Deterministic Plan / Claim
        ↓
Assigned Agents enter target region
Overflow Agents hold outside
        ↓
Local Predictive / Particle enforce safety
```

最终目标：

> **边界/角落减少的是可用 Target capacity，而不是系统正确性；容量不足产生合法 Overflow，而不是让所有 Agent 继续往有限空间里挤。**
