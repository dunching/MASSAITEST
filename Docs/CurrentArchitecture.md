# MassAI Crowd Demo 当前架构

## 2026-08-15 Runtime Owner Commit Barrier 已纠偏

[COMPUTED][HIGH] `MassCrowdRuntime` 现在拥有通用 `FCrowdWorkerResultCommitToken`、`ECrowdWorkerResultOwnerCommitResult`、`FCrowdWorkerResultOwnerCommitBarrier` 和既有 `FCrowdWorkerResultApplyProxy`。通用 Token 覆盖 Generation、PublishSequence、LastAppliedInputSequence、BaseConsumedPublishSequence、BaseAppliedEventSequence 与 BaseStableEntityViewRevision；Host revision/lifecycle 由 adapter 负责。

[COMPUTED][HIGH] `MassAICrowdDemo` 只保留 `FCrowdDemoPreparedRoundCommitPlan` 与 Host callbacks。Plan 保存 Prepared Proxy、Runtime Token、一次构建的 Demo Mass/Target/Resource Plan，以及 PlanRevision/FixedStepIndex；Runtime 代码未引用 Demo、Scenario 或 Demo Prepared Round Plan。Demo 旧 Barrier 文件、类型和所有消费者已删除，无兼容入口。

[COMPUTED][HIGH] 实际写前顺序为 Runtime Token match → Proxy Final Validate → Result side-effect 副本 Prepare → Target/Resource Token/Owner/Revision Validate → Facing/Behavior/Event 与 Mass Handle/Lifecycle/Fragment/Query Validate。实际提交顺序为 Demo Mass apply → Proxy commit → Mass/Projectile/Target/Resource/Facing/Behavior/Event 状态与表现/网络发布 → 后续 Dirty ACK。第一次写入后没有正常可失败返回；`checkf` 只守卫已预验证的 no-fail 不变量。

[COMPUTED][HIGH] 源码结构门确认 `CommitPreparedValidated` 不再重复 Validate/Commit，故 Proxy Final Validate 与 Proxy commit 各一次。WA8 没有关闭：完整 rollback 数组、`TryPrepareRoundApply` 和 Demo-local Round Transaction 仍保留；下一切片先删除完整 rollback 旧数据源，再删除 Round Transaction。

## 2026-08-04 Result Apply 单一 Owner Barrier

[COMPUTED][HIGH] Result Apply 的 Prepare 边界一次性解析 StableEntityRef→Mass Handle，验证 LifecycleSerial、字段 Owner、Fragment/Query collection、重复实体/字段及 Publish/Event/Stable View 水位，并把 Prepared Proxy、Prepared Mass Plan、Commit Token 存入单槽 Pending Finalize。普通最终提交不再读取或重建 Published Dirty Batch。

[COMPUTED][HIGH] Owner Barrier 在任何写入前完成 token、Generation、水位、Stable View、Lifecycle、Mass Handle、Fragment、plan/tick revision，以及 Ordered Event admission/Behavior parity 的副本预演。提交顺序为 Mass Dirty Apply → Proxy Commit → Mass/Facing 资源与表现副作用 → 安装已验证 Behavior/Event 状态；Dirty Batch ACK 只在后续成功 Input Sync 执行。Proxy 不再拥有可观察的提前提交窗口，Barrier 后也不再调用可失败的 Result finalize。

[COMPUTED][HIGH] Legacy `TryPrepareRoundApply` 仍存在，但对本 Tick 待提交 Worker 状态只使用只读 Pending overlay，避免要求“Proxy 已提交”后才能完成 prepare。该 overlay 不复制完整世界，也不替换 Checkpoint 或重启 Runtime。

[INFERRED][HIGH] WA8 仍为 OPEN：`PreparedTargetResourceSlots`、完整 rollback 数组和 Demo-local Round Transaction 尚未迁移/删除；Projectile Boundary 与纯计算 Kernel 仍是保留能力，不属于本切片删除范围。

## 2026-08-03 WA8-R retained Worker checkpoint / rollback

[COMPUTED][HIGH] Result Apply Proxy 维护按 StableRef 排序的实体视图和各 Domain 最新已提交状态。Round Checkpoint 仅在发布门开启时从该视图解码 Movement/Combat；PostFinalize rollback 每 Tick 也从同一视图解码 Movement/Combat。两者都校验 Proxy Generation、Publish/Input 水位、Lifecycle 和 Combat capability，不再消费 Prepared Movement/Combat commit。

[COMPUTED][HIGH] 结构门锁定 Checkpoint 与 PostFinalize 的 `Proxy.GetStableEntityView()`/`Proxy.FindDomain()`，并拒绝两段中的 `GetPreparedMovementBoundaryCommit`/`GetPreparedCombatBoundaryCommit`。9840 T5/600 与 9841 T8/900 保持 Dirty Apply、Checkpoint、Ordered Event、Projectile Golden 全部成立。

[INFERRED][HIGH] 这不是普通 Tick 零全量状态：PostFinalize 仍遍历完整 Boundary Snapshot，并构造 Flow metrics、SoftPressure rollback、Combat rollback 与 Particle applied-state 数组；Formation/SharedFlow/Facing/Business 仍来自旧 Round Prepared facts。WA8 的下一个结构目标是迁移这些必要 side effect/历史记录后删除 Round Transaction，而不是新增另一份 retained cache。

## 2026-08-03 WA8-R Worker Projectile 原子提交

[COMPUTED][HIGH] Projectile 不再是旧 Combat Commit 的 Mass/指标副作用。Result Apply 从 Worker Published Projectile Patch 解码完整 `FCrowdWorkerProjectileState`，在 Agent Dirty Mass 写入前完成 Anchor Lifecycle、FixedStep、capacity、Projectile state set 与 HostCombatResult 校验；随后在同一提交函数内应用 Projectile Mass state，并发布 summary、visual lifecycle 与 hit-response 指标。

[COMPUTED][HIGH] 旧 FacingFinalize 对 Projectile 的 capacity、状态应用和三类记录调用均已物理删除。9837 T8/900 保持 150 Ordered Events、50 次完整攻击链与 Golden Hash=`439379904/1411313634/6141440`；DisableUnity、Architecture 2/2 与 Lifecycle/Events 1/1 通过。

[INFERRED][HIGH] 仍未迁移的是全实体 rollback/checkpoint 与 Target resource side effect。PostFinalize 当前仍按 Boundary Snapshot、Prepared Movement/Combat、SharedFlow、Formation/Business facts 构造完整 rollback 数组；这也是删除 `TryPrepareRoundApply` 前的下一主要阻塞。

## 2026-08-03 WA8-R Worker Patch 直写 / 唯一 Mass Writer

[COMPUTED][HIGH] 当前服务端 Result Apply 从 Worker Published Batch 的 Facing/Combat Dirty Patch 直接构造 Movement/Facing/Combat/Visual Mass Apply Record。Enrich 仅查询命中 Stable Entity 的当前碎片作为身份、Combat baseline 与 Combat-only 视觉解析基线，不读取 Prepared Movement、完整 SharedFlow、Boundary Snapshot 或 Boundary Business Facts。

[COMPUTED][HIGH] 新 Writer 只遍历已完整预验证的 Dirty Entity collection，并写 Movement/Facing、Transform/Velocity、Combat/Visual；Flow Sample 暂不由不含 Flow 字段的 Worker Patch 伪造。旧 FacingFinalize 中的备用 Mass traversal 已物理删除，现只保留资源、Projectile、rollback/表现记录与旧事务完成副作用。

[COMPUTED][HIGH] DisableUnity、Architecture 2/2、Lifecycle/Ordered Event 定向门通过；9835 静态 T5 的第 600 批保持 20 Dirty 实体、Objective=`1/600`、Intent resources=`0`、Result batches=`600` 且零硬错误。

[INFERRED][HIGH] 当前仍是迁移态：Worker Dirty Apply 的 Mass 数据源已独立，但提交时序仍位于 `TryPrepareRoundApply` 之后，旧 Round WorkBatch/Stage 链仍是非 Mass 副作用生产者。WA8 关闭条件是副作用进入 Published Batch 的有序合同并删除该事务链，而不是仅有唯一 Mass Writer。

## 2026-08-03 WA8-R Dirty Plan 写入边界

[COMPUTED][HIGH] 服务端 Mass 热字段的当前生产写入者是 `ApplyPreparedWorkerMassDirtyPlan`。它由 Prepared Published Batch 的 Stable Slot 构造 Dirty records，写入 Movement/Facing、Transform/Velocity、Flow、Combat/Visual，并在完整 Query collection 预验证后执行一次原子 traversal。旧 FacingFinalize 在该标志存在时不再写 Mass。

[COMPUTED][HIGH] Proxy Dirty ACK 在下一次 Input Sync 原位刷新 Boundary cache 后发生，Result Finalize 只处理 Ordered Event/Behavior 等提交后副作用。静态 Objective 不要求 Target Guidance 每 Tick 追平 Clock sequence，只要求 Revision 一致且状态序列不超前。

[COMPUTED][HIGH] 9833 静态 T5 达到 step 645；step 600 的 Dirty apply/cache refresh 均为 600，Objective reuse=600，普通 Intent resources=0，零 `VIOLATION/Rejected`。DisableUnity 与 Architecture 2/2 通过。

[INFERRED][HIGH] 该架构仍是迁移态：Dirty Plan 的 Movement/Facing/Flow metadata 仍由旧 Round Prepared Commit 交叉验证和补充，资源/Projectile/表现事实仍由旧 Finalize 完成。只有这些 metadata/side effect 进入 Published Batch 合同并删除 Round Transaction 后，WA8 才能关闭。

## 0.0.30 2026-08-03 WA8-R Prepared Result Barrier / Semantic Input Journals

[COMPUTED][HIGH] Worker Result Apply 的 Runtime 核心已从单函数验证+改写拆为 `Prepare`/`CommitPrepared`。Demo Round 在 Commit 前验证 Dirty Facing/Combat 的 StableRef、Lifecycle、字段 payload、Combat capability bundle 和 Mass Query fragment 集合；Prepared Lifecycle View 改变会拒绝提交。

[COMPUTED][HIGH] 服务端 Ordered Event、Behavior side effect 与 Dirty ACK 由 per-world 单槽 pending barrier 延迟到 Mass Commit 之后；pending 期间不交换下一 Published Batch。当前实际 Mass 写入仍由 Demo-local Round Prepared Commit 完成，所以该屏障尚未成为最终的直接 Dirty Apply 架构。

[COMPUTED][HIGH] Target Objective 使用合同字段语义哈希去重；Environment/Nav resource 只有内容 Revision 未发布时才进入 Intent，且只在 Runtime 接受输入后 ACK。静态 T5 普通 Intent 已观测到 `resources=0` 和 Objective `published=1/reused=1`。

[INFERRED][HIGH] 当前首要结构缺口仍是 Demo-local `FCrowdDemoRoundWorkBatch`、`BeginBoundaryTransaction`、`TryPrepareRoundApply` 与 Stage 链。下一版本应让 Prepared Dirty plan 自身完成 Mass 原子写入和事件提交，然后删除旧链；Particle 大 Island 与 WA9 继续后置。

## 0.0.29 2026-08-03 Target Scoped Cohort / Particle Closed Islands

[COMPUTED][HIGH] Target work 现在以稳定 `CohortKey` scope 调度。静态 Objective 依靠资源与实体依赖增量失效；只有非零 TargetVelocity 需要每 Clock 调度全部 Cohort。10k 双 Cohort 回归证明单 Cohort Movement 变化只增加 40 个 128-entity Guidance shard，另一 Cohort 不发布、不重建。

[COMPUTED][HIGH] Particle Work 先按保守最大修正距离构造闭合 Interaction Island；多个 Island 独立 Solve，结果按稳定 Agent/Pair 顺序归并，并由全局 exact Applied-State 验证守门。输出记录 Island 数、Cell shard 数、跨 Cell pair 数、最大 Island 和 monolithic fallback。

[INFERRED][HIGH] 该版本关闭 Target 受影响 Cohort 回归和 Particle 的“多个闭合 Island 不再整世界求解”子项，但没有关闭大型单 Island 的 Cell Barrier。当前 Cell ownership 是可观测基础，不是跨 Cell 并行 Solver；下一版本应实现稳定 Cell-Pair Owner 与每轮 Barrier merge 后再跑高密度 10k Particle Island。

## 0.0.28 2026-08-03 WA8.5 Bounded Work/Timer/Spatial Complexity

[COMPUTED][HIGH] Work Ring 使用固定 `Priority × Domain` Buckets、稳定 Bucket Cursor 和持久 Key Index；完整 Drain 的 bucket probe 数受固定 Bucket 数约束，不随队列规模平方增长。Time Wheel 使用最小 Tick Heap，未来 Bucket 不被 `DrainDue` 扫描。

[COMPUTED][HIGH] Spatial Index 由 Lifecycle 与 Movement Dirty 增量维护。普通 Movement 更新只刷新 Entry；只有 Cell Key 改变才从旧 Cell 删除并稳定插入新 Cell。Runtime 暴露 Work probe、到期 Bucket scan、Spatial rebuild/update/migration 累计值。

[COMPUTED][HIGH] Complexity 3/3 与完整 RuntimeV2 32/32 通过：Work Ring 覆盖 1k/2k/5k/10k，Sparse Time Wheel 保留 10k 未来 Bucket 且提前 drain 扫描为 0，Spatial 覆盖 10k×1%/10% 并保持 full rebuild=`0`。

[INFERRED][HIGH] 下一复杂度风险在 Particle，而不是继续改 Work Ring：Target 已调用 128 Entity Guidance Shard，但仍需受影响 Cohort 失效回归；Particle Worker 当前仍执行单次完整 Solver，需要闭合 Island/Cell Owner 分片及稳定 Barrier Merge。

## 0.0.27 2026-08-03 Client Legacy Round Intermediate Diagnostics Removed

[COMPUTED][HIGH] 客户端产品路径不再计算或比较没有本地生产者的 Round 中间哈希；对应的 902 行比较实现、`bLegacyRoundDiagnosticsProduced` 状态和 Compare 缓存中的 `ParticleMetrics`/`ServerClientParticleHashMatch` 已物理删除。结构门逐符号拒绝这些字段、Legacy unavailable 日志以及 Dynamic Flow、Particle 和 Projectile 的客户端中间比较标记。

[COMPUTED][HIGH] 低频 Round Checkpoint 仍只在完整组装后执行状态误差比较；普通 Epoch 的一致性由 Worker Digest 与稀疏 Authority Correction 负责。服务端场景/性能汇总仍是独立验收数据，不因删除客户端对称比较而消失。

[COMPUTED][HIGH] DisableUnity、结构门与 Round Checkpoint 2/2 通过；9809 Correction-off 双进程运行得到一次 queue/apply、零 Position/Velocity/Yaw 误差、Epoch/Input=`221/307`、Runtime failure=`0`，且旧客户端中间日志为零。

[INFERRED][HIGH] 当前主线进入 WA8.5 复杂度收敛。优先级是 Work Ring/Time Wheel 扫描遥测与微基准，其次 Spatial 跨 Cell 增量迁移，再处理 Target/Particle 分片和 Target >900 Tick 长窗口；不能用 20 实体门替代 WA9 的 10k 规模门。

## 0.0.26 2026-08-03 Dedicated Round Checkpoint / Sparse Correction Revision Barrier

[COMPUTED][HIGH] 网络状态合同现已分离：普通推进只发送有序 Intent；Unreliable Digest 发现 `Domain × Scope` 分歧；可靠 Authority Correction 只携带命中 Scope 的成员、字段记录和 tombstone；终局/Late Join/Resync 使用独立 Round/Worker Checkpoint。Demo 旧 `FCrowdDemoCorrectionFrame` 类型、普通完整 State Correction producer/consumer 和共享 Checkpoint 外壳已删除。

[COMPUTED][HIGH] Round Checkpoint 由服务端完成 Round 后发布，客户端完整验证/组装后在下一 Owner Boundary 原子应用。它不依赖已退休的本地 Round fixed-step 时钟，不替换 Worker Runtime Generation，也不清空未消费 Intent。可靠 Worker packet 使用 4 KiB 块，避免 UE reliable partial bunch 缓冲溢出。

[COMPUTED][HIGH] 稀疏 Correction 的全局有序水位由 `LastAppliedAuthorityCorrectionSequence` 持有。每个 Domain Context 至少继承该水位，并与当前 Stage Work 的 CorrectionRevision 取最大；因此修补闭包和所有后续 Clock/Timer 输出不能退回旧 revision。9807 的多个 Combat Scope Correction 后客户端继续到 Epoch 905/Input 991，Runtime failure 为 0。

[COMPUTED][HIGH] 客户端不再执行无生产者的 Legacy Round 中间哈希判定；该诊断明确标为 unavailable。生产一致性由 Worker Digest、稀疏 Correction 前后误差、下一 Digest，以及低频 Checkpoint 状态误差判定。

[INFERRED][HIGH] 当前剩余工作不是重新引入完整 Correction。WA8 下一结构项是删除仍保留的 Legacy Round 诊断实现/字段并审计 Plan/Checkpoint 边界外完整 Query 为零；之后进入 WA8.5 微基准和 Target >900 Tick 长窗口门，最后才运行 WA9 三个 10k 场景。

## 0.0.25 2026-08-03 Legacy Ordinary Full Correction Disabled（历史切片）

[COMPUTED][HIGH] 旧 Round `AgentStates` 完整纠错默认不再发布；`ShouldBuildCorrectionFrame` 只在显式 `-CrowdDemoLegacyFullCorrectionDiagnostic` 下开放。普通网络纠错由 WA7-R Unreliable Digest 检测和可靠 Scope Correction 修补承担，不再驱动旧客户端全世界 rollback。

[COMPUTED][HIGH] RoundResult/Late Join 的完整 Checkpoint 发布门保持独立。9791 T8 在 900 Worker batch 后仅产生 1 个终局 Checkpoint，业务事件与 Golden Hash 不变；默认单进程双 PIE 完成 300 Epoch 无纠错预测与稀疏修复，同时旧 full frame/header 计数为 0。

[COMPUTED][HIGH] DisableUnity 与三项定向自动化通过；9790 T5/600 保持 Target verified=`20`、stale=`0`，9791 T8/900 保持 Ordered Event=`150` 和 Hash=`439379904/1411313634/6141440`。

