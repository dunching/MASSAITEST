# Mass Projectile 与通用 Hit Framework 设计

## 1. 文档职责

本文定义 `MassCrowdProjectiles` / `MassCrowdSpatial` / `MassCrowdCombat` 的可复用 Projectile、空间碰撞、ImpactFact 与 HitFact 机制。

本文只负责插件级机制：

```text
Projectile simulation
Spatial broadphase
Swept narrowphase
ImpactFact
HitFact
Host Combat Adapter boundary
Projectile network / presentation facts
```

本文不负责 Demo 的 T7/T8 VAT、Attack clip、HitFlash、KnockUp 展示或人工审片；这些内容统一由 `RangedCombatVatAndHitResponseDesign.md` 负责。

当前完成状态与测试证据统一查看 `FeatureChecklist.md` / `TestScenarioMatrix.md`。

---

## 2. 核心原则

大规模远程单位的生产路径固定采用：

```text
Entity-native Projectile
        ↓
Persistent Worker Projectile Domain
        ↓
Stable Spatial Broadphase
        ↓
Fixed-step Swept Narrowphase
        ↓
ImpactFact
        ↓
HitFact
        ↓
Host Combat Adapter
        ↓
Damage / Status / Reactive / Presentation
```

不得退化为：

```text
每颗 Projectile 一个 Actor
Projectile × 全体 Agent 扫描
客户端自己决定 Damage
命中后直接写 Host 私有状态
```

---

## 3. Projectile 模拟权威

最终 Projectile simulation 属于 Persistent Worker 模拟域。

Worker 持有并推进：

```text
Projectile identity
Lifecycle
Source / owner ref
Kinematics
Trajectory profile
Collision profile
Effect profile
Age / lifetime
Impact / expire state
```

Engine 侧 Mass Fragment、Network packet、Presentation instance 都只是 Worker 结果的代理或适配状态，不得成为第二套可独立推进的 Projectile simulation owner。

如果宿主使用 Mass Projectile Entity 作为 Engine 集成载体，其 Fragment 必须由 Worker Result Apply 更新；GT 不再从 Fragment 独立推进另一套轨迹。

---

## 4. Stable Projectile Identity

Projectile 必须拥有稳定身份，例如：

```text
ProjectileId
SpawnSequence
LifecycleSerial
Source StableEntityRef
FireSequence
```

身份不能只依赖数组下标、对象池槽位或临时 Mass Entity Handle。

槽位复用后，旧 Lifecycle 的 Impact、Correction、Expire、Visual Event 必须被拒绝。

---

## 5. Projectile State

通用 Projectile 状态至少包含：

```text
ProjectileRef
SourceRef
Optional TargetRef
PreviousPosition
Current / ProposedPosition
Velocity
Radius
Navigation / Collision Layer
QueryMask
ResponseMask
TrajectoryProfileKey
EffectProfileKey
SpawnTick
AgeTicks
LifetimeTicks
State = Active / Impacted / Expired
```

数据必须是版本化 POD，可参与：

```text
Stable Hash
Checkpoint
Correction
Replay
Networking
Deterministic Merge
```

---

## 6. Trajectory Profile

不同轨迹必须由显式 Profile 表达，而不是通过 Weapon Name 或 AgentId 写隐式分支。

可以逐步支持：

```text
Linear
Ballistic
Homing
Piercing
Bounce
Mortar / fixed-flight landing
```

第一原则是：

> 每种轨迹模型都必须明确自己的 fixed-step 状态、碰撞语义和 determinism contract。

直射 Swept Projectile 和“锁定落点 + 固定飞行时间”的 Mortar 不能共用一个含糊分支。

---

## 7. Collision Body Snapshot

Projectile collision 不能直接在 Worker 中访问 Actor / World / Mass View。

空间系统消费版本化 POD Body Snapshot：

```text
StableEntityRef
StartPosition
AppliedEndPosition
PhysicalRadius
Navigation / Interaction Layer
Query / Response Mask
Lifecycle
Alive / Relevant state
```

