# MassAI Crowd 当前阶段计划

## 1. 文档职责

本文只描述**从当前 `main` 继续收敛到 `TargetArchitecture.md` 的实施顺序**。

已完成能力看 `FeatureChecklist.md`；当前有效测试证据看 `TestScenarioMatrix.md`；历史过程通过 `Docs/History/` 与 Git 历史追溯。

当前总方向：

```text
WA8 Source Architecture Cut          = structurally closed
        ↓
Post-cut Runtime Regression Gate     = next
        ↓
T5 Long-Window Correctness
        ↓
Duplicate Kernel / Host Shell Cleanup
        ↓
Large Particle Island Scaling
        ↓
WA9 Full-Scale Acceptance
        ↓
Target Architecture
```

---

## 2. 当前优先级

| 顺序 | Gate | 状态 | 关闭条件 |
|---:|---|---|---|
| 0 | WA8 Source Architecture Cut | CLOSED / structural | 第一代跨帧 Round Transaction、完整 rollback source、`FCrowdDemoRoundWorkBatch`、`BeginBoundaryTransaction`、`TryPrepareRoundApply`、BoundaryOrchestrator、旧 Stage surface、Prepared second-pass commit channels 已从 Production source 退出。 |
| 1 | Post-cut Runtime Regression | OPEN | 新 `main` 完成 UE build、Architecture automation、核心 T1–T8、checkpoint/network/late join/diagnostic 回归；旧 baseline 不能自动继承为当前 PASS。 |
| 2 | T5 Long-Window Correctness | OPEN | 关闭 step ~886 Target Demand `feasible-region-insufficient`；Static/Moving 1000+ Tick 都通过。 |
| 3 | Duplicate Kernel / Host Shell Cleanup | OPEN | 删除确认失去消费者的 Demo generic duplicate；把 RoundSimPipeline 按 Host Plan / Bootstrap / Metrics / Checkpoint 职责进一步拆分。 |
| 4 | Large Particle Island Scaling | OPEN | Island-level task parallelism与单大型 Interaction Island Cell-Pair Owner / per-round barrier 分片获得确定性与性能证据。 |
| 5 | WA9 Full-Scale Acceptance | OPEN | 同一 Production Runtime 在 1k→2k→5k→10k 完整 Simulation + Network + Presentation + Performance 门通过。 |

双端 T8 runner 的日志误判/超时属于验收工具可靠性问题，必须在完整 WA9 前关闭。

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

## 3.3 为什么 WA8 只标 structural closed

本轮 source cut 修改了 Demo live execution path，并删除了旧 PostFinalize/Particle 诊断 side effects。

因此当前只可以确认：

```text
source ownership / transaction structure = closed
```

不能确认：

```text
runtime behavior regression = passed
```

后者由 Gate 1 单独关闭。

---

# 4. Gate 1 — Post-cut Runtime Regression

这是当前最高优先级。

目标不是新增功能，而是证明 source cut 后的 Worker-only live path 真正可运行，并重新建立可信测试基线。

## 4.1 Build / structure

至少执行：

```text
Development Editor build
DisableUnity build where current project requires it
CrowdDemo.Architecture.PersistentWorkerProductionStructure
Runtime Worker Result Apply / Owner Commit Barrier automation
```

要求：

- 无编译错误。
- 旧 transaction / Stage / Prepared second-pass symbol 不复活。
- `CrowdDemoRoundSimProcessors.h` 仍只有 InputSync + ResultApply 两个 simulation processor。
- Runtime 不反向依赖 Demo。

## 4.2 Bootstrap / ordinary tick

必须分别验证：

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

禁止为了回归通过重新引入旧 Round fallback。

## 4.3 Result Apply / checkpoint

继续验证：

- stale Generation / Publish / Input / Lifecycle / Stable View 在 first write 前拒绝。
- Dirty Mass Apply 是首次 authoritative GT write。
- Proxy commit / side effects 不重复。
- Dirty Batch ACK 不重复。
- Checkpoint 在 Worker owner commit 之后生成。
- authoritative correction 会正确 invalidate in-flight generation。

## 4.4 T1–T8 回归策略

不要求第一轮立刻把所有场景都重新宣告正式 PASS；顺序建议：

```text
Architecture + Runtime atomicity
→ 最小 T8 server-only
→ T5 short window
→ T1/T2/T3/T4/T6/T7
→ 双端 T8
→ T5 long window
```

原因：最小 T8 最快覆盖 Worker Combat/Projectile/ResultApply；T5 最快暴露 Target/Movement/Particle bootstrap 与连续推进问题。

## 4.5 Diagnostics recovery

旧 PostFinalize/Particle 第二遍路径删除后，需要逐项确认指标现在由什么权威事实产生。

分类：

```text
A. still produced from Worker result / retained state
B. can be derived at checkpoint time
C. test-only diagnostic that needs a new explicit observer
D. obsolete metric that should be deleted
```

禁止仅为恢复日志重新建立第二套 simulation commit path。

## 4.6 Gate 关闭条件

至少满足：

```text
UE build PASS
Architecture automation PASS
Runtime ResultApply atomicity PASS
first-step bootstrap PASS
ordinary Production intent path PASS
minimal T8 server-only post-cut PASS
representative movement/target scenario post-cut PASS
checkpoint/network basic regression PASS
critical diagnostics either restored or explicitly retired
```

关闭后，`TestScenarioMatrix.md` 才能把对应 pre-cut baseline 恢复为当前 PASS。

---

# 5. Gate 2 — T5 Long-Window Correctness

当前已知历史 blocker：长窗口在 step ~886 出现 Target Demand `feasible-region-insufficient`；600 Tick 成功不能覆盖。

先在 Gate 1 证明新主链基本运行，再继续定位长期问题，避免把 source-cut regression 与旧 T5 correctness bug 混在一起。

处理原则：

- 建立稳定 1000+ Tick Static / Moving runner。
- 同时记录 Target Revision、Feasible Graph Hash、Demand、Plan、Quota、Unrouted、Cohort revision。
- 判断问题来自 Demand、可行图变化、old Plan reuse、quota execution、terminal retention 还是供需失配。
- 修复必须作用于通用 Target Region Transport 合同。
- 禁止按 step / AgentId / map / region 写生产特判。
- 修复后重跑短窗口与 10k scoped cohort 专项，避免局部修复破坏增量失效行为。

关闭门：

```text
T5 Static >= 1000 Tick PASS
T5 Moving >= 1000 Tick PASS
TargetRegionTransport automation PASS
RuntimeV2 Target automation PASS
stale lifecycle = 0
unexplained invalid plan = 0
unexplained unrouted = 0
repeated run Stable Hash consistent
```

---

# 6. Gate 3 — Duplicate Kernel / Host Shell Cleanup

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

# 7. Gate 4 — Large Particle Island Scaling

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

# 8. Gate 5 — WA9 Full-Scale Acceptance

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
- Target / Cohort / Polar Transport
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

---

# 10. 当前不优先做

在上述 Gate 关闭前，不优先：

- 新 Demo-specific Movement Algorithm。
- 新 Slot / permanent Region Owner 系统。
- 新 GT Simulation Authority。
- 为已删除 Round API 建兼容层。
- 通过减少实体数量/关闭安全阶段伪造性能 PASS。
- 没有 profile 证据前重写 Target min-cost flow。

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

当前最重要的下一动作不是继续设计 Authority，而是：

> **先把刚完成的 Worker-only source cut 重新跑成可信 runtime baseline，再继续做 correctness、清理和 scale。**