[INFERRED][HIGH] `FCrowdDemoCorrectionFrame` 仍被终局/加入 Checkpoint 与显式诊断共用，因此本版本只关闭普通生产语义，不声称类型和 RPC 已物理删除。下一结构切片是专用 Checkpoint 载荷迁移后删除 Legacy Correction 外壳；WA8.5/WA9 继续后置。

## 0.0.24 2026-08-03 Single Commit Owner / On-demand Checkpoint Serialization

[COMPUTED][HIGH] 每 Tick Movement Commit 现在只有 `FCrowdDemoPreparedMovementBoundaryCommit::Finalize.CommitPlan` 一个 owner；旧的 `PreparedMovementCommitPlan` 完整副本、PostFinalize State 副本和常驻 Checkpoint State 副本均已删除。Commit 生命周期延长到 Owner Barrier 末尾，再由 `FinishFixedStep` 同时释放 Movement/Combat Commit。

[COMPUTED][HIGH] Mass Apply 只为 Dirty Ref 构造 Final Business；PostFinalize 从唯一 Commit 派生指标与 rollback 输入；Checkpoint Publisher 仅在发布门之后构造网络 State。DisableUnity、17 项定向自动化、9785 T5/600 与 9786 T8/900 Golden 通过。

[INFERRED][HIGH] 这尚不是完整的“普通 Epoch 零完整状态序列化”。旧 Round Correction Publisher 仍将完整 AgentStates 周期性发送，完整 BoundarySnapshot 与 Demo-local DAG 也仍按成员规模工作；它们是下一 WA8 删除对象，WA9 继续后置。

## 0.0.23 2026-08-03 Persistent Mass Handle / Dirty Mass Apply

[COMPUTED][HIGH] Mass Lifecycle Owner 持久维护完整 `StableEntityRef→FMassEntityHandle` 索引，并在 Spawn、Lifecycle Recycle、Destroy 与 teardown 同步更新；Handle 解析再次核对 Fragment 中的 Provider/StableId/LifecycleSerial。

[COMPUTED][HIGH] 普通 Result Apply 从 Proxy Dirty Batch 传播唯一 Dirty StableRef。最终提交先将这些 Ref 完整解析和验证为按 Archetype 分组的 `FMassArchetypeEntityCollection`，确认 Query Fragment 集合完全匹配后才开启唯一原子写入；提交使用 `ForEachEntityChunkInCollections`，不存在无界完整 ResultCommit Query traversal。bootstrap 仍允许一次完整 Dirty 集合。

[COMPUTED][HIGH] PostFinalize 与 Checkpoint 记录现在从已验证的 Worker/Boundary 结果构造，不再为发布记录扫描全部 Mass Fragment。Dirty Mass 遥测独立报告 Batch、最近实体数、累计实体数和稳定成员数。

[COMPUTED][HIGH] DisableUnity 与三项定向自动化通过；9782 T5/600 保持 Guidance=`20/20`、零 stale/violation；9784 T8/900 保持事件 50/50/50/50/50 和 Golden Hash=`439379904/1411313634/6141440`。

[INFERRED][HIGH] WA8 尚未整体关闭：普通 Round DAG 仍计算完整成员的 Commit/PostFinalize 数据，`BoundarySnapshot` 仍是完整数组。下一结构项是把普通帧完整 CPU 数组收敛到 Dirty 增量，并把完整序列化限制到 300 Tick Checkpoint/诊断边界；WA9 继续后置。

## 0.0.22 2026-08-03 Unreliable Digest / Dirty Proxy / Gather 删除

[COMPUTED][HIGH] WA7-R Digest 现在使用可丢失、自覆盖的 Client Unreliable RPC；接收 Inbox 以最高 DigestSequence 拒绝迟到和重复，允许序列跳号并由下一 cadence 覆盖缺失。

[COMPUTED][HIGH] Worker Result Apply Proxy 以稳定排序实体视图和 Stable Slot 保存 Lifecycle membership，只有 Spawn/Despawn 才重建视图。每个 Published Batch 汇入 latest-wins Dirty Batch，普通 Round refresh 只验证和应用 Dirty Facing/Combat slot，成功后按 PublishSequence ACK。

[COMPUTED][HIGH] `FCrowdDemoRoundBoundaryGatherStage`、`RequestSubmitQuery`、`CopyCurrentEntities` 和 Result Apply 完整 Mass fallback 已删除。完整 Snapshot Mass 读取只存在于 Input Sync 首次 bootstrap/Plan Revision 边界；普通 Tick 缺 Dirty Batch 时 fail-closed，不回退全查询。

[COMPUTED][HIGH] DisableUnity、四项定向自动化、9779 T8/900 Golden 与 9781 T5/600 通过；两场 step 600 均保持 publish/hash/token=`1/3/598`。9780 的延长 T5 在 step 886 暴露 Target Demand 可行区不足，作为独立长窗口缺陷 OPEN。

[INFERRED][HIGH] WA8 尚未整体关闭：最终 `ResultCommitQuery` 和 Demo-local Round DAG 仍按完整成员工作。下一结构项是持久 Mass Handle 索引与 Dirty Mass Apply Plan，而不是开始 WA9。

## 0.0.21 2026-08-03 Production Snapshot Hash 降频

[COMPUTED][HIGH] 全六域 Production 普通 Tick 使用 `AdvanceBoundarySnapshotEpochToken` 更新 Round 输入身份，Token 只折叠 bootstrap baseline、FixedStep、Plan Revision 和已应用输入水位，不扫描 record array。完整 Snapshot Hash 固定在 bootstrap/fallback、300 Tick checkpoint、Shadow/Canary 与显式全量诊断边界。

[COMPUTED][HIGH] `FullBoundarySnapshotHashCount`、`BoundarySnapshotEpochTokenCount` 与 `FullBoundarySnapshotPublishCount` 独立计数。9772 T8 和 9773 T5 在 step 600 均为 publish/hash/token=`1/3/598`；T8 golden 与性能门未回归，T5 的移动 Objective 仍保持单 resource 增量。

[COMPUTED][HIGH] Target parity 校验现在只在实际存在 `TargetTopologySlots` 时要求 Worker/Legacy Target state；这消除了目标资源晚一帧可见时的空工作误报，不改变 Objective Revision 出现后的 Production 所有权。

[INFERRED][HIGH] 当前剩余热路径 O(N) 成本是 `CopyCurrentEntities`、排序、StableEntityRef→record map、逐实体 Domain lookup/codec 与完整 DAG 成员消费。下一结构切片是稳定 dense Proxy view + Published Dirty Batch；本版本只关闭完整 Hash 扫描，不声称完整 Snapshot 容器已删除。

## 0.0.20 2026-08-03 Worker Proxy Snapshot 原位刷新

[COMPUTED][HIGH] Production 普通 Tick 不再维护 `WorkerInputSnapshotCache` 或三套 facts cache，也不再从缓存构造 Records、复制完整 Snapshot 再发布。bootstrap 的 `BoundarySnapshot` 成为稳定存储，Worker Proxy 先验证全部实体和 hot state，再一次性原位更新 Movement/Facing/Combat。

[COMPUTED][HIGH] `RefreshBoundarySnapshot` 只接受已经按 StableEntityRef 排序的成员，原位验证字段与成员唯一性并刷新 Hash；它不分配新的 record array、不排序。结构门拒绝重复缓存、普通 Proxy 路径中的 `BuildBoundarySnapshot`、record-array copy 与 `PublishBoundarySnapshot`。

[COMPUTED][HIGH] 9766 T8 与 9770 T5 均在 step 600 报告 `in_place_snapshot_refreshes=600`、`full_snapshot_publishes=1`。T8 黄金计数/哈希/性能不变；T5 正常 Intent 仅携带单一 Objective resource，零硬失败。

[INFERRED][HIGH] 当前 Snapshot 的容器复制已经移除，但普通 Tick 的完整 O(N) 遍历与 StableHash 仍存在。它属于下一 Production dirty-view 迁移和 WA8.5 Mirror/Hash 降频工作，不能记为“完整 Snapshot 已删除”。

## 0.0.19 2026-08-03 公共 Boundary Orchestrator/WorkGraph 删除

[COMPUTED][HIGH] `MassCrowdBoundaryOrchestrator.h/.cpp` 与 `MassCrowdBoundaryWorkGraph.h/.cpp` 已从插件物理删除，对应 Legacy 单元测试也已删除。插件不再公开 task/resource/commit-envelope/prepared-patch transaction 外壳；Projectile Boundary 和 SharedFlow/Movement/Particle/Facing 纯计算 Kernel 不受影响。

[COMPUTED][HIGH] Round 的异步批次调度与四个 typed join 已收为 Demo-local `FCrowdDemoRoundWorkBatch` / `FCrowdDemoRoundWorkGraph`。结构门验证六个 Legacy 文件不存在、生产 include 为零、旧 Runtime WorkGraph 符号为零；Development Editor DisableUnity 与结构门通过。

[COMPUTED][HIGH] 9765 全 Production T8 完成 900 Tick，事件计数 50/50/50/50/50、duplicate/expired=`0/0`、固定三哈希一致，fixed-step p95=`34.230ms`、realtime=`0.998`。

[INFERRED][HIGH] 当前剩余 WA8 风险不是公共 API，而是 Round 普通帧是否仍构造完整 `BoundarySnapshot`/Mass Gather，以及 Demo-local 批次仍沿用 Boundary Transaction 命名。完成实际 Query/Processor 注册审计并消除正常帧完整 Snapshot 后，才可进入 WA8.5。

## 0.0.18 2026-08-03 Round 四阶段/Poll 外壳删除

[COMPUTED][HIGH] Round 生产调度现在只保留两个 Mass Processor。Worker Input Sync 在自己的 Execute 中直接应用 Authority Plan；Worker Result Apply 先交换 Worker Published Batch，再调用单一 `AdvanceRoundWorkerFrame`，按顺序准备并原子提交上一批、发布 Authority/Client/PostFinalize/Checkpoint 事实，然后在无 in-flight step 时提交下一批。四个旧 `Execute*Stage` 函数和 `PollRoundWorkBatch` API 已物理删除。

[COMPUTED][HIGH] `TryPrepareRoundApply` 取代旧 Poll shell 名称，表达其职责是把已完成的异步任务和 Worker 水位验证为可提交 Apply Plan，而不是拥有独立 Frame Transaction。完成上一批与准备下一批仍构造各自独立的 Movement/Particle/Facing Stage 对象，避免跨 boundary 携带局部阶段状态。

[COMPUTED][HIGH] 结构门验证旧符号为零、恰有两个 Processor 类型和直接入口关系；Development Editor DisableUnity 与结构门通过。9757 T5 全 Production 达到 step 600，task=`5`、stale/block wait/simulation lag=`0/0/0`、无硬错误。

[COMPUTED][HIGH] T8 的剩余失败已分两层关闭：Projectile ImpactId 改为 Tick-major 有序事件身份，Movement Profile v2 / MovementControl v9 增加权威 PreferredVelocity 所有权位，防止 Worker Planning 覆盖已选中的 BusinessOverride。9764 全 Production 900 Tick 恢复 50/50/50/50/50、duplicate/expired=`0/0` 与固定三哈希；p95=`34.181ms`、realtime=`0.998`。

[COMPUTED][HIGH] 后续 0.0.19 已迁出最后消费者并删除公共 WorkGraph/Orchestrator；本段保留为 0.0.18 时点的历史状态。

## 0.0.17 2026-08-03 Target Objective 驱动 Particle 外部实体

[COMPUTED][HIGH] Target 场景的移动目标运动学不再冻结在 bootstrap MovementControl。公共 `PrimaryTargetParticleAgentId` 把 Target 与 Particle 的同一外部实体身份固定下来；Worker Particle 每 Tick 从版本化 `ObjectiveRevision` 解码位置和速度，按固定步长生成 Start/Predicted，并把 Objective Resource 纳入依赖声明与观察。MovementControl 中的外部实体仅保留静态物理 profile。

[COMPUTED][HIGH] Particle final closure 在常规求解失败时执行确定性有界量化闭包：公共 progress 快路径、稳定旋转候选、固定体优先、四种稳定贪心顺序与每序最多 1000 节点 DFS；最后仍由 exact all-pair/environment 验证决定是否提交。`UnifiedHardInfeasibleCount` 是诊断计数，不再否定一个已通过 exact safety 的终态。

[COMPUTED][HIGH] 定向门通过 Development Editor DisableUnity、`ParticleUsesLiveTargetObjective`、Core Particle 6/6、GatherMergeCommit、TargetControlRoundTripAndDomain 与 PostFinalizeMinimalQuery。9756 全 Production T5 达到 step 600，正常帧 task=`5`、Runtime failure=`0`、simulation lag=`0`、stale lifecycle=`0`、零 `VIOLATION/Rejected`；step 300/600 的 Intent 均为单一 Objective resource 增量。

[INFERRED][HIGH] 该版本只关闭 Target Objective→Particle 动态输入接缝。Round 四阶段/Poll shell、公共 Orchestrator/WorkGraph 剩余消费者和 WA8.5 的 10k 数据结构改造仍未完成。

## 0.0.16 2026-08-02 WA8 Round Runner 物理删除

[COMPUTED][HIGH] Round 生产路径已从公共 `FCrowdMassBoundaryRunner` 迁出。`FCrowdDemoRoundWorkBatch` 只负责 Demo 内不可变任务 DAG 的非阻塞执行与遥测；它不拥有 Runtime transaction、prepared patch 或 commit envelope。GT 在全部任务和 Worker 水位就绪后调用 `ValidateRoundApplyPlan`，完整验证 Commit Target/Lifecycle 与 Movement/Business/Combat/Flow/Target/Particle 摘要，再一次性应用 Mass。

[COMPUTED][HIGH] `MassCrowdBoundaryRunner.h/.cpp` 和 Runner 专项测试已物理删除。公共 `MassCrowdBoundaryOrchestrator` 的任务/遥测类型与 `FCrowdMassBoundaryWorkGraph` 输入组装仍被 Round 使用，所以它们不是零消费者，暂不能删除。

[COMPUTED][HIGH] 真实 T8 已验证新批次到 step 300，零 stale、零 block wait；step 415 的 Particle failure-trace replay hash 不一致仍为当前 Round 完整回归阻塞项。

## 0.0.15 2026-08-02 WA8 Demo transaction shell 脱钩

[COMPUTED][HIGH] Friendly 与 Mixed Coordinator 已完全脱离 `FCrowdMassBoundaryRunner`，Mixed 同时脱离 `FCrowdMassBoundaryWorkGraph`。Friendly fallback 同步执行纯 Nav/Facing plan；Mixed fallback 用线程池 Future 执行 Movement → Particle → Facing typed Kernel 链。两者的 GT 写入边界均保留“先完整解析与验证全部实体，再一次性提交”的原子合同，不再制造 Legacy commit envelope。

[COMPUTED][HIGH] 当前 Demo 生产源码中，Runner/WorkGraph 的唯一剩余消费者是 `CrowdDemoRoundSimPipelineSubsystem`。插件中的公共声明/实现及 Legacy 单元测试仍在，因此公共 API 尚不能删除。Projectile Boundary 与纯计算 Kernel 不属于该删除范围。

[COMPUTED][HIGH] 9706 fallback、9707 Friendly Production 和 9709 Mixed 全 Production 均通过有效双端业务门；构建与两个 Coordinator 架构门通过。9705 暴露的 Behavior evaluation kinematics 水位已按自主预测合同修复：bootstrap 严格状态 parity，普通 Intent 严格控制/命令 parity，预测状态误差由 Digest/Correction 管理。9714 全默认 Shadow 完成 600/600 expectation 与双端业务 PASS。

[INFERRED][HIGH] WA8 仍 OPEN：下一步把 Round 改为直接 Worker Apply/纯 Kernel fallback，随后删除 Runner/WorkGraph 公共类型与 Legacy 测试并执行 AST/注册审计。

## 0.0.14 2026-08-02 WA8 Friendly 直接 Worker Result Apply

[COMPUTED][HIGH] Friendly 的全 Production 路径已绕过 `FCrowdMassBoundaryRunner`：Worker Behavior 与 Facing tail 在相同 InputSequence 闭合后，GT 构造并完整验证 Mass Commit Plan，再一次性提交 Movement 与 Logistics 事实。bootstrap 使用空 Entries 的 MovementControl v8 和逐实体静态 Profile；普通 Tick 不复制完整控制列表。Shadow/Canary 暂时保留 Nav/Runner 对照。

[COMPUTED][HIGH] 9703 真实双端门以 `direct_worker_apply=1` 完成全部 Friendly 业务事实与生命周期复用，双端 state hash=`3180435972084878253`，resnapshot 始终为 1。普通 step 300/600 的资源与生命周期输入均为 0，复用帧仅发送实际变化的 spawn/despawn/profile=`1/1/1`。

[INFERRED][HIGH] WA8 尚未关闭：Mixed/Friendly 的 Shadow/Canary fallback 仍引用 Runner/WorkGraph，Round 四阶段/Poll shell 仍存在。下一结构切片先把 parity 从 transaction shell 脱钩，再物理删除公共 Legacy API。

## 0.0.13 2026-08-02 WA8 Mixed 直接 Worker Result Apply

[COMPUTED][HIGH] full-Worker Mixed Production 已绕过 `FCrowdMassBoundaryRunner` 和 `FCrowdMassBoundaryWorkGraph`：有序 Intent 进入同一 Runtime 后，GT 只消费同一输入水位的 Worker Facing dirty proxy，完整验证实体、Lifecycle、sequence、payload 与全部 Mass Handle，再原子提交 Slot/Transform/SpatialSafety。Checkpoint 的 `source` 现在区分 `WorkerResultApply` 与 Legacy fallback。

[COMPUTED][HIGH] bootstrap MovementControl v8 是 `Entries=0` 的 O(1) 全局设置资源；每个实体的半径、速度、InteractionLayer 和 Particle profile 由 `MovementProfileRevision` 建立。普通 Tick 只发送 Clock、业务 context、资源/生命周期/层变化增量，不再发送完整 movement entry 列表。

