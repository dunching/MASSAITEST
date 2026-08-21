# MassAI Crowd 当前测试场景矩阵

## 1. 文档职责

本文记录**当前可用于判断实现状态的测试合同、最后有效 baseline 和下一次必须取得的证据**。

本文件不保存逐日 runner 日志，也不把架构切换前的 PASS 自动继承为新主链当前 PASS。

Target 边界/容量精确合同：

```text
Reference/TargetRegionBoundaryCapacityContract.md
```

当前状态定义：

```text
PASS          已在当前相关执行结构上获得正式证据
STATIC PASS   只完成静态结构/源码审计；不是 UE runtime PASS
BASELINE      旧执行结构最后有效结果，可用于对比，但不是当前验收
REVALIDATE    以前有 PASS/baseline，但相关 live path 已改变，必须重跑
DIAGNOSTIC    有真实业务结果，但正式 runner/验收形式未通过
FAIL          已知真实 correctness failure 尚未被关闭
NOT RUN       当前结构上尚未执行
OPEN          实现/规模门本身尚未完成
RETIRED       验收对象/机制已从当前架构物理删除，不再作为当前测试门
```

---

# 2. 当前关键证据摘要

| Gate | 当前状态 | 最后证据 / 当前结论 | 下一次必须证明 |
|---|---|---|---|
| PR12 Source Architecture Cut | STATIC PASS / CLOSED | 旧 transaction / Stage / Prepared second-pass production symbols 为 0；Processor surface 仅 2 个 `UMassProcessor`；Owner Barrier 在 Checkpoint 前。 | 保持不回归。 |
| Persistent Worker Production structure automation | PASS | `CrowdDemo.Architecture.PersistentWorkerProductionStructure` PASS 1/1。 | 后续 Target/host 改动继续回归。 |
| Runtime Worker Result Apply / Owner Barrier | PASS | `MassCrowd.Runtime.WorkerResultApply` PASS 4/4；Host atomicity fixture PASS。 | 后续 ownership 改动继续回归。 |
| Default Unity / DisableUnity | PASS | Slice C final source：UE 5.8 Development Editor Default full rebuild 199/199，DisableUnity/NoAdaptiveUnity full rebuild 262/262，均真实链接。 | 后续 Runtime/Target TU 修改继续多模式回归。 |
| Minimal Full Production T8 server-only | PASS | 900 batches、90,940 patches、150 Ordered Events、attack/spawn/impact/damage=50/50/50/50、duplicate=0/0、Golden 一致。 | Target capacity 修改不应影响；后续双端仍需 formal runner。 |
| T1/T2/T3/T4 post-cut canonical | PASS / POST-CUT | Slice C final source：T3 generic FlowBinding path 2/2 deterministic PASS；T2/T4 各 1/1 Full Worker Production runner gate PASS。T1 未在 Slice C 改动。 | 后续对应 domain 修改继续回归。 |
| T7 client authority correction recovery | PASS / POST-CUT | canonical 2/2 + 60 秒 extended 3/3；每次 sparse correction 后 prediction 恢复，runtime hard failure=0、sidecar mismatch=0、最终 authority converge。 | 保持 clean barrier、correction fence、same-revision conflict fail-closed。 |
| 双端 T8 formal runner | OPEN / tool issue | 业务历史证据存在，但正式 runner completion 仍有误判/超时债。 | Phase 1 修 runner 后正式双端执行。 |
| T5 Worker Target observability | PASS ON MAIN | ResultApply `Target` / `TargetCohort` 只读 checkpoint valid；Capacity/Assignable/Overflow/CapacityHold machine-readable；absolute Plan build tick 保留可观察但不污染跨进程语义 hash；main landing runner gates PASS 9/9。 | 后续 Target schema/observer 修改继续保持。 |
| T5 Static long-window | PASS ON MAIN | `main@182f4d8` 20-agent `fixed_step=1199` 重复 2/2 PASS；`worker_state_hash=2552718252579781132`、transport/execution/guidance=`2904239141/215575823/3690210786` 均 MATCH；capacity/assignable/overflow/hold=`162/20/0/0`；unrouted/rejection=0。 | 保持 deterministic regression。 |
| Moving objective absolute clock | PASS | objective effective tick 与 Worker absolute tick 对齐；pre-round uptime 不进入 objective age。 | 边界能力实现后保持。 |
| Runtime-owned dynamic SharedFlow | PASS | moving objective 下 Environment revision 随语义变化推进；旧 bootstrap-only stale resource 问题关闭。 | 边界能力实现后保持。 |
| T5 Moving step ~398 SourceAttachment failure | RETIRED / fixed cause | clock-domain mismatch + stale dynamic SharedFlow 已修；`source_attachment_failures=20/20` signature 不再出现。 | 不回归。 |
| T5 Moving boundary/corner capacity | PASS / CLOSED ON MAIN | `main@182f4d8` canonical Moving 2/2 PASS，`fixed_step=1199`；capacity/assignable/overflow/CapacityHold=`16/16/4/4`；worker/transport/execution/guidance=`14917583195891517447/1132313112/3830538097/1265691592` 全部 MATCH；rejection/source-attachment/unrouted/overbook/hard failure=0。Correctness CLOSED；realtime `0.662/0.661`，performance OPEN。 | 保持 correctness；性能单独进入后续 gate。 |
| TargetRegionTransport automation | PASS ON MAIN | Core 2/2（含 BoundaryCapacity 1/1）、`CrowdDemo.SoftPressure.TargetRegionTransport` 7/7、Plugin/Core equivalence 1/1、WorkPipeline 1/1。 | 后续 Target 变更继续全量回归。 |
| RuntimeV2 Target | PASS ON MAIN | RuntimeV2 Target 4/4、TargetObservability 2/2、Dynamic SharedFlow 1/1；既有 GuidanceShard10k 1/1（505.870s）证据保留。 | 保持 scoped invalidation/determinism。 |
| T6-A HeterogeneousTransit | PASS / CLOSED ON MAIN | 最终 `8147/8148` 2x deterministic：20/20/20/20，hard/swept/deadlock/unreachable=0；Worker hash `17016630613422674083`，progress/topology/demand/transport/guidance/validation hashes 全部 MATCH；RuntimeV2 42/42。 | 保持 heterogeneous Profile、Particle safety、Target handoff 与 future-effective Objective fence。 |
| T6-B HeterogeneousTargetStatic | PASS / CLOSED ON MAIN | PR #22 已合并；`8165/8166` 2x canonical deterministic PASS；20/20 inside、7 profile、capacity/assignable/overflow/hold=`238/20/0/0`、unrouted/overbook/hard failure=`0/0/0`；Worker 与 Target 五组语义 hash 全部 MATCH。 | 保持 profile-aware capacity、Worker-only authority 与 T6-B runner gate。 |
| T6-C HeterogeneousTargetMoving | PASS / CLOSED ON MAIN | Correctness 经 PR #23 合并到 `main@7f0f4247`；旧 `8150` smoke 不再代表当前状态。 | 保持 heterogeneous moving target correctness，不外推为 LateJoin、Dual T8 或 performance PASS。 |
| Lifecycle | BASELINE / REVALIDATE | 旧 2/2 证据存在；T1、T6-A、T6-B、T6-C 已有 post-cut evidence。 | 完整 LateJoin lifecycle 继续回归。 |
| WorkRing / TimeWheel / Spatial 10k | BASELINE | 1k/2k/5k/10k scheduler、10k sparse wakeup、10k dirty spatial 专项已有记录。 | WA9 前最终源码重跑。 |
| Target 10k 双 Cohort scoped invalidation | PASS / regression invariant | 受影响 Cohort 执行，未受影响 Cohort 无 Dirty/Topology rebuild。 | finite capacity / claim 改动后必须保持。 |
| Particle 多闭合 Island | BASELINE | independent sub-solve + stable merge + global exact validation 已有专项证据。 | Particle scaling 改动前后都需 reference regression。 |
| Particle 多 Island UE Task 并行 | OPEN | 当前算法分岛不等于每岛独立 UE Task。 | 实现后证明 determinism + speedup。 |
| Particle 大型单 Island | OPEN | Cell-Pair Owner / per-round Barrier 未完成。 | 1k/2k/5k/10k dense single-island correctness/perf。 |
| Networking / Late Join post-cut | PARTIAL / LATEJOIN OPEN | Worker network automation 3/3 与 T7 digest/sparse correction recovery 已重验；一次非 canonical late-join client 暴露 objective bootstrap decode fail-closed，未在本轮处理。 | 独立重跑 checkpoint/LateJoin 正式 baseline；不得用 T7 correction PASS 替代。 |
| Performance / Scaling | OPEN | 专项 baseline 不等于完整 production performance gate。 | Phase 2 按 1k→2k→5k→10k 依次建立 correctness-preserving evidence。 |
| Automated Behavior / Visual Acceptance | OPEN | 尚未执行最终自动化行为/视觉门。 | Phase 3 处理；包含延期的 UE 5.8 ProcessingQueue editor issue。 |
| Human Visual Acceptance | OPEN | 尚未执行最终人工视觉门。 | Phase 4 处理；包含延期的 T2/T3 manual visual abnormality。 |
| WA9 完整 10k Production | NOT RUN | 尚无当前完整 gameplay/network/presentation 10k 门。 | 1k→2k→5k→10k 完整验收。 |

