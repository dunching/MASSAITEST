# 通用 Behavior Source 架构

## 1. 文档职责

本文定义 MassCrowdSimulation 的通用 Behavior / Capability 组合模型。

它回答：

- 一个 Agent 能做什么；
- 当前有哪些行为意图同时生效；
- 多个 Source 如何组合成 Movement / Facing / Constraint / Interaction / Business / Presentation 结果；
- Source 如何被创建、更新、停止、复制和恢复。

本文不负责：

- 当前阶段是否完成；查看 `FeatureChecklist.md`。
- 当前代码已经接入到哪里；查看 `CurrentArchitecture.md`。
- Standard Source 的具体目录；查看 `MassCrowdStandardSourcesDesign.md`。
- Target / Particle / Projectile 的专项算法；查看对应 Design 文档。

---

## 2. 核心模型

最终行为模型不是单一 `ActiveBehavior`。

一个 Agent 可以同时存在多个 Behavior Source：

```text
Agent
├── Move / Pursue Source
├── MaintainDistance Source
├── FaceTarget Source
├── SpeedLimit Source
├── Cargo / Business Source
└── 临时 HitReaction / MovementLock Source
```

这些 Source 共同贡献到不同 Channel，再由 Resolver 形成最终结果。

因此：

```text
Behavior Source
≠ 职业
≠ Faction
≠ 单一状态机枚举
```

旧 `ActiveBehavior` 一类枚举只允许作为迁移输入或诊断标签，不拥有最终生产权威。

---

## 3. Faction、Capability、Behavior、Cohort 必须分离

### Faction

只表达：

```text
关系
权限
目标过滤
伤害/交互规则
```

Faction 不自动授予移动能力，也不决定 Networking、Presentation 或 Particle 安全算法。

### Capability

表达 Agent 能做什么，例如：

```text
CanMove
CanPursue
CanCarry
CanAttack
CanUseRangedAttack
```

Capability Profile 是稳定、不可变、排序后的数值 ID 集合。

实体可以使用有界 Add / Remove Modifier 得到当前有效 Capability Binding。

### Behavior Source

表达当前正在贡献什么意图。

例如：

```text
PursueEntity
MaintainDistance
FaceEntity
MovementLock
TimedImpulse
```

### Cohort

表达共享宏观运动事实的一组实体，例如共享 Objective、NavigationLayer、MovementProfile、CapabilityProfile 或 Target Strategy。

Cohort 不等于 Faction。

---

## 4. Stable Identity

行为实例使用稳定实体身份：

```text
StableEntityRef
= ProviderId
+ StableEntityId
+ LifecycleSerial
```

Source Handle 固定为：

```text
StableEntityRef
+ ControllerId
+ SourceSequence
```

`LifecycleSerial` 用来拒绝实体槽位复用后的旧 Command、Source、Hit、Correction 和 Stop。

多个 Controller 不得共享只有 AgentId / SourceSequence 的缩略键。

---

## 5. Provider 与 Registry

Behavior Source 通过开放 Provider 注册，而不是由 Runtime 内建全部业务类型。

Provider 只能通过 Registry Builder 注册：

```text
ProviderId
Capability Profile
Source Spec
Context Schema
State Schema
Evaluator
```

Registry 必须在 World 开始正式模拟前冻结。

以下情况必须拒绝：

```text
重复 ProviderId
重复 SourceTypeId
同 ID 不同 Schema
未知 Schema
冻结后修改 Registry
非法 Channel / Blend Mode
```

完整 Registry Hash 进入网络和一致性基线。

Core / Runtime 不得内建敌我、Boss、AttackTarget、Pickup、Deliver、Warehouse 或具体 Demo 场景语义。

---

## 6. Source Spec

Source Spec 使用稳定数值合同，不使用热路径字符串、UObject 或自动编号。

至少包含：

```text
SourceTypeId
SchemaVersion
RequiredCapabilities
ChannelMask
DefaultPriority
ExclusiveGroup
Lifetime Policy
Replication Policy
```

复制策略只允许明确的模式，例如：

```text
ServerOnly
ResolvedOnly
Predictable
```

未知 TypeId、Schema 不匹配、缺失 Capability 或非法配置必须 fail-closed。

---

## 7. Payload、Context 与 Persistent State

Evaluator 只读取不可变 POD Context，并通过有界 Writer 产生 Contribution 和下一状态。

Evaluator 不得直接访问：

```text
UWorld
Actor
Component
FMassEntityManager
Mass Fragment View
Network RPC
Presentation Instance
宿主业务账本
```

标准 Evaluation Context 至少包含：

```text
FixedStep / Simulation Tick
StableEntityRef
Position
Velocity
Facing
Capability Binding
Source Instance
```

扩展 Context 使用：

```text
ContextTypeId + SchemaVersion + POD Payload
```

需要跨 fixed-step 保持的数据必须进入 Source Persistent State，例如 Wander 的随机状态或 MaintainDistance 的迟滞状态，而不是依赖墙钟时间或全局随机数。

Context、Payload 和 Persistent State 都必须有明确容量上限。

---

## 8. Command 生命周期

Source 由稳定 Command 驱动：

```text
Start
Update
Stop
```

Command 排序键固定为：

```text
EffectiveFixedStep
→ StableEntityRef
→ ControllerId
→ CommandSequence
```

幂等规则：

