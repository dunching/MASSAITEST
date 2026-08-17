# MassCrowdSimulation 插件模块边界

本文只定义可复用插件的模块职责、依赖方向与宿主边界。

当前生产结构以 `../CurrentArchitecture.md` 为准；最终产品方向以 `../TargetArchitecture.md` 为准。

## 1. 产品边界

```text
MassCrowdCore
      ↓
MassCrowdRuntime
   ↙      ↓       ↘        ↘
Spatial  Combat  Networking Presentation
   ↘      ↓          ↘       ↙
      Projectiles   StandardSources
              ↘       ↙
             Host / Demo
```

实际 Build.cs 依赖可比上图更细，但必须保持一个原则：**通用插件不得反向依赖 Demo/项目业务模块。**

## 2. 模块职责

| 模块 | 长期职责 | 禁止承担 |
|---|---|---|
| `MassCrowdCore` | 稳定 POD、排序、量化、Hash、纯算法 Kernel | UWorld、Actor、Mass EntityManager、网络、表现、Scenario、Demo 业务 |
| `MassCrowdRuntime` | Persistent Worker、World State、Work/Timer/Dependency、Resource、Domain Registry、Result Apply/Commit Barrier | Demo Scenario、具体攻击/物流规则、资产路径 |
| `MassCrowdSpatial` | 通用空间索引、候选查询、稳定空间事实 | 业务目标选择、Demo 地图特判 |
| `MassCrowdCombat` | 通用 Hit/Impact/Combat fact 与可复用解析边界 | 项目伤害公式、掉落、任务、库存 |
| `MassCrowdProjectiles` | Mass projectile、Broadphase/Sweep、生命周期与通用命中事实 | Demo 攻击脚本、特定 VAT/武器资产 |
| `MassCrowdNetworking` | Snapshot、Lifecycle、Intent/Correction/Checkpoint、assembly、resync、relevancy | 模拟权威、Demo Round 协议作为产品 API |
| `MassCrowdPresentation` | StableEntityRef→实例槽、ISM/VAT、插值、视觉生命周期 | 反向决定模拟、业务或服务端生命周期 |
| `MassCrowdStandardSources` | 通用 Movement/Facing/Constraint Source 与 Evaluator | 判断敌我、选择攻击目标、提交伤害/物流 |
| `MassCrowdTests` | 纯 fixture、Runtime/Network/Boundary/Hash 自动化 | Shipping 运行依赖 |
| `MassCrowdDemoBusiness` | Demo 业务 Planner、Provider、Ledger/Host Intent | 通用 Runtime 机制 |
| `MassAICrowdDemo` | Scenario、地图、World Adapter、验收、录像、故障注入 | 复制通用 Runtime/Kernel/Networking/Presentation 实现 |

## 3. Core 约束

`MassCrowdCore` 是最底层通用算法层，应保持尽可能纯净：

```text
POD Input
→ deterministic kernel
→ POD Output
```

Core 禁止包含：

```text
CrowdDemo
SimRound
/Game/Maps
UWorld
Actor
Mass EntityManager
Replication RPC
Presentation Slot
```

## 4. Runtime 约束

`MassCrowdRuntime` 是通用模拟基础设施，不是 Demo 业务中心。

它负责：

```text
Persistent Worker State Owner
WorkRing / TimeWheel / DependencyIndex
Entity / Resource / Dirty State Stores
Domain Registry
Deterministic Shard Merge
Checkpoint / Digest 基础
Worker Result Apply Proxy
Owner Commit Barrier
```

业务 Domain 通过公共 Executor/Adapter 接入；Runtime 不因某个业务模块依赖 Runtime 而反向依赖该业务模块。

## 5. Standard Sources 与业务 Recipe

插件提供的 StandardSources 只表达通用能力，例如：

```text
MoveToLocation
FollowEntity
PursueEntity
FleeFromEntity
MaintainDistance
Facing
MovementLock
SpeedLimit
```

敌人、攻击、护送、物流、取货、交付等属于宿主 Planner/Recipe。

因此：

```text
Standard Source = 怎么移动/约束
Host Business   = 为什么移动、目标是谁、业务是否合法
```

## 6. Demo 边界

Demo 最终只应提供：

```text
T1-T8 Scenario
测试地图
Round/固定窗口测试控制
业务 Fixture
故障注入
Golden Hash
性能指标
录像 / FFmpeg
人工审片
Demo-specific Host Adapter
```

以下内容不得成为 Demo 长期第二实现：

```text
第二套 Worker Runtime
第二套 Commit Barrier
第二套 Networking state machine
第二套 Presentation lifecycle
重复的通用 Target/Particle/Flow Kernel
长期 Round Transaction 兼容框架
```

## 7. 网络与表现边界

Networking 和 Presentation 都是 Worker 输出的消费者/适配层：

```text
Worker Authority
   ├── Network Adapter
   ├── Mass Proxy
   └── Presentation Proxy
```

它们不得反向决定 Worker 的普通模拟状态。

## 8. 文档事实源

- 当前模块实际状态：`../CurrentArchitecture.md`
- 最终模块终态：`../TargetArchitecture.md`
- 字段 Writer/Owner：`WorkerOwnershipMatrix.md`
- Behavior 机制：`../EntityBehaviorSourceArchitecture.md`
- 生产复制详细合同：`../MassCrowdUnifiedRuntimeAndReplicationContract.md`

旧 `MassCrowdSimulationPluginArchitecture.md` 的日期快照和历史阶段不再作为模块边界事实源。