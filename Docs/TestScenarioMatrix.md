# MassAI Crowd 当前测试场景矩阵

## 1. 文档职责

本文只记录**当前仍有效、可以用于判断实现状态的测试合同与证据**。

本文不保存逐日 runner 日志，不把历史失败/修复过程堆叠成时间线。历史运行可从 Git 历史、Saved 产物和 `Docs/History/` 追溯。

状态定义：

```text
PASS       已有当前有效正式证据
DIAGNOSTIC 有真实结果，但验收工具/形式门未正式通过
FAIL       当前仍可复现的真实失败
NOT RUN    当前阶段尚未执行
OPEN       设计/规模门尚未具备完整完成证据
```

---

# 2. 当前关键证据摘要

| Gate | 状态 | 当前有效证据 |
|---|---|---|
| Runtime Worker Result Apply | PASS | 4/4：LifecycleOwnerAndEvents、RuntimeVariablePublishedBatch、OwnerCommitBarrierAtomicity、OwnerCommitModuleStructure。 |
| Demo Prepared Round Adapter | PASS | 1/1；Target/Resource stale revision、invalid Owner、非法引用、重复 Slot/entity/field 与成功单次 Apply。 |
| Architecture / Legacy Barrier | PASS | 旧 Demo Barrier 文件、类型、include、注册和测试消费者为 0；Runtime 不依赖 Demo/Scenario/Demo Round Plan。 |
| TargetRegionTransport automation | PASS | 7/7。 |
| RuntimeV2 Target | PASS | 1/1。 |
| Lifecycle | PASS | 2/2。 |
| 最小全 Production T8 server-only | PASS | 900 batches、90940 patches、150 Ordered Events、attack/spawn/impact/damage=`50/50/50/50`、duplicate=`0/0`、Golden=`439379904/1411313634/6141440`、fixed-step p95=`18.579ms`、commit p95=`0.281ms`、realtime=`0.999`。 |
| 双端 T8 正式 runner | DIAGNOSTIC | server/client RoundResult、client visual、Golden/event/perf 日志存在；正式脚本仍误判完成日志缺失并超时，不能登记正式 PASS。 |
| T5 600 Tick | PASS | 现有短窗口 Production 结果可通过。 |
| T5 >900 Tick | FAIL | step 886 左右存在 Target Demand `feasible-region-insufficient` 记录；短窗口不能覆盖。 |
| WorkRing / TimeWheel / Spatial 10k | PASS | Work 1k/2k/5k/10k；10k sparse wakeup；10k×1%/10% Movement Dirty，Spatial full rebuild=`0`。 |
| Target 10k 双 Cohort scoped invalidation | PASS | 单 Cohort 更新只执行受影响 5k Cohort 的 40×128 Guidance shards；另一 Cohort 无 Dirty/Topology rebuild。 |
| Particle 多闭合 Island | PASS | 多 Island 独立 Solve + 稳定 Merge + 全局 exact Applied-State validation。 |
| Particle 大型单 Island | OPEN | Cell-Pair Owner / per-round Barrier 分片未关闭。 |
| WA9 完整 10k Production | NOT RUN | 尚未完成 Behavior→Target→Movement→Particle→Combat→Projectile→Network→Presentation 的完整 10k 门。 |

---

# 3. 原子提交故障门

Runtime / Demo Result Apply 必须持续覆盖以下失败零写入合同：

| 故障 | 期望 |
|---|---|
| Prepare 后 Generation 变化 | 拒绝，Host Apply=0，Proxy commit=0，side effect=0 |
| Publish/Input/Event 水位过期 | 拒绝，零部分提交 |
| Stable Entity View revision 变化 | 拒绝，零部分提交 |
| LifecycleSerial 失效 | 拒绝，零部分提交 |
| Mass Handle / Fragment collection 无效 | 拒绝，零部分提交 |
| Target/Resource Owner 或 Revision stale | 拒绝，零部分提交 |
| 重复 entity-field / slot | 拒绝，零部分提交 |
| Ordered Event admission 失败 | 拒绝，零部分提交 |
| 成功路径 | Host FinalValidate/Apply/side effect 各一次；Proxy commit 一次；Event/Dirty ACK 不重复 |

