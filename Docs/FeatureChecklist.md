# MassAI Crowd 当前功能检查表

## 1. 文档职责

本文只回答：**当前 `main` 已经具备哪些能力，哪些能力仍未关闭。**

实现原理查阅 `CurrentArchitecture.md`，最终要求查阅 `TargetArchitecture.md`，下一步查阅 `PhasePlan.md`，测试证据查阅 `TestScenarioMatrix.md`。

状态定义：

```text
DONE    当前能力/结构已经成立，并有源码或有效测试证据
PARTIAL 主体成立，但仍有明确缺口
OPEN    尚未达到最终完成定义
```

---

# 2. 产品与模块边界

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Demo / Plugin 分离 | DONE | `MassCrowdSimulation` 是可复用产品本体；`MassAICrowdDemo` 是生产架构验证宿主。 |
| Core 隔离 | DONE | `MassCrowdCore` 保持纯数据/确定性 kernel 边界，不拥有 Demo/World/Actor 语义。 |
| Runtime 不反向依赖 Demo | DONE | 通用 Worker Runtime 与 Result Commit Barrier 位于插件侧；Demo 通过 Host adapter 接入。 |
| Standard Sources 模块 | DONE | 通用 Movement/Facing/Constraint Source 独立于 Demo 业务语义。 |
| Networking 独立模块 | DONE | Snapshot/Lifecycle/Intent/Correction/Checkpoint 等公共网络能力不以 Demo 为唯一接口。 |
| Presentation 独立模块 | DONE | StableEntityRef 实例生命周期、ISM/VAT 等表现路径与 Simulation Authority 分离。 |

---

# 3. Persistent Worker Authority

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Persistent Worker Runtime | DONE | 每 World 持续 Worker Runtime 已存在并持有迁移后的模拟状态。 |
| Worker Production Owners | DONE | Lifecycle、Behavior、Flow/Resource、Target、Combat/Projectile、Movement、Particle、Facing 等主要模拟域已有 Worker Owner。 |
| Simulation Mass Processor 收敛 | DONE | 主模拟边界收敛为 Worker Input Sync + Worker Result Apply；客户端可附加 Visual Processor。 |
| 单字段单 Production Writer | DONE / guarded | Worker 字段有 Owner 合同与结构门；旧 Round 事务残留仍需 WA8 物理退出。 |
| Worker Result Apply Proxy | DONE | 支持 Stable Entity View、Dirty Published Batch、ACK 和 retained domain state。 |
| Runtime Owner Commit Barrier | DONE | Token/Final Validate/no-fail commit 已下沉 `MassCrowdRuntime`，旧 Demo Barrier 已删除。 |
| Dirty Mass Apply | DONE | 使用持久 StableEntityRef→Mass Handle 索引，只遍历完整预验证的 Dirty EntityCollection。 |
| 普通帧 Legacy Round Transaction 完全退出 | OPEN | 完整 rollback 旧数据源、`TryPrepareRoundApply` 与 Demo-local Round Transaction 仍是 WA8 关闭项。 |

---

# 4. 多实体调度与复杂度基础

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Entity → Work → Shard → Task | DONE | Worker 以 Work 而非“一实体一线程”调度；同 Domain Work 稳定分片后执行短 UE Tasks。 |
| WorkRing | DONE | Current/Next Epoch、有界容量、稳定 WorkKey 去重、Priority×Domain bucket 与公平游标。 |
| TimeWheel | DONE | 稀疏绝对 Simulation Tick wakeup；未到期 bucket 不做全量扫描。 |
| DependencyIndex | DONE | Entity/Resource/Cohort 依赖可增量唤醒相关 Work。 |
| Deterministic Shard Merge | DONE | 结果按 Domain/ShardOrdinal/稳定 Key 合并，不依赖 Task 完成顺序。 |
| Spatial Index 增量维护 | DONE | Movement Dirty 只更新受影响 Entry；跨 Cell 才迁移，不做普通帧全量 rebuild。 |
| 10k Work/Timer/Spatial 微基准 | DONE | 已有 1k/2k/5k/10k Work、10k sparse wakeup、10k×1%/10% Spatial 回归证据。 |

---

# 5. Agent / Behavior / Capability

| 能力 | 状态 | 当前结论 |
|---|---|---|
| StableEntityRef + LifecycleSerial | DONE | 生命周期复用后的旧事实可被拒绝。 |
| 持续 Spawn/Despawn | DONE | Worker/Runtime 支持持续 lifecycle，而不是只依赖固定 Round 集合。 |
| Capability Profile | DONE | 能力与 Faction/Behavior 分离。 |
| Behavior Source Registry / World Store | DONE | 多 Source 并存、稳定 Handle、Registry/Schema/Hash 合同已存在。 |
| Standard Source Resolver | DONE | 通用 Move/Follow/Pursue/Flee/MaintainDistance/Facing/Constraint 等由插件模块提供。 |
| Demo Business Planner 独立模块 | DONE | 攻击、物流等项目语义从通用 Runtime 分离。 |
| Faction / Capability / Behavior / Cohort 分离 | DONE | 当前设计与公共合同已按不同职责建模。 |

---

