# MassAI Crowd 当前阶段计划

## 1. 文档职责

本文只描述**从当前 `main` 收敛到 `TargetArchitecture.md` 的下一步实施顺序**。

本文不记录已经完成阶段的逐日流水账；已完成能力查阅 `FeatureChecklist.md`，有效测试证据查阅 `TestScenarioMatrix.md`，历史过程通过 `Docs/History/` 与 Git 历史追溯。

当前总方向不再改变：

```text
Current Architecture
        ↓
WA8 Legacy Removal
        ↓
T5 Long-Window Correctness
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
| 1 | WA8 Legacy Removal | OPEN | Demo-local 普通帧完整 rollback/旧数据源、`TryPrepareRoundApply`、`FCrowdDemoRoundWorkBatch`、`BeginBoundaryTransaction` 与 Demo-local Round Transaction 从生产路径物理退出。 |
| 2 | T5 Long-Window Correctness | OPEN | 关闭 step 886 左右的 Target Demand feasible-region-insufficient；T5 1000+ 连续窗口通过，不能用 600 Tick 结果替代。 |
| 3 | Large Particle Island Scaling | OPEN | 单个大型 Interaction Island 获得 Cell-Pair Owner / 逐轮 Barrier 分片；高密度规模测试证明正确性、确定性与性能。 |
| 4 | WA9 Full-Scale Acceptance | OPEN | 同一 Production Runtime 在 1k→2k→5k→10k 完整链路上通过模拟、网络、表现和性能门。 |

双端 T8 正式 runner 的日志误判/超时也必须修复，但它属于**验收工具可靠性前置项**，不得被当成新的模拟架构阶段。

---

# 3. Gate 1 — WA8 Legacy Removal

## 3.1 已经成立的基础

以下不再重复实现：

- Persistent Worker 已成为 Production Simulation Authority。
- Simulation Mass Processor 已收敛到 Worker Input Sync + Worker Result Apply 主边界。
- Runtime 通用 Worker Result Commit Token / Owner Commit Barrier 已位于 `MassCrowdRuntime`。
- Demo 只通过 Host-specific Prepared Plan 接入最终验证与 no-fail apply。
- Worker Dirty Mass Apply 已使用 StableEntityRef→Mass Handle 持久索引和 Dirty EntityCollection。
- Movement、Behavior、Target、Particle、Combat、Projectile、Lifecycle 等生产域已有 Worker Owner。

## 3.2 已完成的 WA8 子切片

本轮已完成：

```text
Primary SharedFlow Runtime Resource ownership
RoundSimPipeline → UMassCrowdRuntimeSubsystem

WorkerInputSync → RuntimeSubsystem SharedFlow resource
WorkerInputSync → RoundSimPipeline direct dependency = 0

Target Prepared ResourceId
pointer-address → CrowdWorkerResourceIds::Environment
```

SharedFlow 算法、Round Stage 顺序和 Round Transaction 本切片未改变。

## 3.3 当前必须删除的 Legacy

继续按以下顺序实施，不建立第三套 retained cache、兼容 wrapper 或 fallback：

1. **替换普通帧完整 rollback 数据源**
   - 只保留真正需要 replay/checkpoint/diagnostic 的 retained/delta 历史。
   - Formation、Flow、Facing、Business 等需要保留的历史事实必须来自明确版本化状态，不再由完整 Boundary Snapshot 每 Tick 展开。
   - 删除失去消费者的完整 rollback CPU 数组。

2. **删除 Demo-local Round Prepare / Transaction**
   - `TryPrepareRoundApply`
   - `FCrowdDemoRoundWorkBatch`
   - `BeginBoundaryTransaction`
   - Demo-local Round Transaction
   - 与上述结构绑定的普通生产 Stage / fallback / 测试断言

3. **结构审计**
   - 正常 Production 路径不再依赖完整 Round DAG。
   - 同一模拟字段只有一个 Production Writer。
   - Runtime 不反向依赖 Demo。
   - 不保留旧文件名 shim、typedef、wrapper 或 alias。

## 3.4 WA8 验证门

WA8 删除切片完成后至少执行：

```text
Development Editor DisableUnity
Runtime WorkerResultApply / Owner Barrier
CrowdDemo Architecture / Legacy zero-symbol gates
Target / Resource revision fault gates
T8 server-only Golden
T5 >= 1000 Tick long-window attempt
```

WA8 只有在旧生产结构**物理退出**后才能标记完成；“旧路径不再常用”不算关闭。

---

# 4. Gate 2 — T5 Long-Window Correctness

当前已知问题：历史长窗口在 step 886 左右出现 Target Demand `feasible-region-insufficient`。600 Tick 成功不能覆盖这个问题。

处理原则：

- 先建立稳定可复现的 1000+ Tick T5 runner。
- 同时记录 Static / Moving Target-relative 状态、Feasible Graph Hash、Demand、Plan、Quota、Unrouted 数量和 Cohort revision。
- 定位是需求生成、可行图变化、旧 Plan reuse、quota execution 还是终端供需失配。
- 修复必须作用于通用 Target Region Transport 合同，不允许按 step、AgentId、地图或特定 Region 加生产特判。
- 修复后必须重新跑短窗口，防止为了 step 886 修复破坏已有 20 实体与 10k scoped cohort 行为。

关闭门：

```text
T5 Static 1000+ Tick PASS
T5 Moving 1000+ Tick PASS
TargetRegionTransport automation PASS
RuntimeV2 Target automation PASS
zero stale lifecycle / invalid plan / unexplained unrouted
连续稳定窗口无高频落位往返振荡
```

---

# 5. Gate 3 — Large Particle Island Scaling

当前 Particle 已能：

- 构建 Interaction closure。
- 将多个互不影响的闭合 Island 独立 Solve。
- 稳定归并 Agent / Pair 结果。
- 使用全局 exact Applied-State validation 守门。
- 验证失败时 fail-closed fallback。

当前未关闭的是：**一个大型高密度 Island 内部怎么并行。**

下一实现目标：

```text
Large Island
    ↓