移动目标必须提供 Start → AppliedEnd，而不是只给当前点。

这样 Projectile 与目标都在同一个 fixed-step 时间区间内做相对运动碰撞。

---

## 8. Stable Spatial Broadphase

Broadphase 的目标是避免：

```text
O(Projectiles × Agents)
```

生产实现应使用稳定空间索引，例如 Uniform / Hierarchical Grid。

目标 Body 按其 swept bounds 注册到覆盖 Cell。

Projectile 使用自己的 swept bounds 查询候选。

所有：

```text
Cell Key
Cell 内实体顺序
Candidate 顺序
Pair Key
```

必须稳定。

不能依赖 `TMap` / `TSet` 非稳定迭代顺序产生不同结果。

---

## 9. Swept Narrowphase

Projectile Narrowphase 使用：

```text
Projectile Previous → Proposed
Target Start → AppliedEnd
```

执行相对 Sweep。

不能只查询 Projectile 最终点附近目标，否则高速 Projectile 会穿透移动目标。

命中决胜顺序应稳定，例如：

```text
Earliest quantized TOI
→ Target StableEntityRef
→ Projectile Stable Key
```

Faction、CollisionLayer、NavLayer、已命中过目标、Pierce policy 等都必须使用明确的稳定过滤规则。

---

## 10. Environment Collision

环境碰撞通过版本化 Environment / Surface Snapshot 提供。

环境使用稳定 `SurfaceId` / Geometry fact，不把世界对象指针带入 Worker。

Projectile 对环境执行 Swept query。

环境 Impact 和实体 Hit 必须分离：

```text
Impact on environment
≠ HitFact against gameplay target
```

如果 Projectile profile 允许跨 Navigation Layer，则必须显式声明；桥上 / 桥下 XY 重叠实体不能默认互相成为普通地面候选。

---

## 11. ImpactFact

`ImpactFact` 只描述几何碰撞事实。

推荐字段：

```text
ImpactEventId
ProjectileRef
SourceRef
TargetRef or SurfaceId
SimulationTick
QuantizedTimeOfImpact
HitPosition
HitNormal
RelativeVelocity
EffectProfileKey
```

ImpactFact 不包含：

```text
AActor*
UObject*
HealthComponent*
Host private fragment pointer
具体 Damage 结果
```

---

## 12. HitFact

`HitFact` 是 Host Combat 可消费的稳定业务请求。

推荐字段：

```text
StableHitEventId
ApplySimulationTick
SourceRef
CauserRef / ProjectileRef
TargetRef
HitPosition
HitDirection
EffectProfileKey
QuantizedMagnitudeOverrides
HitFlags
```

HitFact 不直接决定 Host Damage 公式。

`EffectProfileKey` 由 Host 解析成：

```text
Damage
Armor interaction
Status
Horizontal impulse
Vertical impulse
Visual effect
```

同一个 StableHitEventId 在 replay / network retry 后只能被结算一次。

---

## 13. Host Combat Adapter

插件负责：

> 谁在什么时候以什么几何事实命中了谁。

宿主负责：

```text
Damage formula
Armor
Health
Status
Death
Loot
Mission
Inventory
Faction business rules
```

Host Adapter 只能消费已经通过完整验证的 HitFact。

宿主不得重新执行 Projectile trajectory 或空间查询来“二次确认”命中，否则会产生第二套碰撞权威。

同一个 HitFact 只能有一个业务 Writer。

---

## 14. Projectile Worker Domain

Projectile Domain 是 Worker DAG 的一部分。

高层关系：

```text
Behavior / Combat Intent
        ↓
Combat / Reactive
        ↓
Projectile Spawn / Clock
        ↓
Projectile Simulation
        ↓
Spatial Broadphase / Sweep
        ↓
Impact / Hit Events
        ↓
Result Publish
```

Projectile Domain 只消费冻结的实体 / Resource / Spatial facts，并输出：

