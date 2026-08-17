# MassCrowd 通用 Runtime 与 Replication 合同

## 1. 文档职责

本文是 `TargetArchitecture.md` 的详细 Reference，定义持续 Agent、Behavior、Lifecycle、Worker Result、Networking、Late Join、Correction 和 Presentation 的长期数据合同。

事实优先级：

```text
当前实现
→ CurrentArchitecture.md

最终全局方向
→ TargetArchitecture.md

详细 Runtime / Replication contract
→ 本文

当前完成状态 / 证据
→ FeatureChecklist.md / TestScenarioMatrix.md
```

本文不得覆盖 `TargetArchitecture.md` 的 Full Worker Authority 原则。

---

## 2. 单一 Simulation Authority

每 World 的 Persistent Worker Runtime 是日常模拟状态唯一权威。

Worker 最终拥有：

```text
Lifecycle
Behavior
Flow / Resource
Target / Cohort
Combat / Reactive
Projectile Simulation
Movement
Particle / Interaction
Facing
Simulation Clock
```

以下状态都是代理或适配副本：

```text
Mass Fragment
Actor / Component
Network cache
ISM / VAT instance
Presentation cache
```

它们不能与 Worker 同时独立推进同一模拟字段。

特别是 Projectile：

> Projectile simulation authority 属于 Worker Projectile Domain；Engine-side Mass Projectile Entity / Fragment 只作为 Worker Result Apply 后的 Engine 集成代理，不再拥有第二套独立轨迹模拟权威。

---

## 3. Stable Entity Identity

通用实体身份：

```text
StableEntityRef
= ProviderId
+ StableEntityId
+ LifecycleSerial
```

`LifecycleSerial` 必须拒绝槽位复用后的旧：

```text
Spawn
Despawn
Behavior Command
Correction
Hit
Projectile relation
Presentation event
```

短 AgentId、Mass Entity Handle、Actor pointer、数组下标都不能单独作为跨系统生产身份。

---

## 4. 统一 Agent Facts

通用 Agent 基线可以包含：

```text
StableEntityRef
Faction / Team relationship facts
CapabilityProfileKey
Capability Modifiers
Effective Capability Binding
TargetRef / ObjectiveRef
MovementProfile
PresentationProfile
BusinessTaskRef or host business reference
```

其中：

- Faction 只表达关系、权限和目标过滤；
- Capability 表达能做什么；
- Behavior Source 表达当前做什么；
- Cohort 表达共享宏观运动事实；
- PresentationProfile 只控制表现，不授予业务能力。

---

## 5. Behavior Source Contract

行为实例由 Runtime World Store 按 StableEntityRef 保存。

Source Handle：

```text
StableEntityRef
+ ControllerId
+ SourceSequence
```

SourceSet 具有：

```text
Revision
Stable Hash
Sorted Source Instances
Persistent Source State
```

Behavior Registry 在正式模拟前冻结，并进入网络基线。

具体 Source / Resolver 合同查看：

```text
EntityBehaviorSourceArchitecture.md
MassCrowdStandardSourcesDesign.md
```

---

## 6. GT / Network → Worker 输入合同

正常输入只允许外部事实：

```text
Spawn
Despawn
Membership change
Gameplay Command
Behavior Source Command
Objective Revision
Resource / Environment Revision
Authority Correction
```

GT 不得把刚从 Worker 应用出来的位置、速度、Facing、Combat、Projectile 状态作为普通输入再次 echo 回 Worker。

显式 Authority Correction 是唯一正常覆盖 Worker 已拥有字段的外部入口。

---

## 7. Worker → 外部输出合同

Worker 发布：

```text
Dirty State Patch
Ordered Gameplay Event
Checkpoint
Digest
Diagnostic / Metrics
```

### Dirty State

同一实体同一可覆盖字段可以 latest-wins 合并。

Key：

```text
StableEntityRef + Field
```

### Ordered Event

以下事实不能被状态合并吞掉：

```text
Spawn
Despawn
Damage
Death
Projectile Impact
Business Commit
其他不可覆盖事件
```