Stable Spatial Cells
    ↓
Cell-Pair Ownership
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

- Pair owner 必须由稳定 Key 决定，不能由线程完成顺序决定。
- 跨 Cell Pair 不能重复处理或漏处理。
- 每轮 Barrier 后再进入下一轮传播，禁止 shard 读取其他 shard 未提交的可变状态。
- 大 Island 与 monolithic reference 在允许误差/量化合同内一致。
- 不为了性能取消 Hard/Swept/Environment Safety。

关闭门至少包含 1k/2k/5k/10k 高密度单 Island 微基准和确定性重复运行。

---

# 6. Gate 4 — WA9 Full-Scale Acceptance

WA9 不再是单个 WorkRing 或 Spatial 微基准，而是**完整 Production Agent Runtime 验收**。

规模阶梯固定为：

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

最终性能目标以 `TargetArchitecture.md` 为准：

```text
Worker simulation lag p95 <= 66.667 ms
Client frame p95         <= 33.333 ms
Visual p95               <= 16.667 ms
Realtime                 >= 0.95
Propagation limit hit    = 0
Ordered Event loss       = 0
```

GT Result Apply 必须维持有界 Dirty Apply，不允许为了 10k 性能重新恢复完整 Mass traversal。

---

# 7. 验收工具前置项

当前双端 T8 存在“日志已经生成，但正式脚本误判缺失并超时”的 runner 问题。

在完整 WA9 前修复：

- 完成条件只读取明确稳定的 machine-readable 结果或唯一终局标记。
- 区分“场景业务失败”和“runner 自身解析失败”。
- 已有 server/client RoundResult、client visual、Golden/event/perf 日志不能被脚本误报为不存在。
- runner FAIL 时不得根据内部日志手工改报正式 PASS；只能单独记录诊断证据。

---

# 8. 暂不做的事情

在上述四个 Gate 关闭前，不优先开展：

- 新的 Demo-specific Movement Algorithm。
- 新 Slot / Region Owner 系统。
- 新的 GT Simulation Authority。
- 为旧 Round API 建兼容层。
- 只为了跑过场景降低实体数量或关闭安全阶段。
- 没有 profile 证据前重写 Target min-cost flow 算法。

`FCrowdTargetRegionTransportKernel::SolveTransport()` 的逐单位增广可能成为大规模性能热点，但必须先在 WA9/大 deficit profile 中证明，再决定是否改算法。

---

# 9. 完成定义

当本计划关闭时，代码应满足：

```text
Persistent Worker = unique simulation authority
Mass              = entity/proxy/integration layer
Network           = versioned simulation-fact transport
Presentation      = visual proxy
Demo              = production verification host
Legacy Round DAG  = not production dependency
```

随后 `PhasePlan.md` 应重新生成下一阶段计划，而不是继续追加日期流水账。