---

# 3. Post-cut Regression Gate

## 3.1 已通过的核心门

```text
Default Unity Development Editor               PASS
DisableUnity Development Editor                PASS
PersistentWorkerProductionStructure            PASS
Runtime OwnerCommit / ResultApply               PASS
first-step bootstrap                           PASS
ordinary direct-intent                         PASS
minimal T8 server-only                         PASS
Worker Target observability                    PASS
T1/T2/T3/T4 canonical                          PASS / POST-CUT
T7 client presentation + correction recovery  PASS / POST-CUT
```

这些结果与 PR #23 landing 证明 Worker-only live path、T1–T4、T6-A、T6-B、T6-C 和 T7 sparse correction recovery 已有当前证据，但不自动关闭 LateJoin/其余 network 场景、完整双端 T8、Performance 或视觉验收。

## 3.2 First-step bootstrap 合同

必须保持：

```text
valid boundary facts
→ BeginWorkerBootstrapPreparation
→ one-shot synchronous bootstrap graph
→ SubmitPreparedWorkerBootstrapInput
→ accepted Worker input sequence > 0
→ current fixed step Worker-owned
→ Worker Published Result
→ Runtime Owner Barrier
→ Dirty Mass Apply
→ Checkpoint / FinishFixedStep
```

普通 Tick 不得重新创建完整 Round DAG。

