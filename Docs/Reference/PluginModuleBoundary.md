# MassCrowdSimulation 插件模块边界

本文定义可复用插件的**模块职责、实际编译依赖主干与宿主边界**。

- 当前生产结构：`../CurrentArchitecture.md`
- 最终产品方向：`../TargetArchitecture.md`
- 字段 Owner：`WorkerOwnershipMatrix.md`

---

## 1. 当前编译依赖主干

当前 `Build.cs` 的关键插件依赖可以概括为：

```text
MassCrowdCore
   ├── MassCrowdSpatial
   ├── MassCrowdCombat
   │
   └──────────────┐
                  ▼
          MassCrowdRuntime
          (Core + Spatial)
              │
      ┌───────┼────────┬─────────────┐
      ▼       ▼        ▼             ▼
 Projectiles Networking Presentation StandardSources
   │
   ├── Core
   ├── Spatial
   ├── Combat
   └── Runtime
```

更精确地说：

| 模块 | 关键插件级依赖 |
|---|---|
| `MassCrowdCore` | 无其他 MassCrowd 模块 |
| `MassCrowdSpatial` | `MassCrowdCore` |
| `MassCrowdCombat` | `MassCrowdCore` |
| `MassCrowdRuntime` | `MassCrowdCore`, `MassCrowdSpatial` |
| `MassCrowdProjectiles` | `MassCrowdCore`, `MassCrowdSpatial`, `MassCrowdCombat`, `MassCrowdRuntime` |
| `MassCrowdNetworking` | `MassCrowdCore`, `MassCrowdRuntime` |
| `MassCrowdPresentation` | `MassCrowdCore`, `MassCrowdRuntime` |
| `MassCrowdStandardSources` | `MassCrowdCore`, `MassCrowdRuntime` |

UE Engine / Mass / Render / NetCore 等依赖未在上图逐项展开。

最重要的反向依赖禁则是：

> **任何通用插件模块都不得依赖 `MassCrowdDemoBusiness` 或 `MassAICrowdDemo`。**

---

## 2. 模块职责

| 模块 | 长期职责 | 禁止承担 |
|---|---|---|
| `MassCrowdCore` | 稳定 POD、ID、排序、量化、Hash、纯算法 Kernel | UWorld、Actor、Mass EntityManager、网络、表现、Scenario、Demo 业务 |
| `MassCrowdSpatial` | 通用空间索引、候选生成、稳定 Broadphase / spatial facts | 业务 Target 选择、Demo 地图特判 |
| `MassCrowdCombat` | 通用 Impact / Hit / Combat fact、Effect / resolver 边界 | 产品伤害公式、任务、掉落、库存 |
| `MassCrowdRuntime` | Persistent Worker、Entity/Resource State、WorkRing、TimeWheel、Dependency、Domain Registry、Shard Merge、Result Apply / Commit Barrier | Demo Scenario、具体攻击/物流规则、测试资产路径 |
| `MassCrowdProjectiles` | Worker Projectile domain 接入、projectile state/codec、Mass proxy adapter、Broadphase / Sweep / HitFact 组合 | Demo 攻击脚本、特定武器/VAT资产、产品伤害账本 |
| `MassCrowdNetworking` | Snapshot、Lifecycle、Intent、Correction、Checkpoint、Digest、assembly、resync、relevancy | Simulation Authority、Demo Round 协议作为产品核心 API |
| `MassCrowdPresentation` | StableEntityRef→Instance Slot、ISM/VAT、插值、visual lifecycle | 反向决定模拟、业务或服务端 lifecycle |
| `MassCrowdStandardSources` | 通用 Movement/Facing/Constraint Source、Context、Evaluator | 判断敌我、选择攻击目标、提交伤害/物流 |
| `MassCrowdTests` | 纯 fixture、Runtime/Network/Boundary/Hash 自动化 | Shipping 运行依赖 |
| `MassCrowdDemoBusiness` | Demo Planner、Provider、Business Ledger / Host Intent | 通用 Runtime 机制 |
| `MassAICrowdDemo` | Scenario、World Adapter、地图、验收、录像、故障注入 | 复制通用 Runtime / Kernel / Networking / Presentation 实现 |

