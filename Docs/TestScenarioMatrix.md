# MassAI Crowd 当前测试场景矩阵

## 1. 文档职责

本文记录**当前可用于判断实现状态的测试合同、最后有效 baseline 和下一次必须取得的证据**。

本文件不保存逐日 runner 日志，也不把 source cut 之前的 PASS 自动继承为新架构当前 PASS。

当前状态定义：

```text
PASS          已在当前相关执行结构上获得正式证据
STATIC PASS   只完成静态结构/源码审计；不是 UE runtime PASS
BASELINE      source cut 前的最后有效结果，可用于对比，但不是当前验收
REVALIDATE    以前有 PASS/baseline，但相关 live path 已改变，必须重跑
DIAGNOSTIC    有真实业务结果，但正式 runner/验收形式未通过
FAIL          已知真实 correctness failure 尚未被关闭
NOT RUN       当前结构上尚未执行
OPEN          实现/规模门本身尚未完成
RETIRED       验收对象/机制已从当前架构物理删除，不再作为当前测试门
```

当前最重要的证据边界：

> **最新 Worker Production source cut 已删除 Legacy Round Transaction、旧 Stage surface 和 Prepared second-pass commit channels；UE build、PIE、T1–T8、network/late join/scale 尚未在这条新主链上重新执行。**

因此本文保留旧数值作为 baseline，但不把它们登记成 post-cut PASS。

---

# 2. 当前关键证据摘要

| Gate | 当前状态 | 最后证据 / 当前结论 | 下一次必须证明 |
|---|---|---|---|
| PR12 Source Architecture Cut | STATIC PASS | 静态审计确认旧 transaction / Stage / Prepared second-pass production symbols 为 0；Processor surface 仅 2 个 `UMassProcessor`；Owner Barrier 在 Checkpoint 前。 | UE build + architecture automation。 |
| Persistent Worker Production structure automation | NOT RUN | 新 `CrowdDemo.Architecture.PersistentWorkerProductionStructure` 测试已写入源码。 | 在合并后的 `main` 实际执行并 PASS。 |
| Runtime Worker Result Apply / Owner Barrier | BASELINE | 旧 Runtime atomicity / dirty batch / event watermark 证据存在。 | post-cut 重新运行 Runtime automation。 |
| Legacy Demo Prepared Round Adapter | RETIRED | 旧 Target/Resource prepared transaction tests 随机制删除，不再是当前验收门。 | 由 Runtime Owner Barrier + Worker domain result tests 替代。 |
| Minimal Full Production T8 server-only | REVALIDATE | source cut 前 baseline：900 batches、90,940 patches、150 Ordered Events、attack/spawn/impact/damage=50/50/50/50、duplicate=0/0。 | 新 bootstrap + direct intent path 上重新跑正式 server-only T8。 |
| 双端 T8 formal runner | REVALIDATE / tool issue | source cut 前存在业务日志，但 runner 有 completion 误判/超时。 | 修 runner 后在新主链正式双端执行。 |
| T5 Worker Target observability | PASS ON CANDIDATE | ResultApply `Target` / `TargetCohort` 只读 checkpoint valid；Worker Target rejection 已进入 runner hard-failure gate。 | 合并 main 后重跑，再进入长窗口。 |
| T5 600 Tick | PASS ON CANDIDATE | Static 20-agent：fixed_step=599、generation=1、input=665、publish=600、Worker Target checkpoint valid=1、unrouted=0。 | 合并 main 后重跑。 |
| T5 >900 Tick | FAIL / unresolved baseline | step ~886 曾出现 Target Demand `feasible-region-insufficient`；尚无修复证据。 | Static + Moving >=1000 Tick 无该失败。 |
| TargetRegionTransport automation | PASS ON CANDIDATE | `CrowdDemo.SoftPressure.TargetRegionTransport` PASS 7/7。 | Target 算法修改后再次回归。 |
| RuntimeV2 Target | PASS ON CANDIDATE | Target domain 1/1、10k scoped cohort 1/1、Target observability 2/2。 | 合并 main 后重跑。 |
| Lifecycle | BASELINE | 旧 2/2 结果存在。 | source cut 后回归，特别验证 first bootstrap / ongoing intent 生命周期序列。 |
| WorkRing / TimeWheel / Spatial 10k | BASELINE | 1k/2k/5k/10k scheduler、10k sparse wakeup、10k dirty spatial 专项已有记录。 | WA9 前再次在最终 source state 执行。 |
| Target 10k 双 Cohort scoped invalidation | BASELINE | 受影响 5k Cohort 执行 40×128 Guidance shards；未受影响 Cohort 无 Dirty/Topology rebuild。 | T5/Target 变更后保持同样 scoped invalidation。 |
| Particle 多闭合 Island | BASELINE | independent sub-solve + stable merge + global exact validation 已有专项证据。 | Particle scaling 改动前后都需 reference regression。 |
| Particle 多 Island UE Task 并行 | OPEN | 当前算法分岛不等于每岛独立 UE Task。 | 实现后证明 determinism + speedup。 |
| Particle 大型单 Island | OPEN | Cell-Pair Owner / per-round Barrier 未完成。 | 1k/2k/5k/10k dense single-island correctness/perf。 |
| Networking / Late Join post-cut | REVALIDATE | 公共合同与旧证据存在，但 live host path 已改变。 | checkpoint/correction/late join baseline 在新主链重新跑通。 |
| WA9 完整 10k Production | NOT RUN | 尚无当前完整 gameplay/network/presentation 10k 门。 | 1k→2k→5k→10k 完整验收。 |