## 3.3 Ordinary Production tick

必须保持：

```text
no bootstrap rebuild
→ TrySubmitFullWorkerProductionIntent
→ Runtime-owned resource/objective refresh as needed
→ SubmitIntentBatch
→ Worker work propagation
→ Owner Barrier commit
```

---

# 4. Result Apply 原子提交故障门

当前 Runtime 合同继续要求以下失败为零写入：

| 故障 | 期望 |
|---|---|
| Prepare 后 Generation 变化 | reject；Host Apply=0；Proxy commit=0；side effect=0 |
| Publish/Input/Event 水位过期 | reject；零部分提交 |
| Stable Entity View revision 变化 | reject；零部分提交 |
| LifecycleSerial 失效 | reject；零部分提交 |
| Mass Handle / fragment collection 无效 | reject；零部分提交 |
| illegal field owner / duplicate entity-field | Prepare 或 FinalValidate 阶段拒绝 |
| Ordered Event admission 失败 | reject；不推进 event watermark |
| 成功路径 | Host FinalValidate/Apply/side effect 各一次；Proxy commit 一次；Dirty ACK 不重复 |

Target capacity 实现不得恢复 Demo Target/Resource Prepared Transaction。

---

# 5. T1–T8 场景职责

| 场景 | 主要验证内容 | 当前状态 | 下一次重点 |
|---|---|---|---|
| T1 | 参与集切换、压力传播、staging reset、新平衡 | PASS / POST-CUT | 当前 correction-rebase 源码 canonical PASS；保持 bootstrap scratch、Lifecycle、Particle state reset。 |
| T2 | 开放区域群体移动、Macro Guidance、自然落位 | PASS / POST-CUT | 当前 correction-rebase 源码 canonical PASS；保持 Shared Flow → MovementPlanning → Worker Result。 |
| T3 | 双向交换、Local Predictive、公平让行、安全穿越 | PASS / SLICE C GENERIC FLOWBINDING | 20 explicit FlowBindings、2 CohortKeys、2 ObjectiveRefs、2 generic FlowResourceIds；2x canonical acceptance/FlowBinding hashes MATCH，10/10 crossed/completed，deadlock/unreachable/hard/swept/rejection=0。`FormationIndex` 不再连续选择 Flow，authoritative preferred-velocity bypass=0。 | 保持 common Worker MovementPlanning、LocalPredictive、Particle 与 determinism；视觉/性能仍 OPEN。 |
| T4 | 窄通道/出口安全、环境约束 | PASS / POST-CUT | 当前 correction-rebase 源码 canonical PASS；保持 non-particle/obstacle bootstrap + Worker movement。 |
| T5 | Static/Moving Target、Polar Transport、长期稳定 | CLOSED ON MAIN | PR #18 已合并；`main@182f4d8` Static/Moving 均 1199-step 2/2 deterministic PASS。Correctness CLOSED，performance OPEN。 |
| T6 | 异构 Radius/Mobility/Distance Band 联合运行 | T6-A/T6-B/T6-C CORRECTNESS CLOSED | T6-A、T6-B、T6-C correctness 均已进入 main；T6-C 经 PR #23 合并。不得外推为 LateJoin、Dual T8、performance 或 visual acceptance PASS。 |
| T7 | VAT、多视觉状态、HitReact/Knockback/Death | PASS / POST-CUT / CORRECTION RECOVERY VERIFIED | Worker combat result → client presentation；canonical 2/2 + extended 3/3，correction 后最终 converge。 |
| T8 | Combat、Projectile、Impact/Hit、Damage、Event、Golden | SERVER PASS / DUAL OPEN | server-only 当前 PASS；双端 formal runner 仍需关闭。 |

