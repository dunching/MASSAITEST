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
| Default Unity / DisableUnity | PASS | Runtime Unity helper collision 已关闭；current main 的 moving orchestration 修改后 clean ForceUnity / DisableUnity 都通过。 | Target capacity 修改后重跑。 |
| Minimal Full Production T8 server-only | PASS | 900 batches、90,940 patches、150 Ordered Events、attack/spawn/impact/damage=50/50/50/50、duplicate=0/0、Golden 一致。 | Target capacity 修改不应影响；后续双端仍需 formal runner。 |
| 双端 T8 formal runner | REVALIDATE / tool issue | 业务历史证据存在，但正式 runner completion 仍有误判/超时债。 | 修 runner 后正式双端执行。 |
| T5 Worker Target observability | PASS | ResultApply `Target` / `TargetCohort` 只读 checkpoint valid；Worker Target/domain rejection 已进入 runner hard-failure gate。 | CapacityHold/Overflow 实现后要区分合法 saturation 与真正 rejection。 |
| T5 Static long-window | PASS | 20-agent `fixed_step=1199` 重复 2/2 PASS；`worker_state_hash=16065067781684863977` 两次一致；unrouted=0、rejection=0。 | Target capacity 修改后至少重跑 1 次并保持 deterministic。 |
| Moving objective absolute clock | PASS | objective effective tick 与 Worker absolute tick 对齐；pre-round uptime 不进入 objective age。 | 边界能力实现后保持。 |
| Runtime-owned dynamic SharedFlow | PASS | moving objective 下 Environment revision 随语义变化推进；旧 bootstrap-only stale resource 问题关闭。 | 边界能力实现后保持。 |
| T5 Moving step ~398 SourceAttachment failure | RETIRED / fixed cause | clock-domain mismatch + stale dynamic SharedFlow 已修；`source_attachment_failures=20/20` signature 不再出现。 | 不回归。 |
| T5 Moving boundary/corner capacity | FAIL / active | canonical Moving 继续运行至 absolute step 1460：`feasible_regions=3/16`、`desired=19`、`source_attachment_failures=0`，现有 Demand 仍把合法 clipped capacity shortage 当 fatal。 | 实现 clipped topology + finite capacity + Overflow/CapacityHold。 |
| TargetRegionTransport automation | PASS | `CrowdDemo.SoftPressure.TargetRegionTransport` PASS 7/7。 | Target capacity 合同修改后扩展边缘/角落测试并全量回归。 |
| RuntimeV2 Target | PASS | Target domain、10k affected cohort、Target observability 当前均 PASS。 | Target capacity 修改后保持 scoped invalidation/determinism。 |
| Lifecycle | BASELINE / REVALIDATE | 旧 2/2 证据存在。 | 完整 post-cut T1/T6 等继续回归。 |
| WorkRing / TimeWheel / Spatial 10k | BASELINE | 1k/2k/5k/10k scheduler、10k sparse wakeup、10k dirty spatial 专项已有记录。 | WA9 前最终源码重跑。 |
| Target 10k 双 Cohort scoped invalidation | PASS / regression invariant | 受影响 Cohort 执行，未受影响 Cohort 无 Dirty/Topology rebuild。 | finite capacity / claim 改动后必须保持。 |
| Particle 多闭合 Island | BASELINE | independent sub-solve + stable merge + global exact validation 已有专项证据。 | Particle scaling 改动前后都需 reference regression。 |
| Particle 多 Island UE Task 并行 | OPEN | 当前算法分岛不等于每岛独立 UE Task。 | 实现后证明 determinism + speedup。 |
| Particle 大型单 Island | OPEN | Cell-Pair Owner / per-round Barrier 未完成。 | 1k/2k/5k/10k dense single-island correctness/perf。 |
| Networking / Late Join post-cut | REVALIDATE | 公共合同存在，但 live host path 改写后正式 evidence 未补齐。 | checkpoint/correction/late join baseline 在新主链重跑。 |
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
```

这些结果证明 Worker-only live path 的基础链可运行，但不自动关闭仍未重跑的 T1/T2/T3/T4/T6/T7、network/late join、双端 T8。

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
| T1 | 参与集切换、压力传播、staging reset、新平衡 | REVALIDATE | bootstrap scratch、Lifecycle、Particle state reset。 |
| T2 | 开放区域群体移动、Macro Guidance、自然落位 | REVALIDATE | Shared Flow → MovementPlanning → Worker Result。 |
| T3 | 双向交换、Local Predictive、公平让行、安全穿越 | REVALIDATE | direct intent 后局部预测与 determinism。 |
| T4 | 窄通道/出口安全、环境约束 | REVALIDATE | non-particle/obstacle bootstrap + Worker movement。 |
| T5 | Static/Moving Target、Polar Transport、长期稳定 | FAIL / active | Static 已 PASS；Moving 当前 blocker 是 clipped boundary capacity / overflow。 |
| T6 | 异构 Radius/Mobility/Distance Band 联合运行 | REVALIDATE | finite capacity 必须 profile-aware；同时回归 particle safety。 |
| T7 | VAT、多视觉状态、HitReact/Knockback/Death | REVALIDATE | Worker combat result → presentation consumption。 |
| T8 | Combat、Projectile、Impact/Hit、Damage、Event、Golden | SERVER PASS / DUAL REVALIDATE | server-only 当前 PASS；双端 formal runner 仍需关闭。 |

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

## 6.2 当前 active failure：clipped edge capacity

canonical Moving long-window 的下一真实 failure：

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

该问题按 `Reference/TargetRegionBoundaryCapacityContract.md` 处理。

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
| Target legal Overflow observability | OPEN | 实现 capacity contract 后必须 machine-readable 区分 Assigned / Overflow / true UnroutedFailure。 |
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
1. 实现 TargetRegionBoundaryCapacityContract
2. Target boundary/corner/capacity automation
3. TargetRegionTransport + RuntimeV2 Target + 10k scoped regression
4. Static T5 >=1000 Tick regression
5. Moving T5 >=1000 Tick + deterministic repeat
6. T1/T2/T3/T4/T6/T7 regression
7. network / checkpoint / late join
8. 双端 T8 runner
9. Duplicate Kernel / Host cleanup
10. Particle scaling
11. WA9 1k→2k→5k→10k
```

如果第 1–5 步失败，不进入 Particle/WA9，也不通过反弹 Target、减少 Agent、关闭安全约束或删除 Demand gate 来伪造 PASS。

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
