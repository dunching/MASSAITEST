# MassAI Crowd 当前功能检查表

## 1. 文档职责

本文只回答：**当前 `main` 已经具备哪些能力，哪些仍未关闭。**

实现原理看 `CurrentArchitecture.md`；下一步看 `PhasePlan.md`；测试证据看 `TestScenarioMatrix.md`。

Target 边界/容量精确合同：

```text
Reference/TargetRegionBoundaryCapacityContract.md
```

状态：

```text
DONE    当前源码能力/结构已经成立
PARTIAL 主体成立，但有明确缺口、模式限制或需要重新验证
OPEN    尚未达到最终完成定义
```

---

## 2. 产品与模块边界

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Demo / Plugin 分离 | DONE | `MassCrowdSimulation` 是可复用 Runtime；`MassAICrowdDemo` 是生产架构验证宿主。 |
| Core 隔离 | DONE | `MassCrowdCore.Build.cs` 只依赖 `Core`。 |
| Runtime 不反向依赖 Demo | DONE | 通用 Worker Runtime / Result Barrier 在插件侧。 |
| Standard Sources | DONE | 通用 Movement/Facing/Constraint Source 与 Demo 业务分离。 |
| Networking 独立模块 | DONE | Checkpoint/Intent/Correction/Relevancy 等公共合同已存在。 |
| Presentation 独立模块 | DONE | StableEntityRef 实例生命周期与 Simulation Authority 分离。 |
| Build.cs 依赖事实有文档 | DONE | `Reference/PluginModuleBoundary.md`。 |

---

## 3. Persistent Worker Authority

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Persistent Worker Runtime | DONE | 每 World 的 Async Runtime、Owner continuation、Shard Tasks、状态 Store 已存在。 |
| Worker Production Owner | DONE | Lifecycle、Behavior、Flow/Resource、Target、Combat/Projectile、MovementPlanning、Movement、Particle、Facing 等已有 Worker Domain Owner。 |
| Demo live server Worker-only path | DONE | 首次 one-shot bootstrap 后当前 fixed step 立即 Worker-owned；后续 Tick direct intent。旧 Round simulation fallback 已删除。 |
| Plugin Shadow / Canary 能力 | DONE | 通用插件仍保留 Shadow/Canary；不等于 Demo live Round path 仍有 Legacy fallback。 |
| 非 Full Production Demo server | DONE / fail-closed | 当前已收口 Round path 不回退旧 DAG；不满足 Full Production 条件时拒绝继续，而不是运行第二套 simulator。 |
| Simulation Mass Processor 收敛 | DONE | `CrowdDemoRoundSimProcessors.h` 只剩 Worker InputSync + ResultApply 两个 `UMassProcessor`。 |
| 单字段单 Production Writer | DONE / guarded | Worker field owner + Runtime Owner Barrier 已成立；duplicate host code 只能是 adapter/diagnostic，不得重新推进同一字段。 |
| Worker Result Apply Proxy | DONE | Stable Entity View、Domain Proxy、Dirty Batch、ACK。 |
| Runtime Owner Commit Barrier | DONE | Token → Proxy validate → Host FinalValidate → Dirty Mass Apply → Proxy commit → no-fail side effects。 |
| Legacy Round Transaction 退出 | DONE | `FCrowdDemoRoundWorkBatch`、`BeginBoundaryTransaction`、`TryPrepareRoundApply`、BoundaryOrchestrator 等已从 Production source 物理删除。 |
| Prepared second-pass commit 退出 | DONE | Movement、Target/Resource、Particle Diagnostic 的旧 Prepared transaction channels 已删除。 |
| Post-cut runtime regression | PARTIAL | Build、Architecture、OwnerBarrier、Bootstrap/direct-intent、T1/T2/T3/T4、T5、T6-A/T6-B/T6-C、T7 client presentation + sparse correction recovery 与 T8 server-only 已通过；LateJoin、完整双端 T8 等仍需正式回归。 |