Ordered Event 必须拥有连续或可验证的稳定 Sequence。

---

## 8. Worker Result 原子提交

Result Apply 使用 Runtime Owner Commit Barrier。

高层顺序：

```text
Prepared Worker Result
        ↓
Runtime Token Validate
        ↓
Generation / Publish / Input / Event Watermark Validate
        ↓
Stable Entity View Validate
        ↓
Host FinalValidate
        ↓
──────── first write boundary ────────
        ↓
Host no-fail state apply
        ↓
Proxy no-fail commit
        ↓
Ordered Event / Network / Presentation side effects
        ↓
后续 Dirty ACK
```

所有正常可失败检查必须发生在第一次状态写入前。

不能使用“先部分写入，再靠完整 rollback 数组补偿”作为正常原子提交方案。

Runtime Barrier 不解释 Demo Round、Scenario、Target Actor 或宿主 Damage 语义；Host-specific validation 通过 Prepared Plan / Adapter 接入。

---

## 9. 持续 Lifecycle

生产世界不依赖 Round Reset。

必须支持：

```text
Initial population
Spawn
Despawn
Death removal
Slot recycle
Membership enter / exit
Relevancy enter / exit
Late Join
持续运行
```

Spawn / Despawn 在安全 Simulation Boundary 原子应用。

Despawn 原因可以区分：

```text
Death
Host destroy
Business recycle
Relevancy exit
```

客户端 Presentation 回收不能反向决定服务端实体生命周期。

---

## 10. Cohort 与 Membership

Cohort 由共享宏观运动事实形成，例如：

```text
Objective
NavigationLayer
MovementProfile
CapabilityProfile
Macro Strategy
Environment Revision
```

Cohort 不等于 Faction。

Membership 必须支持持续增量变化，不能假设一个 Round 内完整 Agent 集合永久固定。

Cohort / Resource 变化通过 Worker DependencyIndex 唤醒受影响 Work；静态未受影响 Cohort 不应因为纯时钟推进而全量重算。

---

## 11. Replication Fact Categories

网络按事实类别同步，而不是简单高频复制所有 Transform。

### Initial / Relevant Snapshot

包含进入客户端相关集时需要建立 Worker / Proxy baseline 的稳定事实。

必须有明确：

```text
Protocol Version
Generation
Sequence
Registry / Resource Hash
Chunk identity
```

### Lifecycle Delta

```text
Spawn
Despawn
Membership change
```

必须使用有界 batch。

### Behavior

```text
Capability Binding
SourceSet baseline
Reliable Source Command delta
Persistent Source State
Resolved Hash
```

### Intent

表达服务端/宿主产生的可预测外部事实，不携带完整 Worker 世界状态。

### Correction

只修复确实不一致的 Scope / Entity / Field。

Correction 必须携带：

```text
Generation
CorrectionRevision
StableEntityRef / Scope
Source sequence / revision
```

### Checkpoint

用于：

```text
Late Join baseline
低频完整恢复
严重 desync
Round / scenario acceptance snapshot
```

Checkpoint 不是普通每帧 transform replication。

### Digest

用于廉价检测：

```text
Entity / Field / Scope Hash mismatch
```

发现 mismatch 后再请求明确 Correction / Resync。

---

## 12. 有界 Packet / Chunk

所有可能达到 O(N) 的网络载荷必须：

```text
bounded batch
bounded chunk
bounded assembly
assembly timeout
duplicate handling
sequence validation
```

不可通过无限增大可靠 RPC、partial bunch 或队列上限解决规模问题。

Navigation / Shared Flow 等大型共享资源优先同步：

```text
Resource Revision
Stable Hash
Asset / Resource reference
```

而不是复制整张场。

---

## 13. Late Join

最终 Late Join 顺序：

```text
Checkpoint / Relevant Baseline
        ↓
Resource Revisions
        ↓
Behavior / Source Baseline
        ↓
Ordered Event Baseline
        ↓
后续 Intent / Delta / Event
```

Baseline 未完成前，不允许把后续 Delta 当作已拥有完整前置状态来应用。