场景不能互相替代。

---

# 6. T5 Target Region 长窗口门

## 6.1 已关闭的历史失败

### Static historical step ~886

Static `fixed_step=1199` 已重复 2/2 PASS，当前不再把旧 static step ~886 当 active blocker。

### Moving step ~398

已确认并修复：

```text
A. round-local Objective effective tick
   vs Worker absolute tick clock-domain mismatch

B. Production fast path 未持续刷新 Runtime-owned dynamic SharedFlow
```

修复后：

```text
absolute_tick == effective_tick
objective age = 0 at publication
Environment revision 1 → dynamic revisions
source_attachment_failures 20 → 0
```

## 6.2 已修复：clipped edge capacity

canonical Moving long-window 的历史 failure：

```text
absolute fixed_step = 1460
epoch = 1190
input = 2470
target_revision = 1
target = (-3171, 1900)
target_velocity = (-90, 0)
feasible_regions = 3 / 16
desired = 19
source_attachment_failures = 0
topology_cells = 432
topology_edges = 202
flow_revision = 27
flow_build_hash = 3365518101
```

解释：

- Objective clock 正常。
- Dynamic SharedFlow 正常刷新。
- SourceAttachment 正常。
- Target 靠近环境边缘后完整 Polar topology 被真实空间裁剪。
- 现有 Demand 仍要求超出当前有效容量的人口，因此 fail-closed。

当前 main 已按 `Reference/TargetRegionBoundaryCapacityContract.md` 实现：

```text
Environment/SharedFlow-clipped feasible Cell
deterministic finite capacity
Desired / Assignable / Overflow
reachability-aware admission
Occupied + ActiveClaims <= Capacity validation
CapacityHold with zero inward Target pressure
moving invalidation/release/refill
```

最终 canonical Moving 2/2 均在 fixed_step=1199 PASS，且语义 hash 完全一致。

## 6.3 新验收合同

### Clipped topology

| 项目 | 要求 |
|---|---|
| Center | 完整/近完整 Polar topology 与旧 baseline 一致 |
| Edge | 只保留真实 NavMesh/Environment-valid Cell；理论 Region 缺失不是 fatal |
| Corner | 可只剩局部/约 1/4 可行域，仍应保持 valid topology |
| Invalid cell | capacity=0，不得产生 claim / guidance admission |

