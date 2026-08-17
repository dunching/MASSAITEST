# Demo Business Planning 设计

## 1. 文档职责

本文只定义 `MassCrowdDemoBusiness` 的**业务规划、Provider、Host Intent 和宿主接入边界**。

它不定义通用 Worker Runtime、Behavior Source 协议、Projectile 几何、Networking 或 Presentation 生命周期。

相关事实源：

- 通用 Behavior Source：`EntityBehaviorSourceArchitecture.md`
- Standard Sources：`MassCrowdStandardSourcesDesign.md`
- Projectile / HitFact：`MassProjectileHitFrameworkDesign.md`
- 当前生产结构：`CurrentArchitecture.md`
- 当前完成状态：`FeatureChecklist.md`

---

## 2. 模块边界

`MassCrowdDemoBusiness` 是项目侧业务模块，不是插件通用机制层。

它负责：

```text
Demo Source Provider
Capability / Source 业务映射
Source Set Diff
Business Planner
Business Ledger
Context Request
Host Intent
Prepared Business Patch
Demo-specific Stable Hash
```

它不应直接依赖或操作：

```text
UWorld / Actor
Mass EntityManager
Networking transport
Presentation slot
Projectile trajectory / broadphase
Particle / Target / Shared Flow kernel
Demo Scenario UObject
```

需要 World、Mass、Nav、Network、Presentation 或 Scene 对象的适配，由 `MassAICrowdDemo` 主宿主模块负责。

依赖方向固定为：

```text
MassCrowdDemoBusiness
        ↓
MassCrowd public contracts / Standard Sources

MassAICrowdDemo
        ↓
MassCrowdDemoBusiness + MassCrowd plugin adapters
```

通用插件不能反向引用 Demo Business。

---

## 3. Planner 输入合同

Planner 只读取按 `StableEntityRef` 稳定排序的不可变 POD Snapshot。

典型输入包括：

```text
StableEntityRef / Lifecycle
Faction / Capability
当前业务账本事实
Objective / Target fact
Cargo / Warehouse fact
已解析的 Movement / Combat prerequisite
Fixed Simulation Tick
Resource / Context Revision
```

Planner 不得在规划阶段：

- 直接查询 Actor 或 World；
- 直接读取 Mass Fragment；
- 修改 Source Runtime；
- 修改 Health / Cargo / Cooldown / Ledger；
- Spawn Projectile；
- 发布 Network / Presentation 事件。

Planner Registry 使用稳定 `PlannerId`，注册后冻结。重复 ID、未知 Planner、Schema/Revision 错误、重复实体、超出有界输出或互斥 Host Intent 冲突必须拒绝完整 Decision Batch。

---

## 4. Planner 输出合同

Planner 通过有界 Writer 只输出**意图和候选事实**：

```text
Desired Source Set
Context Requests
Host Intents
Business Diagnostics
Stable Hash
```

### Desired Source Set

用于表达 Agent 希望持续拥有哪些 Behavior Source。

例如 Pursue + MaintainDistance + FaceEntity 是持续 Source 组合，而不是把“Attack”重新变成一个通用互斥 Behavior。

### Context Requests

用于向宿主请求后续 Evaluate 所需的版本化 POD Context，例如 Target Kinematics、Formation Anchor 或业务上下文。

### Host Intent

用于表达不能由 Behavior Source 自己提交的业务动作，例如：

```text
Claim / Requeue / Cancel
Pickup / Deliver
Attack Commit Request
Round-specific Fire Request
测试 Hit 注入
```

Host Intent 不是写操作；它必须先转成 Prepared Patch 并完成完整验证。

---

## 5. Demo Planner 组合

Demo 可以拥有领域语义明确的 Planner，例如：

```text
Logistics
PursueAttack
GuardFlee
Roam
Escort
VatShowcase
RangedAttack
NoBusiness
```

这些 Planner 只是验证同一套 Runtime 能否承载不同产品业务。

Reaction 是正交层：HitReaction、TimedImpulse、Death MovementLock 等临时状态可以覆盖普通移动贡献，但不能无理由删除原有持久 Source、Cargo 或任务状态。

Faction 只决定关系、权限、目标过滤和伤害规则；不能直接选择 Particle、Networking 或 Presentation 实现。

---

## 6. Source 与业务提交边界

业务 Planner 负责“为什么做、目标是谁、业务是否合法”，Standard Source / Runtime 负责“如何形成通用运动贡献并安全执行”。

例如远程攻击：

```text
Planner
→ 选择/验证业务 Target
→ Desired Pursue / Distance / Facing Sources
→ Attack Host Intent

Behavior / Movement Runtime
→ Resolve Sources
→ Guidance / Local Predictive / Particle

Host Commit
→ 验证 Attack Intent
→ 生成 Projectile / Damage 等业务 side effect
```

Attack 合法性、Damage、Inventory、Cargo、Mission 等不能被塞进通用 Movement Source。

---

## 7. 原子提交

生产顺序应保持：

```text
Gather immutable business facts
        ↓
Planner WORK
        ↓
Stable merge + validation
        ↓
Build Source Command / Context / Host Intent candidates
        ↓
Behavior / Movement / Projectile / Business prepare
        ↓
Final Validate
        ↓
no-fail Host Apply
```

所有可能失败的检查必须发生在第一次业务写入前。

Planner、Coordinator 或 Provider 不得直接修改 Ledger、Health、Cargo、Cooldown 或 Projectile authoritative state。

Host side effect 只能消费已经通过 Owner Commit Barrier 的 Prepared Plan / Ordered Event。

---

## 8. 确定性与幂等

- Planner 输入必须稳定排序。
- Planner / Provider / Source / Schema / Adapter 使用稳定数值 ID。
- 相同输入应产生相同 Desired Source、Host Intent 和 Stable Hash。
- Source Set 通过 stable diff 产生 Start / Update / Stop，不逐步 Stop-All / Start-All。
- Host Intent 必须携带稳定身份、Lifecycle、Revision 和 Commit/Event identity。
- 重复提交必须幂等或被明确拒绝，不能重复 Damage、Pickup、Deliver 或 Fire。

---

## 9. Demo 与产品的边界

`MassCrowdDemoBusiness` 可以作为“产品业务层应该如何接入插件”的示例，但它不是插件产品 API。

真实项目可以替换整套 Demo Planner / Ledger，同时继续复用：

```text
Persistent Worker Runtime
Behavior Source / Standard Sources
Movement / Target / Particle
Projectile / HitFact
Networking
Presentation
Owner Commit contracts
```

Demo-specific Planner 名称、角色比例、T1-T8、Golden Hash 和历史阶段编号不得进入插件底层。
