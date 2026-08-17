# Worker Ownership Matrix

本文是**字段所有权与写入边界**的精确参考，不承担当前架构总览、阶段计划或测试状态职责。

- 当前实现事实：`../CurrentArchitecture.md`
- 最终终态：`../TargetArchitecture.md`
- 当前迁移项：`../PhasePlan.md`

---

## 1. 总规则

同一个模拟字段在任意时刻只能有一个 Production Owner。

最终方向：

```text
External Facts
    ↓
Persistent Worker Simulation Authority
    ↓
Mass / Network / Presentation Proxies
```

Worker 已消费后产生的代理状态不得作为普通输入再次 Echo 回 Worker。

允许改变 Worker 权威的入口只有明确外部事实，例如：

```text
Spawn / Despawn
Gameplay / Behavior Command
Objective / Resource Revision
Environment Revision
Authority Correction
Resnapshot / World transition
```

---

## 2. 稳定身份

生产身份固定使用：

```text
StableEntityRef
= ProviderId
+ StableEntityId
+ LifecycleSerial
```

以下都只是局部句柄或代理位置，不能替代跨生命周期身份：

```text
short AgentId
FMassEntityHandle
Presentation Slot
Projectile visual slot
array index
```

---

## 3. Simulation / Business / Proxy 所有权

| 字段 / 域 | Simulation Owner | Host / GT 边界 | Network / Presentation |
|---|---|---|---|
| Lifecycle | Persistent Worker | Host 提交 Spawn / Despawn 外部事实；Mass adapter 维护 StableRef→Handle 映射 | 复制生命周期；回收本地表现 |
| Behavior Source Set | Persistent Worker | Host 提交 Registry / Context / Command | baseline、command、hash、resync |
| Flow / Resource revision | Persistent Worker | Host 发布版本化环境/导航资源 | 必要 Revision / Hash |
| Target / Cohort | Persistent Worker | Host 提供 Objective / Target 事实 | 复制必要目标事实或权威结果 |
| Combat simulation state | Persistent Worker | Host 提供规则输入、合法性事实和外部业务约束 | Ordered Event / correction / visual facts |
| Business ledger / damage formula | Host product layer | Host 自己是业务 Owner；通过 Prepared Patch / Event 与 Worker 交界 | 只复制已提交业务事实 |
| Reactive motion state | Persistent Worker | Host 通过 Hit / Effect fact 发起，不直接积分位置 | correction / presentation |
| Projectile simulation | Persistent Worker Projectile domain | Mass Projectile Entity 只是 Engine proxy / apply target；Host 不建立第二推进器 | spawn / impact / correction / visual facts |
| Movement Plan | Persistent Worker | Mass 只保存消费后的 proxy 或诊断 | correction / presentation consumption |
| Movement | Persistent Worker | Result Apply 写 Mass Transform / Velocity proxy | correction / interpolation |
| Particle / Interaction | Persistent Worker | GT 不进行并行逐实体安全求解 | 不单独成为网络 authority |
| Facing / Final Kinematic | Persistent Worker | Result Apply 写最终 Mass proxy | presentation / correction |
| Simulation Clock | Persistent Worker | GT 只提供启动/外部时间事实，不推进第二套 clock | checkpoint / digest baseline |
| Ordered Gameplay Event sequence | Worker Owner merge | Host 在原子提交后消费 side effect | 有序 / 可靠传输按合同执行 |
| Presentation facts | Worker / Host 已提交事实源 | GT 只做 adapter | Presentation subsystem 拥有视觉实例状态，但不反向驱动模拟 |

关键区分：

> **Worker 拥有 Combat / Reactive 的模拟状态；Host 拥有具体产品业务规则和业务账本。**

例如“当前正在 windup、Projectile state、Reactive impulse”属于 simulation；“护甲公式、掉落、任务、库存、最终业务扣血规则”属于 Host product layer。

---

## 4. Work 所有权

调度单位不是“线程化 Entity”，而是 Work：

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

Shard Task：

- 读取冻结 Context；
- 写 shard-local `FCrowdWorkerDomainOutput`；
- 不直接竞争写全局 `EntityStateStore`；
- 不因为 Task 完成先后改变最终结果。

Owner Merge 才能统一安装 Dirty State、Ordered Event、Next Work、Wakeup 和依赖声明。

---

## 5. Result Apply 原子边界

第一次写入前必须完成所有可能失败的验证，包括：

```text
Generation
Publish / Input / Event watermark
Stable Entity View
Lifecycle
Field Owner
Mass Handle / Fragment collection
Resource / Target revision
Behavior / Event admission
Host-specific token / prepared plan
```

全部通过后进入 no-fail commit 区：

```text
Host / Mass proxy Apply
→ Runtime Proxy Commit
→ 已验证 Host business / resource side effects
→ Ordered Event / Network / Presentation publish
→ 后续 ACK
```

禁止把“先写一部分，再靠普通 rollback 恢复”当作原子提交模型。

---

## 6. Networking 与 Presentation 不是 Simulation Owner

Networking 可以拥有：

```text
transport sequence
packet assembly
relevancy cache
resync state
client-side replicated baseline
```

但不能拥有 Position、Movement、Combat 等服务端 Simulation 真相。

Presentation 可以拥有：

```text
Instance Slot
interpolation buffer
VAT frame
visual tombstone
resolved visual cache
```

但这些状态不能反向决定 Worker Lifecycle、Movement 或 Combat。

---

## 7. 迁移禁则

- 同一字段不得同时存在 Worker Writer 和 Legacy Boundary Writer。
- Shadow / Canary 只用于迁移验证，不能成为长期第二套 Runtime。
- Mass Fragment 不得成为已迁移字段下一 Epoch 的隐式 authority input。
- Presentation Slot 不得成为 Lifecycle identity。
- Network Correction 不是普通每帧 Transform Authority。
- Demo 不得拥有可被复用的第二套 Commit Barrier、Worker Runtime 或 Simulation Clock。
- 被替代的 Barrier、Round Transaction、rollback 数据源、fallback、alias 和重复 kernel 在迁移完成后应物理删除。

---

## 8. 当前迁移债务

当前 `main` 的具体 OPEN 项以 `../CurrentArchitecture.md`、`../PhasePlan.md` 为准。

现阶段主要是：

```text
WA8 legacy rollback / Round Transaction removal
T5 long-window correctness
large single Particle island scaling
WA9 full-scale acceptance
```

旧 `FullWorkerAuthorityOwnershipMatrix.md` 已退出 active tree；历史内容通过 Git 追溯。