---

## 4. Bootstrap 边界

| 能力 | 状态 | 当前结论 |
|---|---|---|
| 一次性 Worker bootstrap preparation | DONE | 首次接管时同步构造初始 Movement/Target/Projectile/Facing control facts。 |
| Bootstrap 跨帧 Round scheduler | DONE / removed | 不再存在。当前 `FCrowdDemoBootstrapSynchronousGraph` 是 stack-local one-shot graph。 |
| Bootstrap result 直接作为 Mass authority | DONE / prohibited | Bootstrap compute 不直接形成旧 Round commit；必须先提交 Worker，再从 Worker Published Result 进入唯一 Owner Barrier。 |
| 普通 Tick 重建 bootstrap DAG | DONE / prohibited | 已进入 Worker 后，普通 Production Tick direct `SubmitIntentBatch`。 |

---

## 5. 多实体调度

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Entity → Work → Shard → Task | DONE | Work 是调度单位；同 Domain Work 稳定分片后由短 UE Tasks 执行。 |
| Work kinds | DONE | Entity / Pair / Resource / Cohort / Timer。 |
| WorkRing | DONE | Current/Next Epoch、有界容量、WorkKey 合并、Priority×Domain bucket、公平游标。 |
| TimeWheel | DONE | 稀疏 Simulation Tick wakeup。 |
| DependencyIndex | DONE | Entity/Resource/Cohort 增量依赖传播。 |
| Deterministic merge | DONE | Owner merge 不依赖 Task 实际完成顺序。 |
| `ShardEntityCount` 真实语义 | DONE / naming debt | Planner 实际按 WorkItem 数量切片。 |
| Spatial Index 增量维护 | DONE | Spawn/Despawn/UpdateEntity/Cell migration。 |
| 10k scheduler/spatial 专项基础 | DONE / baseline | 旧专项证据存在；不等价于当前完整 10k gameplay acceptance。 |

---

## 6. Behavior / Capability / Lifecycle

| 能力 | 状态 | 当前结论 |
|---|---|---|
| StableEntityRef + LifecycleSerial | DONE | 可拒绝生命周期复用后的 stale facts。 |
| 持续 Spawn/Despawn | DONE | Worker Runtime 不依赖固定 Round agent set。 |
| Capability / Faction 分离 | DONE | Faction 不直接等于 Capability。 |
| Behavior Source Registry / World State | DONE | 多 Source、稳定 Handle、Schema/Hash 合同已存在。 |
| Behavior Worker executor | DONE | Behavior 是 Worker Domain。 |
| T1 generic Lifecycle / Behavior state path | IMPLEMENTED / VALIDATED / SLICE D1-B1 | T1 scenario 仅发布通用 versioned Lifecycle 输入和零 Movement 输出的 semantic Behavior Source；Worker 拥有 transition/order/revision，ResultApply 提供可观察状态。Focused translation/determinism/stale/order tests 与 T1–T4 server-only gates PASS；movement、Flow、Particle participation、reset/correction、acceptance 均保持后续 slice。 |
| Demo Business 独立模块 | DONE | 攻击/物流等项目语义与通用 Runtime 分离。 |

---