### Finite capacity

| 项目 | 要求 |
|---|---|
| Cell capacity | deterministic finite value；共享不等于无限共享 |
| Total capacity | `Σ Capacity(feasible cell)` |
| Assignable | `min(DesiredPopulation, TotalFeasibleCapacity)` |
| Overflow | `max(0, DesiredPopulation - TotalFeasibleCapacity)` |
| Overbooking | `Occupied + ActiveClaims <= Capacity` 永远成立 |

### Overflow / CapacityHold

合法 capacity shortage：

- 不得计为 Target demand rejection。
- 不得等同于 `UnroutedFailure`。
- 额外 Agent 不得继续对已满 Target interior 施压。
- 新容量出现后按 deterministic order 重新参与。

### Moving cell lifecycle

- valid→invalid：release/migrate claim；无其它容量则 Overflow。
- invalid→valid：增加 capacity；Overflow deterministic refill。
- 不得出现 stale claim / stale plan / overbook / 大规模抢占往返振荡。

## 6.4 Gate 关闭条件

```text
Target boundary/corner unit/automation tests PASS
TargetRegionTransport full suite PASS
RuntimeV2 Target PASS
Target 10k scoped invalidation PASS
Static T5 >=1000 Tick PASS + deterministic repeat
Moving T5 >=1000 Tick PASS + deterministic repeat
Worker Target rejection = 0 for legal capacity saturation
source_attachment_failures = 0 for canonical moving path
Occupied / ActiveClaims never exceed Capacity
legal Overflow != unexplained UnroutedFailure
stale lifecycle = 0
invalid/stale plan/claim = 0
```

不得按 step / AgentId / map / region 写生产特判。

---

# 7. Worker Runtime 规模 baseline

以下基础专项不等于完整 gameplay PASS。

## 7.1 WorkRing

```text
1k
2k
5k
10k
```

目标复杂度仍是固定 Priority×Domain bucket 约束，而不是 Work 数量平方扫描。

## 7.2 TimeWheel

10k future wakeup 必须保持稀疏；无到期 Tick 时不扫描全部 future work。

## 7.3 Spatial

历史专项输入：

```text
10k entities
1% Movement Dirty  → about 100 migrations
10% Movement Dirty → about 1000 migrations
full rebuild        = 0
```

WA9 前要在最终源码状态重跑。

---

# 8. Target 10k scoped invalidation regression invariant

当前记录：

```text
10k agents
= 5k Cohort A + 5k Cohort B
```

只修改 Cohort A 相关 Movement/Target facts 时：

- 只唤醒受影响 Cohort。
- 5k Cohort 以 128 WorkItem shard 执行。
- 未受影响 Cohort 无 Dirty Guidance。
- 未受影响 Cohort 不重建 Topology。

finite capacity / claim 实现不得退化为每次局部变化都重算所有 Cohort。

---

# 9. Particle 验收矩阵

| 能力 | 当前状态 | 证据要求 |
|---|---|---|
| Pair key canonicalization | BASELINE | `(min(A,B), max(A,B))` 唯一关系 |
| Spatial neighbor query | BASELINE | 稳定排序、局部 Cell query |
| 多闭合 Island 构建 | BASELINE | 无跨 Island 漏 Pair |
| Island sub-solve + stable merge | BASELINE | 相同输入结果稳定 |
| Global exact Applied-State validation | BASELINE | 分片结果最终统一安全校验 |
| Validation fail-closed fallback | BASELINE | 不静默接受不安全结果 |
| 多 Island UE Task parallelism | OPEN | 不以 `bUsedIslandSharding` 代替真实 task parallelism |
| 大型单 Island Cell-Pair sharding | OPEN | stable pair owner + per-round barrier |
| 大型单 Island 10k dense perf | NOT RUN | Gate 4 完成后执行 |

注意：Target capacity admission 不能下放给 Particle。Particle 只裁决最终安全。

---

# 10. Networking / Correction / Late Join

公共合同仍要求：