---

# 3. Post-cut Regression Gate

在任何旧 Demo 场景恢复为 `PASS` 前，先关闭这组最低门。

## 3.1 Build / structure

| 项目 | 当前状态 | 要求 |
|---|---|---|
| Development Editor build | NOT RUN | 新 `main` 编译成功。 |
| DisableUnity build | NOT RUN | 在当前项目仍需要 `bUseUnity=false` 的前提下验证完整 TU。 |
| PersistentWorkerProductionStructure | NOT RUN | 2 processors、retired symbols=0、Owner Barrier/Checkpoint ordering。 |
| Runtime OwnerCommit atomicity | REVALIDATE | stale token/view/lifecycle/field 必须 first-write 前拒绝。 |

## 3.2 First-step bootstrap

必须看到一条完整事实链：

```text
valid boundary facts
→ BeginWorkerBootstrapPreparation
→ one-shot synchronous bootstrap graph
→ SubmitPreparedWorkerBootstrapInput
→ accepted Worker input sequence > 0
→ current fixed step marked Worker-owned
→ Worker Published Result
→ Runtime Owner Barrier
→ Dirty Mass Apply
→ Checkpoint / FinishFixedStep
```

验收要求：

- 没有 Legacy Round fallback。
- 没有 Prepared Movement / TargetResource / ParticleDiagnostic 第二遍 commit。
- bootstrap result 不直接作为 Mass simulation authority。
- first Worker result 的 applied input sequence 必须与 current step expected sequence 精确匹配。

## 3.3 Ordinary Production tick

第二个及后续普通 Tick 必须证明：

```text
no bootstrap rebuild
→ TrySubmitFullWorkerProductionIntent
→ SubmitIntentBatch
→ Worker work propagation
→ Owner Barrier commit
```

普通 Tick 不得重新创建完整 Round DAG。

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

已经删除的 Demo Target/Resource Prepared Transaction fault fixture 不再属于当前合同；Target/Resource authority 应通过 Worker resource/domain revision 与 Result Apply 基线验证。

---

# 5. T1–T8 场景职责

T1–T8 是 Demo 验收宿主的归因场景，每个场景只证明自己的能力。