[COMPUTED][HIGH] Worker MovementPlanning 在没有 Target state、也没有可达共享 Flow 时，无论 Behavior 是否携带 MovementGoal，都会回退 `Resolved.DesiredVelocity`。Mixed 尚无 TargetControl cohort/flow，故其全局 sparse control 暂时关闭 LocalPredictive，仍由 Worker Particle 与 GT safety commit 保证不重叠；9701 的 600 Tick 正式门通过，最小间距=`70.04cm`、p95=`17.641ms`、硬失败=`0`。

[INFERRED][HIGH] 该版本没有关闭 WA8：Friendly 仍有 Production Runner，Round 仍有四阶段/Poll shell，Mixed fallback/public Legacy API 仍服务 Shadow/Canary。LocalPredictive 只有在 Mixed TargetControl 可路由 cohort/flow 合同完成并通过拥堵门后才能恢复。

## 0.0.12 2026-08-02 WA8 full-Worker Mixed Production 所有权门

[COMPUTED][HIGH] Mixed Production 不再把 Legacy projectile/combat parity 当作 Worker 提交条件。提交前仍完整验证 Worker control revision、fixed step、payload schema/hash、entity/lifecycle membership 和 combat alive/health；Shadow/Canary 继续执行 Legacy 对照。

[COMPUTED][HIGH] Production 的 combat/projectile 状态与累计遥测均来自 Worker published output。9689 在六个相关 Domain 全 Production 下完成 step 600，fixed-step p95=`17.624ms`、stale reject=`0`、projectile duplicate=`0`，并继续稳定运行到 step 1055。

[INFERRED][HIGH] 当前并非 WA8 终态：GT `PrepareMixedCombatBoundary` 仍生成 Legacy expected projectile/health/result，只是 Production 不再消费其 parity。下一结构切片删除该 Production 预计算，再删除 Friendly/Mixed Legacy Runner 与 Round Boundary shell。

[COMPUTED][HIGH] 9690 已替代上一句的剩余状态：Production 现已跳过 `PrepareMixedCombatBoundary` 和 expected combat payload；Shadow/Canary 保留对照。GT `PrepareMixedCombatAttackPlan` 仍存在，因为 Mixed Business Planner 的 MovementLock 与行为标签尚依赖当前 Attack Phase。

[COMPUTED][HIGH] 9691 再次替代最后一句：通用 Worker Combat state v2 新增 `bMovementLocked`，MovementPlanning 在 CombatReactive barrier 后消费该位；Mixed Production 已跳过 GT Attack Planner。Behavior MovementLock 不再拥有 Combat 实体的 Production 锁定语义。9691 step 600 业务门通过且无拒绝。

## 0.0.11 2026-08-02 WA8 T8 串行帧屏障关闭

[COMPUTED][HIGH] Production Movement/Particle/Facing 不再提交第二个异步 tail：`PollBoundaryWork`完整验证 Worker Movement、Particle kinematics 与 Facing sequence/成员/字段后，直接构造 `FCrowdMassMovementFinalizeWork` 原子提交计划。Shadow/Canary仍保留异步对照路径。9686 因此把每 Tick pending 从约3帧降到约2帧，p95降到约51ms，realtime升到约0.669。

[COMPUTED][HIGH] 普通全 Worker T8 在同 Generation、同 Plan、Projectile已bootstrap、无Target Region且五个相关域全Production时，Clock Intent在Business输入建立后立即提交，与后续SharedFlow/Movement/Legacy诊断事务并行。首次Tick、Plan切换、Target场景和非Production模式全部回退到完整prepared提交。9687把pending进一步降到`902/901`，两轮p95=`33.999/33.981ms`、realtime=`0.998/1.000`。

[COMPUTED][HIGH] 9687两轮900 Tick均完成50次acquire/windup/spawn/impact/damage且duplicate=0；Round 1 hash仍为`439379904/1411313634/6141440`，零Violation/Rejected。跨Round继续保持Generation=`1`，PlanRevision变化只在step 0走baseline，step 1恢复普通Clock快路径。

[INFERRED][HIGH] 该结果关闭T8 realtime缺口，不关闭WA8结构删除。下一切片固定为全Worker Mixed 600 Tick完整业务门；通过后才开始删除Friendly/Mixed Legacy Runner、Round四阶段和Boundary shell。

## 0.0.10 2026-08-02 WA8 同 World 跨 Round continuation 关闭

[COMPUTED][HIGH] 9685 全 Worker T8 在同一 Runtime Generation 上完成 Round 1 的 900 Tick 后，Round 2 从 step 0 继续到 step 300；`resnapshots=1` 保持不变，未发生 Runtime restart、Checkpoint resync 或 Domain execution rejection。此前 9680 的 Round 2 continuation 阻塞已关闭。

[COMPUTED][HIGH] 跨轮次修复包含三个独立合同：更高 ProjectileControl Revision 且 `bReplaceState=true` 可以重置 FixedStep；新 MovementControl Resource replan 覆盖同轮带锚点的旧 TimeWheel continuation；PlanRevision 变化只发送一次有序 InputSnapshot baseline。普通 Tick 仍不携带完整实体状态，Production 规划继续以 Worker Facing/Movement 覆盖 Position/Velocity/Yaw。

[COMPUTED][HIGH] 9685 当时的主阻塞是性能与剩余迁移：Round 1 fixed-step p95=`67.854ms`、realtime=`0.500`、boundary pending frames=`2698`。该性能数据已由0.0.11取代；全 Worker Mixed完整业务覆盖与Legacy transaction shell仍未关闭。

[INFERRED][HIGH] 下一切片应直接消除每 Tick 约三帧 Boundary pending，并证明 realtime≥0.95；不要再修改已通过的 Projectile/Plan transition 语义，除非出现新的失败证据。

## 0.0.9 2026-08-02 WA8 全 Worker T8 业务闭合检查点

[COMPUTED][HIGH] Behavior step 9 的根因已经修复：Round 没有 Behavior event 事务消费者，却为每个 autonomous expectation 保留 MatchedEventBatch，容量 8 在第 10 次验证时必然 fail-closed。`QueueAutonomousExpectation` 现在显式区分是否捕获事件；Round 不捕获，Mixed/Friendly 继续捕获并显式 ACK。`MassCrowd.BehaviorSource.AutonomousNoEventCapture` 已通过。

[COMPUTED][HIGH] Combat step 27 的唯一差异是 HitFlash 时间使用了两套时钟。Worker 使用 `SimulationTick × 1/30`，Legacy 使用未对齐 wall-time。Combat/Projectile 纯模拟现在统一使用 canonical SimulationTick 时间，Position、Health、Attack、Reactive、HitFlash 和 Visual 等完整 Combat payload 在 900 Tick 对照中保持一致。

[COMPUTED][HIGH] Round 末尾不再提交重复 Clock Tick；仅由 float 累计产生的小余量被吸收到最后一个完整 boundary 的时间标签，900 Tick 后成功生成 checkpoint。9680 全 Worker T8 得到 acquired/windup/spawned/impacted/damage=`50/50/50/50/50`、duplicate fire/hit=`0/0`，ProjectileControl=`published 1/reused 899`，新确定性 golden 为 attack/projectile/event=`439379904/1411313634/6141440`。旧 `41852579/488896174/4204062592` 只属于 wall-time Combat 时钟合同。

[COMPUTED][HIGH] 9680 当时的门未整体关闭：fixed-step p95=`67.871ms`、realtime=`0.500`，且 Round 2 曾触发 Domain execution failure。该 Round 2 failure 已由上方9685取代；性能与 Mixed 全 Worker业务覆盖仍未关闭。因此 WA8 Legacy 删除与 WA9 完整矩阵均不得开始。

[INFERRED][HIGH] 下一切片固定为：修复同 World 新 Round 的 Runtime generation/resource continuation → 消除每 Tick 三帧 boundary pending、恢复 realtime≥0.95 → 复跑全 Worker T8 性能门与 Mixed 600 Tick业务门 → 再决定 Projectile HostInput 兼容路径和 Legacy 事务删除。

## 0.0.8 2026-08-02 WA8 Projectile clock 与迁移兼容边界

[COMPUTED][HIGH] Worker Runtime 现为每个已加载 ProjectileControl 的 SimulationTick 排入一个有序 CombatClock work；Projectile/Combat executor 可在资源 revision 不变时使用绝对 SimulationTick 自主推进，且不会重放 bootstrap SpawnRequests。

[COMPUTED][HIGH] Round 与 Mixed 都能计算 ProjectileControl 的语义 hash。完整 Worker Runtime Production 时只在语义变化发布资源；Shadow/Canary 迁移态仍逐 Tick发布新鲜 HostInput，因为旧比较路径的 kinematics/attack state 尚未全部迁入 Worker Store。

[COMPUTED][HIGH] Mixed apply 不再把 Worker epoch 的 StateRevision 当成配置 ControlRevision；等待门改看输出 FixedStep，配置一致性改看 payload 内 ControlRevision。真实兼容门 9664 使用 Behavior+Combat Production、Movement Shadow，在 step 600 PASS，ProjectileControl published=600/reused=0，证明业务回归但不证明增量网络成本。

[COMPUTED][HIGH] Development Editor DisableUnity、`MassCrowd.RuntimeV2.ProjectileCombatDomain`、`CrowdDemo.WorkerV2.WA5.MixedCombatDomain`、Round PostFinalize 与 Mixed Architecture 定向门通过。Production Behavior 输入现在排 autonomous membership expectation，不再用 Legacy prepared content 逐字段否决 Worker 状态。

[COMPUTED][HIGH] 本节记录的T8 step 9阻塞已由0.0.9解除，Round 2 continuation又由0.0.10解除。仍有效的未完成项是Mixed全Worker业务覆盖与realtime性能门；因此仍不能删除Shadow兼容发布或Legacy parity。

[INFERRED][HIGH] 当前下一切片以上方0.0.9为准，不再重复执行已关闭的Behavior admission诊断。

## 0.0.7 2026-08-02 WA8 Mixed ordinary intent

[COMPUTED][HIGH] Mixed bootstrap继续使用一次完整BoundarySnapshot，但普通Worker帧已改为`SubmitIntentBatch`。每帧仍为Legacy GT业务Runner构造的Snapshot不再进入Worker普通输入；Worker只消费Clock、版本资源、显式Lifecycle/Profile journal与Behavior输入。

[COMPUTED][HIGH] Mixed Coordinator在真实Lifecycle操作成功后记录有界Despawn、Spawn和Movement Profile Revision，并只在Runtime接受普通Intent后清空。Worker Despawn原因是独立的1-based transport ID，不直接复用从零开始的`ECrowdDespawnReason`。

[COMPUTED][HIGH] Mixed Production验证当前Lifecycle、InputSequence、FixedStep、Ordered Event与GT事务token后，原子提交Worker-owned Source/Resolved/Business输出；不再要求异步Worker运动学或Source输出Hash与GT Prepared镜像相等。`WorkerAuthoritativeSparse`覆盖此合同。

[COMPUTED][HIGH] Production run 9651在step 226/271分别接受首次Despawn与Spawn/Profile Revision，step 600 PASS，spawn/despawn=`1/1`、stale reject=`0`、Worker普通Intent submitted=`600`且零`VIOLATION`。Development Editor DisableUnity与Mixed Architecture测试通过。

[COMPUTED][HIGH] WA8仍未关闭。Mixed/Friendly Legacy GT业务Runner仍读取完整Mass Snapshot，Target/Projectile剩余输入尚未完全增量化，Round四阶段、Boundary Runner/WorkGraph、Mailbox与`PollBoundaryWork`仍待物理删除。

## 0.0.6 2026-08-02 WA8 Friendly Behavior incremental input

[COMPUTED][HIGH] Friendly bootstrap still uses one complete BoundarySnapshot, but ordinary Worker intents now omit evaluation contexts whose typed external record list is empty. A Clock intent schedules Behavior for every live Worker entity; the executor refreshes Position, Velocity, and Facing from Worker Movement and advances the fixed step locally.

[COMPUTED][HIGH] Behavior expectations are split by input shape. Sparse external contexts compare content only for affected entities; context-free Clock epochs require autonomous lifecycle/fixed-step progress and still capture ordered Source/Business events. Friendly Production commits Worker-owned Source Set, Resolved Channels, and Business contributions rather than requiring stale GT kinematic hashes to match.

[COMPUTED][HIGH] The GT transaction remains a fail-closed apply token: current membership, input sequence, fixed step, Worker payload validity, ordered events, and unchanged Prepared base are validated before `CommitWorkerAuthoritative`; duplicate commit is rejected. The new `MassCrowd.BehaviorSource.WorkerAuthoritativeSparse` test covers the sparse two-entity case and authoritative kinematic hash divergence.

[COMPUTED][HIGH] Run 9644 passed the 40-second server Friendly flow; run 9645 passed the same real server+client flow and closed the previous client backlog gate. Development Editor DisableUnity, RuntimeV2 31/31, Friendly 2/2, and the WA8 structure gate passed.

[COMPUTED][HIGH] WA8 remains open. Friendly's Legacy Runner still gathers a complete Mass Snapshot for GT business transaction planning, Mixed remains on its Legacy ordinary snapshot path, and remaining Target/Projectile inputs plus the Round four-stage shell have not been deleted.

## 0.0.5 2026-08-02 WA8 lifecycle/profile intent boundary

[COMPUTED][HIGH] Ordinary Worker input has explicit Despawn, Spawn, and typed `MovementProfileRevision` arrays. Shadow assigns one continuous InputSequence order, validates the candidate lifecycle set, and commits its cached membership only after Runtime accepts the batch.
[COMPUTED][HIGH] Movement profiles are stored per StableEntityRef in the Worker entity store. MovementPlanning, Movement, and Particle enumerate the live store membership and consume the same profile field; the global MovementControl entry list is only a frozen bootstrap/compatibility fallback.
[COMPUTED][HIGH] Round bootstrap publishes per-entity profiles once. Normal Round intents carry clock plus changed resources and do not materialize a BoundarySnapshot inside `SubmitIntentBatch`. Sparse Movement correction removes derived Facing, Particle, and MovementPlan continuation before critical replanning.
[COMPUTED][HIGH] Friendly now has a real production lifecycle/profile journal. `RecycleTrackedAgent` creates and validates the replacement entity, then records ordered Despawn -> Spawn -> Profile records in `UCrowdDemoMassSubsystem`; Worker Input Sync drains and acknowledges those records only after Runtime acceptance and atomically changes the local owner memberships.
[COMPUTED][HIGH] Friendly Worker bootstrap uses one BoundarySnapshot and ordinary Worker frames use the intent-only entry. Its public replication stream publishes a reliable lifecycle Spawn before source/business records for the replacement lifecycle. Server production run 9626 passed a real recycle with zero stale-lifecycle commit.
[COMPUTED][HIGH] This is not WA8 closure. Friendly's Legacy business Runner still constructs a complete Mass Snapshot, Mixed remains a Legacy snapshot caller, and Target/Projectile inputs plus the Round four-stage shell remain. External dual-end Friendly run 9625 did not finish the client business PASS inside the gate window, despite the client Runtime continuing to apply intents without hard failure.

## 0.0.4 2026-07-30 WA全面Worker权威迁移

[INFERRED][HIGH] 全面Worker权威已经取代“四个GT Boundary Processor”作为AB5终态；目标合同见`FullWorkerAuthorityArchitecture.md`，字段级当前/终态Writer见`FullWorkerAuthorityOwnershipMatrix.md`。

[COMPUTED][HIGH] 当前代码仍是迁移态而非终态：Round bootstrap/resync 使用完整快照，普通帧已切到无 BoundarySnapshot 参数的有序 Intent 入口；MovementControl 的完整实体 Profile 只在 bootstrap、Worker Generation 或 PlanRevision 变化时发布，普通 Tick 复用 Worker Resource Store 并由绝对 SimulationTick 驱动 MovementPlanning continuation。四个Round Boundary Processor仍作为WA8前的`Legacy Domain Adapter`承载尚未删除的Boundary事务外壳；完整 BoundarySnapshot、同 Plan 内 Spawn/Profile Revision journal、Target/Projectile剩余增量化和Legacy Mailbox删除尚未完成。

[COMPUTED][HIGH] WA7-R 的有序 Intent、Digest、稀疏 Correction、Checkpoint 与 300 Tick 双PIE无纠错门已形成当前网络基线；最新 RuntimeV2 为31/31。WA8第一批输入收敛切片已删除普通 Round Intent 对完整 BoundarySnapshot 和每 Tick MovementControl Profile 的依赖，但WA8未关闭，仍需物理删除四节点、完整Mass Gather、Boundary事务与Mailbox。

## 0.0.3 2026-07-30 持久Worker Simulation目标架构

[COMPUTED][HIGH] PW1–PW8已进入混合生产架构：每World唯一`FCrowdAsyncSimulationRuntime`持有SoA Mirror、Simulation Clock、Input Queue和Published Exchange；Round/Mixed/Friendly只提交冻结输入，GT每帧一次消费Published Batch。Movement Production由`PersistentRuntimeAuthority`单独拥有，Particle、Target、Combat因PW7 fail-closed判定继续使用下方0.0.2的强一致Boundary。

[COMPUTED][HIGH] PW1三缓冲保持Building/Published/Consuming显式槽位、同实体State latest-wins、不可覆盖Event有界有序、每Consumer Frame最多一次交换和Violation锁存；Development/DebugGame Editor `-DisableUnity`、PW1定向7/7、MassCrowd 72/72与CrowdDemo 134/134通过。

[COMPUTED][HIGH] PW2 Runtime由`UMassCrowdRuntimeSubsystem`每World唯一持有，使用有界输入队列和短生命周期Owner Pump维护纯数据SoA镜像；Task不捕获Mass、World或UObject。Generation失效、活动Pump teardown和全量Resnapshot已由定向10/10覆盖；Development/DebugGame Editor `-DisableUnity`、MassCrowd 75/75与CrowdDemo 134/134通过。

[COMPUTED][HIGH] PW3已把Round/Mixed/Friendly冻结Boundary Snapshot接入Worker Shadow：首次全量，随后只发Lifecycle、Dirty State、快照Resource和成功Prepared Commit后的Behavior Command Journal；Worker确认Input Sequence后比较Entity/Lifecycle、State Hash及源Snapshot元数据Hash。该路径只诊断、不写Mass；Mixed实际300批累计165条Command，Mixed/Friendly各连续600批无积压、无跳批、无Violation。

