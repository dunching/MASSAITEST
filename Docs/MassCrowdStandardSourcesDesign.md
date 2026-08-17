# MassCrowd Standard Sources 设计

## 1. 文档职责

本文定义 `MassCrowdStandardSources` 模块随插件提供的通用 Behavior Source。

它回答：

- 插件应该开箱提供哪些通用 Movement / Facing / Constraint Source；
- 这些 Source 需要什么 Context / Persistent State；
- 哪些能力属于通用插件，哪些必须留在 Host / Demo Business；
- 多个 Source 应如何组合。

Behavior Source 协议、Registry、Command、Resolver 和网络基础合同统一查看 `EntityBehaviorSourceArchitecture.md`。

---

## 2. 模块边界

```text
MassCrowdCore
├── 稳定 ID
├── Source POD
├── Contribution / Resolver
└── Hash / Quantization

MassCrowdRuntime
├── Provider / Registry
├── World Store
├── Context
├── Source lifecycle
└── Worker / Movement pipeline integration

MassCrowdStandardSources
├── 通用 Movement Source
├── 通用 Facing Source
├── 通用 Constraint Source
├── 通用 Context Schema
└── 通用 Evaluator

Host / Demo Business
├── 目标选择
├── Attack legality
├── Pickup / Deliver
├── Combat / Logistics Recipe
└── 业务与表现解释
```

`MassCrowdStandardSources` 只能单向依赖公共 Core / Runtime API。

Runtime 不得反向依赖 StandardSources，也不得根据 Standard Source TypeId 写特判。

---

## 3. Source 的粒度原则

Standard Source 应表达可独立复用的基础意图，而不是把完整高层业务塞进一个巨大 Behavior 类。

例如：

```text
Escort
= FollowEntity
+ MaintainDistance
+ FormationOffset
+ FaceMovement
+ optional SpeedLimit
```

```text
Pursue target
= PursueEntity
+ MaintainDistance
+ FaceEntity
```

真正的 Attack、Pickup、Deliver、Damage、Inventory 等仍由宿主业务负责。

不得为了“统一接口”把所有上层已经算好的结果都塞进一个无语义 Vector Payload，再用同一个 Evaluator 冒充不同自主行为。

---

## 4. 推荐 Standard Source 集

| Source | Channel | 主要职责 |
|---|---|---|
| `MoveToLocation` | Movement | 朝静态世界位置移动，支持到达半径和速度限制。 |
| `ArriveAtLocation` | Movement | 在减速半径内确定性降速。 |
| `FollowEntity` | Movement | 使用目标运动学 Context 保持跟随偏移。 |
| `PursueEntity` | Movement | 根据目标位置/速度做有界预测截获；不判断敌我。 |
| `FleeFromEntity` | Movement | 从目标运动学事实生成反向运动；不决定威胁规则。 |
| `MaintainDistance` | Movement / Constraint | 维持最小/最大距离并带迟滞。 |
| `FaceMovement` | Facing | 朝最终移动方向。 |
| `FaceEntity` | Facing | 朝指定稳定目标。 |
| `FormationOffset` | Movement Additive | 相对宿主提供的 Formation Anchor 保持偏移。 |
| `MovementLock` | Constraint | 显式禁止普通移动。 |
| `SpeedLimit` | Constraint | 限制最大移动速度。 |
| `TimedImpulse` | Movement | 有期限高优先级位移/速度贡献，例如通用 Knockback。 |
| `WanderSteering` | Movement | 确定性漫游，持久保存随机/候选状态。 |

具体 TypeId 可以扩展，但语义边界必须保持稳定。

---

## 5. 明确不属于 StandardSources 的能力

以下内容属于 Host / Product：

```text
选择哪个敌人
是否允许攻击
Attack phase
Damage / Armor
Pickup / Deliver
Warehouse
Cargo ownership
Loot
Quest
Faction 战术
```

### Attack Intent

攻击合法性、Windup、Fire、Commit、Damage 都不是一个通用 Movement Source。

StandardSources 可以提供：

```text
PursueEntity
MaintainDistance
FaceEntity
MovementLock
```

但“现在开枪”属于 Host Intent / Combat Domain。

### Death

通用 `MovementLock` 可以用于阻止死亡实体继续移动，但：

```text
死亡语义
掉落
销毁
复活
积分
任务
```

仍由宿主拥有。

---

## 6. TargetKinematics Context

以下 Source 共享版本化 `TargetKinematics` Context：

```text
FollowEntity
PursueEntity
FleeFromEntity
FaceEntity
MaintainDistance
```

至少包含：

```text
Target StableEntityRef
Position
Velocity
Facing
Navigation / Interaction Layer
Fact Revision
```

