# MassAI Crowd 当前阶段计划

## 1. 文档职责

本文只描述**从当前 `main` 继续收敛到 `TargetArchitecture.md` 的实施顺序**。

已完成能力看 `FeatureChecklist.md`；当前有效测试证据看 `TestScenarioMatrix.md`；历史过程通过 `Docs/History/` 与 Git 历史追溯。

当前总方向：

```text
PHASE 0  Unified Behavior / Development Rule Cut
        ↓
PHASE 1  Missing Specialist Correctness Gates
        ↓
PHASE 2  Performance / Scaling: 1k → 2k → 5k → 10k
        ↓
PHASE 3  Automated Behavior / Visual Acceptance
        ↓
PHASE 4  Human Visual Acceptance
        ↓
PHASE 5  Demo Acceptance CLOSED
```

T5 的边界/容量精确设计见：

```text
Reference/TargetRegionBoundaryCapacityContract.md
```

---

## 2. 当前阶段与状态

| Phase | 名称 | 状态 | 关闭条件 |
|---:|---|---|---|
| 0 | Unified Behavior / Development Rule Cut | SLICE B/B.5 CLOSED ON MAIN / SLICE C VALIDATED | 规则合同与 generic Objective/Cohort/Flow binding foundation 已进入 main；Slice C 已将 T3 迁移到显式 Cohort/Objective/FlowBinding 与 common current-position Worker MovementPlanning，并通过 2x canonical 与相邻场景回归。其余 Demo migration debt 仍 OPEN。 |
| 1 | Missing Specialist Correctness Gates | OPEN | LateJoin、完整双端 T8、剩余 specialist correctness 与已确认 scenario-coupling migration debt关闭。 |
| 2 | Performance / Scaling | OPEN | 同一 Production path 依次通过 1k → 2k → 5k → 10k correctness-preserving performance gates。 |
| 3 | Automated Behavior / Visual Acceptance | OPEN | 自动化行为/视觉指标与相关 editor/runner gates 建立并通过。 |
| 4 | Human Visual Acceptance | OPEN | 人工视觉审查通过，不用表现修饰掩盖模拟问题。 |
| 5 | Demo Acceptance CLOSED | OPEN | 前述阶段均关闭且最终 Demo acceptance 证据完整。 |

当前 correctness 状态：

```text
T6-A                         CLOSED
T6-B                         CLOSED
T6-C                         CLOSED / merged via PR #23
T3 generic Flow migration    IMPLEMENTED / VALIDATED IN SLICE C
LateJoin                     OPEN
Dual T8                      OPEN
Performance                  OPEN
Automated Behavior / Visual  OPEN
Human Visual                 OPEN
```

Phase 1 的已确认代码迁移债：

```text
T1          scenario-name Flow bypass + zero authoritative velocity path
Moving Flow scenario-enum-driven refresh
T6-A        scenario-progress-driven TargetRegion activation
T4          group_exit_hold living in runtime host
T7          FormationIndex-driven continuous movement
```

这些项必须收敛到 `TargetArchitecture.md` 定义的统一 Worker movement contract。Slice B/B.5 已在 main 提供不依赖 scenario identity 的 Objective/Cohort/Flow binding、独立 Flow revisions、current-position sampling 与 `Environment` fallback；Slice C 已迁移 T3，但未外推到 T1、T4、T6-A、T7 或 moving-objective Flow refresh。

---

# 3. 已关闭 — WA8 Source Architecture Cut

## 3.1 已完成的关键结构切换

当前源码已经完成：

```text
Persistent Worker = live server simulation authority
Mass              = entity / engine bridge
ResultApply       = unique host write boundary
Round DAG         = no longer a live server scheduler
```

已物理删除或退出 Production source 的关键结构包括：

```text
full SoftPressure rollback history
FCrowdDemoRoundWorkBatch
BeginBoundaryTransaction
TryPrepareRoundApply
BoundaryOrchestrator / transaction-poll state
legacy Round Stage structs
PostFinalize / AuthorityCommit / ClientPredictionCommit Stage
Prepared Movement Boundary Commit
Prepared Target/Resource Commit Plan
Prepared Particle Diagnostic Commit
PreparedRuntime* second-pass arrays tied to that path
```