这些门属于 Runtime 原子性基础，后续 WA8 删除旧 Transaction 时必须继续通过。

---

# 4. T1–T8 场景职责

T1–T8 是 Demo 验收宿主的**归因场景**，每个场景只证明自己的能力，不能互相替代。

| 场景 | 主要验证内容 | 不应被误解为 |
|---|---|---|
| T1 | 参与集切换、压力传播、staging reset、新平衡 | 持续 Spawn/Despawn 的唯一验收 |
| T2 | 开放区域群体移动、宏观 Guidance、最终自然落位 | 窄口通行 |
| T3 | 双向交换、局部预测、公平让行、安全穿越 | Target Region 站位 |
| T4 | 窄通道/出口安全、环境约束 | 目标围攻 |
| T5 | 静态/移动 Target、Distance Band、Polar Region Transport、长窗口稳定 | 单帧 inside-band 即完成 |
| T6 | 异构半径/Mobility/距离带在同一路径联合运行 | 给不同职业写不同安全算法 |
| T7 | VAT、多业务视觉状态、HitReact/Knockback/KnockUp/Death | Projectile 几何/命中框架全部验收 |
| T8 | Combat、Projectile、Impact/Hit、Damage、Event、Golden | 完整 10k 产品性能门 |

场景最终 package、参数和自动化命令应由脚本/配置作为机器可读来源；本文只维护测试语义与当前有效结果。

---

# 5. T5 Target Region 长窗口门

T5 当前是明确的 Correctness Blocker。

短窗口已证明：

- Target-relative Polar topology / demand / plan / guidance 基础链可运行。
- Static/Moving Target 能进入现有验收窗口。
- Target Cohort scoped invalidation 在 10k 双 Cohort 专项成立。

但以下结论尚未成立：

```text
Target Region Transport long-window stable = NO
```

正式关闭 T5 长窗口至少需要：

| 项目 | 要求 |
|---|---|
| Static duration | >= 1000 Tick |
| Moving duration | >= 1000 Tick |
| Demand | 无 unexplained feasible-region-insufficient |
| Plan | 无 stale/invalid plan 循环 |
| Quota | 无持续未路由人口积累 |
| Stable placement | 连续窗口无明显“靠近→推开→重新抢 anchor”振荡 |
| Lifecycle | stale=0 |
| Determinism | 同输入重复运行 Stable Hash 一致 |

修复不得通过 step/AgentId/地图特判完成。

---

# 6. Worker Runtime 复杂度门

当前已经通过的规模基础：

## 6.1 WorkRing

```text
1k
2k
5k
10k
```

完整 drain 的 bucket probe 应受固定 `Priority × Domain` bucket 数约束，不随 Work 数量平方增长。

## 6.2 TimeWheel

10k 未来 Wakeup 保持稀疏；在没有到期 Tick 时不得扫描全部未来 bucket。

## 6.3 Spatial

10k 实体下：

```text
1% Movement Dirty  → 100 Cell migration（若测试输入保证跨 Cell）
10% Movement Dirty → 1000 Cell migration
full rebuild        = 0
```

这些微基准证明调度/索引复杂度基础，不证明完整 10k Gameplay PASS。

---

# 7. Target 10k scoped invalidation 门

有效专项结论：

```text
10k agents
= 5k Cohort A + 5k Cohort B
```

只修改 Cohort A 的相关 Movement/Target 事实时：

- 只唤醒受影响 Cohort。
- 5k Cohort 以 128-item Guidance shard 执行，当前记录为 40 shards。
- 未受影响 Cohort 不产生 Dirty Guidance。
- 未受影响 Cohort 不重建 Topology。

