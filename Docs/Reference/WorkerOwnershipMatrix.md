# Worker Ownership Matrix

本文是字段所有权与写入边界的精确参考，不承担当前架构总览或阶段计划职责。

当前实现事实以 `../CurrentArchitecture.md` 为准；最终终态以 `../TargetArchitecture.md` 为准。

## 1. 总规则

同一个模拟字段在任意时刻只能有一个 Production Owner。

```text
External Facts
    ↓
Persistent Worker Authority
    ↓
Mass / Network / Presentation Proxies
```

Worker 已消费后产生的代理状态不得作为普通输入再次 Echo 回 Worker。只有显式 Authority Correction、Resnapshot、资源 Revision 或 Gameplay Command 可以改变 Worker 权威。

## 2. 稳定身份

生产身份固定使用：

```text
ProviderId
+ StableEntityId
+ LifecycleSerial
```

短 AgentId、Mass Handle、Presentation Slot、Projectile Slot 均不是跨生命周期权威身份。

## 3. 字段所有权

| 字段/域 | 最终 Production Owner | Mass/GT 职责 | Network / Presentation 职责 |
|---|---|---|---|
| Lifecycle | Persistent Worker | 接收 Spawn/Despawn 外部事实；维护 StableRef→Mass Handle 代理映射 | 复制生命周期事实；回收本地表现 |
| Behavior Source Set | Persistent Worker | Host 只提交 Command / Context / Registry | 复制 baseline、command、hash/resync |
| Flow / Resource Revision | Persistent Worker | GT/Host 发布不可变资源 Revision | 复制必要 Revision/Hash，不复制模拟权威 |
| Target / Cohort | Persistent Worker | Host 提供 Objective / Target 外部事实 | 复制必要 Target/Objective 事实或结果 |
| Combat / Reactive | Persistent Worker + Host business adapter | Host 提供业务合法性/伤害账本接口，不直接推进 Worker 字段 | Ordered Event / presentation facts |
| Projectile Simulation | Worker domain / reusable projectile module | Mass Projectile Entity 为引擎代理与提交目标，不建立第二推进器 | 复制 projectile/event/presentation facts |
| Movement Plan | Persistent Worker | Mass 只保存已消费代理或诊断 | correction / presentation consumption |
| Movement | Persistent Worker | Result Apply 写 Mass Transform/Velocity 代理 | correction / interpolation |
| Particle / Interaction | Persistent Worker | GT 不参与逐实体安全求解 | 不单独成为网络权威 |
| Facing / Final Kinematic | Persistent Worker | Result Apply 写最终代理状态 | presentation / correction |
| Simulation Clock | Persistent Worker | GT 只提供外部时间/启动边界，不推进第二套模拟时钟 | checkpoint/digest 时间基线 |
| Ordered Gameplay Event | Worker Owner 分配全局序列 | Host 在原子提交后消费 no-fail side effect | 可靠/有序复制 |
| Presentation State | Presentation subsystem | Simulation 只发布已解析事实 | Presentation 是视觉 owner，不反向写模拟 |

## 4. Work 所有权

调度单位不是线程化 Entity，而是 Work：

```text
Entity / Pair / Resource / Cohort / Timer
                ↓
              WorkItem
                ↓
               Shard
                ↓
             UE::Tasks
                ↓
       Deterministic Owner Merge
```

Shard Task 只能读取冻结 Context，并写自己的局部 `FCrowdWorkerDomainOutput`。Task 不直接竞争写全局 EntityStateStore。

## 5. Result Apply 原子边界

第一次写入前必须完成：

```text
Generation
Publish/Input/Event watermarks
Stable Entity View
Lifecycle
Field Owner
Mass Handle / Fragment Collection
Resource / Target Revision
Behavior / Event admission
Host-specific token
```

全部验证通过后才进入 no-fail commit 区：

```text
Host/Mass Apply
→ Proxy Commit
→ Worker state/event side effects
→ Network / Presentation publish
→ 后续 ACK
```

禁止“先写一半，失败后靠 rollback 恢复”作为普通提交模型。

## 6. 迁移禁则

- 同一字段不得存在 Worker Writer + Legacy Boundary Writer 双写。
- Shadow/Canary 只用于迁移验证，不能成为长期第二套 Runtime。
- Mass Fragment 不得成为 Worker 已迁移字段的隐式下一步输入源。
- Presentation Slot 不得成为生命周期身份。
- Network correction 不得被解释为普通每帧 Transform Authority。
- Demo 不得拥有可被复用的第二套 Commit Barrier、Worker Runtime 或 Simulation Clock。
- 被替代的旧 Barrier、Round Transaction、rollback 数据源、fallback 和 alias 应在迁移完成后物理删除。

## 7. 当前仍需收敛的迁移债务

当前 `main` 的具体未关闭项以 `../CurrentArchitecture.md` 为准。现阶段主要包括旧 Demo Round Transaction / rollback 数据源、Target 长窗口问题、大型单 Particle Island 分片以及完整 WA9 规模验收。

旧 `FullWorkerAuthorityOwnershipMatrix.md` 的历史内容不再作为现行字段事实源。