---

## 3. Core 纯度

`MassCrowdCore` 的基本形态应保持：

```text
POD Input
→ deterministic kernel
→ POD Output
```

Core 禁止出现：

```text
CrowdDemo
SimRound
/Game/Maps
UWorld
Actor
FMassEntityManager
Replication RPC
Presentation Slot
```

需要 Engine World、Mass 实体、网络或表现的能力必须放在更上层模块。

---

## 4. Runtime 是 Simulation 基础设施，不是业务中心

Runtime 负责：

```text
Persistent Worker State Owner
EntityStateStore
ResourceStore
DirtyStateStore
WorkRing / TimeWheel / DependencyIndex
Domain Registry
Shard dispatch / deterministic merge
Checkpoint / Digest 基础
Worker Result Apply Proxy
Owner Commit Barrier
```

Runtime 可以依赖 Spatial 来维护通用空间运行时能力，但不能通过反向依赖 Projectiles、Demo Business 或 Scenario 来理解具体产品语义。

Projectile 等更高层 Domain 通过注册 Executor、Resource/Field codec 和 adapter 接入 Runtime。

---

## 5. Projectiles 的特殊依赖位置

`MassCrowdProjectiles` 当前位于 Runtime 之上，因为它组合：

```text
Runtime worker contracts
+
Spatial broadphase
+
Combat Impact / Hit facts
+
Mass projectile proxy / lifecycle integration
```

这意味着：

```text
Projectiles → Runtime
```

而不是：

```text
Runtime → Projectiles
```

通用 Resource ID、Field ID 或 Executor 注册合同必须放在足够低的模块，避免为了 Projectile 产生 Runtime 的反向依赖。

---

## 6. Standard Sources 与 Host Recipe

Standard Sources 只表达“怎么移动/朝向/约束”：

```text
MoveToLocation
FollowEntity
PursueEntity
FleeFromEntity
MaintainDistance
FaceMovement / FaceEntity
MovementLock / SpeedLimit
TimedImpulse
```

Host Business 负责“为什么移动、目标是谁、业务是否合法”：

```text
Enemy selection
Attack legality
Escort
Logistics
Pickup / Deliver
Damage / Inventory / Mission
```

因此高层 Combat / Escort / Logistics 应由 Host Planner 组合通用 Source，而不是把产品语义重新下沉 Runtime。

---

## 7. Demo 边界

Demo 可以保留：

```text
T1–T8 Scenario
测试地图
Round / 固定窗口控制
业务 Fixture
故障注入
Golden Hash
性能指标
录像 / FFmpeg
人工审片
Demo-specific Host Adapter
```

Demo 不得长期保留：

```text
第二套 Worker Runtime
第二套 Commit Barrier
第二套 Networking state machine
第二套 Presentation lifecycle
重复的通用 Flow / Target / Particle Kernel
Legacy Round Transaction 兼容框架
```

测试需要验证生产插件路径，而不是为了旧测试冻结 Legacy 类型。

---

## 8. Networking / Presentation 边界

```text
Worker Simulation Authority
   ├── Mass Proxy
   ├── Network Adapter
   └── Presentation Adapter
```

Networking 自己拥有 transport / assembly / relevancy 状态；Presentation 自己拥有视觉实例状态。

但二者都不能反向成为 Movement、Combat、Lifecycle 等 Simulation Field 的生产权威。

---

## 9. 文档边界

- 当前模块实际状态：`../CurrentArchitecture.md`
- 最终模块终态：`../TargetArchitecture.md`
- 字段 Writer / Owner：`WorkerOwnershipMatrix.md`
- Behavior 机制：`../EntityBehaviorSourceArchitecture.md`
- Projectile：`../MassProjectileHitFrameworkDesign.md`
- Runtime / Replication：`../MassCrowdUnifiedRuntimeAndReplicationContract.md`

旧 `MassCrowdSimulationPluginArchitecture.md` 已退出 active tree；需要历史阶段时使用 Git 历史。