[COMPUTED][HIGH] PW4已关闭：SharedFlow、Facing和Business支持稳定Shard归并，Business Payload按字段规范化以排除结构体padding；每World Runtime新增显式容量的短Task Shadow Scheduler、按Kernel Work Sequence门、跨Poll全局提交序交付及Invalidate/Stop排空。Round生产只复制自包含输入做Shadow，不写Mass；9111 step 300累计900项全部完成、in-flight=0、mismatch=0，Shard大小1–64轮换并交替正反派发。Development/DebugGame `-DisableUnity`、MassCrowd 78/78与CrowdDemo 134/134通过。

[COMPUTED][HIGH] PW5已关闭：Worker Owner把接受输入对应的完整State或零项结果发布到三缓冲Exchange；GT `UCrowdDemoWorkerResultApplyProcessor`每帧一次交换，只写Presentation/诊断代理。应用门覆盖Generation/Publish/Event Sequence、Hash、字段Owner Mask和GT当前Lifecycle；9112首批20 Patch全部进入20个代理，stale/event为0。Development/DebugGame `-DisableUnity`、MassCrowd 80/80与CrowdDemo 134/134通过。

[COMPUTED][HIGH] PW6最终生产路径已收敛：`FCrowdWorkerMovementAuthority`持有Movement字段Owner、双样本插值历史和Correction Revision；Canary/Production净化Worker Input Snapshot并拒绝GT回送Position/Velocity/Facing。Production直接接受已由短Task Boundary DAG完成的Movement Domain Tail并交给唯一Mass代理Writer，不再重复提交第二个Movement Task，也不要求旧Boundary Hash；Shadow/Canary仍执行独立比较但不是并行生产Writer。

[COMPUTED][HIGH] PW7已关闭：`FCrowdWorkerConsistencyDomainEvaluator`把公共前置条件以及Particle闭合Island、Target原子Cohort Plan、Combat连续/幂等/回滚条件实现为显式Evidence、Decision与Failure。9121真实Round中三类Domain分别因开放Island、网络语义未冻结、缺少Rollback证明得到`KeepBoundary`且零Violation；当前Particle/Target/Combat集合提交语义继续留在Boundary，后续只有新专项证据才能改变判定。

[COMPUTED][HIGH] PW8已关闭：跨Round输入使用绝对Simulation Time，Dynamic Flow Round Hash每Fixed Step只折叠一次并进入rollback snapshot；1k/2k/5k/10k均持续到step 300且Input Queue为0，10k接受`3010000`状态、Worker simulation lag=`9.677ms`。T1–T9、Mixed/Friendly/Continuous、单进程双PIE与T7 Production录屏通过；录屏含58状态事件、0 mismatch、0 freeze。Development/DebugGame `-DisableUnity`、MassCrowd 83/83和CrowdDemo 135/135通过。

[COMPUTED][HIGH] 10k完整Demo的强一致Boundary约145ms/step，因此当前证据只证明Persistent Runtime/Exchange持续守恒，不证明整条游戏流水线10k实时。Particle/Target/Combat仍保留Boundary；Movement不存在GT/Mass与Worker双写。

## 0.0.2 2026-07-30 异步Fixed-Step Boundary覆盖说明

[COMPUTED][HIGH] 2026-07-30当前 Round 生产代码由 AuthorityInput、ResultCommit、PostCommit、RequestSubmit 四个显式注册的 GT/Mass Boundary Processor 驱动深度1 Runner Mailbox；每帧各阶段最多执行一次，Pending 立即返回。旧 `UCrowdDemoRoundSimFixedStepPipelineProcessor`、`ROUND_DYNAMIC_FLAGS`、UObject Stage Adapter 与手工 `CallExecute()` 已物理删除。

[COMPUTED][HIGH] 通用 Frame Transaction、阶段幂等/顺序门及普通 fixed-step 的 Mass 访问 1/0/1 合同位于插件 `MassCrowdRuntime`；项目 `UCrowdDemoRoundSimPipelineSubsystem` 只持有每 World 实例和 Round 性能附加状态。四个项目 Processor、具体 Query、Target/Combat capability tag 及业务 Stage 仍留在 `MassAICrowdDemo`，插件不引用 CrowdDemo 类型。

[COMPUTED][HIGH] 当前实现遵循每World深度1 Mailbox合同：GT非阻塞消费完整Result、原子Commit并生产下一不可变Request；UE::Tasks Worker消费Request并发布Result；Pending时GT立即返回且客户端继续视觉插值。

[COMPUTED][HIGH] Plan、Correction与权威RoundResult在每帧Mailbox Poll之前应用；Correction会重开PlanApply boundary，并使在途Generation失效。失效只断开GT mailbox，旧Worker闭包仍只持有不可变Snapshot/线程安全WorkState并可自然释放。8827 T5S普通Correction流、双端hash与性能门通过。

[COMPUTED][HIGH] capability切换后的最新 Development/DebugGame × ForceUnity/DisableUnity 四构建、MassCrowd 85/85、CrowdDemo 139/139及项目架构3/3通过；Base+Target T5S 9321/9322功能、复制、Correction与双端hash通过，但backlog p95=`170.807/136.398ms`超出`66.667ms`。AB5仍缺该性能回退修复和完整真实场景矩阵；AB6仍缺强制Pending Correction、teardown/地图切换、完成顺序、全场景与FFmpeg门，因此不得汇报“生产验收全部完成”。

## 0.0.1 2026-07-29 T9覆盖说明

[COMPUTED][HIGH] T9在DP0–DP6之后新增通用Attack Profile/State/Intent与Host Attack Adapter。业务Planner只决定Acquire/Windup/Commit/Recovery/Cooldown及目标，Movement继续由Standard Sources和安全链处理；Melee/MidRange经一次Boundary稳定Spatial Index执行相对Sweep，Ranged生成Mass Projectile，三类Impact统一进入Combat Resolver与Prepared Health/Death提交。

[COMPUTED][HIGH] `FCrowdImpactFact`现使用通用`ImpactId + ImpactTypeId`；Projectile仍以Projectile ID作为ImpactId，Melee/MidRange使用Attack Commit稳定ID。生产`AttackTarget` Source已删除，Behavior Registry黄金值版本化为`11335697795273479593`。

[COMPUTED][HIGH] T9 Mixed可靠Agent Payload为显式v2，包含攻击Profile、Target、Phase、PhaseEnter、CooldownEnd和FireSequence并拒绝旧布局。8517双端20实体门通过死亡、目标重选、TargetRegion/Flow缓存重建、Projectile守恒、双端Hash与`2.910ms` p95。

## 0.0 2026-07-29 PJ0覆盖说明

[COMPUTED][HIGH] 本文件顶部与第PJ节描述当前状态；后续按日期记录的迁移切片是历史执行证据，其中“尚未接入”“下一步”或“未授权100/500”不得覆盖R0–R7、S0–S6关闭结论。

[COMPUTED][HIGH] PJ0–PJ6现已关闭。Projectile跨Boundary持久状态只位于`MassCrowdProjectiles`的Mass Fragment；Boundary使用不可变Snapshot和临时Prepared数组，最终由已验证Patch一次写回。空间查询、Combat事实/Resolver和Projectile运行时分别归属`MassCrowdSpatial`、`MassCrowdCombat`和`MassCrowdProjectiles`，Demo不再拥有第二套Kernel/Fragment/Store。

## 0. 2026-07-28 R基线源码事实

[COMPUTED][HIGH] Runtime当前新增了公共`ICrowdBehaviorSourceProvider`、`FCrowdBehaviorRegistryBuilder`、稳定Provider/Context ID、冻结时Registry Hash、标准位置/速度/Facing Context、最多8项96字节扩展Context、96字节实例状态以及Writer Next State。

[COMPUTED][HIGH] 当前Runtime通过开放Provider注册并冻结Registry，领域Source由Demo Provider拥有；Mixed读取Resolved Movement/Facing，公共Scheduler使用通用Stage/Task/Scope键；Projectile由Mass Fragment唯一持有，旧跨Boundary数组权威与固定镜像路径已删除，Boundary内临时Gather/Prepared数组仍作为事务数据使用；StateTree Adapter已拆为默认禁用兄弟插件。

[COMPUTED][HIGH] 当前已有随`MassCrowdSimulation`加载的`MassCrowdStandardSources` Runtime模块，Provider ID=`100`。模块提供13种稳定TypeId的自主Evaluator、TargetKinematics/FormationAnchor v1 POD Context、MaintainDistance/Wander持久State；`MassCrowdRuntime`和`MassCrowdCore`均不反向依赖该模块或特判其TypeId。

[COMPUTED][HIGH] `MassCrowdSpatial`提供Movement安全索引、稳定Body Snapshot、Uniform Grid、Swept Bounds查询、移动球相对Sweep和环境AABB Sweep；`MassCrowdCombat`提供Impact/Hit、Effect Profile、纯Resolver和Prepared Host Commit；`MassCrowdProjectiles`提供Spawn/Profile/State、Mass Fragment/Trait/Store、生命周期事件、Kernel和Boundary Pipeline。Runtime只单向依赖Spatial，Projectiles依赖Core/Spatial/Combat/Runtime/MassEntity，三个公共模块均不引用Demo。

[COMPUTED][HIGH] Mixed Movement Worker消费Resolved Movement Goal/Velocity/Facing/Constraint，执行`FCrowdMassMovementPipelineWork → Particle Constraint → Facing Finalize`并在Prepared Boundary中一次提交Movement、Business与Slot状态；Business数组非空不再跳过移动。Local Predictive与Particle按InteractionLayer过滤跨层邻居。Presentation Resolver按Property保留最高优先级Override和稳定排序的全部Additive记录。

[COMPUTED][HIGH] 开放Behavior框架、通用Scheduler、行为网络v3、Standard Sources、Projectile三模块、Demo Business与T9均已关闭；T9后完整自动化为MassCrowd 64/64与CrowdDemo 133/133。既有20/100/500同路径并发Projectile门继续作为PJ6基线，T9本身只验收固定20实体。详细数值以`TestScenarioMatrix.md`为准。

## 1. 文档职责

[COMPUTED][HIGH] 本文件只描述当前生产代码、数据所有权和已验证边界。阶段计划查阅`PhasePlan.md`，验收状态查阅`FeatureChecklist.md`与`TestScenarioMatrix.md`，退出实验与旧端口查阅`Docs/History`。

[INFERRED][HIGH] 生产运行、持续生命周期与复制的长期合同以`MassCrowdUnifiedRuntimeAndReplicationContract.md`为事实源；Behavior Source详细合同与现行关闭状态以`EntityBehaviorSourceArchitecture.md`为事实源。本文中的Round、Scenario和testcase均是Demo实现事实，不是插件最终产品API。

## 1.1 当前能力与未来目标

[COMPUTED][HIGH] 当前已实现的是固定 Agent 集合、Round Bootstrap/Plan、双端 fixed-step、分块 correction/checkpoint、RoundResult/hash 验收，以及插件 Core/Runtime 的通用运动 kernel 与 WORK 合同。

[COMPUTED][HIGH] 当前已实现生产Relevant Snapshot、Demo RoundBootstrap adapter、lifecycle batches、真实Mass lifecycle store、组合式Behavior Source Runtime、Runtime静态Recast Graph/Flow资源、owner-only late-join channel、空间RelevantSet、可靠状态/latest-wins correction、公共Presentation slot lifecycle，以及20实体continuous混合Sandbox。行为架构事实源为`EntityBehaviorSourceArchitecture.md`。

[COMPUTED][HIGH] P0–P5公共产品闭环已完成：旧Round接入P1 Orchestrator与P3 channel/Presentation，J已删除O(N)安全检查，`NavFlowProductSmall`与专用`FriendlyLogisticsSmall`场景均通过。

[COMPUTED][HIGH] Behavior Source核心模型和World Store已进入Mixed生产路径：Movement Goal/Facing/Constraint消费Resolver输出并进入完整Movement/Particle/Facing安全链；边界使用完整Prepare/Validate后Final Apply；Source Codec v3进入可靠状态、late join与resync。StandardSources自主Evaluator、Demo五Controller稳定Diff、目标丢失Stop、临时压制精确恢复及同路径20/100/500服务端门均已有当前证据。真实StateTree业务Task不属于现行框架门。

[INFERRED][HIGH] P0–P5历史关闭结论继续成立；同路径20/100/500与第三方Source/并发Projectile同场组合门均已有当前证据。

[INFERRED][HIGH] 未来目标必须复用同一生产 Runtime、Networking 和 Presentation；Demo Round 协议只能成为生产协议的测试适配器，不能先维持一套测试协议、再另行替换成不兼容的生产协议。

## 1.2 历史源码只读审计矩阵（2026-07-22；非当前状态）

[COMPUTED][HIGH] 本节冻结2026-07-22审计快照，表内“缺失/后续归属”不得作为当前状态引用；当前状态以1.1节、`EntityBehaviorSourceArchitecture.md`和文件末尾日期更晚的检查点为准。

| 当前对象 | 当前职责 | 生产可复用 | Demo 专用 | 缺失/跑偏 | 后续归属 |
|---|---|---|---|---|---|
| Demo Round Bootstrap | [COMPUTED][HIGH] Server将排序后的固定Round初态以显式版本adapter写入Relevant Snapshot；复制bounded metadata并可靠multicast chunks，Client组装后构造本地packet进入既有Pipeline。 | [COMPUTED][HIGH] 直接复用`MassCrowdNetworking` Snapshot primitives。 | [COMPUTED][HIGH] Agent字段adapter、ServerTime与Round消费入口是Demo宿主职责。 | [COMPUTED][HIGH] 尚无late join重发及后续lifecycle delta。 | [INFERRED][HIGH] E新增通用Delta；Demo仍只做宿主adapter。 |
| `RoundPlanPacket` | [COMPUTED][HIGH] 描述 RoundId、持续时间、宽限、Scenario 规则与起始时间。 | [COMPUTED][HIGH] fixed-step 生效边界和 revision 思路可参考。 | [COMPUTED][HIGH] 是。 | [COMPUTED][HIGH] 固定 Round/Testcase 语义，不是 Behavior/Task Delta。 | [INFERRED][HIGH] 保留 Demo Director；生产行为进入公共 Behavior/Objective API。 |
| Correction/Checkpoint header 与 chunks | [COMPUTED][HIGH] 以 100-agent chunk 传输 Round correction/checkpoint，支持组装、重复/乱序处理和超时。 | [COMPUTED][HIGH] 分块与 assembly 机制部分可复用。 | [COMPUTED][HIGH] 类型和日志带 Round 语义。 | [COMPUTED][HIGH] 面向固定完整 Agent 集合，尚无 Relevancy/Lifecycle 通用合同。 | [INFERRED][HIGH] 抽取为 `MassCrowdNetworking` Correction primitives。 |
| readiness | [COMPUTED][HIGH] 验证客户端 AgentCount 与可见实例数后延迟 Round 开始，带超时 VIOLATION。 | [COMPUTED][HIGH] 测试宿主同步门可复用。 | [COMPUTED][HIGH] 是。 | [COMPUTED][HIGH] 不是生产网络相关集或 late-join ready 合同。 | [INFERRED][HIGH] Demo 验收层。 |
| `RoundResultHeader` | [COMPUTED][HIGH] 紧凑版本化 Round 验收汇总，最大序列化 2048 字节。 | [COMPUTED][HIGH] 版本化和 bounded header 思路可参考。 | [COMPUTED][HIGH] 是。 | [COMPUTED][HIGH] 含 T1–T8 指标，不是生产状态协议。 | [INFERRED][HIGH] Demo 结果层。 |
| Runtime Identity/Lifecycle fragments | [COMPUTED][HIGH] `FCrowdMassAgentFragment`保存迁移期 AgentId 及 ProviderId/StableEntityId/LifecycleSerial，并映射 Core StableEntityRef；Demo 另有 Id/VisualId/LifecycleSerial。 | [COMPUTED][HIGH] StableEntityRef POD与Runtime映射已实现；Lifecycle 预验证已用于 Movement Commit，F/G 已验证真实销毁、同槽位高 serial 复用与 stale 拒绝。 | [COMPUTED][HIGH] Demo 身份仍并存。 | [COMPUTED][HIGH] Round Movement 排序/提交仍使用 AgentId/Lifecycle，尚未迁移为 StableEntityRef 排序的统一产品 CommitPlan。 | [INFERRED][HIGH] P1 Runtime boundary。 |
| 当前完整 Agent 集合预验证 | [COMPUTED][HIGH] Movement/Finalize 在提交前验证完整 AgentId/Lifecycle/result 集合。 | [COMPUTED][HIGH] 原子整批提交门可复用。 | [COMPUTED][HIGH] 当前 expected set 来自固定 Round 集合。 | [COMPUTED][HIGH] 不支持动态 Relevant set 与增量 membership。 | [INFERRED][HIGH] Runtime boundary set + Networking Relevancy。 |
| T1 OpenSpawn/staging | [COMPUTED][HIGH] 所有 Mass 实体持续存在；testcase 只切换 Particle 参与状态并在 staging/active 布局间重置。 | [COMPUTED][HIGH] 压力传播和布局测试可复用。 | [COMPUTED][HIGH] 是。 | [COMPUTED][HIGH] 不是 spawn、despawn、死亡移除或槽位复用。 | [INFERRED][HIGH] 继续作为 fixture，禁止充当生命周期验收。 |
| Mass 实体真实 spawn/despawn 入口 | [COMPUTED][HIGH] `SpawnAgents`在场景启动时先销毁全部 tracked entities，再一次性创建固定数量；`DestroyTrackedAgents`做全量销毁。 | [COMPUTED][HIGH] 使用 `UMassSpawnerSubsystem` 的实际创建/销毁可作最小参考。 | [COMPUTED][HIGH] 固定场景启动/清理。 | [COMPUTED][HIGH] 无 fixed-step 增量 spawn/despawn、死亡移除、乱序 delta 或槽位复用 API。 | [INFERRED][HIGH] `MassCrowdRuntime` lifecycle。 |
| Target Region membership | [COMPUTED][HIGH] Demand/Plan 使用 Agent 列表与 MembershipHash，支持同一 Round 内计划重建和 claim 迁移。 | [COMPUTED][HIGH] Cohort 求解 kernel 与 membership hash 可复用。 | [COMPUTED][HIGH] 当前 cohort 由 Scenario/Capability profile 准备。 | [COMPUTED][HIGH] 没有网络 Membership Delta 或 fixed-step 通用加入/退出 API。 | [INFERRED][HIGH] Runtime Cohort + Networking Membership Delta。 |
| Combat `HitFact` | [COMPUTED][HIGH] Demo POD 含 EventId、目标 Agent/Lifecycle、伤害和击退/击飞事实，并执行去重与 Lifecycle 校验。 | [COMPUTED][HIGH] 业务事件形状和幂等规则部分可复用，且已有公共 StableEntityRef 可供后续引用。 | [COMPUTED][HIGH] 类型、状态和事务仍在 Demo。 | [COMPUTED][HIGH] 尚无插件 GameplayEvent 公共出口，现有 HitFact 尚未改用 StableEntityRef。 | [INFERRED][HIGH] 宿主 Combat 经公共 Gameplay Event 接口接入。 |
| Projectile visual events | [COMPUTED][HIGH] Demo multicast `Spawn/Impact/Expire`，客户端按 ProjectileId 去重并维护视觉实例。 | [COMPUTED][HIGH] 表现事件分类与幂等键可参考。 | [COMPUTED][HIGH] 是。 | [COMPUTED][HIGH] 未进入 `MassCrowdPresentation`，事件缺少通用 Lifecycle/EventId 合同。 | [INFERRED][HIGH] Presentation Event API。 |
| Client ISM/VAT 实例生命周期 | [COMPUTED][HIGH] 固定 Agent 集合按 `InstanceIndex`更新；数量不符时下一帧全量 rebuild。Projectile 视觉每帧清空并重建 active instances。 | [COMPUTED][HIGH] VAT playback、插值和 correction offset 已有 Demo 证据。 | [COMPUTED][HIGH] 资产与 processor 属于 Demo。 | [COMPUTED][HIGH] 无按生产 Spawn/Despawn Delta 的稳定增量实例创建、回收和槽位复用合同。 | [INFERRED][HIGH] `MassCrowdPresentation`。 |
| `MassCrowdNetworking` | [COMPUTED][HIGH] 已有通用Relevant Snapshot及StableEntityRef驱动的Spawn/Despawn/Membership batches；模块不包含Demo RPC或复制属性。 | [COMPUTED][HIGH] Snapshot primitives已由Demo adapter消费；Delta状态机提供严格序列、bounds、原子apply与membership hash。 | [COMPUTED][HIGH] 实际UFUNCTION/UPROPERTY发送包装仍在Demo Coordinator。 | [COMPUTED][HIGH] Delta网络包装、Correction/Event、Relevancy和late join调度缺失。 | [INFERRED][HIGH] `MassCrowdNetworking`。 |
| Nav Surface Graph / Shared Flow | [COMPUTED][HIGH] Core保存稳定分层节点、多边形、邻接宽度/坡度/整数成本并构建layer-aware integration/direction；Runtime从静态Recast tile/poly/portal提取图。 | [COMPUTED][HIGH] Core图和Flow不含World、Actor、Demo或地图路径；Runtime提取器仅依赖NavigationSystem/Recast运行数据。 | [COMPUTED][HIGH] J Coordinator直接拥有Graph、按目标Flow cache、marker、移动采样和验收指标。 | [COMPUTED][HIGH] J虽已组合G/H/Presentation，但公共Runtime尚无Graph生命周期、provider、revision、cache、预算或handle；动态NavMesh重建不在本轮合同内。 | [INFERRED][HIGH] P2 Runtime Nav资源。 |
| `MassCrowdPresentation` | [COMPUTED][HIGH] 当前只有 `IModuleInterface` 模块壳。 | [COMPUTED][HIGH] 无运行实现可复用。 | [COMPUTED][HIGH] 实际 ISM/VAT/Projectile visual 全部在 Demo。 | [COMPUTED][HIGH] 插件 Presentation API 与实例生命周期缺失。 | [INFERRED][HIGH] `MassCrowdPresentation`。 |
| Plugin Source 的 Enemy/Friendly/Faction 特判 | [COMPUTED][HIGH] 指定插件 Source 中未检出这些阵营分支。 | [COMPUTED][HIGH] 当前运动 kernel 保持 provider-neutral。 | [COMPUTED][HIGH] 否。 | [INFERRED][HIGH] 未来仍必须通过 Faction 只做关系/过滤的合同防止回流。 | [INFERRED][HIGH] Core 公共事实 + 宿主关系策略。 |
| Demo testcase 替代产品生命周期路径 | [COMPUTED][HIGH] T1 active/inactive 与 boundary layout reset、Round 全量重置会改变测试参与状态或位置，但不创建/销毁对应 Agent。 | [COMPUTED][HIGH] 仅测试诊断可复用。 | [COMPUTED][HIGH] 是。 | [COMPUTED][HIGH] 若称为动态生命周期即属错误。 | [INFERRED][HIGH] 保留 fixture，并新增独立 production lifecycle 测试。 |

