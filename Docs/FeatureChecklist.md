# MassAI Crowd 当前功能检查表

## 1. 文档职责

本文只回答：**当前 `main` 已经具备哪些能力，哪些仍未关闭。**

实现原理看 `CurrentArchitecture.md`；源码冲突审计看 `SourceConsistencyAudit.md`；下一步看 `PhasePlan.md`；测试证据看 `TestScenarioMatrix.md`。

状态：

```text
DONE    当前能力/结构已经成立
PARTIAL 主体成立，但有明确缺口或运行模式限制
OPEN    尚未达到最终完成定义
```

---

## 2. 产品与模块边界

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Demo / Plugin 分离 | DONE | `MassCrowdSimulation` 是可复用 Runtime；`MassAICrowdDemo` 是验证宿主。 |
| Core 隔离 | DONE | `MassCrowdCore.Build.cs` 只依赖 `Core`。 |
| Runtime 不反向依赖 Demo | DONE | 通用 Worker Runtime / Result Barrier 位于插件侧。 |
| Standard Sources | DONE | 通用 Movement/Facing/Constraint Source 独立于 Demo 业务。 |
| Networking 独立模块 | DONE | Checkpoint/Intent/Correction/Relevant Snapshot 等位于公共模块。 |
| Presentation 独立模块 | DONE | StableEntityRef 实例生命周期与 Simulation Authority 分离。 |
| Build.cs 依赖事实有文档 | DONE | 精确主干由 `Reference/PluginModuleBoundary.md` 维护。 |

---

## 3. Persistent Worker Authority

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Persistent Worker Runtime | DONE | 每 World 的 Worker Runtime、Owner Pump、Shard Task、状态 Store 已存在。 |
| Worker Production Owner 实现 | DONE | Lifecycle、Behavior、Flow/Resource、Target、Combat/Projectile、Movement、Particle、Facing 等已有 Production-capable Owner/Executor。 |
| 普通 Demo 默认 Full Production | PARTIAL | **无显式 Production 参数时 WorkerV2 / Movement / Behavior 当前默认 Shadow。** 正式 Production runner/path 已存在，但不能把“实现已完成”写成“所有启动默认接管”。 |
| Simulation Mass Processor 收敛 | DONE | 核心 Processor = Worker Input Sync + Worker Result Apply；客户端可有 Visual Processor。 |
| 单字段单 Production Writer 合同 | DONE / guarded | Worker field owner / commit 合同存在；旧 Round 数据源与事务残留仍需 WA8 物理退出。 |
| Worker Result Apply Proxy | DONE | Stable Entity View、domain proxy、Dirty Batch、ACK 已存在。 |
| Runtime Owner Commit Barrier | DONE | Token / Proxy validate / Host FinalValidate / no-fail commit 位于 `MassCrowdRuntime`。 |
| 普通帧 Legacy Round Transaction 完全退出 | OPEN | Round rollback source、`TryPrepareRoundApply`、Demo-local transaction 等仍是 WA8。 |

---

## 4. 多实体调度

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Entity → Work → Shard → Task | DONE | Work 是调度单位；同 Domain Work 分片后通过短 UE Tasks 执行。 |
| Work kinds | DONE | Entity / Pair / Resource / Cohort / Timer。 |
| WorkRing | DONE | Current/Next Epoch、有界容量、WorkKey 去重、Priority×Domain bucket、公平游标。 |
| TimeWheel | DONE | 稀疏 Simulation Tick wakeup。 |
| DependencyIndex | DONE | Entity/Resource/Cohort 依赖可增量传播。 |
| Deterministic merge | DONE | Owner merge 不依赖 Task 实际完成顺序。 |
| `ShardEntityCount` 真实语义 | DONE / naming debt | 当前 Planner 实际按 WorkItem 数量切片，名称仍容易误导。 |
| Spatial Index 增量维护 | DONE | Worker SpatialIndex 支持 Spawn/Despawn/UpdateEntity/Cell migration。 |
| 10k scheduler/spatial 专项 | DONE | 仓库已有对应本地 UE 回归记录；不等价于完整 10k gameplay。 |

---

## 5. Behavior / Capability / Lifecycle

| 能力 | 状态 | 当前结论 |
|---|---|---|
| StableEntityRef + LifecycleSerial | DONE | 可拒绝生命周期复用后的 stale facts。 |
| 持续 Spawn/Despawn | DONE | 不依赖固定 Round agent set。 |
| Capability / Faction 分离 | DONE | Faction 不直接等于 Capability。 |
| Behavior Source Registry / World State | DONE | 多 Source、稳定 Handle、Schema/Hash 合同已存在。 |
| Behavior Worker executor | DONE | Behavior 是 Worker Domain，依赖 Lifecycle/Input。 |
| Demo Business 独立模块 | DONE | 攻击/物流等项目语义与通用 Runtime 分离。 |

---

## 6. 群体运动

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Shared Flow | DONE | 世界空间宏观 Guidance。 |
| Target-relative Polar Transport | DONE | Polar Cell / Demand / Plan / Quota / Guidance 已进入 Core + Worker Target Domain。 |
| Target Cohort scoped invalidation | DONE | 已有 10k 双 Cohort 本地回归证据。 |
| Local Predictive | DONE | MovementPlanning 中消费通用事实并进入统一移动链。 |
| Particle Soft/Hard/Environment Safety | DONE | 最终安全层已进入 Worker Interaction Domain。 |
| 多 Interaction Island 分解 | DONE | conservative closure graph → connected components → 独立子 Solve → stable merge → global validation。 |
| 多 Island Task 并行 | OPEN | 当前多个 Island **仍在一个 Particle Resource Work 内顺序循环 Solve**；`bUsedIslandSharding` 不代表 UE Task 并行。 |
| 大型单 Island 内部分片 | OPEN | 单 component 当前走 monolithic solve；Cell-Pair Owner / per-round barrier 未完成。 |
| T5 >900 Tick 稳定性 | OPEN | step ~886 的 feasible-region-insufficient 记录尚未关闭。 |