Primary Shared Flow Runtime Resource 也已由 `UMassCrowdRuntimeSubsystem` 持有，Worker input 不再以 RoundSimPipeline 作为资源 owner。

## 3.2 当前 bootstrap 例外

WA8 source cut 并没有删除首次 Worker 接管所需的 host bootstrap preparation。

当前允许：

```text
一次性 Boundary facts
→ synchronous bootstrap graph
→ initial Movement / Target / Projectile / Facing controls
→ SubmitPreparedWorkerBootstrapInput()
→ 当前 fixed step 立即成为 Worker-owned
```

这不是旧 Round Transaction 的保留版本，也不能扩展成新的长期 GT simulator。

## 3.3 Source cut 与 runtime evidence 分离

WA8 的 source ownership / transaction structure 已关闭。

运行正确性不依赖历史结构结论自动继承，而由后续 regression / scenario gate 单独建立。

---

# 4. Phase 1 Backlog Detail — Missing Specialist Correctness Gates

当前已经重新证明：

```text
Default Unity Development Editor        PASS
DisableUnity Development Editor         PASS
PersistentWorkerProductionStructure     PASS
Runtime WorkerResultApply / OwnerBarrier PASS
first-step bootstrap                    PASS
ordinary direct-intent                  PASS
minimal T8 server-only                  PASS
Worker Target observability             PASS
T1/T2/T3/T4 post-cut canonical          PASS
T6-A HeterogeneousTransit                PASS / 2x deterministic
T6-B HeterogeneousTargetStatic           CLOSED
T6-C HeterogeneousTargetMoving           CLOSED / PR #23 MERGED
T7 client presentation                  PASS
T7 sparse correction recovery           PASS / 5-run soak
```

这意味着 Worker-only live path 已从“结构成立”进入“核心链真实可运行”。

Phase 1 尚未整体关闭，因为仍缺：

```text
network late join / remaining scenarios
双端 T8 formal runner
其余被 source cut 影响的 diagnostics
confirmed unified-behavior migration debt
```

## 4.1 Build / structure

继续保持：

- Default Unity 与 DisableUnity 都必须可编译。
- 旧 transaction / Stage / Prepared second-pass symbol 不复活。
- `CrowdDemoRoundSimProcessors.h` 仍只有 InputSync + ResultApply 两个 simulation processor。
- Runtime 不反向依赖 Demo。

## 4.2 Bootstrap / ordinary tick

已经建立的关键合同必须保持：

```text
first Production step
  → one-shot bootstrap
  → Worker accepts input sequence
  → same step uses Worker Owner Barrier commit

later Production steps
  → no bootstrap DAG rebuild
  → direct SubmitIntentBatch
  → Worker result sequence exact match
```

禁止为了后续 T5/边界修复重新引入旧 Round fallback。

## 4.3 Result Apply / checkpoint

继续验证：

- stale Generation / Publish / Input / Lifecycle / Stable View 在 first write 前拒绝。
- Dirty Mass Apply 是首次 authoritative GT write。
- Proxy commit / side effects 不重复。
- Dirty Batch ACK 不重复。
- Checkpoint 在 Worker owner commit 之后生成。
- authoritative correction 会正确 invalidate in-flight generation。

## 4.4 Diagnostics recovery

旧 PostFinalize/Particle 第二遍路径删除后，指标按以下分类恢复：

```text
A. still produced from Worker result / retained state
B. can be derived at checkpoint time
C. test-only diagnostic that needs a new explicit observer
D. obsolete metric that should be deleted
```

Worker Target observability 已按只读 ResultApply `Target` / `TargetCohort` observer 完成；类似修复不得重新建立 simulation writer。

---

# 5. Historical Closed Evidence — T5 Long-Window Correctness