[COMPUTED][HIGH] 历史P0审计快照：当时Core/Runtime运动链、AgentFacts、Snapshot/lifecycle和20实体Sandbox已实现，而late join、Presentation及J公共消费尚未完成；这些缺口后来已由P3–P5关闭。Hit/VAT和部分Target诊断继续属于Demo宿主语义。

## 1.3 P0 产品化闭环审计（2026-07-23）

| 审计问题 | 当前源码事实 | P0 决定 |
|---|---|---|
| canonical gather 后仍读哪些事实 | [COMPUTED][HIGH] `BoundaryGather`一次读取Runtime/运动/Formation/Facing及Combat业务事实；Combat prepare消费不可变business overlay。Facing prepare只做完整集合只读验证，最终`ApplyPreparedCommit`先复核目标集合再执行唯一运动/视觉写回遍历。FlowSample诊断遍历仍保留。 | [INFERRED][HIGH] P1继续把剩余Target/Particle诊断变成prepared patch；允许独立完整集合验证遍历。 |
| WORK 调度 | [COMPUTED][HIGH] Round即时`Async/Future.Get`为0；SoftPressure每个boundary一次Orchestrator Dispatch和一次completion-event Wait，真实worker链覆盖SharedFlow、TargetTopology、TargetDemand、TargetPlan、TargetGuidance、Movement、Particle与FacingFinalize/MovementFinalize，且Movement直接消费worker SharedFlow/Target结果。GT仍保留SharedFlow/Target同步副本，Business仍是屏障，Obstacle仍是旧同步Movement路径。 | [COMPUTED][HIGH] 该行右侧原P1“维持单次Dispatch/Wait”目标已由2026-07-30 AB架构取代；现行目标是单槽Mailbox和普通Tick零Wait。 |
| Nav所有权 | [COMPUTED][HIGH] Core持有Graph/Flow数据与kernel，Runtime只有静态Recast提取器；Round Pipeline和J Coordinator分别持有资源或cache，插件没有World级生命周期所有者。 | [INFERRED][HIGH] P2由Runtime subsystem统一持有静态cooked Recast派生Graph及Flow cache。 |
| J绕过公共层 | [COMPUTED][HIGH] J直接管理LifecycleWorld、Graph/Flow cache、O(N)安全检查、全量状态Multicast和ISM实例。 | [INFERRED][HIGH] P5前这些只能视为Demo验收组合，不能视为插件产品路径闭环。 |
| 物流事实归属 | [COMPUTED][HIGH] 当前Runtime只有通用Behavior/BusinessCommitRequest，Cargo carrier ledger与业务规则位于Demo Adapter。 | [INFERRED][HIGH] P4采用混合边界：Runtime公开Task/Cargo/Inventory POD与事务合同，库存权威、Planner和仓库策略留在宿主Adapter。 |
| late join与相关集 | [COMPUTED][HIGH] Snapshot和lifecycle batch primitives已存在，但没有per-client baseline调度、空间相关集、恢复序列或插件RPC channel。 | [INFERRED][HIGH] P3最小闭环必须包含owner-only client channel、Snapshot→Delta恢复序列、空间网格provider和关系闭包。 |
| Demo兼容镜像 | [COMPUTED][HIGH] Round专用状态仍被rollback、指标、结果和资产适配消费；J完整状态包、直接Flow cache和直接ISM只有J消费者。 | [INFERRED][HIGH] P1/P5只删除迁移后无消费者的镜像；Round观察/控制事实可保留，但不得继续拥有独立运动或实体状态权威。 |

[INFERRED][HIGH] GT/WORK完成定义冻结为“一次canonical gather、不可变基础Snapshot、显式POD overlays、依赖图WORK、稳定merge、完整集合预验证、唯一GT原子最终写回”；独立验证遍历是fail-closed合同的一部分。

[INFERRED][HIGH] Nav V1冻结为仅消费cooked静态Recast topology；允许Objective重新attachment与Flow重建，不实现运行时动态NavMesh topology更新。

[INFERRED][HIGH] Networking/Presentation产品闭环必须包含bounded durable delta、unreliable latest-wins correction、late-join baseline、动态相关集接口、StableEntityRef实例生命周期和Cargo视觉；Demo全量状态包或直接ISM不能替代这些公共能力。

## 2. 正式场景与当前生产链

[COMPUTED][HIGH] 顶层场景只保留`SimRoundObstacle=0`与`SimRoundSoftPressure=1`；T1–T8是SoftPressure下的能力测试，不是八套独立模拟架构。

```text
RoundPlanApply
→ Business / Target Fact Prepare
→ SharedFlowFieldBuild
→ Flow Guidance Candidate
→ [可选] Target Topology / Demand / Plan / Guidance Candidate
→ [可选] Business Guidance Candidate
→ Guidance Compose
→ Local Predictive
→ MovementPredict
→ Particle Safety / SF1 Obstacle Constraint
→ Facing
→ MovementFinalize
→ Authority Commit / Client Prediction Commit
```

[COMPUTED][HIGH] Guidance provider固定为`BusinessOverride > TargetRegion > SharedFlow > Stop`；只有`Guidance Compose`写最终宏观自主速度，不再依靠processor先后覆盖同一Intent。

[COMPUTED][HIGH] Local Predictive只消费Composed Guidance并输出局部可执行速度；Particle只处理预测位置的Soft/Hard/Swept/Obstacle/Bounds约束；Facing在稳定前只消费自主速度，不读取局部避让或Particle修正向量。

[COMPUTED][HIGH] `MovementFinalize`是普通fixed-step中`FCrowdDemoRoundSimStateFragment`的唯一写入点；它先由Runtime WORK构建全局稳定Commit plan并预验证完整AgentId/Lifecycle集合，再在同一GT boundary同步发布Runtime movement/state与Demo checkpoint兼容镜像。PlanApply与correction只在boundary恢复或应用状态。

## 3. GT / WORK与数据所有权

[COMPUTED][HIGH] Coordinator只负责RoundPlan场景控制、公共channel的版本化RoundResultHeader/correction适配、readiness和紧凑汇总，不承载Flow、Transport、Local Predictive或Particle算法。

[COMPUTED][HIGH] Guidance Compose、Local Predictive、MovementPredict、Particle与Facing已经使用不可变POD输入在WORK线程执行；GT等待完成并验证完整AgentId/result集合后统一写回对应Mass fragments。Compose、Local Predictive、Particle与Facing只调用Core纯kernel，MovementPredict是Runtime的确定性POD积分阶段。

[COMPUTED][HIGH] `MassCrowdRuntime`现已建立`UMassCrowdMovementTrait`、Agent identity、simulation state、properties、facing 与 movement output 持久 fragments，以及按 CapabilityProfile 稳定分批的 Gather、全局 AgentId 稳定 Merge、Lifecycle 全量预验证和 Commit 映射。Guidance、local velocity 与 Particle 等中间结果已经改由 prepared POD 传递，不再作为持久 Runtime fragments。

[COMPUTED][HIGH] 每个fixed-step在Plan/correction边界处理完成后先构建Runtime基础snapshot；Shared Flow、Target Region与Business分别发布按AgentId排序的不可变Guidance overlay。`FCrowdMassRuntimeBridge::BuildGuidanceRecords`把基础snapshot与三类overlay稳定合并为Compose WORK记录，要求Shared Flow覆盖全部Agent，并拒绝重复provider/Agent、错误revision或错误provider。WORK完成且全量AgentId/result集合验证通过后，GT一次更新Runtime composed与同源prepared SoA；旧Demo composed与MoveIntent已删除。Runtime结果、overlay与基础snapshot都是可重建派生状态，correction rollback后从恢复的权威事实重新生成，不复制进rollback snapshot。

[COMPUTED][HIGH] Local Predictive不再重新读取Runtime identity/state/properties/composed fragments来构建WORK输入；它直接消费同一boundary基础snapshot与prepared Core composed结果。Runtime WORK调用Core kernel并输出pair、grant、result、summary和可选trace。Mass查询只保留完整结果集合验证与Runtime local-velocity发布；Demo Pipeline继续保存grant、diagnostic和rollback状态，旧Demo local-velocity fragment已删除。

[COMPUTED][HIGH] MovementPredict直接消费boundary snapshot、prepared composed与prepared local-velocity结果；仅通过Mass查询叠加T1 boundary freeze和业务垂直运动事实，随后由`FCrowdMassMovementPredictWork`稳定排序、选择自主/局部速度、限制局部速度并积分预测位置。其prepared预测结果是Particle的正式输入，不再由Particle重新读取Runtime identity/properties镜像拼装基础事实。

[COMPUTED][HIGH] Particle Safety从同一boundary snapshot与prepared MovementPredict结果构造Core agent；`FCrowdMassParticleWork`执行Core Solve和applied-state安全复验，输出candidate/applied summary、hash、pair、result及可选trace。GT保留诊断、路线/稳定指标和T1状态采集，只发布Runtime particle与同源prepared结果；旧Demo particle fragment已删除。

[COMPUTED][HIGH] Facing现由`FCrowdMassFacingWork`在WORK线程调用Core kernel；它消费boundary snapshot、prepared composed与prepared particle结果。GT只从Facing fragment读取上一boundary settled计数，完整校验结果集合后同步发布Runtime Facing与Demo Facing兼容镜像；Movement Finalize只消费Runtime Facing结果。

[COMPUTED][HIGH] Shared Flow生产构建和Preferred生成现由`FCrowdMassSharedFlowWork`在WORK线程调用Core kernel。权威对象是Runtime定义的不可变Flow resource、动态anchor及T3双cohort resource，当前实例仍由Demo Pipeline迁移期托管；Demo `FCrowdDemoSharedFlowField`只作为Transport、末态指标和rollback迁移期结构镜像，不再执行生产Build或运动Guidance采样。GT准备Config、Target位置和每Agent停止/Goal事实，WORK稳定排序后输出Flow sample与SharedFlow candidate，GT在完整AgentId集合校验后发布Runtime candidate和Demo FlowSample阶段事实，不再发布Demo GuidanceCandidates。

[COMPUTED][HIGH] Target Region生产Topology、Demand、短期Plan、quota/claim execution、validation与Guidance现由`FCrowdMassTargetRegionWork`在线程池调用Core kernel。Target Demand已从统一基础snapshot和prepared Flow输出构建Agent输入，不再重复读取Mass；其余Demo processor仍负责场景规则准备、等待WORK、验证结果并发布兼容镜像。Demo Target Region kernel已退出这些生产调用。Plan与quota execution作为同一份状态进出WORK，转换后的Demo镜像继续服务Round指标、生命周期诊断、资源引用rollback及客户端调试绘制，当前尚未把这些宿主职责迁入Runtime subsystem。

[COMPUTED][HIGH] Runtime movement output已经接入正式Finalize与Commit：Particle或SF1 Obstacle阶段发布prepared final kinematics，Facing发布prepared yaw；`FCrowdMassMovementFinalizeWork::BuildInputFromPrepared`将这些结果与boundary snapshot稳定组装，再按CapabilityProfileKey分批生成Movement并全局按AgentId稳定Merge。GT不再为Finalize输入执行第一遍全实体Mass Gather，但仍在任何写入前对完整AgentId/Lifecycle及Runtime Facing/Particle或SF1 Obstacle结果执行原子预验证，随后同步更新Runtime simulation/movement与Demo RoundSim最终checkpoint状态。Authority/Client Commit均从Runtime MovementOutput写Transform、Velocity和Demo Movement，并校验其与RoundSim状态一致。

[COMPUTED][HIGH] Demo仍持有`MovementFinalize` processor外壳和Round指标/rollback采集，因此这不是整个Pipeline移出Demo；但最终运动事实已经只由Runtime Commit plan产生，Demo不再独立重算另一份最终Movement。

[COMPUTED][HIGH] 第十二切片将Compose、Local Predictive与MovementPredict合并为`FCrowdMassMovementPipelineWork`。GT从同一boundary snapshot、三类Guidance overlay、Local公平历史、T1 boundary fact和Reactive垂直运动事实构建一份按AgentId排序的不可变输入；一个ThreadPool任务内部顺序调用原有三个Runtime纯阶段；完整结果集验证通过后，GT在一次Mass查询中同步发布Runtime identity/state/properties/candidates/composed/local与Demo ProposedMovement。三个旧processor实现已物理删除，不存在逐阶段`Async→Get→GT发布→再Gather`链。

[COMPUTED][HIGH] 合并WORK仍保留三个独立阶段hash与诊断语义；性能归类把任务内部Compose耗时记入GuidanceCompose，其余GT准备、Local Predictive与MovementPredict记入LocalPredictive。计时值不进入确定性hash。

[COMPUTED][HIGH] 当前尚未达到“整个boundary只读取Mass一次”：基础运动事实及Compose→Local Predictive→MovementPredict→Particle/Obstacle→Facing→MovementFinalize的WORK输入链已收敛到同一snapshot/prepared链，但Business状态准备、T1/诊断/累计器读取、原子镜像预验证、阶段兼容写回及最终业务采集仍由分阶段GT processors处理；最终Mass archetype也尚未按能力拆分。因此不能宣称完整GT/WORK迁移完成。