```text
Projectile dirty state
Impact / Hit ordered event
Next work / wakeup
Dependency observations
Diagnostics
```

它不直接写 Mass、Actor、Presentation 或 Host Health。

---

## 15. Spawn

发射请求必须具有稳定身份，例如：

```text
SourceRef
SimulationTick
FireSequence
ProjectileProfileKey
```

同一个 Fire 请求重放时必须幂等。

Windup / Attack phase 是否允许发射属于 Host Combat；Projectile 模块只接受已经合法的 Spawn Request。

不得让同一个 Windup 在重复执行、网络重试或 replay 后产生多颗重复 Projectile。

---

## 16. Expire / Destroy

Projectile 生命周期包括：

```text
Spawn
Active simulation
Impact or Lifetime expire
Publish final fact
Despawn / recycle
```

Despawn 必须使用稳定 Projectile identity / Lifecycle。

对象池可以复用 Engine-side entity slot，但不能复用旧 Lifecycle 事实。

---

## 17. Reactive Motion 边界

HitFact 可以请求 Knockback / KnockUp，但 Projectile 模块不直接修改目标 Transform。

正确链路：

```text
Projectile HitFact
→ Host / Combat Resolve
→ Reactive Movement Fact / Source
→ Movement Predict
→ Particle Safety
→ Final Apply
```

这样受击位移仍然遵守统一的 Hard / Swept / Obstacle / Bounds 规则。

---

## 18. Networking

服务端是 Projectile gameplay authority。

客户端不为每颗大规模 Projectile 创建持续复制 Actor。

网络可以同步：

```text
Spawn fact
Correction / checkpoint
Impact fact
Expire fact
Relevant projectile baseline
```

Presentation 使用这些事实重建：

```text
ISM
Niagara
Trail
Impact VFX
```

客户端视觉不能反向决定服务端命中和 Damage。

---

## 19. Presentation

Projectile Presentation 与 Projectile Simulation 分离。

Presentation 只消费：

```text
ProjectileRef
VisualProfile
Spawn time
Transform / interpolated state
Impact / Expire event
```

渲染实例是否被创建、隐藏或回收不能改变 Worker Projectile lifecycle。

---

## 20. Determinism 与 Fail-Closed

必须拒绝：

```text
重复 ProjectileRef
stale Lifecycle
非法 Profile
非法半径 / NaN
候选顺序不稳定
同一 HitEvent 重复消费
容量溢出
丢失不可覆盖 Impact / Hit Event
错误 Resource Revision
```

不可通过静默丢 Projectile、静默丢 Hit、提高队列上限或客户端隐藏实例制造“规模通过”。

---

## 21. 插件模块边界

```text
MassCrowdCore
→ Stable ID / POD / Hash

MassCrowdSpatial
→ Spatial Index / Broadphase / Sweep

MassCrowdCombat
→ ImpactFact / HitFact / generic combat contract

MassCrowdProjectiles
→ Projectile state / profile / executor / lifecycle

MassCrowdRuntime
→ Worker scheduling / state / publication

Host / Demo
→ business resolve / assets / scenario / acceptance
```

依赖必须保持单向；公共插件模块不得 include Demo Coordinator、Scenario、T8 或宿主 Damage 类型。

---

## 22. 验收边界

插件级 Projectile / Hit Framework 至少需要覆盖：

```text
高速 Swept hit
移动目标相对 Sweep
环境命中
Layer filtering
多 Projectile 并发
同 TOI 稳定决胜
Pierce / expire policy
Spawn 幂等
HitEvent 幂等
Lifecycle reuse
Replay / correction
Broadphase candidate complexity
Network baseline / late join
Presentation event continuity
```

T7/T8 VAT、HitFlash 和 Demo 战斗表现不属于本文的插件机制验收，统一查看 `RangedCombatVatAndHitResponseDesign.md`。

历史外部项目审计、PJ0–PJ6 迁移阶段、固定 32 槽等旧实现不再保留在本文正文；需要追溯时使用 Git 历史。