---

## 7. Combat / Projectile

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Combat/Reactive Worker Domain | DONE | Combat 与 Projectile field 都归 `CombatReactive` execution rank。 |
| Worker Projectile simulation authority | DONE | Executor 内持有 active Projectile state，并产生 dirty state / ordered event / wakeup。 |
| Entity-native Projectile integration | DONE | 公共 Projectile 模块与 Mass integration 存在，不依赖逐 Projectile Actor 作为规模路径。 |
| Broadphase + swept hit | DONE | 公共空间候选与相对/环境 Sweep 已存在。 |
| ImpactFact / HitFact | DONE | 几何/命中事实与 Host 业务解释分离。 |
| Demo Combat Extension | DONE | Worker-side pure C++ extension 不能访问 UWorld/Mass/UObject 隐式状态。 |
| T8 server-only formal evidence | DONE | 仓库记录有 900 batch / 150 event / 50 次攻击链 Golden；ChatGPT 本轮未独立运行。 |

---

## 8. Result Apply

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Prepare / Commit 分离 | DONE | Published Batch 先 Prepare 再提交。 |
| Runtime Commit Token | DONE | Generation / Publish / Input / Event / Stable View 基线被验证。 |
| Host FinalValidate | DONE | 宿主可在首次写入前拒绝 stale Mass/资源/生命周期事实。 |
| No-fail commit 区 | DONE | HostApply → ProxyCommit → SideEffects；不以写后补偿 rollback 作为正常路径。 |
| Dirty Batch / ACK | DONE | 成功提交后消费；错误 sequence 拒绝。 |
| Demo-local rollback source 退出 | OPEN | WA8。 |

---

## 9. Networking

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Versioned packet/chunk | DONE | Checkpoint/Intent/Correction 使用 Generation/Sequence/Hash。 |
| Sparse correction / digest | DONE | 普通一致性不要求每帧全量 Transform Authority。 |
| Late Join | DONE | Checkpoint/资源/Event baseline/后续增量合同存在。 |
| Relevancy | DONE | Relevant set/snapshot 位于公共 Networking。 |
| 双端 T8 正式 runner | PARTIAL | 日志有诊断证据，但正式 runner 仍有误判/超时记录，不能登记正式 PASS。 |

---

## 10. Presentation

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Stable slot table | DONE | StableEntityRef ↔ instance slot 独立生命周期。 |
| Spawn/Update/Despawn | DONE | 表现层拥有自己的幂等实例生命周期。 |
| VAT / Hit response | DONE | Demo 有五状态与 HitFlash 路径。 |
| Presentation 非 Simulation Authority | DONE | 不反向推进 Worker 状态。 |
| 10k 完整客户端表现门 | OPEN | WA9。 |

---

## 11. Legacy / 可读性治理

| 能力 | 状态 | 当前结论 |
|---|---|---|
| RoundSim 不再是 Processor DAG | DONE | 动态 Simulation Processor 已收敛；旧 Stage struct 仍存在。 |
| WorkerInput 与 RoundSimPipeline 完全解耦 | OPEN | WorkerInputSync 仍读取 `GetRuntimeSharedFlowField()`。 |
| Demo generic duplicate kernel 全退出 | OPEN | Particle/SharedFlow 仍有 diagnostics consumer；其他需要 repo-wide audit。 |
| 巨型 Demo 文件职责拆分 | OPEN | Pipeline/Processors/Mixed/Coordinator 仍显著混合迁移和测试职责。 |
| Demo Unity build 恢复 | OPEN / deferred | 当前 `bUseUnity=false` 是 legacy helper 同名造成的结构债信号；应在清理后再评估。 |
| 源码阅读地图 | DONE | `SourceReadingMap.md`。 |
| Legacy 风险清单 | DONE | `LegacyCodeInventory.md`。 |
| 文档↔源码审计 | DONE / ongoing | `SourceConsistencyAudit.md` 维护当前冲突。 |

---

## 12. 当前规模结论

当前可以说：

```text
10k-aware architecture          = YES
10k scheduler/spatial tests     = YES
10k target scoped test          = YES
full 10k gameplay acceptance    = NO
```

完整 WA9 仍必须覆盖：

```text
Behavior
Target
Movement
Particle
Combat / Projectile
Networking
Presentation
```

---

## 13. 当前主要 OPEN Gate

1. **WA8 Legacy Removal**：断开 RoundSimPipeline resource/transaction/rollback 依赖，删除失去消费者的 Stage 与重复实现。
2. **T5 Long-Window Correctness**：关闭 step ~886 feasible-region-insufficient，Static/Moving 1000+ Tick。
3. **Particle Scaling**：先明确 island-level task parallelism，再做大型单 Island Cell-Pair/per-round barrier。
4. **WA9 Full-Scale Acceptance**：1k→2k→5k→10k 同一 Production path，双端网络/表现/性能。
5. **Test Harness Reliability**：修复双端 T8 runner 误判/超时。

历史完成过程不在本文累积；测试细节以 `TestScenarioMatrix.md` 为准。