该门证明 Target 工作可以按 Cohort 增量失效；不替代 T5 1000+ 长窗口正确性，也不替代完整 WA9。

---

# 8. Particle 验收矩阵

| 能力 | 当前状态 | 证据要求 |
|---|---|---|
| Pair key canonicalization | PASS | `(min(A,B), max(A,B))` 唯一关系 |
| Spatial neighbor query | PASS | 稳定排序、局部 Cell 查询 |
| 多闭合 Island 构建 | PASS | 无跨 Island 漏 Pair |
| 多 Island 独立 Solve | PASS | 结果稳定归并 |
| Global exact Applied-State validation | PASS | 分片结果最终统一安全校验 |
| Validation fail-closed fallback | PASS | 不静默接受不安全结果 |
| 大型单 Island 内部 Cell-Pair sharding | OPEN | 稳定 Pair owner + 每轮 Barrier |
| 大型单 Island 10k 密度性能 | NOT RUN | Gate 3 完成后执行 |

大型单 Island 的完成不能通过“多个 Island 很快”来替代。

---

# 9. Networking / Correction / Late Join 门

当前公共合同至少持续验证：

- Packet Generation / Sequence / StableHash。
- Checkpoint / Intent / Correction 区分。
- 可靠 payload 单 chunk 不超过 4 KiB 安全大小。
- 重复/乱序/旧 Generation 拒绝。
- Sparse Correction 不要求 World 重建。
- Digest mismatch 可触发 scope correction/resync。
- Late Join baseline 顺序：Checkpoint → Resource Revisions → Event Baseline → Delta。
- baseline 未完成前拒绝普通增量。

已记录 49 KiB payload → 13 chunks 的可靠分块回归。

---

# 10. T8 当前正式基线

当前最可信的最小全 Production T8 是 server-only 正式门：

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

这证明当前 Worker Combat/Projectile/Result Apply 最小链成立。

它**不证明**：

- 双端正式 runner 已通过。
- 大规模 Combat 已通过。
- 完整 WA9 已通过。

---

# 11. 双端 T8 runner 状态

当前状态：`DIAGNOSTIC`。

已观察到：

- server RoundResult 存在。
- client RoundResult 存在。
- client visual 完成日志存在。
- 服务端 Golden / Event / Performance 数据存在。

但正式 `RunCrowdDemo` runner 仍会错误报告部分完成日志缺失并超时。

规则：

> 正式 runner 返回 FAIL 时，不得因为人工看到内部日志就把场景登记为 PASS。

应单独标记“业务证据存在 / runner failure”，修复脚本后重新执行正式门。

---

# 12. WA9 最终规模矩阵

WA9 必须在同一 Production 架构上逐级执行：

| 规模 | Simulation | Network | Presentation | Performance | 状态 |
|---:|---|---|---|---|---|
| 1,000 | 完整 Domain 链 | 双端 | 全 Relevant 表现 | Target gate | NOT RUN |
| 2,000 | 完整 Domain 链 | 双端 | 全 Relevant 表现 | Target gate | NOT RUN |
| 5,000 | 完整 Domain 链 | 双端 | 全 Relevant 表现 | Target gate | NOT RUN |
| 10,000 | 完整 Domain 链 | 双端 | 全 Relevant 表现 | Final gate | NOT RUN |

完整 Domain 链至少包括：

```text
Lifecycle
Behavior
Flow / Resource
Target
Combat / Projectile
Movement
Local Predictive
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

# 13. 测试证据规则

以后只向本文写入**当前有效最终结果**，不再追加“某日某切片下一步是什么”。

新增证据必须明确：

1. 测试名称/场景。
2. 实体规模。
3. Server-only / 双端 / PIE 类型。
4. PASS / FAIL / DIAGNOSTIC / NOT RUN。
5. 关键正确性计数。
6. 关键性能指标。
7. 是否正式 runner 结果。
8. 该结果不能证明什么。

历史端口号、PID、逐次调试失败和被后续修复覆盖的指标不再进入核心矩阵。