| 场景 | 主要验证内容 | Post-cut 状态 | 下一次重点 |
|---|---|---|---|
| T1 | 参与集切换、压力传播、staging reset、新平衡 | REVALIDATE | bootstrap scratch、Lifecycle、Particle state reset。 |
| T2 | 开放区域群体移动、Macro Guidance、自然落位 | REVALIDATE | Shared Flow → MovementPlanning → Worker Result。 |
| T3 | 双向交换、Local Predictive、公平让行、安全穿越 | REVALIDATE | direct intent 后局部预测与 determinism。 |
| T4 | 窄通道/出口安全、环境约束 | REVALIDATE | non-particle/obstacle bootstrap + Worker movement。 |
| T5 | Static/Moving Target、Polar Transport、长期稳定 | FAIL / REVALIDATE | 先重跑短窗口，再关闭 step ~886 long-window blocker。 |
| T6 | 异构 Radius/Mobility/Distance Band 联合运行 | REVALIDATE | cohort/capability、target、particle safety。 |
| T7 | VAT、多视觉状态、HitReact/Knockback/Death | REVALIDATE | Worker combat result → presentation consumption。 |
| T8 | Combat、Projectile、Impact/Hit、Damage、Event、Golden | REVALIDATE | 最小 server-only 优先；随后双端 formal runner。 |

场景不能互相替代：例如 T8 不证明完整 10k，T5 600 Tick 不证明 long-window stability，多 Island 不证明单大 Island scaling。

---

# 6. T5 Target Region 长窗口门

当前已知 correctness blocker 仍按未关闭处理。

最后 baseline：

- 600 Tick 短窗口曾通过。
- >900 Tick 在 step ~886 出现 `feasible-region-insufficient`。
- 10k 双 Cohort scoped invalidation 专项曾通过。

这些结果的正确使用方式是：

```text
600 Tick baseline          → post-cut smoke comparison
step 886 failure           → unresolved correctness target
10k scoped invalidation    → regression invariant
```

正式关闭至少需要：

| 项目 | 要求 |
|---|---|
| Static duration | >= 1000 Tick |
| Moving duration | >= 1000 Tick |
| Demand | 无 unexplained feasible-region-insufficient |
| Plan | 无 stale/invalid plan loop |
| Quota | 无持续未路由人口积累 |
| Placement | 连续窗口无明显 anchor 抢占往返振荡 |
| Lifecycle | stale=0 |
| Determinism | 同输入重复 Stable Hash 一致 |

修复不得按 step / AgentId / map / region 写生产特判。

---

# 7. Worker Runtime 规模 baseline

以下属于 source cut 前已有的基础专项，不等于当前完整 gameplay PASS。

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

WA9 前要在最终源码状态重跑，而不是无限继承 baseline。

---

# 8. Target 10k scoped invalidation baseline

最后记录：

```text
10k agents
= 5k Cohort A + 5k Cohort B
```

只修改 Cohort A 相关 Movement/Target facts 时：

- 只唤醒受影响 Cohort。
- 5k Cohort 以 128 WorkItem shard 执行，记录为 40 shards。
- 未受影响 Cohort 无 Dirty Guidance。
- 未受影响 Cohort 不重建 Topology。

此结果继续作为 Target 修改后的 regression invariant，但不是 post-cut end-to-end PASS。

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

历史上记录过约 `49 KiB → 13 chunks` 的分块回归；当前标记为 baseline。

Post-cut 必须额外验证：

```text
Worker owner commit
→ retained proxy state
→ RoundResult / Checkpoint host assembly
→ client apply / late join
```

不能依赖已经删除的 PostFinalize prepared arrays。

---

# 11. T8 server-only 历史 baseline