[COMPUTED][HIGH] Rollback snapshot已停止复制Target topology、demand、guidance和短期plan大数组；短期plan以资源key引用不可变资源，snapshot只保存quota/claim等可变执行态、累计器和游标。恢复后按Target Fact重建派生结果。

[COMPUTED][HIGH] Server与Client运行同一fixed-step driver和纯C++ kernels；客户端visual只读取client RoundSim/visual state并提交ISM，不计算gameplay movement。

## 4. 已删除的当前兼容面

[COMPUTED][HIGH] TargetApproach、TargetSlotLayout、旧Polar Density及其execution diagnostic已从settings、fragment、kernel、processor、rollback、metrics、CLI与自动化中删除；Target Fact已提取为独立纯kernel。

[COMPUTED][HIGH] RoundResultHeader使用contract v2版本化NetSerialize；仅Server本地消费的Performance汇总不再进入复制payload，AgentState仍只经100-agent checkpoint chunks传输。高熵异构自动化为1566字节，8790真实T6M运行时为1970字节，均低于2048字节硬门且无Native NetSerialize Warning。

## 5. 当前验证事实

[COMPUTED][HIGH] 第十二切片通过Development、DebugGame Editor（均`-DisableUnity`）、`CrowdDemo` 105/105与`MassCrowd` 13/13。新增`MassCrowd.Runtime.MovementPipelineWork`证明合并前后三个阶段hash相同、输入反序不变且重复overlay被拒绝。8723 T2、8724异构T6、8725 T1和8726 T8均通过双端安全、同步与性能门；fixed-step p95分别为`3.849/5.337/1.649/2.131ms`，agents/visible均为`20/20`，correction位置/速度/Yaw误差均为0。

[COMPUTED][HIGH] 2026-07-22：Development、DebugGame Editor（均使用`-DisableUnity`）、`git diff --check`、当前105/105项`CrowdDemo`自动化及13/13项`MassCrowd`插件自动化通过。Runtime测试覆盖Target Region四阶段WORK、统一boundary输入、合并Movement Pipeline、MovementPredict语义、输入反序、重复Agent/overlay拒绝及旧/Core/Runtime等价。

[COMPUTED][HIGH] 8663 T2生产回归通过Runtime Finalize/Commit链：handoff/inside/terminal=`20/20/20`、Region coverage=`16/16`、安全与invalid/fallback为0、双端candidate/applied hash匹配、correction位置/速度/Yaw误差均为0；fixed-step p95=`3.529ms`、Commit p95=`0.021ms`。8664 SF1 Single authority路径运行未出现VIOLATION；该短运行未覆盖完整RoundResult。

[COMPUTED][HIGH] Facing生产迁移后的8665 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`、双端Yaw误差为0；fixed-step/Commit p95=`3.638/0.020ms`。8666 SF1 Single无Particle路径也完成Facing→Finalize→Authority Commit且无VIOLATION。

[COMPUTED][HIGH] Shared Flow生产迁移后的8667 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`、Flow/Transport双端hash一致、Hard/Swept/Obstacle/Bounds与invalid/fallback为0；fixed-step/Flow p95=`3.166/0.264ms`。8668 SF1 Single authority烟雾确认build hash=`267519150`、rebuild=`1`且无Fatal/Assertion/Ensure/`LogWindows: Error`/VIOLATION；该短运行没有完成整轮路线验收。

[COMPUTED][HIGH] Target Region Runtime接管后的8669 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`、plan/guidance unrouted及validation failure为0，五类Target Region hash双端一致；fixed-step/Topology/Demand/Plan/Guidance p95=`4.061/0.012/0.198/1.252/0.221ms`。8671异构T6 Static覆盖7个Capability cohort，inside-band=`20`、feasible coverage=`20`、unrouted/invalid/validation failure=`0`，五类hash双端一致，fixed-step p95=`6.552ms`。两次运行均为20/20可见、安全违规0且性能门通过；correction使用零误差快速路径，未实际触发历史重放。

[COMPUTED][HIGH] Guidance overlay与Local Predictive统一输入第二切片通过Development、DebugGame、`MassCrowd` 12/12及`CrowdDemo` 102/102。8677 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`，Compose/Local Predictive双端hash一致，fixed-step p95=`3.854ms`；8678异构T6保持inside/coverage=`20/20`、unrouted/invalid/validation failure=0、五类Transport及Compose/Local Predictive hash一致，fixed-step p95=`5.265ms`。两次运行安全、同步、可见实例与性能门通过。

[COMPUTED][HIGH] MovementPredict、Particle与Facing统一输入第三切片通过Development、DebugGame、`MassCrowd` 12/12及`CrowdDemo` 102/102。8681 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`，安全与双端hash通过，fixed-step p95=`5.379ms`；8682异构T6保持inside/coverage=`20/20`、安全、同步和五类Transport hash通过，fixed-step p95=`6.598ms`。8683 SF1 smoke保持Flow hash=`267519150`、rebuild=`1`且无硬错误；该短运行未完成整轮。

[COMPUTED][HIGH] MovementFinalize统一输入第四切片通过Development、DebugGame、`MassCrowd` 12/12及`CrowdDemo` 102/102。8684 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`，fixed-step p95=`4.393ms`；8685异构T6保持inside/coverage=`20/20`，fixed-step p95=`5.438ms`。两者安全、同步、prepared/兼容镜像原子门与性能门通过。8686 SF1 smoke保持Flow hash=`267519150`、rebuild=`1`且prepared Obstacle→Finalize路径无硬错误；短运行未完成整轮。

[COMPUTED][HIGH] MovementFinalize查询职责第五切片已将写前原子一致性检查与提交后业务/指标采集拆为`ValidationQuery`和`ApplyMetricsQuery`。前者只读取身份及Demo/Runtime的Particle、Facing、Obstacle镜像；后者不再读取已由prepared链替代的MoveIntent、Runtime properties、Runtime Particle和Runtime Facing。两段仍在同一processor和同一fixed-step boundary内执行，写前完整集合验证与失败时零部分写入语义保持不变。Development、DebugGame、`MassCrowd` 12/12及`CrowdDemo` 102/102通过；8687 T2保持terminal=`20/20`、coverage=`16/16`，fixed-step p95=`4.149ms`；8688异构T6保持inside/coverage=`20/20`，fixed-step p95=`5.683ms`。8689 SF1 smoke保持Flow hash=`267519150`、rebuild=`1`且无硬错误。

[COMPUTED][HIGH] 第六切片已把第五切片的`ApplyMetricsQuery`彻底拆成`MovementFinalize::ApplyQuery`与独立`PostFinalizeMetricsProcessor`。Finalize现在只执行prepared Commit plan的Demo/Runtime状态原子写入；post-finalize只读最终状态并采集Flow路线、T1/T3/T4进度、SoftPressure rollback和Combat snapshot，且仍位于VisualResolve之前，因此采样语义未改变。Pipeline为每个boundary记录Finalize成功step；post-finalize和Authority/Client Commit均以该标记为门，Finalize失败时不得对旧状态生成新snapshot或提交旧Movement。该职责拆分增加一次20实体只读查询，尚不是最终单次Mass读取实现。

[COMPUTED][HIGH] 第六切片通过Development、DebugGame、`MassCrowd` 12/12和`CrowdDemo` 102/102。8693 T2保持terminal=`20/20`、coverage=`16/16`，fixed-step p95=`4.074ms`，client Game/Render/GPU p95=`2.790/4.963/4.593ms`；8694异构T6保持completed/settled/inside-band/coverage=`20/20`，fixed-step p95=`5.073ms`，client Game/Render/GPU p95=`3.451/5.068/4.712ms`。8695 SF1 smoke保持Flow hash=`267519150`、rebuild=`1`且无硬错误。

[COMPUTED][HIGH] 第七切片继续收缩`PostFinalizeMetricsProcessor`查询：FormationIndex与checkpoint `RadiusCm`由boundary formation facts提供，Composed Guidance由prepared Runtime结果转换；post-finalize不再读取Formation、Composed Guidance、Particle Properties或未使用的Particle Constraint fragment。`RadiusCm`与Particle `PhysicalRadiusCm`保持不同语义，禁止互相替代。

[COMPUTED][HIGH] 首次8697异构T6回归因误把Particle `PhysicalRadiusCm`写入rollback checkpoint的`RadiusCm`，从第一帧correction起出现agent mismatch；该实现已修正为保存精确Formation radius。8698随后达到rollback hit/miss/mismatch=`80/0/0`、inside/coverage=`20/20`、fixed-step p95=`6.540ms`且双端无硬错误。8699 T2保持terminal/inside=`20/20`、coverage=`16/16`、rollback=`54/0/0`、fixed-step p95=`4.322ms`；8702 SF1保持Flow hash=`267519150`、rebuild=`1`。

[COMPUTED][HIGH] 第八切片又删除post-finalize对FlowSample与ObstacleConstraint fragment的读取。rollback FlowSample从同一prepared Runtime Shared Flow输出重建；SF1 penetration按原定义从boundary起点与Finalize终点复验膨胀障碍。8703异构T6 rollback=`80/0/0`、inside/coverage=`20/20`、fixed-step p95=`4.595ms`；8704 SF1保持Flow hash=`267519150`、rebuild=`1`且无硬错误。

[COMPUTED][HIGH] 第九切片删除post-finalize对GuidanceCandidates与Facing fragment的读取。完整rollback GuidanceCandidates由boundary snapshot和Flow/Target/Business prepared overlay通过Runtime Bridge重新构建；Facing processor在发布Runtime结果时同步保存包含连续settle计数与最终资格的精确rollback fact，post-finalize只按AgentId消费该prepared事实。Development、DebugGame、`MassCrowd` 12/12与`CrowdDemo` 102/102通过；8705异构T6 rollback hit/miss/mismatch=`80/0/0`、inside/coverage=`20/20`、fixed-step p95=`4.551ms`且双端无硬错误；8706 SF1保持Flow hash=`267519150`、rebuild=`1`且无硬错误。

[COMPUTED][HIGH] 第十切片把T1 OpenSpawn状态收敛为PipelineSubsystem中的唯一运行时权威状态，并为每个boundary生成按AgentId稳定排序的prepared事实；MovementPredict、Particle和客户端视觉只消费该事实或唯一runtime，已物理删除`FCrowdDemoOpenSpawnRelaxationFragment`。pending reset在完整集合验证后原子消费，重复、缺失、错位或过期事实均拒绝。

[COMPUTED][HIGH] Combat/Visual rollback现在采用两阶段完成门：PostFinalize只记录Identity与最终RoundSim movement facts；VisualStateResolve完成最终Health、BusinessState、AttackPhase、Reactive、HitFlash和VisualState事实。snapshot只有在`MovementFactsComplete && CombatFactsComplete`后才进入`SnapshotReadyForReplay`，不完整snapshot不得用于correction replay或checkpoint发布。

[COMPUTED][HIGH] `PostFinalizeMetricsProcessor`当前Mass requirements仅为`FCrowdDemoMassIdentityFragment`与只读`FCrowdDemoRoundSimStateFragment`；结构自动化同时禁止OpenSpawn及六个Combat/Visual fragment回流。Development与DebugGame Editor（`-DisableUnity`）、T1 4/4、Combat 15/15、结构1/1、`MassCrowd` 12/12及完整`CrowdDemo` 105/105通过。

[COMPUTED][HIGH] 默认Unity Development仍在未由本切片修改的`MassCrowdSimulation`插件旧`.cpp`中因匿名命名空间辅助函数重名失败；当前验证使用`-DisableUnity`。该构建兼容性债务未被运行回归掩盖，也不应写成第十切片行为回退。

[COMPUTED][HIGH] 第十切片双端运行：8707 T1保持六阶段、传播与settling合同，Particle安全违规和invalid/fallback均为0，rollback hit/miss/mismatch=`53/0/0`，agents/visible=`20/20`，fixed-step p95=`1.851ms`；8709异构T6保持inside/coverage=`20/20`、rollback=`53/0/0`及五类Transport hash一致，fixed-step p95=`6.334ms`；8708 T7与8710 T8均无安全、同步或snapshot完整性错误，T8 attack/projectile/event hash双端一致，fixed-step p95分别为`2.559/2.215ms`。8714 SF1短时smoke保持Flow hash=`267519150`、rebuild=`1`；该smoke未完成整轮。

[COMPUTED][HIGH] 第十一切片物理删除六个已被Runtime权威状态取代的Demo迁移镜像：`RoundMoveIntent`、`RoundGuidanceCandidates`、`RoundComposedGuidance`、`RoundLocalVelocity`、`RoundParticleConstraint`与`RoundFacing` fragments。Mass template、spawn初始化、processor requirements/写入、派生rollback副本及旧`BuildGatherRecord()`适配入口均同步删除；Facing连续settle状态改由`FCrowdMassFacingFragment`持有并由rollback显式恢复。MovementFinalize直接验证Runtime Facing/Particle，不再做Runtime↔Demo派生镜像一致性检查。

[COMPUTED][HIGH] 本切片没有删除`RoundProposedMovement`、`RoundFlowSample`或`RoundObstacleConstraint`：三者分别承载MovementPredict阶段传输/诊断、Transport与路线/rollback事实、SF1正式障碍安全结果，不是Runtime结构的逐字段重复。`RoundSimState`仍是checkpoint、网络结果和Demo指标所需的最终提交状态，也不是待删迁移镜像。

[COMPUTED][HIGH] 第十一切片通过Development与DebugGame Editor（`-DisableUnity`）、结构删除与Runtime适配器等价测试、`CrowdDemo` 105/105及`MassCrowd` 12/12。8722 T2保持terminal/inside=`20/20`、coverage=`16/16`、安全与双端hash通过，fixed-step/client frame p95=`4.028/5.248ms`；8717异构T6保持completed/settled=`20/20`、七类Capability及Transport/T6 hash一致，fixed-step/client frame p95=`4.556/5.481ms`；8718 T1与8719 T8分别为`1.641/2.025ms`，T8三类业务hash双端一致。8721 SF1 smoke保持Flow hash=`267519150`、rebuild=`1`，该短运行未完成整轮。

[COMPUTED][HIGH] 收敛后20实体fixed-step p95：T1=`1.131ms`、T2=`3.073ms`、T3=`2.938ms`、T4=`3.376ms`、T5S=`5.362ms`、T6A=`3.114ms`、T6S=`4.261ms`、T7热复跑=`1.739ms`、T8=`1.598ms`；这些通过运行的realtime均不低于1.000，step-limit hit为0。

[COMPUTED][HIGH] T1的staging/active测试布局切换现已显式归类：普通`non_correction_discontinuity=0`，测试boundary reset jump=21，Round reset jump=20；不得把测试夹具换布局混入普通移动连续性失败。

[COMPUTED][HIGH] 客户端性能窗口现从Round 1实际激活开始，并分别记录Game、Render、GPU、shader compile、async loading、visual asset compiling与PSO precache；启动热身单独统计，不再与Round 1混算。

## P1–P5 产品化执行检查点（2026-07-23）

[COMPUTED][HIGH] `FCrowdMassBoundaryOrchestrator`提供依赖任务键、不可变prepared patch、事务状态、稳定CommitPlan hash及Gather/Queue/Work/Wait/Merge/Validate/Commit计时；Runtime record使用`FCrowdStableEntityRef`与`FCrowdAgentFacts`。旧Round已消费Orchestrator且即时`Async/Future.Get`为0。

[COMPUTED][HIGH] 2026-07-23历史P1关闭：canonical gather包含完整Combat业务事实；Business/Combat、SharedFlow、按Cohort拆分的Target四段、Obstacle、Movement、Particle与Facing均进入一次Dispatch/Wait的typed Worker DAG，唯一GT writer在完整CommitEnvelope验证后提交。源码无`Async/TFuture/Future.Get`；8132 T2、8137 T6、8138 T7、8139 T8双端门通过。该关闭仅保留为typed DAG与原子提交证据；一次Wait不再属于最终合同。

[COMPUTED][HIGH] `UMassCrowdRuntimeSubsystem`按World拥有Nav Graph与Flow cache；8122 `NavFlowProductSmall`真实验证98节点、234有向边、4层、2个有引用Flow资源、cache hit=1、9504字节，并与20实体P1 boundary同时运行。P2已关闭。

[COMPUTED][HIGH] `AMassCrowdReplicationActor`为每个PlayerController提供owner-only baseline/state/correction通道；客户端冲突、缺序列、损坏或超限后fail-closed，请求服务端销毁并重建channel以发布新baseline。`UMassCrowdPresentationSubsystem`按StableEntityRef管理Profile slot、swap-remove反向表、视觉状态和Cargo引用。

[COMPUTED][HIGH] J已删除完整MixedState multicast、私有visual maps与直接ISM调用；ContinuousLifecycle已删除可靠lifecycle multicast。两者均通过公共channel与Presentation运行，生命周期唯一同步写者固定到`TG_PostUpdateWork`，避免Mass processing期间调用同步Create/Destroy。

[COMPUTED][HIGH] J的O(N)`IsMoveSafe`已删除并改用公共`FCrowdSpatialSafetyIndex`；8153 step600双端保持active/visible=`20/20`、最小间距=`71.51cm`及既有业务指标。旧Round correction/bootstrap/ResultHeader/projectile已使用公共owner-only channel，实体视觉使用公共Presentation；P5已关闭。

[COMPUTED][HIGH] P4新增`FCrowdLogisticsTransactionStore`和专用`CrowdDemo_FriendlyLogisticsSmall`地图。8154延迟客户端从公共baseline/reliable channel恢复最终状态；20实体、40总量/5交付、竞争、幂等、死亡后cargo恢复、fallback sink、两次不可达退避及取消通过，双端hash=`3180435972084878253`。Cargo attach/detach=`2/2`，携货与交付近景证据已保存。

[COMPUTED][HIGH] P0–P5当时的累计门：Development/DebugGame Editor `-DisableUnity`通过，`MassCrowd`40/40、`CrowdDemo`115/115；8151 Round、8153 J、8154 P4、8156 NavFlow与8157双客户端late join均通过且零硬错误。该历史检查点当时停止在K前；当前B0–B7与K状态以本文1.1节、`EntityBehaviorSourceArchitecture.md`和`PhasePlan.md`为准。