## 7. 群体运动 / Target

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Shared Flow | DONE | 世界空间 Macro Guidance。Primary runtime resource 由 `UMassCrowdRuntimeSubsystem` 持有。 |
| Generic Objective/Cohort/Flow binding | CLOSED ON MAIN / SLICE B+B.5 | Worker entity-level binding 支持多个独立 versioned SharedFlow；显式 non-zero `CohortKey` 仅表示稳定宏观分组，不从 Agent/Formation identity 派生。MovementPlanning 从当前位置采样、按 `(FlowResourceId, Revision)` execution-local typed reuse；clear 恢复 `Environment` fallback。DependencyIndex 已双向化并支持原子批量差分替换，所有 Resource revision 通过注册依赖泛型传播。B1–B7、D1–D8、F1–F4、clear/rebind 与 100/1k/10k scale regression 已通过。UE 5.8 rendered Editor/client ProcessingQueue assertion 延期到 Phase 3。 |
| T3 generic FlowBinding consumer | IMPLEMENTED / VALIDATED / SLICE C | 20 entities 以 fixture-owned 10/10 Cohort、2 个 ObjectiveRef、2 个 generic Flow resources 与 20 个 FlowBindings 进入同一 Worker MovementPlanning；当前 Worker position sampling 已由专项测试与 canonical 2x 证明。`FormationIndex` 仅保留在 fixture/acceptance/debug，T3 authoritative preferred-velocity bypass 已移除。视觉与性能仍 OPEN。 |
| Moving Objective absolute clock | DONE | Objective effective tick 与 Worker persistent absolute tick 对齐；pre-round uptime 不再混入 objective age。 |
| Runtime-owned dynamic SharedFlow refresh | DONE | Full Worker Production moving objective 在 intent/resource publish 前刷新 Runtime-owned dynamic SharedFlow；Environment revision 只随语义变化推进。 |
| Target-relative Polar Transport | DONE / core | Polar Cell / Demand / Plan / Quota / Guidance 已进入 Core + Worker Target Domain。 |
| Worker Target long-window observer | DONE | 只读聚合 ResultApply `Target` / `TargetCohort`，输出 machine-readable checkpoint；runner 对 Worker Target/domain rejection fail-closed。 |
| Target Cohort scoped invalidation | DONE / regression invariant | 10k 双 Cohort scoped 专项已经验证；Target 改动后必须持续保持。 |
| NavMesh/Environment-clipped Target Cell contract | DONE | Topology 只让 immutable Environment/SharedFlow 可行 Cell 贡献 capacity；理论 Region 缺失不再直接 fatal；PR #18 已进入 main。 |
| Finite Target Cell capacity | DONE | 使用 Cell 可用几何、angular/radial span 与 physical spacing 推导 deterministic finite capacity；invalid Cell capacity=0。 |
| Target Plan / Claim admission | DONE | reachability-aware deterministic admission；Plan/Execution validation 保证 `Occupied + ActiveClaims <= Capacity`。 |
| CapacityHold / Overflow semantics | DONE | `Desired/Assignable/Overflow` 显式保留；CapacityHold 产生零 Target inward pressure，并与 `UnroutedFailure` 区分。 |
| Moving Cell invalidation / refill | DONE | semantic topology/plan 更新时 release/migrate/reassign，新容量按 stable entity order refill。 |
| Local Predictive | DONE | 位于 MovementPlanning / movement chain。 |
| Particle Soft/Hard/Environment Safety | DONE | 最终安全层位于 Worker Interaction Domain。 |
| 多 Interaction Island 分解 | DONE | closure graph → components → sub-solve → stable merge → global validation。 |
| 多 Island UE Task 并行 | OPEN | 当前算法分岛不等于每岛独立 UE Task。 |
| 大型单 Island 内部分片 | OPEN | Cell-Pair Owner / per-round barrier 未完成。 |
| T5 Static >1000 Tick | DONE / PASS ON MAIN | `main@182f4d8` fixed_step=1199 重复 2/2，worker/transport/execution/guidance hash MATCH，capacity/assignable/overflow/hold=162/20/0/0。 |
| T5 Moving >1000 Tick | DONE / PASS ON MAIN | `main@182f4d8` canonical fixed_step=1199 重复 2/2，capacity/assignable/overflow/hold=16/16/4/4，worker/transport/execution/guidance deterministic，rejection=0。Correctness CLOSED；performance OPEN。 |
| T6-A HeterogeneousTransit | DONE / PASS ON MAIN | 20 agents、7 profiles、Full Worker Production；最终 8147/8148 2/2 deterministic，Particle/domain/hard overlap/environment/stale failure=0。Correctness CLOSED。 |
| T6-B HeterogeneousTargetStatic | DONE / PASS ON MAIN | PR #22 已合并；8165/8166 canonical 2x PASS；Worker hash 与 Target topology/demand/plan/execution/guidance deterministic MATCH；7 profile、20/20 inside、unrouted/overbook/hard failure=0。Correctness CLOSED。 |
| T6-C HeterogeneousTargetMoving | DONE / PASS ON MAIN | PR #23 已合并到 `main@7f0f4247`；correctness CLOSED。 |