source cut 前最后可信的最小 Full Production T8 记录：

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
fixed-step p95      = 18.579 ms
commit p95          = 0.281 ms
realtime            = 0.999
```

当前状态：`REVALIDATE`。

这些数值现在的用途只有两个：

1. 作为新主链重新运行时的回归比较基线。
2. 证明 source cut 前 Worker Combat/Projectile/ResultApply 曾经形成完整最小链。

它们**不是** PR12 后的性能结果。

---

# 12. 双端 T8 runner

历史状态：业务结果 `DIAGNOSTIC`，正式 runner failure。

曾观察到 server/client RoundResult、client visual、Golden/event/perf 日志存在，但 runner 会误报部分 completion marker 缺失并超时。

当前要求：

- 先修 machine-readable completion contract。
- 区分 scenario failure 与 runner/parser failure。
- runner 返回 FAIL 时不得人工登记正式 PASS。
- source cut 后重新产生 server/client Golden、events、performance evidence。

---

# 13. Diagnostics Recovery Matrix

旧 PostFinalize / Particle prepared second-pass 被删除后，以下类别必须重新审计：

| 类别 | 当前状态 | 处理原则 |
|---|---|---|
| Worker runtime / queue / domain metrics | EXPECTED PRESENT | 直接来自 Runtime owner 状态。 |
| Dirty Mass / ResultApply metrics | EXPECTED PRESENT | 来自 Worker Result Apply path。 |
| Checkpoint final entity state | EXPECTED PRESENT / REVALIDATE | 从 retained Worker proxy/domain state assembly。 |
| Particle per-step diagnostics | REVALIDATE | 确认是否从 Worker output/retained state恢复；不能重建旧 second-pass commit。 |
| Target stability diagnostics | REVALIDATE | 可在 checkpoint derive 或独立 test observer；不得成为 simulation owner。 |
| Route / T1 / T3 / T6 special metrics | REVALIDATE | 按场景决定保留、重写 observer 或退休。 |
| obsolete legacy transaction telemetry | RETIRED | 不因测试需要重新引入。 |

---

# 14. WA9 最终规模矩阵

WA9 必须在同一 Worker-only Production architecture 上逐级执行：

| 规模 | Simulation | Network | Presentation | Performance | 状态 |
|---:|---|---|---|---|---|
| 1,000 | 完整 Domain 链 | 双端 | Relevant lifecycle | Target gate | NOT RUN |
| 2,000 | 完整 Domain 链 | 双端 | Relevant lifecycle | Target gate | NOT RUN |
| 5,000 | 完整 Domain 链 | 双端 | Relevant lifecycle | Target gate | NOT RUN |
| 10,000 | 完整 Domain 链 | 双端 | Relevant lifecycle | Final gate | NOT RUN |

完整 Domain 链：

```text
Lifecycle / Input
Behavior
Flow / Resource
Target
Combat / Projectile
Movement Planning
Movement / Local Predictive
Particle / Interaction
Facing / Finalize
Publish / Result Apply
Networking
Presentation
```

性能门使用 `TargetArchitecture.md`：

```text
Worker simulation lag p95 <= 66.667 ms
Client frame p95         <= 33.333 ms
Visual p95               <= 16.667 ms
Realtime                 >= 0.95
Propagation limit hit    = 0
Ordered Event loss       = 0
```

---

# 15. 当前推荐执行顺序

```text
1. UE build
2. PersistentWorkerProductionStructure automation
3. Runtime OwnerBarrier / ResultApply automation
4. first-step bootstrap smoke
5. ordinary direct-intent smoke
6. minimal T8 server-only
7. T5 short window
8. T1/T2/T3/T4/T6/T7 regression
9. network / checkpoint / late join
10. 双端 T8 runner
11. T5 Static/Moving >=1000 Tick
12. Particle scaling
13. WA9 1k→2k→5k→10k
```

如果第 1–7 步失败，先修 post-cut regression，不要立即把问题归因到旧的 T5 step 886 或规模算法。

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

> **架构大切换后，旧 PASS 可以做 baseline，但不能自动继承为当前 PASS。**