[COMPUTED][HIGH] T7首次冷运行8777出现client frame p95=`112.235ms`、collapsed steps p95=`4`；新增证据后的两次普通运行8781/8783连续通过，frame p95=`6.016/5.820ms`，Round内shader/loading/PSO帧均为0。`-noshaderddc`控制运行8782因shader job超过60秒而未进入场景，只证明冷资源门可以阻塞ready，不能事后归因为8777的唯一根因。

[COMPUTED][HIGH] T5M 8785通过安全、同步、Transport与性能门：fixed-step p95=`6.196ms`，稳定诊断`valid=1`且无merge block/chatter；移动目标窗口`settled_windows=0`、相对位置peak-to-peak p95=`125.431cm`，所以它是稳定追随技术通过，不是静止落位结论。

[COMPUTED][HIGH] 8788把T6M诊断总窗口增加到60秒后，最终inside/coverage仍为`16/20`；因此“只因45秒不足”已被反驳。8789生命周期诊断记录1591次重建，所有634个具备新Plan继承资格的claim均完成迁移，`dropped_still_feasible=0`；主要变化来自真实环境图变化与path/execution invalid，不是claim迁移丢失。

[COMPUTED][HIGH] 8788/8789进一步确认`guidance_mode=3`为显式`EngagedHold`而非Terminal guidance意外清零。旧实现把Hold固定为世界坐标零速，强于“目标靠近时不主动后退”的设计。8790改为通用单向Hold：抑制目标向实体靠近的径向分量，保留切向运动及目标远离时的跟随分量。

[COMPUTED][HIGH] 8790原P0 T6M的Round末inside/coverage恢复为`20/20`，Hard/Swept/Obstacle/Bounds、invalid/fallback、双端hash、correction均通过；fixed-step p95=`12.137ms`，client Game/Render/GPU p95=`10.332/6.852/5.802ms`。最后90步诊断仍记录terminal population最低`18/20`、Region coverage最低`17/20`、particle settled window=0；这些值保留为移动目标过程诊断，不再作为AcquireThenHold实体的持续重排硬门。

[COMPUTED][HIGH] 用户确认的T6M终态合同为：实体取得正确Terminal并进入AcquireThenHold后，只要交互资格仍有效就不主动换Region；目标靠近时不主动后退，切向运动与目标远离跟随仍可执行。当前Demo的资格失效条件为Region变成Supply、策略不再是AcquireThenHold，或距离超过`Maximum + 100cm`释放滞回；目标Actor销毁/业务Target丢失尚未形成Demo运行场景，不能写成已验收。

## 6. 准确停止点

[COMPUTED][HIGH] 本节按实施时间保留历史切片；下文各切片中的“当前/下一步”只描述当时阶段门，不覆盖本文件1.3节和本节最后一条记录的现行P0/P1停止点。

[COMPUTED][HIGH] 可复用产品边界现由`Docs/MassCrowdSimulationPluginArchitecture.md`定义。阶段1插件骨架与阶段2纯Core迁移已完成；阶段3已把Shared Flow、Target Region、Guidance Compose、Local Predictive、Particle Safety、Facing以及最终Movement组装/稳定Merge/Commit切到Runtime WORK。各阶段Demo processor外壳、Round指标、诊断及rollback协调仍属于Demo宿主。

[COMPUTED][HIGH] 单boundary Gather前二十一切片已接入：`TryBeginFixedStep()`成功后由专用GT processor一次读取身份、FormationIndex、Formation Radius、RoundSim位置/速度/Yaw、Movement速度上限和Particle半径/间隔/Mobility，构建Runtime `FCrowdMassBoundarySnapshot`及最小Demo formation facts。Flow、Target与Business发布Guidance overlay；Runtime稳定合并后，Compose、Local Predictive、MovementPredict、Particle/Obstacle、Facing与MovementFinalize从snapshot/prepared链构建WORK输入，不再为这些基础事实重复读取Mass fragments。FacingFinalize的原子写回同时提交最终运动与当前boundary最新的Combat/Visual状态并捕获最终记录；PostFinalize与CheckpointPublisher只消费prepared事实。

[COMPUTED][HIGH] 该snapshot只保存boundary开始时的基础运动事实；Target Fact、Combat、Business override、Transport plan/guidance、Local Predictive与Particle等依赖前序阶段的派生事实仍按原processor顺序产生。correction恢复后重新执行Gather，因此snapshot不是rollback权威状态，也不进入rollback副本。

[COMPUTED][HIGH] RoundResultHeader运行时门已关闭；T6M按AcquireThenHold资格保持合同技术放行。最后90步严格Region窗口继续保留为观察指标，不再要求已接战实体追随移动目标持续重排。单进程DebugGame PIE、当前版人工审片、所有业务事实单次Mass读取/统一原子提交和按能力archetype拆分仍未执行。

[COMPUTED][HIGH] 第十三切片将Facing与MovementFinalize合并为Runtime `FCrowdMassFacingFinalizeWork`：一个不可变POD任务严格执行Facing Resolve→Finalize输入组装→CommitPlan构建和目标集合验证。Demo以单一`UCrowdDemoRoundFacingFinalizeProcessor`先验证完整AgentId/Lifecycle、Particle或SF1 Obstacle最终运动事实及Facing结果，再在一次Mass遍历中同步发布Runtime Facing、Runtime Movement和Demo RoundSim；旧Facing与MovementFinalize processor实现已物理删除。

[COMPUTED][HIGH] 第十三切片通过Development、DebugGame、`MassCrowd` 14/14与`CrowdDemo` 105/105。8727 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`、fixed-step p95=`3.847ms`；8728异构T6保持wall/corridor/completed/settled=`20/20/20/20`、inside/coverage=`20/20`、fixed-step p95=`6.121ms`。两次双端运行的Particle硬安全、invalid/fallback、correction误差和明确错误日志均为0。

[COMPUTED][HIGH] 第十四切片新增Runtime `FCrowdMassParticlePipelineWork`：同一个Particle WORK先执行Core Solve，再根据boundary snapshot与prepared MovementPredict生成完整、稳定排序的publish plan。publish plan同时覆盖Particle active实体、T1 inactive实体、invalid安全回退、外部Target粒子保留结果及供FacingFinalize消费的最终kinematics；Demo不再从ProposedMovement和FlowSample Mass fragments重新拼装这些事实。

[COMPUTED][HIGH] Particle processor当前只以Mass查询验证完整AgentId/Lifecycle集合并发布Runtime Particle fragment；路线与稳定性诊断改为消费publish plan、boundary snapshot、prepared prediction和prepared Shared Flow。旧的Mass二次读取/派生块已物理删除，最终kinematics直接由publish plan提供。Identity与最终RoundSim state仍是post-finalize对实际提交状态采样的必要事实，不复制第二份权威状态。

[COMPUTED][HIGH] 第十四切片通过Development、DebugGame、`MassCrowd` 15/15与`CrowdDemo` 105/105。8729 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`、fixed-step p95=`3.496ms`；8730异构T6保持wall/corridor/completed/settled=`20/20/20/20`、inside/coverage=`20/20`、fixed-step p95=`4.599ms`。两次运行均保持Particle硬安全与invalid/fallback为0、agents/visible=`20/20`、双端correction位置/速度/Yaw误差为0。

[COMPUTED][HIGH] 第十五切片物理删除`FCrowdMassParticleConstraintFragment`及其Trait、Demo模板和测试archetype注册。Particle processor现在没有Mass query：它只消费boundary snapshot、prepared prediction/Flow和`FCrowdMassParticlePipelineWork`的完整publish plan来生成Demo诊断、累计器、fixture与rollback事实，不再把同一Particle结果写入临时Mass镜像。

[COMPUTED][HIGH] FacingFinalize直接使用`PreparedRuntimeFinalKinematics`作为Particle/SF1 Obstacle最终运动事实，并在任何写回前把它与Runtime Commit plan及实际Mass身份/Lifecycle进行完整集合预验证。结构自动化禁止临时Particle fragment和Particle query回流；插件Core/Runtime仍不包含Demo路线、指标或fixture语义。

[COMPUTED][HIGH] 第十五切片通过`git diff --check`、Development、DebugGame、`MassCrowd` 15/15与`CrowdDemo` 105/105。8731 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`、fixed-step p95=`3.966ms`；8732异构T6保持wall/corridor/completed/settled=`20/20/20/20`、inside/coverage=`20/20`、fixed-step p95=`5.013ms`。两次运行均保持Particle硬安全与invalid/fallback为0、agents/visible=`20/20`、双端correction位置/速度/Yaw误差为0。

[COMPUTED][HIGH] 第十六切片增加`FCrowdDemoPreparedParticleDiagnosticCommit`。Particle processor只准备当前boundary的候选/应用Summary、route/stability样本、cross-profile计数、failure fixture和OpenSpawn输入，不再直接更新任何持久累计器。prepared commit包含step/revision/agent count原子门，且每个boundary最多发布一次。

[COMPUTED][HIGH] `PostFinalizeMetricsProcessor`只有在FacingFinalize已成功原子写入当前boundary后，才按原顺序一次性提交Particle诊断：stability、cross-profile、route及counterfactual、OpenSpawn、failure fixture、最终Particle summary。FacingFinalize失败时该boundary的候选指标不会冒充已提交状态；rollback snapshot仍在诊断提交之后记录，因此累计器和快照时序保持一致。

[COMPUTED][HIGH] 第十六切片通过`git diff --check`、Development、DebugGame、`MassCrowd` 15/15与`CrowdDemo` 105/105。8733 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`、fixed-step p95=`3.892ms`；8734异构T6保持wall/corridor/completed/settled=`20/20/20/20`、inside/coverage=`20/20`、fixed-step p95=`5.244ms`。两次运行均保持Particle candidate hash、能力、安全、同步和correction零误差门。

[COMPUTED][HIGH] 第十七切片新增按AgentId稳定排序的`FCrowdDemoPreparedPostFinalizeAgentRecord`。FacingFinalize仍先完成全量身份/Lifecycle/Commit目标预验证，再在唯一写回遍历中同步更新Runtime Facing、Runtime Movement与Demo RoundSim，并从实际写入后的RoundSim捕获AgentId、Lifecycle与最终状态记录。

[COMPUTED][HIGH] `PostFinalizeMetricsProcessor`现在无Mass query；它只消费上述最终记录、boundary snapshot及各阶段prepared事实，提交Particle诊断并生成路线、场景进度、rollback和Combat movement snapshot。最终验收样本因此来自实际原子写入值，而非重新计算的候选值；FacingFinalize失败时不会发布记录、推进诊断或允许Authority/Client Commit。

[COMPUTED][HIGH] 首次8735生产运行暴露了新prepared记录只在Plan激活时清空、没有按boundary清空的生命周期错误：step 0后所有后续发布被判为重复并触发VIOLATION。该问题已修正为`PublishBoundarySnapshot`每个fixed-step清空派生记录，并加入结构自动化锁定此合同。

[COMPUTED][HIGH] 修复后的Development、DebugGame、`MassCrowd` 15/15与`CrowdDemo` 105/105通过。8737 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`、fixed-step p95=`4.067ms`；8738异构T6保持wall/corridor/completed/settled=`20/20/20/20`、inside/coverage=`20/20`、fixed-step p95=`4.716ms`。两次运行realtime factor均为1.000、agents/visible=`20/20`，双端correction位置/速度/Yaw误差为0。

[COMPUTED][HIGH] 第十八切片将Engine Transform、Mass Velocity和Demo movement写入并入FacingFinalize已经存在的全量原子写回遍历。该遍历仍在任何写入前完整验证AgentId、Lifecycle、Facing、final kinematics和Runtime Commit plan；验证失败时五类最终状态均不写入。

[COMPUTED][HIGH] Authority/Client Commit processor现在无Mass query，不再重复读取Identity、RoundSim和Runtime movement，也不再执行第二次Transform/Velocity遍历；它们只验证当前boundary已有完整post-finalize records并记录角色相关Commit阶段。最终运动事实、Engine表现状态与checkpoint状态由同一写回遍历产生。

[COMPUTED][HIGH] 第十八切片通过`git diff --check`、Development、DebugGame、`MassCrowd` 15/15与`CrowdDemo` 105/105。8739 T2保持terminal=`20/20`、coverage=`16/16`、fixed-step p95=`3.638ms`；8740异构T6保持completed/settled/inside/coverage=`20/20`、fixed-step p95=`5.003ms`。两次运行realtime factor均为1.000、agents/visible=`20/20`，双端correction位置/速度/Yaw误差为0。

[INFERRED][HIGH] 运动终态的重复Mass遍历已关闭，但整个boundary仍包含Business、Target、T1、Visual/Combat和checkpoint等宿主查询；因此仍不能宣称全pipeline只有一次Mass读取。下一步应先审计CheckpointPublisher是否重复读取已存在的最终记录与prepared业务事实，再决定archetype拆分；当前不得进入100/500。

[COMPUTED][HIGH] 第十九切片新增按AgentId稳定排序的`PreparedCheckpointAgentStates`。VisualStateResolve在同一boundary完成Combat/Visual状态决议后，将最终RoundSim、Formation Radius和Combat NetState整批组装并与FacingFinalize捕获的最终记录、boundary formation facts逐项校验；数组每个boundary显式清空，禁止复用旧checkpoint事实。

[COMPUTED][HIGH] CheckpointPublisher已删除Identity、RoundSim、Formation、Stats、Business、Attack、Reactive、HitFlash和Visual九类fragment查询，只在需要构建correction或RoundResult时消费prepared checkpoint states。SoftPressure rollback ready门、checkpoint chunk与RoundResult构建顺序未改变；Publisher不再是额外Mass遍历。

[COMPUTED][HIGH] 第十九切片通过`git diff --check`、Development、DebugGame、`MassCrowd` 15/15与`CrowdDemo` 105/105。8741 T2保持terminal=`20/20`、coverage=`16/16`、fixed-step p95=`3.598ms`；8742异构T6保持completed/settled/inside/coverage=`20/20`、fixed-step p95=`4.780ms`。两次运行realtime factor均为1.000，agents/visible=`20/20`，客户端correction revision gap与位置/速度/Yaw误差均为0。

[INFERRED][HIGH] Checkpoint重复读取已关闭；当前剩余的明显宿主重复读取位于VisualStateResolve本身：它仍为最终速度、Formation Radius和身份读取RoundSim/Formation/Identity，同时FacingFinalize最终记录与boundary formation facts已经持有其中大部分事实。下一切片只应收缩VisualStateResolve输入，不得改变Combat写入时序、checkpoint内容或rollback双完成门。

[COMPUTED][HIGH] 第二十切片删除VisualStateResolve对Formation和RoundSim fragments的读取。该阶段按Identity.AgentId在FacingFinalize最终记录与boundary formation facts中执行稳定二分查找，使用prepared最终速度解析Visual状态，并从同一最终记录组装checkpoint与Particle applied hash；缺失Agent、Lifecycle错位或集合数量不一致会拒绝当前boundary。

[COMPUTED][HIGH] Identity读取仍被保留：Stats、Business、Attack、Reactive、HitFlash和Visual fragments本身不携带稳定AgentId，当前需要Identity把可变业务状态与prepared终态安全关联。直接删除Identity会依赖Mass chunk顺序，违反稳定映射合同。

[COMPUTED][HIGH] 第二十切片通过`git diff --check`、Development、DebugGame、`MassCrowd` 15/15与`CrowdDemo` 105/105。8743 T2保持terminal=`20/20`、coverage=`16/16`、fixed-step p95=`3.137ms`；8744异构T6保持completed/settled/inside/coverage=`20/20`、fixed-step p95=`4.023ms`。两次双端运行的correction revision gap及位置/速度/Yaw误差为0，realtime factor分别为1.000/1.001。

[COMPUTED][HIGH] 第二十一切片物理删除独立`VisualStateResolveProcessor`。`FacingFinalize`在原有全量身份/Lifecycle/Commit目标预验证通过后，于同一次Mass写回遍历中提交最终运动、Transform/Velocity以及宿主Combat/Visual状态；Visual决议使用最终自主运动速度，未读取Local Predictive或Particle修正向量作为朝向意图。该合并保留Identity映射，不依赖chunk顺序，也没有把Demo Combat字段加入MassCrowdCore公共POD。

[COMPUTED][HIGH] 同一次写回还生成稳定的post-finalize records和checkpoint states。`PostFinalize`先记录movement rollback与路线指标，再从prepared checkpoint/post-finalize/formation facts完成Combat rollback及Particle applied hash；movement/combat双完成门、correction boundary和checkpoint chunk内容保持原顺序。CheckpointPublisher继续无Mass query。

[COMPUTED][HIGH] 第二十一切片通过`git diff --check`、Development与DebugGame Editor（`-DisableUnity`）、`MassCrowd` 15/15和`CrowdDemo` 105/105。8745 T2为terminal/inside=`20/20`、coverage=`16/16`、fixed-step p95=`3.924ms`；8746异构T6为completed/settled/inside/coverage=`20/20`、fixed-step p95=`4.994ms`；8747 T7客户端实际覆盖idle/move/attack/hit/death五态；8748 T8 spawn/impact/damage=`50/50/50`、duplicate fire/hit=`0/0`且attack/projectile/event hash双端一致。四次运行agents/visible=`20/20`，correction位置/速度/Yaw误差为0，无明确硬错误。

[INFERRED][HIGH] 最终运动与Combat/Visual现在共享一次GT终态写回，但整个boundary仍不是严格“一次Mass读取、一次Mass写回”：BusinessPrepare、Ranged/Hit业务处理以及Movement WORK结果的中间Mass镜像仍有独立查询或发布。下一切片应先盘点这些剩余接缝并优先删除无消费者的中间镜像；当前不能直接进入archetype拆分或100/500。

[COMPUTED][HIGH] 第二十二切片确认Runtime GuidanceCandidates、ComposedGuidance与LocalVelocity三个Mass fragments只有写入者而没有fragment读取者；正式消费者已全部读取prepared SoA。三者及其Trait/模板注册、processor发布和无调用适配入口已经物理删除，Gather记录改用普通`FCrowdMassGuidanceCandidates` POD。`FCrowdDemoRoundProposedMovementFragment`仍被SF1 ObstacleConstraint消费，因此保留，不把不同语义的桥接一并删除。

[COMPUTED][HIGH] Runtime `Agent/SimulationState/Properties`不是中间镜像，而是插件Runtime持久合同。身份在authority spawn以及双端Plan激活时由稳定Demo identity初始化；状态在bootstrap/Plan激活时同步，能力属性在Plan应用完异构profile后同步；普通fixed step只由FacingFinalize更新Runtime state。8751首次回归证明旧MovementWork写入曾暗中承担客户端Runtime identity初始化，修复后8753/8754从Round 1到Round 2均保持correction位置、速度和Yaw误差为0。

[COMPUTED][HIGH] RangedCombat、HitResponse与ReactiveMotion当前不是无消费者镜像：Stats、Business、Attack、Reactive、HitFlash和Visual是T7/T8连续业务状态，三个阶段按严格顺序读写，FacingFinalize消费最终版本。现存债务是每个阶段内部均先gather再apply、跨阶段重复查询，而不是这些fragments本身应被删除。

[COMPUTED][HIGH] 第二十三切片将上述三个阶段替换为唯一`UCrowdDemoRoundCombatBoundaryProcessor`。它第一次Mass遍历只采集按AgentId稳定排序的宿主Combat事实，随后在POD数组上严格执行Attack/Projectile、Hit resolve和Reactive motion，第二次Mass遍历在完整AgentId集合校验后原子提交最终业务状态与ReactiveStep。T8从每boundary 5次Combat遍历降为2次，T7从3次降为2次；失败时不发布半套业务状态。

[COMPUTED][HIGH] 跨processor的`PendingProjectileHitFacts`、setter/consumer和旧三个processor实现已删除。Projectile权威数组、T7测试HitFacts以及Stats/Business/Attack/Reactive/HitFlash/Visual仍属于Demo宿主；MassCrowdCore、Runtime通用运动POD和最终FacingFinalize写回合同没有增加Combat字段。

[COMPUTED][HIGH] 第二十三切片通过Development与DebugGame Editor（`-DisableUnity`）、`MassCrowd` 15/15和`CrowdDemo` 105/105。8755 T7为agents/visible=`20/20`、fixed-step p95=`2.452ms`；8756 T8为spawn/impact/damage=`50/50/50`、duplicate fire/hit=`0/0`，attack/projectile/event hash双端一致，fixed-step p95=`2.247ms`，agents/visible=`20/20`。两次双端运行的Particle硬安全、invalid/fallback、correction位置/速度/Yaw误差及明确硬错误均为0。

[COMPUTED][HIGH] 非Combat路径回归同样通过：8757 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`、fixed-step p95=`4.263ms`；8758异构T6保持wall/corridor/completed/settled=`20/20/20/20`、inside/coverage=`20/20`、fixed-step p95=`5.230ms`。两次运行agents/visible=`20/20`，双端correction位置/速度/Yaw误差为0，无明确硬错误。