---

## 8. Combat / Projectile

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Combat/Reactive Worker Domain | DONE | Combat/Projectile 归 Worker domain execution。 |
| Worker Projectile simulation authority | DONE | active Projectile state、dirty state、ordered events、wakeups 由 Worker 推进。 |
| Entity-native Projectile integration | DONE | 公共 Projectile 模块与 Mass integration 已存在。 |
| Broadphase + swept hit | DONE | 公共空间候选与 sweep 合同已存在。 |
| ImpactFact / HitFact | DONE | 几何/命中事实与 Host 业务解释分离。 |
| Demo Combat Extension | DONE | Worker-side pure C++ extension 不应访问 UWorld/Mass/UObject 隐式状态。 |
| T8 server-only post-cut evidence | DONE | 当前 Worker-only path 已重跑 900 batches、150 events、50/50/50/50、duplicate=0、Golden 一致。 |

---

## 9. Result Apply

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Prepare / Commit 分离 | DONE | Published Batch 先 Prepare，再进入唯一 owner barrier。 |
| Runtime Commit Token | DONE | Generation / Publish / Input / Event / Stable View baseline 被验证。 |
| Host FinalValidate | DONE | 首次写入前可拒绝 stale Mass/生命周期事实。 |
| No-fail commit 区 | DONE | HostApply → ProxyCommit → SideEffects。 |
| Dirty Batch / ACK | DONE | 成功提交后消费，重复/错误 sequence 拒绝。 |
| Demo-local rollback source 退出 | DONE | 完整 rollback replay source 已删除；正常原子性不依赖写后补偿。 |
| Prepared Target/Resource transaction | DONE / removed | 旧 host transaction envelope 已删除；Target simulation state 由 Worker domain authority 管理。 |

---

## 10. Networking

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Versioned packet/chunk | DONE | Checkpoint/Intent/Correction 使用 Generation/Sequence/Hash。 |
| Sparse correction / digest | DONE | 普通一致性不要求每帧全量 Transform authority。 |
| Late Join contract | DONE | Checkpoint → Resource Revisions → Event Baseline → Delta。 |
| Relevancy | DONE | Relevant set/snapshot 位于公共 Networking。 |
| Post-cut network/checkpoint regression | PARTIAL / LATEJOIN OPEN | Worker codec/packet transport 3/3、T7 authority digest + sparse correction recovery 已通过 canonical 2/2 和 extended 3/3；LateJoin 与其它正式 network 场景仍 OPEN。 |
| 双端 T8 runner | OPEN | 历史业务日志存在但正式 runner 有误判/超时；post-cut 双端 formal evidence 仍未关闭。 |

---

## 11. Presentation

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Stable slot table | DONE | StableEntityRef ↔ instance slot 独立生命周期。 |
| Spawn/Update/Despawn | DONE | 表现层拥有自己的幂等实例生命周期。 |
| VAT / Hit response | DONE / POST-CUT | T7 Worker combat authority 已在 client presentation 消费 Idle/Move/Attack/HitReact/Knockback/KnockUp/Death；canonical 2/2、extended correction soak 3/3 PASS。 |
| Presentation 非 Simulation Authority | DONE | 不反向推进 Worker 状态。 |
| 10k 完整客户端表现门 | OPEN | WA9。 |