- 同 Key、同内容：幂等成功；
- 同 Key、不同内容：冲突；
- Sequence 倒退：stale；
- Sequence 缺口：拒绝并触发恢复/重同步；
- Update / Stop 不存在实例：拒绝；
- Start 已存在实例但内容冲突：拒绝。

Capability 被撤销时，依赖该 Capability 的 Source 必须在下一个安全 Boundary 中确定性停止。

Boundary 失败时，SourceSet 变化和 Event 均不可见。

---

## 9. Controller 维护期望 Source 集合

高层 Controller / Planner 不应该每个 Tick：

```text
Stop All
→ Start All
```

而应该维护期望 Source Set，并对当前集合做稳定 Diff：

```text
Desired Sources
    vs
Current Sources
        ↓
Start / Update / Stop Delta
```

这样临时 Source 结束后，原持久任务状态可以精确恢复。

例如：

```text
PursueEntity
+ MaintainDistance
+ FaceEntity

临时加入 HitReaction / MovementLock

HitReaction 到期
→ 原 Pursue / Distance / Facing 实例继续
```

不能通过重建全部实例“看起来恢复了”来代替真正状态恢复。

---

## 10. 六类 Resolved Channel

Behavior Source 可以贡献到：

```text
Movement
Facing
Constraint
Interaction
Business
Presentation
```

生产消费者只读取 Resolved Channel，不允许再扫描 SourceSet 并根据具体 TypeId 重建业务含义。

### Movement

支持：

```text
Override
WeightedAdd
Additive
```

### Facing

独立于 Movement，可使用 Override / WeightedAdd。

### Constraint

用于组合：

```text
Movement Lock
Speed Limit
Min / Max restriction
Navigation Layer intersection
```

### Interaction

适用于互斥交互请求，通常使用稳定 Exclusive Winner。

### Business

输出稳定排序的宿主业务请求；互斥业务冲突必须拒绝，不能数值混合。

### Presentation

输出已解析表现请求；Presentation 永远不能反向成为业务或模拟权威。

---

## 11. Resolver 稳定排序

每实体每 Channel 的统一排序键：

```text
Priority descending
→ SourceTypeId
→ ControllerId
→ SourceSequence
```

物理输入顺序不得改变结果。

所有权重运算和最终结果必须采用确定性、可量化的规则。

相同 SourceSet 与相同 Context 必须产生相同 Resolved Hash。

---

## 12. Behavior 与 Movement Safety 的边界

Behavior 决定：

> 想做什么。

Movement / Safety 决定：

> 如何安全执行。

正确链路：

```text
SourceSet
→ Evaluate
→ Resolve Movement / Facing / Constraint
→ Shared Flow / Target Guidance
→ Local Predictive
→ Movement Predict
→ Particle Safety
→ Facing / Finalize
```

Local Predictive、Particle、Obstacle、Bounds 和最终量化属于不可卸载安全阶段，不能建模成“可选 Source”。

因此某个 Source 即使输出高优先级 Movement，也没有权力绕过 Hard / Swept Safety。

---

## 13. Business Source 与宿主边界

通用 Runtime 不解释：

```text
攻击合法性
伤害公式
库存
Warehouse
Loot
任务
具体 Faction 战术
```

这些属于 Host / Demo Business。

高层业务可以通过多个 Standard Source + Host Intent 组合，例如：

```text
Pursue + Attack
Escort
Logistics
Flee
```

攻击本身的最终伤害提交不是一个 Movement Source；它属于宿主业务原子提交。

---

## 14. Networking

网络需要保存和恢复：

```text
Behavior Registry Hash
Capability Binding
SourceSet Revision / Hash
Persistent Source baseline
Reliable Command delta
Persistent Source State
Resolved Hash
```

`Predictable` Source 只有在双端 Registry / Schema 一致时才能本地预测。

命令缺口、Registry Hash 不一致、Schema 错误或 SourceSet Hash 不一致触发显式 resync。

客户端 Relevancy Exit 只清理本地副本，不能向服务端伪造 Stop。

---

## 15. StateTree Adapter

StateTree 是可选上层 Adapter，不是 Runtime 的核心依赖。

允许：

```text
StateTree Task
→ Start / Update / Stop Source Command
→ 等待已提交 Runtime Event
```

禁止：

```text
StateTree Task 直接写 Movement
StateTree Task 直接写 Mass Fragment
StateTree Task 直接提交 Damage
Runtime 反向依赖 StateTree
```

---

## 16. 容量与 Fail-Closed

Behavior 系统必须有显式容量，例如：

```text
每实体最大活动 Source 数
每实体最大 Controller 数
每 Source 最大 Required Capability 数
Context / Payload / State 最大字节数
每 Channel 最大 Contribution 数
```

超过容量、重复 Handle、未知 Source、Schema 错误、非法 Blend、缺失 Capability 或业务冲突必须整批拒绝。

不得通过静默截断 Source 或 Contribution 制造“性能通过”。

---

## 17. 文档状态规则

本文只定义长期 Behavior Source 架构。

R0–R7、S0–S6、具体构建数量、端口号、历史 p95 和迁移阶段不再作为本文正文。

当前是否已经通过：

```text
FeatureChecklist.md
TestScenarioMatrix.md
```

当前下一步：

```text
PhasePlan.md
```

历史实现过程可通过 Git 历史追溯。