当前状态为 CLOSED ON MAIN：PR #18 已合并，并已在 `main@182f4d8dc856102b3a80ade0dc6506ff678c1d6a` 完成 landing revalidation。

## 5.1 已关闭的 T5 子问题

Static T5 已取得 1000+ Tick 重复确定性证据。

Moving T5 原先在 absolute/round-local clock 混用与 dynamic SharedFlow stale 时出现的 SourceAttachment failure 已定位并修复：

```text
Objective effective tick
→ Worker persistent absolute tick domain

Moving Target external fact
→ Runtime-owned dynamic SharedFlow refresh
→ versioned Environment resource
→ Worker Target
```

旧 Moving failure 的典型特征：

```text
source_attachment_failures = 20 / 20
```

在当前修复后不再出现。

因此不能再把 `TargetRevision=1` 或 step ~398 SourceAttachment 当成当前 blocker。

## 5.2 已关闭 blocker：边缘/角落 clipped capacity

canonical Moving 长窗口曾在目标继续靠近 Environment / NavMesh 边缘时暴露以下历史失败：

```text
absolute fixed_step = 1460
target = (-3171, 1900)
target_velocity = (-90, 0)
feasible_regions = 3 / 16
desired = 19
source_attachment_failures = 0
topology_cells = 432
topology_edges = 202
```

这说明：

```text
SourceAttachment        = healthy
Moving objective clock = healthy
Dynamic SharedFlow      = refreshing
```

该问题已通过 clipped feasible Cell、finite capacity、Desired/Assignable/Overflow、reachability-aware admission 与 CapacityHold 关闭。

用户已确认产品/算法设计方向：

> **Target 可以移动到地图边缘/角落；Target Region 只在有效 NavMesh / Environment 区域生成/保留有效 Cell。有效 Cell 的有限容量被占满后，其余 Agent 不得继续往已饱和区域内挤。**

精确合同以：

```text
Reference/TargetRegionBoundaryCapacityContract.md
```

为准。

## 5.3 已实施 Slice A — Clipped Feasible Topology

目标：

```text
Polar candidate cells
→ Environment/NavMesh/reachability/clearance filter
→ only feasible cells contribute Target capacity
```

要求：

- 不要求理论 16 Region 全部存在。
- Target 靠边时自然裁剪；进入角落时可只剩局部/约 1/4 可行域。
- 被裁剪 Cell 不产生 Demand capacity / Plan claim。
- 不通过 reflected Target motion 来规避问题。
- 不按 map / step / region 写生产特判。

## 5.4 已实施 Slice B — Finite Cell Capacity

每个 feasible Cell 必须有确定性的有限容量。

```text
TotalFeasibleCapacity = Σ CellCapacity
AssignablePopulation  = min(DesiredPopulation, TotalFeasibleCapacity)
OverflowPopulation    = max(0, DesiredPopulation - TotalFeasibleCapacity)
```

注意：

- 这不是永久 Agent Slot 系统。
- Cell 可以共享，但不能无限共享。
- Capacity 可以由 usable geometry + physical profile / spacing contract 派生。
- exact capacity formula 必须 deterministic；需要专项设计/测试，而不是 magic number。

## 5.5 已实施 Slice C — Plan / Claim / Overflow

Target Plan / Execution 必须成为容量 admission owner：

```text
Occupied + ActiveClaims <= CellCapacity
```

当容量满时：

- 新 Agent 不得继续消费该 Cell 的占用资格。
- 不得让所有 Agent 继续朝饱和 Target interior 压入，再靠 ORCA/Particle 被动顶住。
- 未获容量的 Agent 进入合法 CapacityHold / Overflow 语义。
- CapacityHold 必须与真正的 `UnroutedFailure` 区分。
- 新容量出现时，Overflow Agent 按稳定顺序重新参与分配。

Local Predictive / ORCA / Particle 仍负责局部安全，不负责 Target capacity admission。

## 5.6 已实施 Slice D — Moving Cell 生命周期

Target 移动导致 Cell valid/invalid 时：