- Packet Generation / Sequence / StableHash。
- Checkpoint / Intent / Correction 分离。
- reliable payload 安全分块。
- duplicate / out-of-order / old generation 拒绝。
- Sparse Correction 不要求 world full rebuild。
- Digest mismatch 可触发 scoped correction/resync。
- Late Join baseline：Checkpoint → Resource Revisions → Event Baseline → Delta。
- baseline 未完成前拒绝普通增量。

Post-cut 必须验证：

```text
Worker owner commit
→ retained proxy state
→ RoundResult / Checkpoint host assembly
→ client apply / late join
```

---

# 11. T8 server-only current evidence

当前 Worker-only Full Production server-only 记录：

```text
batches            = 900
patches            = 90,940
Ordered Events     = 150
attack              = 50
spawn               = 50
impact              = 50
damage              = 50
duplicate fire/hit  = 0 / 0
Golden              = 439379904 / 1411313634 / 6141440
```

性能在 current runs 中仍通过 runner gate；历史 18.579ms fixed-step p95 只保留为比较 baseline，不强制当前每次运行完全相同。

---

# 12. 双端 T8 runner

当前仍属于验收工具债。

要求：

- machine-readable completion contract。
- scenario failure 与 runner/parser failure 分离。
- runner FAIL 时不得人工登记正式 PASS。
- source cut 后正式双端产生 server/client Golden、events、performance evidence。

---

# 13. Diagnostics Recovery Matrix

| 类别 | 当前状态 | 处理原则 |
|---|---|---|
| Worker runtime / queue / domain metrics | PRESENT | Runtime owner 状态。 |
| Dirty Mass / ResultApply metrics | PRESENT | Worker Result Apply path。 |
| Checkpoint final entity state | PRESENT / REVALIDATE | retained Worker proxy/domain state assembly。 |
| Worker Target observability | PASS | `Target` / `TargetCohort` read-only observer；不是 simulation owner。 |
| Target legal Overflow observability | PASS ON MAIN | checkpoint 明确输出 capacity/assignable/overflow/CapacityHold；main landing canonical Moving 的 4 个 Overflow 未计为 UnroutedFailure。 |
| Particle per-step diagnostics | REVALIDATE | Worker output/retained state 或 test-only observer；不能重建旧 second-pass commit。 |
| obsolete legacy transaction telemetry | RETIRED | 不因测试需要重新引入。 |

---

# 14. WA9 最终规模矩阵

| 规模 | Simulation | Network | Presentation | Performance | 状态 |
|---:|---|---|---|---|---|
| 1,000 | 完整 Domain 链 | 双端 | Relevant lifecycle | Target gate | NOT RUN |
| 2,000 | 完整 Domain 链 | 双端 | Relevant lifecycle | Target gate | NOT RUN |
| 5,000 | 完整 Domain 链 | 双端 | Relevant lifecycle | Target gate | NOT RUN |
| 10,000 | 完整 Domain 链 | 双端 | Relevant lifecycle | Final gate | NOT RUN |

完整 Target gate 必须包含边界/角落有限容量和合法 Overflow，不接受靠 ORCA/Particle 被动挤压掩盖 over-admission。

---

# 15. 当前推荐执行顺序

```text
Phase 0  Unified Behavior / Development Rule Cut
Phase 1  Missing Specialist Correctness Gates
Phase 2  Performance / Scaling 1k → 2k → 5k → 10k
Phase 3  Automated Behavior / Visual Acceptance
Phase 4  Human Visual Acceptance
Phase 5  Demo Acceptance CLOSED
```

后续 main regression 若重新出现 Target correctness failure，重新打开 T5 gate；不通过反弹 Target、减少 Agent、关闭安全约束或删除 Demand gate 伪造 PASS。

---

# 16. 测试证据规则

以后写入本文的正式证据必须明确：

1. 测试/场景名。
2. 对应源码架构版本或关键 source cut 是否已包含。
3. 实体规模。
4. Server-only / 双端 / PIE / automation 类型。
5. PASS / STATIC PASS / BASELINE / REVALIDATE / DIAGNOSTIC / FAIL / NOT RUN / OPEN。
6. 关键正确性计数。
7. 关键性能指标。
8. 是否正式 runner 结果。
9. 该证据不能证明什么。

原则：

> **合法容量不足应表现为显式 Overflow/CapacityHold；真正的数据/拓扑/计划错误仍必须 fail-closed。**