缺失 Sequence、Schema、Resource Revision 或 Registry Hash 必须触发显式等待 / resync。

---

## 14. Predictable / ResolvedOnly / ServerOnly

Behavior Source 的网络策略：

### ServerOnly

客户端不自行执行该 Source 的权威逻辑，只消费服务端结果 / 业务事实。

### ResolvedOnly

客户端接收已经解析的通道结果，不需要拥有完整 Source Evaluator 状态。

### Predictable

客户端只有在：

```text
Registry Hash 一致
Schema 一致
Resource Revision 一致
Baseline 完整
```

时才能本地执行。

任何条件不满足都不能静默继续预测。

---

## 15. Correction 与 Generation

普通稀疏 Correction 不应该重建整个 World Generation。

一般规则：

```text
普通 Scope Correction
→ CorrectionRevision++
→ invalidate affected work / result

World switch / full resnapshot / teardown
→ Generation++
```

Generation 变化使旧 Worker Result、旧 Event、旧 Packet、旧 Projectile / Lifecycle fact 整体失效。

---

## 16. Checkpoint / Replay

Checkpoint 至少需要冻结：

```text
Generation
Worker Epoch / Simulation Tick
Last Applied Input Sequence
Last Ordered Event Sequence
Entity State Hash
Resource Revision Hash
Behavior / Source state
必要的 Projectile / Combat / Movement state
```

恢复后 Worker 必须从同一个稳定事实继续推进。

Replay 不能依赖线程完成顺序、墙钟时间或非确定性容器迭代。

---

## 17. Projectile Replication

Projectile Gameplay Authority 在 Worker。

网络只同步客户端恢复 / 表现所需事实：

```text
Projectile baseline when relevant
Spawn
Correction when required
Impact
Expire / Despawn
Checkpoint state
```

客户端可以本地插值 / 预测视觉轨迹，但不能自己产生 Damage authority。

具体 Projectile / Hit 机制查看：

```text
MassProjectileHitFrameworkDesign.md
```

---

## 18. Presentation Contract

Presentation 使用独立 StableEntityRef → Slot 映射。

它消费：

```text
Presentation Profile
Transform / movement proxy
Visual State
VAT timing facts
Cargo facts
Hit / Projectile visual events
```

Presentation 必须支持幂等：

```text
Spawn
Update
Despawn
stale tombstone rejection
```

视觉实例数量、LOD、隐藏、插值或回收不能反向修改 Simulation / Lifecycle authority。

---

## 19. Networking 与 Faction 解耦

网络频率、可靠性和精度由：

```text
Relevancy
Ownership
Fact reliability
Change rate
Budget
Prediction policy
```

决定。

不得因为 Faction = Enemy 就硬编码一套低精度网络，Faction = Friendly 就使用另一套 Simulation path。

---

## 20. Fail-Closed

以下情况必须拒绝或 Resync：

```text
Protocol Version 不支持
Generation mismatch
Lifecycle mismatch
Sequence gap
Registry / Schema mismatch
Resource Revision missing
Stable Hash mismatch
重复不可覆盖 Event
Chunk assembly overflow / timeout
非法字段 Owner
容量溢出
```

不得通过静默丢 Spawn、Despawn、Damage、Projectile Impact、Behavior Command 或 Correction 制造“网络稳定”。

---

## 21. Demo 宿主边界

Demo 使用同一套：

```text
Runtime
Worker Domain
Networking
Presentation
```

额外增加：

```text
Scenario
Round window
Readiness
Golden Hash
Failure injection
Metrics
FFmpeg / manual review
```

这些测试设施不能成为生产 Agent Runtime 必需协议。

`RoundPlan`、测试端口、Scenario 枚举等 Demo 数据不得进入插件通用 Replication API。

---

## 22. 文档状态规则

本文只保留当前长期 Runtime / Replication 合同。

旧 R0–R7、Boundary Scheduler、Mass Fragment Projectile Authority、旧协议迁移阶段和历史性能数据已经退出本文正文。

任何历史段落若与 `TargetArchitecture.md` 冲突，以 TargetArchitecture 为准。