[INFERRED][HIGH] 这是第二十三切片的历史停止点：当时仍不能宣称整个fixed-step只有一次Mass读取，且100/500尚未授权；后续切片及S6已经完成接缝收敛和同路径100/500门，本句不得作为PJ0当前状态。

[COMPUTED][HIGH] 第二十四切片已形成[RoundSim Mass查询与数据所有权矩阵](MassQueryOwnershipMatrix.md)。矩阵区分了Round激活事务、canonical boundary gather、Runtime WORK、Demo Combat、SF1桥接、Networking和Presentation，不再用“遍历次数越少越好”替代真实数据所有权。

[COMPUTED][HIGH] `FCrowdDemoReactiveMotionStepFragment`只在同一boundary由Combat写、Movement读，现已替换为按AgentId排序的`FCrowdDemoPreparedReactiveMotionStep`并物理删除；`FCrowdDemoTargetCapabilityFragment`没有读取者，实际Capability由Particle properties、cohort snapshot和规则驱动，现已物理删除。SoftPressure MovementWork因此保持零Mass遍历，T7/T8垂直Reactive运动仍由prepared事实输入统一Movement WORK。

[COMPUTED][HIGH] `TargetRegionGuidance`不再重复查询Identity/Formation/RoundSim，而是直接消费当前canonical boundary snapshot，Mass遍历由1次降为0。`FlowPreferredVelocity`也用该snapshot验证Runtime结果AgentId集合，删除一遍只读身份扫描，只保留一次需要支持诊断/correction rollback的`RoundFlowSample`持久写回。

[COMPUTED][HIGH] 第二十四切片通过Development与DebugGame Editor（`-DisableUnity`）、MassCrowd 15/15和CrowdDemo 105/105。8759 T7 fixed-step p95=`2.058ms`；8760 T8 fixed-step p95=`1.782ms`且spawn/impact/damage=`50/50/50`、duplicate fire/hit=`0/0`；8761 T2 terminal=`20/20`、coverage=`16/16`、fixed-step p95=`4.378ms`；8762异构T6 completed/settled/inside/coverage=`20/20`、fixed-step p95=`6.221ms`。四次运行agents/visible=`20/20`，Particle硬安全、correction误差和明确硬错误均为0。

[INFERRED][HIGH] 这是第二十四切片的历史停止点：当时下一接缝是SF1 `RoundProposedMovement → RoundObstacleConstraint`，且100/500尚未获准；该接缝已由第二十五切片关闭，同路径100/500已由S6关闭。

[COMPUTED][HIGH] 第二十五切片已关闭该SF1中间桥。MovementWork对两个正式场景均只发布`PreparedRuntimePredictedMovements`；SF1 ObstacleConstraint按canonical boundary snapshot验证AgentId全集，调用既有`FCrowdDemoSharedFlowFieldKernel::ConstrainMovement`并发布`PreparedRuntimeFinalKinematics`。`FCrowdDemoRoundProposedMovementFragment`和`FCrowdDemoRoundObstacleConstraintFragment`连同模板注册、spawn初始化和query读写已物理删除。

[COMPUTED][HIGH] Development、DebugGame（均`-DisableUnity`）、MassCrowd 15/15与CrowdDemo 105/105通过。8763 SF1 Single保持Flow hash=`267519150`、rebuild=1、unreachable=0、goal/wall/corridor/turn=`1/1/1/1`、双端obstacle penetration=0、checkpoint位置误差p95=0；但其correction interval位置误差p95=`26.745cm`，因此不满足`<1cm`严格同步门。8765 T2为terminal=`20/20`、coverage=`16/16`、fixed-step p95=`4.227ms`；8766异构T6为completed/settled/inside/coverage=`20/20`、fixed-step p95=`5.826ms`，两者双端hash与correction均通过。

[COMPUTED][HIGH] 8764 SF1 Cohort 500未进入Round：server在初始复制阶段触发`Ensure !IsBunchTooLarge`，客户端未完成readiness。该结果不证明prepared障碍链在500实体上失败，也不允许宣称500回归通过；当前唯一准确结论是网络启动/复制门存在独立阻塞。

[COMPUTED][HIGH] 第二十六切片把上一boundary的`FCrowdMassFacingFragment::ConsecutiveFinalSettleSteps`纳入唯一`BoundaryGather`，形成按AgentId排序的`FCrowdDemoRoundBoundaryFacingFact`。Facing WORK继续从该prepared事实计算下一状态，FacingFinalize不再为历史settle状态单独读取Mass，因此其遍历由3次降为2次。

[COMPUTED][HIGH] 剩余两次遍历不是中间镜像：第一次只验证实际Mass中的AgentId/Lifecycle、Runtime/Demo身份与完整CommitPlan一一对应；第二次才原子写回Facing、Runtime Movement、Demo RoundSim、Transform/Velocity和宿主Combat/Visual。当前保留二者，避免在遍历中途发现集合错误后留下部分更新。

[COMPUTED][HIGH] 8767曾得到SF1 correction interval位置误差p95=`26.745cm`，而checkpoint误差为0。该问题已在第二十七切片修复：SF1与SoftPressure现在共用128-boundary fixed-step correction历史、AgentId/Lifecycle校验、零误差快速路径及必要时的确定性恢复/重放。8770原参数复测把interval p95降至`0.064cm`、checkpoint/sim p95降至`0.008cm`，且rollback hit/miss/mismatch=`36/0/0`。

[COMPUTED][HIGH] 8764的500实体启动bunch已定位到直接复制的`FCrowdDemoRoundBootstrapPacket`，其`Agents`包含全部完整AgentState且未分块；500状态在Round前作为单一replicated property发送并触发`Ensure !IsBunchTooLarge`。现有correction/checkpoint的100-agent chunk机制尚未用于bootstrap。

[COMPUTED][HIGH] 第二十七切片已移除correction历史的SoftPressure场景门。两个正式Flow场景现在都在PostFinalize成功后记录同一128-boundary历史，等待Combat/Visual最终事实完成后才标记replay-ready；correction按frame fixed-step读取历史状态执行AgentId/Lifecycle/Radius完整校验、真实误差比较、零误差快速路径或恢复后重放。新Round会统一清理history与hit/miss/mismatch/replayed计数。

[COMPUTED][HIGH] 8770 SF1 Single的Round 1 correction snapshot hit/miss/mismatch=`36/0/0`，correction interval位置误差p95由`26.745cm`降至`0.064cm`，checkpoint p95=`0.008cm`；Flow hash、路线和障碍安全保持不变。8771 T2与8772异构T6分别保持原能力与rollback=`54/0/0`、`80/0/0`。

[COMPUTED][HIGH] 阶段 D 已完成Demo Bootstrap生产适配：完整Agents复制属性与旧OnRep路径已删除；8773客户端实际组装revision 1的20 agents、1 chunk、3720 bytes并进入现有Pipeline。Development/DebugGame `-DisableUnity`、定向3/3、MassCrowd 20/20、CrowdDemo 109/109通过，双端fixed-step p95=`3.885ms`、realtime=`1.001`、client frame p95=`3.136ms`且无bunch-too-large、Ensure或VIOLATION。[INFERRED][HIGH] 当前进入E，只实现通用Spawn/Despawn/Membership batches，不提前创建真实Mass生命周期场景。

[COMPUTED][HIGH] 阶段 E 已完成trivially-copyable lifecycle/membership batch合同、四类Despawn原因、严格sequence/revision/fixed-step、稳定hash、重复幂等、缺序列与stale拒绝、despawn后高LifecycleSerial槽位复用及路径无关membership hash。Development/DebugGame `-DisableUnity`、定向3/3、MassCrowd 23/23与CrowdDemo 109/109通过；Networking新增Source未检出Demo/Scenario/Combat反向依赖。[INFERRED][HIGH] 当前进入F，在插件最小Mass World做真实entity boundary apply；E本身不是生命周期运行证据。

[COMPUTED][HIGH] 阶段 F 已把真实Mass entity mutation放入`MassCrowdRuntime`的通用LifecycleStore，Networking adapter仅在协议状态副本通过后调用Runtime并提交。最小World真实验证snapshot create、不同fixed-step spawn、destroy后handle失效、Mass handle serial变化、StableEntityRef高serial槽位复用、membership原子迁移、stale correction/despawn拒绝和完整entity-set hash；Development/DebugGame、定向1/1、MassCrowd 24/24及CrowdDemo 109/109通过。[INFERRED][HIGH] 当前进入G，将该生产路径接入独立Demo continuous lifecycle场景，不使用T1 active标志冒充销毁。

[COMPUTED][HIGH] 阶段 G 已新增独立`ACrowdDemoContinuousLifecycleCoordinator`与CLI入口；该入口在GameMode固定agent spawn之前分支，Round pipeline只空载运行且不拥有生命周期实体。Server以30Hz fixed-step和15-step操作间隔驱动E batches/F Runtime store，population硬上限20，交替执行Membership、Death/BusinessRecycle Despawn与同槽位高LifecycleSerial Respawn；Client用可靠operation wrapper应用同一batch并按StableEntityRef增量维护主体ISM，受击闪色由同一实例PICD slot 2驱动。9203序列44双端entity-set hash=`12305161180829922642`一致，且单主体ISM计数门通过。[INFERRED][HIGH] 阶段G的旧双ISM证据已由当前单主体表现实现取代。

[COMPUTED][HIGH] 阶段 H 新增`MassCrowdRuntimeBehavior`公共合同：transition先验证AgentFacts/Capability/provider，再输出显式Target、Objective、MovementProfile、InteractionIntent和可选BusinessCommitRequest，Commit只更新通用AgentFacts；Runtime不引用Demo。Demo的`CrowdDemoBehaviorAdapters`把基础行为、Cargo pickup/deliver和Attack路由到同一接口，业务ledger以CommitId幂等，且`FCrowdDemoHitFact::HitEventId`作为外部commit id接入既有damage kernel。定向2/2、MassCrowd 25/25、CrowdDemo 111/111与Development/DebugGame通过。[INFERRED][HIGH] 该阶段没有把H接口与G continuous lifecycle组合；组合运行属于J。

[COMPUTED][HIGH] 阶段 I 新增`CrowdNavSurfaceGraph`与`MassCrowdNavSurfaceGraphExtractor`：Core以量化几何生成稳定节点/拓扑hash，layer-specific与closest-polygon attachment避免大多边形质心误判，Shared Flow以预构建反向邻接执行稳定Dijkstra；Runtime提取静态Recast tile/poly/portal并拒绝缺失、过窄、过陡或跨越落差的连接。8800真实地图运行通过98 nodes、234 directed edges、38 tiles、4 extracted/graph layers、13 overlap、76 reachable sloped edges、8/8 reachable markers、drop unreachable，topology hash=`9799951363989120452`；Development/DebugGame、定向3/3、MassCrowd 27/27与CrowdDemo 112/112通过。[INFERRED][HIGH] I本身没有把continuous lifecycle、Behavior、Combat或Logistics组合进probe；该组合由J完成。

[COMPUTED][HIGH] 阶段 J 新增`ACrowdDemoMixedSandboxCoordinator`：GameMode在固定Round spawn前分支，20个真实Mass实体按30Hz boundary运行；行为由距离、Cargo carrier、Health与当前目标事实驱动，在HaulPickup/Deliver、Pursue/Attack、Guard/Flee及Wander/MoveTo间切换。所有移动目标attachment消费I的Recast图与Shared Flow，业务提交消费H provider/ledger并对同一CommitId即时重放验证幂等，死亡/业务回收与行为cohort变化消费E/F lifecycle batches。当前客户端用bounded状态包校正完整AgentFacts并增量维护单主体ISM；8804的普通/HitFlash双ISM描述只保留为历史检查点。[INFERRED][HIGH] 当前状态以本文1.1节、`EntityBehaviorSourceArchitecture.md`和`PhasePlan.md`为准。

## 2026-07-28 产品路径复核增量

[COMPUTED][HIGH] `AMassCrowdReplicationActor`现在支持有界reliable batch；J按帧批量发布状态与correction，ACK后的缓存追赶也按上限分批，未提高原队列或网络预算。`DrainApplyFrames()`返回空仅表示当前无帧，只有客户端状态进入`RequiresResync()`才算stale。
[COMPUTED][HIGH] P4 Coordinator驱动Planner、故障策略、指标和截图，但实体位置由公共`FCrowdMassBoundaryRunner`与Nav Runtime提交；Cargo实例仅由复制后的ownership驱动Presentation attach/detach。7953双端状态hash一致，实例数与相关实体数均为20。
[COMPUTED][HIGH] J 7939、Continuous 7946及Round 7948–7951复测均无Fatal、Assertion、Ensure、`LogWindows: Error`、VIOLATION或resync。J客户端隐藏窗口记录的Actor Tick p95=`400ms`，不能解释为渲染帧p95；服务端fixed-step p95=`1.972ms`。
[COMPUTED][HIGH] 当前累计自动化为MassCrowd 43/43、CrowdDemo 115/115，Development/DebugGame `-DisableUnity`通过，插件Source到Demo的反向依赖为0。现行停止点仍为K前。

## 2026-08-15 WA8-R Target/Resource 统一 Owner Commit Barrier

[COMPUTED][HIGH] `FCrowdDemoPendingWorkerResultFinalize` 现在拥有 `PreparedProxyResult`、`PreparedMassPlan`、`PreparedTargetResourcePlan` 与 `CommitToken`。Target/Resource plan 在 Prepare 阶段只构建一次，包含已适配的 homogeneous state 或按稳定 destination index 准备的完整 cohort runtime，以及 OwnerRevision、BoundaryGeneration、PlanRevision、FixedStepIndex、TargetRevision、BaseStateHash、PreparedStateHash 和 SharedFlow ResourceId/Revision/BuildHash/RebuildCount。
[COMPUTED][HIGH] Prepare 会拒绝非法 SharedFlow resource pointer、缺失 Owner/revision、重复 Slot、重复 entity、重复 entity-field、缺失阶段、错误 fixed-step/target revision 和消失的 cohort Owner；`ValidateExecution`、Owner 查找、Runtime adapter 转换和 lifecycle diagnostic 构造均在首次状态写入前完成。
[COMPUTED][HIGH] 最终屏障实际顺序为：Proxy/worker token validate → prepare ordered-result side effects → Target/Resource token/base-state/Owner final validate → Facing/Behavior/Lifecycle/Handle validate → Mass final validate 与 atomic-write claim → Mass apply → Proxy commit → Target/Resource prepared move apply → remaining validated state side effects → Round commit marker → result presentation/network side-effect publish。Dirty Batch ACK 不在该屏障内，仍由后续 Input Sync 最后执行。
[COMPUTED][HIGH] 旧 `ApplyPreparedBoundaryResourcePatches` 已删除；Final Barrier、`AdvanceRoundWorkerFrame` 和 PostFinalize 都不再读取 `PreparedTargetResourceSlots`、查找 cohort 或调用 `ValidateExecution`。`PreparedTargetResourceSlots` 本身仍作为 Prepare 输入保留，构建后立即清空。
[COMPUTED][HIGH] 第一次写入后的 Target/Resource 路径只按 Final Validate 已锁定的 destination index 移动 Prepared runtime 或 homogeneous fields；没有返回失败的正常分支。外部 Authority/Client publish 与下一帧 Dirty ACK 仍在完整 commit 之后。
[COMPUTED][HIGH] 本切片未删除完整 rollback 数组、`TryPrepareRoundApply` 或 Demo-local Round Transaction，因此 WA8 与 Legacy Transaction 清理仍为 OPEN。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