```text
valid → invalid
  → remove capacity
  → release/migrate claim
  → reassign or Overflow

invalid → valid
  → add capacity
  → deterministic refill from Overflow
```

要求无 stale claim、无超卖、无大规模抢占往返振荡。

## 5.7 T5 关闭证据矩阵

至少：

```text
A. Center / full topology
B. Edge clipped topology
C. Corner / quarter topology
D. Capacity saturation
E. Capacity release / refill
F. Moving edge-in / edge-out
G. Deterministic repeat
```

正式关闭条件：

```text
T5 Static >= 1000 Tick PASS + repeat deterministic
T5 Moving >= 1000 Tick PASS + repeat deterministic
TargetRegionTransport automation PASS
RuntimeV2 Target automation PASS
Target affected cohort 10k PASS
boundary/corner capacity tests PASS
Occupied / ActiveClaims never exceed Capacity
legal Overflow does not count as unexplained UnroutedFailure
stale lifecycle = 0
invalid plan / stale claim = 0
Worker Target rejection = 0 for legal capacity saturation
```

当前 main landing 证据：

```text
BoundaryCapacity                              PASS 1/1
Core TargetRegionTransport                    PASS 2/2
Demo TargetRegionTransport                    PASS 7/7
RuntimeV2 Target/TargetObservability           PASS 4/4
GuidanceShard10k prior regression             PASS 1/1 (505.870s; not rerun in landing task)
Static T5 fixed_step=1199                     PASS 2/2 deterministic
Moving T5 fixed_step=1199                     PASS 2/2 deterministic
Moving capacity/assignable/overflow/hold      16/16/4/4
Moving rejection/source-attachment/unrouted   0/0/0
Static worker/transport/execution/guidance     MATCH 2/2
Moving worker/transport/execution/guidance     MATCH 2/2
Moving fixed-step p95                         62.745 / 63.837 ms
Moving commit p95                             0.206 / 0.207 ms
Moving realtime                               0.662 / 0.661 (performance OPEN)
```

修复不得按 step / AgentId / map / region 写生产特判。

---

# 6. Phase 1 Migration Backlog — Duplicate Kernel / Host Shell Cleanup

Round Transaction 已删除后，下一类结构债从“跨帧 scheduler”变成了“Demo host 过重与重复实现”。

## 6.1 Repo-wide consumer audit

重点审计 Demo 与 Plugin Core 中的：

```text
Shared Flow
Target Region Transport
Local Predictive
Particle Constraint
Facing / movement helper
```

每个重复实现必须分类为：

```text
Production consumer
Bootstrap adapter
Diagnostic/test-only consumer
No consumer
```

只有 No consumer 或已有公共 replacement 的实现才物理删除。

## 6.2 RoundSimPipeline 拆分

当前 `UCrowdDemoRoundSimPipelineSubsystem` 已不再是 live simulation scheduler，但仍承担较多 host 职责。

目标拆分方向：

```text
Scenario / Plan Host
Bootstrap Input Adapter
Acceptance / Metrics
Checkpoint / RoundResult Host
Diagnostic State
```

拆分要求：

- 不新增第二个 simulation owner。
- 不把 Worker state 镜像成完整每帧 host state。
- 不建立新的 retained full-array transaction。
- 每个拆分切片都保持现有 Worker Result Apply barrier。

## 6.3 命名清理

`FCrowdDemoBoundaryFacingWorkState` 等历史命名可在职责确认后改名；同时清除已经没有语义的 `Consumed/Shadow/Round` 字段。

命名清理不能与算法行为修改混成一个大 patch。

---

# 7. Phase 2 Prerequisite — Large Particle Island Scaling

当前 Particle 已具备：

- conservative interaction closure。
- connected component / Island decomposition。
- 独立子 Solve。
- deterministic stable merge。
- global exact Applied-State validation。
- validation failure fail-closed fallback。

当前未关闭两层并行：

```text
A. multiple independent islands → actual UE Task parallelism
B. one large dense island        → internal Cell-Pair sharding
```

大型单 Island 目标：