Evaluator 必须验证 Source Payload 中的 TargetRef 与 Context TargetRef 一致。

Evaluator 不得自行查找 Actor、Mass Entity、World 或 NavMesh。

---

## 7. FormationAnchor Context

`FormationOffset` 使用版本化 `FormationAnchor` Context：

```text
AnchorRef
Anchor Position / Rotation
Anchor Velocity
Local Offset
Revision
```

Formation Source 只计算相对偏移贡献，不负责选择队长、业务阵型或永久 Slot ownership。

宿主可以提供阵型事实，但通用 Runtime 不解释“护送”“军团”“职业站位”等语义。

---

## 8. MaintainDistance State

`MaintainDistance` 不能只使用单帧阈值，否则会在边界附近反复切换方向。

它应保存有界 Persistent State，例如：

```text
Inside / TooNear / TooFar mode
Last transition revision
Hysteresis state
```

迟滞必须使用 fixed-step / stable state，而不是 render DeltaSeconds。

临时 Source 压制结束后，原 MaintainDistance State 必须继续，而不是重新初始化。

---

## 9. Wander State

`WanderSteering` 必须是确定性的。

Persistent State 至少需要保存：

```text
Deterministic random state
Current candidate / direction
Next reselection tick
Optional host candidate revision
```

禁止依赖：

```text
Wall clock
全局随机数
线程执行顺序
Render frame DeltaSeconds
```

Replay、Late Join、Correction 和临时压制恢复后必须从同一状态继续。

可通行候选 / Nav sampling 可以由宿主或版本化 Context 提供，Evaluator 不直接查询 World。

---

## 10. TimedImpulse

`TimedImpulse` 是通用有期限 Movement Contribution，可用于：

```text
Knockback
短时冲量
外力推动
```

但它仍然必须进入统一安全链：

```text
TimedImpulse
→ Resolve Movement
→ Movement Predict
→ Particle Safety
→ Final Apply
```

它没有权力绕过 Hard / Swept / Obstacle / Bounds。

真正的 Hit、Damage、KnockUp 业务事实仍由 Combat / Host 提供。

---

## 11. Source 组合

### Pursue + Attack

```text
PursueEntity
+ MaintainDistance
+ FaceEntity
+ optional SpeedLimit

Host Combat Planner
→ Attack Intent
→ 在需要的提交窗口临时增加 MovementLock
```

### Escort

```text
FollowEntity
+ MaintainDistance
+ FormationOffset
+ FaceMovement
+ optional SpeedLimit
```

### HitReaction

```text
原持久 Source 保留
+ TimedImpulse
+ optional MovementLock
```

反应结束后：

```text
原 Source Handle
原 Payload
原 Persistent State
```

必须继续存在。

---

## 12. 生产消费规则

Standard Source 只输出 Contribution。

生产 Movement 系统消费的是 Resolved Channel：

```text
Source Instances
→ Evaluate
→ Resolve Movement / Facing / Constraint
→ Guidance integration
→ Local Predictive
→ Movement Predict
→ Particle Safety
→ Facing / Finalize
```

Production Adapter 禁止：

```text
扫描 SourceSet
if SourceTypeId == Pursue ...
if SourceTypeId == Wander ...
```

来重新解释行为。

如果 Business / Presentation Channel 非空，也不能隐式停止移动。

只有：

```text
Resolved Movement 本身为零
显式 MovementLock
显式 SpeedLimit
安全层拒绝候选
```

才允许限制最终移动。

---

## 13. 扩展规则

第三方可以通过公共 Registry 增加新的 Source Type，但必须满足：

1. 稳定 TypeId / Schema；
2. POD Payload / Context / State；
3. 不访问 World / Actor / Mass View；
4. 只通过 Contribution Writer 输出；
5. 明确 Required Capability；
6. 明确 Channel / Priority / Replication Policy；
7. 遵守容量上限；
8. 能参与 Stable Hash / Replay / Late Join。

注册一个新 TypeId 只证明扩展 API 可用，不等于对应算法已经经过产品验收。

---

## 14. 验收边界

StandardSources 的完整验收应覆盖：

```text
自主 Evaluator
Context / Schema validation
Persistent State replay
Start / Update / Stop lifecycle
Resolver composition
不同输入顺序等价
临时 Source 压制与精确恢复
InteractionLayer
完整 Movement / Particle Safety chain
Network baseline / late join / resync
第三方 TypeId
```

当前哪些 Source 已经完成、跑过什么规模，以 `FeatureChecklist.md` 和 `TestScenarioMatrix.md` 为准。

S0–S6、历史端口、具体 p95 和迁移流水账不再保存在本文正文；需要追溯时使用 Git 历史。