---

## 12. Diagnostics / Acceptance

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Worker / ResultApply 基础 metrics | DONE | Worker runtime、proxy、commit metrics 结构仍存在。 |
| RoundResult / Checkpoint host assembly | DONE / structural | host path 仍存在，且 checkpoint 位于 Worker owner commit 后。 |
| 旧 PostFinalize diagnostics | DONE / removed | 依赖旧 Stage / Prepared Particle second-pass 的路径已经删除。 |
| Worker Target observability | DONE | `Target` / `TargetCohort` 只读 checkpoint + runner rejection gate 已进入 current path。 |
| Particle/其它 special metrics current completeness | PARTIAL | 继续确认哪些指标由 Worker retained state 产生、哪些 checkpoint derive、哪些 test-only observer。 |
| post-cut Golden/perf baseline | PARTIAL | T1/T2/T3/T4、T5、T6-A/T6-B/T6-C、T7 与 T8 server-only 有当前数据；LateJoin、完整双端 T8、规模与最终性能仍需更新。 |
| Automated Behavior / Visual Acceptance | OPEN | Phase 3；尚未完成。UE 5.8 ProcessingQueue editor issue 延期到此阶段。 |
| Human Visual Acceptance | OPEN | Phase 4；尚未完成。T2/T3 manual visual abnormality 延期到此阶段。 |

---

## 13. Legacy / 可读性治理

| 能力 | 状态 | 当前结论 |
|---|---|---|
| RoundSim 不再是 Processor DAG | DONE | 旧 Stage surface 已物理删除；只保留两个真实 UMassProcessor。 |
| RoundSim 不再是跨帧 simulation transaction scheduler | DONE | WorkBatch / Boundary transaction / PrepareRoundApply 已删除。 |
| WorkerInput 与 RoundSimPipeline Primary Resource 解耦 | DONE | Shared Flow resource 由 RuntimeSubsystem 持有。 |
| Demo generic duplicate kernel 全退出 | OPEN | 仍需 repo-wide consumer audit。 |
| Bootstrap scratch / historical naming cleanup | OPEN | `BoundaryFacingWorkState` 等仍有旧命名与可能失去消费者的字段。 |
| 巨型 Demo host 文件职责拆分 | OPEN | Pipeline/Processors/Coordinator 仍混合 Scenario/Bootstrap/Metrics/Checkpoint。 |
| Default Unity Runtime compile | DONE | Runtime helper collision 已修复；Default Unity 与 DisableUnity 都有 current PASS 证据。 |
| 文档↔源码当前主事实 | DONE / ongoing | 核心文档需随 Moving Target / T5 correctness 状态持续同步。 |

---

## 14. 当前规模结论

当前可以说：

```text
10k-aware architecture              = YES
10k scheduler/spatial baseline       = YES
10k target scoped regression         = YES
post-cut full 10k gameplay evidence  = NO
full 10k product acceptance          = NO
```

完整 WA9 仍必须覆盖：

```text
Lifecycle / Behavior
Flow / Resource / Target capacity
Combat / Projectile
Movement / Local Predictive
Particle / Interaction
Facing / Result Apply
Networking / Late Join
Presentation
Performance
```

---

## 15. 当前主要 OPEN Gate

1. **Missing Specialist Correctness Gates**：LateJoin/剩余 network、完整双端 T8、剩余 diagnostics 与已确认 unified-behavior migration debt。
2. **Performance / Scaling**：1k→2k→5k→10k，同一 Production path；包含 Particle island scaling。
3. **Automated Behavior / Visual Acceptance**：自动化行为、视觉与 editor/runner gates。
4. **Human Visual Acceptance**：人工视觉审查。
5. **Demo Acceptance**：只有前述阶段全部关闭后才可标记 CLOSED。

历史完成过程不在本文累积；正式证据状态以 `TestScenarioMatrix.md` 为准。