```text
Large Island
    ↓
Stable Spatial Cells
    ↓
Canonical Cell-Pair Owner
    ↓
Per-Round Shards
    ↓
Barrier Merge
    ↓
Next Constraint Round
    ↓
Global Exact Validation
```

要求：

- Pair owner 由稳定 Key 决定。
- Pair 不重复、不遗漏。
- 每轮 barrier 前禁止读取其他 shard 未提交状态。
- 与 monolithic reference 在量化/容差合同内一致。
- 不通过关闭 Hard/Swept/Environment Safety 换性能。

关闭门至少包含 1k/2k/5k/10k 高密度单 Island 微基准与重复确定性运行。

---

# 8. Phase 2–5 Final Evidence — WA9 Full-Scale Acceptance

WA9 是完整 Production Agent Runtime 验收，不是 WorkRing/Spatial 微基准。

规模阶梯：

```text
1,000
2,000
5,000
10,000
```

至少覆盖：

- Lifecycle / continuous spawn-despawn
- Behavior Source / Capability
- Shared Flow / Resource revision
- Target / Cohort / clipped capacity / Overflow
- Combat / Projectile
- Movement / Local Predictive
- Particle / Interaction
- Facing / Finalize
- Networking / late join / correction
- Presentation / visible lifecycle

性能目标以 `TargetArchitecture.md` 为准：

```text
Worker simulation lag p95 <= 66.667 ms
Client frame p95         <= 33.333 ms
Visual p95               <= 16.667 ms
Realtime                 >= 0.95
Propagation limit hit    = 0
Ordered Event loss       = 0
```

GT Result Apply 必须保持有界 Dirty Apply，不能为了 10k 重新恢复完整 Mass traversal。

---

# 9. 验收工具可靠性

双端 T8 历史上存在“业务日志已经生成，但 runner 误判缺失并超时”的问题。

在 WA9 前必须：

- 使用明确 machine-readable completion result / 唯一终局 marker。
- 区分 scenario failure 与 runner parser failure。
- runner FAIL 时不得人工改报正式 PASS。
- post-cut 第一次双端 T8 必须重新建立新的 Golden / event / performance baseline。

T5 Worker Target runner 当前已能把 demand/plan/guidance/domain rejection 作为 hard failure；容量饱和实现后，必须保证合法 Overflow 不被误报为 rejection。

---

# 10. 当前不优先做

在上述 Gate 关闭前，不优先：

- 新 Demo-specific Movement Algorithm。
- 永久 Agent Slot / permanent Region Owner 系统。
- 新 GT Simulation Authority。
- 为已删除 Round API 建兼容层。
- 通过减少实体数量/关闭安全阶段伪造性能 PASS。
- 没有 profile 证据前重写 Target min-cost flow。

以下问题明确延期到对应阶段，不在 Phase 0/1 借题调查：

```text
T2/T3 manual visual abnormality      → Phase 4 Human Visual Acceptance
UE 5.8 ProcessingQueue editor issue  → Phase 3 Automated Behavior / Visual Acceptance
```

注意：T5 已关闭所需的 **finite Cell capacity / transient claim / Overflow** 不属于“永久 Slot 系统”，是当前 Target Region correctness 合同的一部分。

`FCrowdTargetRegionTransportKernel::SolveTransport()` 的逐单位增广可能成为大 deficit 热点，但必须先有 profile 证据。

---

# 11. 完成定义

当前计划最终应收敛到：

```text
Persistent Worker = unique simulation authority
Mass              = entity/proxy/integration layer
Network           = versioned simulation-fact transport
Presentation      = visual proxy
Demo              = production verification host
Legacy Round DAG  = physically absent from production
```

当前最重要的下一动作是：

> **Phase 0 先把 unified behavior development contract 合入 main；随后 Phase 1 关闭 LateJoin、完整双端 T8、剩余 specialist correctness 与已确认 scenario-coupling migration debt。T6-A、T6-B、T6-C correctness 已关闭；Performance、Automated Behavior / Visual Acceptance、Human Visual Acceptance 均仍 OPEN。**
