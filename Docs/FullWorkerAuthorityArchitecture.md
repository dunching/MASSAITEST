# MassAI 全面 Worker 权威架构

## 0. 文档状态

[INFERRED][HIGH] 本文是 WA0–WA9 的目标架构事实源，并正式取代“四个 GT Boundary Processor”作为 AB5 终态。`AB5FourNodeBoundaryContract.md`只保留迁移历史；现有四节点是临时 `Legacy Domain Adapter`，不得再扩展为长期模拟架构。

[COMPUTED][HIGH] 当前生产仍是混合态：Movement、Particle/Interaction与Target/Cohort已拥有Worker Production Owner，Combat、Lifecycle和Behavior仍依赖Legacy Boundary。本文描述目标及迁移门，不把尚未完成的迁移写成现状。

## 1. 唯一权威与方向

[INFERRED][HIGH] 每 World 的持久 `FCrowdAsyncSimulationRuntime`最终持有 Lifecycle、Behavior、Flow/Resource、Target/Cohort、Combat/Reactive、Movement、Particle/Interaction、Facing 和 Simulation Clock 的唯一模拟权威。Mass Fragment、Actor、ISM/VAT与网络缓存只保存已消费的代理版本。

```text
GT / Network / Scene
  └─ Spawn | Despawn | Command | Resource Revision | Correction
       ↓
Persistent Worker Runtime
  └─ Dirty State Patch | Ordered Event | Checkpoint | Diagnostic
       ↓
Mass Proxy | Network Adapter | Presentation
```

[INFERRED][HIGH] GT不得把刚应用的Worker状态作为普通输入回送。普通Correction只递增`CorrectionRevision`并在Owner barrier失效相关工作；全量Resnapshot、World切换和teardown才递增`Generation`。

## 2. Runtime v2 调度基础设施

| 组件 | 冻结合同 |
|---|---|
| `FCrowdWorkerWorkRing` | [INFERRED][HIGH] Current/Next Epoch双队列；同Epoch稳定键去重；显式优先级和公平游标；容量有界。 |
| `FCrowdWorkerTimeWheel` | [INFERRED][HIGH] 按绝对Simulation Tick排序Movement、Projectile、Cooldown、恢复和睡眠唤醒；取消以StableEntityRef/Lifecycle为边界。 |
| `FCrowdWorkerDependencyIndex` | [INFERRED][HIGH] 记录空间邻居、Target/Cohort、Resource订阅和显式实体依赖；变更只唤醒闭合依赖集合。 |
| `FCrowdWorkerResourceStore` | [INFERRED][HIGH] Current与Building Revision分离；Building旁路验证成功后只在Epoch边界原子交换。 |
| `FCrowdWorkerDirtyStateStore` | [INFERRED][HIGH] 同实体/字段状态latest-wins；Damage、Death、Spawn、Despawn等事实必须进入不可覆盖Ordered Event。 |

[INFERRED][HIGH] 生产10k预设为Work=`80000`、Wakeup=`40000`、Dependency Edge=`320000`、Dirty Entity=`16000`、Ordered Event=`64000`、每Epoch最多8轮传播、Shard=`64`实体；所有容量可配置但运行时禁止无界扩容。

## 3. Domain DAG

[INFERRED][HIGH] Domain Executor是无UObject的纯C++实例，在Runtime启动前注册。Runtime冻结DAG后固定执行：

```text
Lifecycle/Input
→ Behavior
→ Flow/Resource
→ Target
→ Combat/Reactive
→ Movement
→ Particle/Interaction
→ Facing/Finalize
→ Publish
```

[INFERRED][HIGH] 每个Epoch读取冻结的Entity与Resource版本；Shard只写独立输出槽。Interaction Pair键固定为`min(StableRefA, StableRefB), max(...)`，Owner按Domain、StableEntityRef和Pair Key稳定归并。

[INFERRED][HIGH] 达到传播轮数上限的工作延期到Next Epoch并计数，不递归自旋；容量不足、开放Interaction Island、事件丢失、依赖漏标或双Writer均fail-closed。

## 4. 公共与项目模块边界

[INFERRED][HIGH] `MassCrowdRuntime`拥有通用调度、队列、Resource、Checkpoint、指标和`ICrowdWorkerDomainExecutor`接口。Particle/Target等可复用Executor位于插件对应模块；`MassAICrowdDemo`只保留Demo规则转换、Demo专用Combat规则及视觉/网络Adapter。

[INFERRED][HIGH] Runtime不依赖项目模块，也不因Projectile依赖Runtime而反向依赖Projectile。业务Executor通过启动前注册接入，Runtime只消费POD输入并产出POD结果。

## 5. 迁移原则

[INFERRED][HIGH] 每个域严格执行`Shadow → 封闭实体Canary → Production → 关闭Legacy Writer`。同一字段在任意时刻只能有一个Production Writer；迁移状态以`FullWorkerAuthorityOwnershipMatrix.md`逐字段审计。

[INFERRED][HIGH] 四节点在WA8前原样承担未迁移域，不先合并、不继续结构优化。WA8删除四节点、完整Mass Gather、Boundary Request/Result/Commit、Frame Transaction和旧Mailbox；最终模拟Processor只保留Worker Input Sync与Worker Result Apply。

## 6. 外部合同稳定性

[INFERRED][HIGH] Behavior Codec v3、HitFact、Projectile和Replication外部载荷语义保持不变。网络Adapter从Worker Checkpoint/Patch/Event编码既有载荷，不再从Mass Fragment构造模拟权威。

[INFERRED][HIGH] Late Join固定顺序为Checkpoint→Resource Revisions→Event Baseline→后续Delta；baseline完成前拒绝增量。Checkpoint与Ordered Event Sequence共同构成rollback/replay边界。

## 7. 验收定义

[INFERRED][HIGH] 最终结构门要求四节点类、Frame Transaction、生产完整Mass Gather、Boundary Commit、`CallExecute()`、阻塞Wait和字段双Writer均为0；模拟Processor恰为Input Sync和Result Apply。

[INFERRED][HIGH] 最终性能门为Worker simulation lag p95≤`66.667ms`、client frame p95≤`33.333ms`、visual p95≤`16.667ms`、realtime≥`0.95`、GT Result Apply p95相对同规模基线不回退、propagation limit hit=`0`、Ordered Event loss=`0`。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