# 6. 群体运动链

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Shared Flow | DONE | 承担大尺度共享导航 Guidance。 |
| Target Distance Band | DONE | 目标中心距离带与 Physical Radius/Mobility 解耦。 |
| Target-relative Polar Region Transport | DONE | 目标附近使用 Polar Cell / Demand Region / Transport quota 进行宏观人口分布，不使用永久 Agent Slot。 |
| Target Cohort scoped invalidation | DONE | 10k 双 Cohort 回归证明只重算受影响 Cohort；128 Guidance shard 路径成立。 |
| Local Predictive Interaction | DONE | 位于宏观 Guidance 与 Particle Safety 之间，处理短期轨迹可执行性。 |
| Particle Soft/Hard/Environment Safety | DONE | 负责最终安全修正，不由业务职业或地图特判决定。 |
| 多闭合 Interaction Island 独立 Solve | DONE | 多个互不影响 Island 可独立求解、稳定归并并做全局 exact validation。 |
| 大型单 Interaction Island 内部分片 | OPEN | Cell-Pair Owner / per-round Barrier 并行 Solver 尚未完成。 |
| T5 >900 Tick 长窗口稳定性 | OPEN | step 886 左右曾出现 Target Demand feasible-region-insufficient；600 Tick PASS 不能覆盖。 |

---

# 7. Combat / Projectile

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Combat Worker Owner | DONE | 攻击/Reactive 等生产状态进入 Worker 权威链。 |
| Mass Projectile entity | DONE | Projectile 使用实体化状态与公共空间/命中接口，不依赖逐 Projectile Actor。 |
| Broadphase + swept hit | DONE | 量化空间候选与相对/环境 Sweep 已进入公共 Projectile 路径。 |
| ImpactFact / HitFact | DONE | 插件负责稳定命中事实，宿主负责 Damage/业务解释。 |
| Projectile Worker Patch 原子提交 | DONE | Projectile state/summary/visual lifecycle/hit-response 由 Worker Result Apply 提交。 |
| T8 Combat/Projectile Golden | DONE | 当前 server-only 正式门有完整 900 batch / 150 event / 50 次攻击链证据。 |

---

# 8. Networking

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Versioned packet / chunk | DONE | Checkpoint/Intent/Correction 使用 Generation/Sequence/Hash；可靠块保持安全大小。 |
| 4 KiB safe reliable chunk | DONE | 49 KiB 载荷拆分回归已验证。 |
| Sparse Correction | DONE | CorrectionRevision/Scope correction 不要求普通帧完整世界状态。 |
| Digest | DONE | 使用可覆盖的 Unreliable 一致性探测语义。 |
| Late Join | DONE | Checkpoint → Resource Revisions → Event Baseline → 后续增量的长期合同已建立。 |
| 普通完整 Round Correction | DONE / retired | 正常路径已退出，低频 Checkpoint 保留。 |
| 双端 T8 正式 runner | PARTIAL | server/client 结果与 visual 日志存在，但正式脚本有误判/超时；当前不能登记正式 PASS。 |

---

# 9. Presentation

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Stable slot table | DONE | StableEntityRef ↔ instance slot、swap-remove reverse table 与 tombstone 机制已存在。 |
| Spawn/Update/Despawn 幂等 | DONE | 表现生命周期与服务端实体生命周期分离。 |
| VAT / Hit response | DONE | Demo 已有 Idle/Move/Attack/HitReact/Death 等表现验证路径。 |
| Presentation 非 Authority | DONE | 表现结果不能反向决定 Worker Simulation State。 |
| 10k 完整客户端表现验收 | OPEN | 尚未完成 WA9 级完整规模视觉门。 |

---

# 10. Atomic Result Apply

| 能力 | 状态 | 当前结论 |
|---|---|---|
| Prepare / Commit 分离 | DONE | Result Apply 先构造不可变候选，再进入唯一提交。 |
| Runtime Commit Token | DONE | Generation、Publish/Input/Event 水位与 Stable View revision 在提交前冻结/验证。 |
| Host FinalValidate | DONE | Mass Handle/Lifecycle、Target/Resource Owner/Revision、Behavior/Event 等宿主事实在首次写入前验证。 |
| No-fail commit 区 | DONE | 第一次写入后只执行已验证操作；不以“部分写入后补偿 rollback”冒充原子性。 |
| Dirty ACK 顺序 | DONE | ACK 在成功提交之后发生，重复 ACK 拒绝。 |
| Demo-local Round rollback 数据源退出 | OPEN | WA8 当前主要结构债。 |

---

# 11. 当前规模结论

当前可以明确说：

```text
10k-aware architecture        = YES
10k scheduler/spatial tests   = YES
10k target scoped test        = YES
full 10k gameplay acceptance  = NO
```

已有规模证据不能外推为完整 10k Production Ready。

完整 WA9 必须同时覆盖 Behavior、Target、Movement、Particle、Combat、Projectile、Networking 与 Presentation。

---

# 12. 当前 OPEN 项

当前只维护以下主要 OPEN 项，不再把历史切片重复列入检查表：

1. **WA8 Legacy Removal**
   - 普通帧完整 rollback 旧数据源
   - `TryPrepareRoundApply`
   - `FCrowdDemoRoundWorkBatch`
   - `BeginBoundaryTransaction`
   - Demo-local Round Transaction / 失去消费者的 Stage

2. **T5 Long-Window Correctness**
   - step 886 feasible-region-insufficient
   - Static/Moving 1000+ Tick 连续稳定门

3. **Large Particle Island Scaling**
   - Cell-Pair Owner
   - per-round Barrier merge
   - 高密度 1k/2k/5k/10k 单 Island

4. **WA9 Full-Scale Acceptance**
   - 完整 1k→2k→5k→10k Production 场景
   - 双端网络与表现
   - 最终性能门

5. **Test Harness Reliability**
   - 修复双端 T8 正式 runner 的完成日志误判/超时

历史完成项与失败过程不再追加到本文；需要追溯时使用 Git 历史与 `Docs/History/`。
