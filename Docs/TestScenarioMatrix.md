# MassAI Crowd Demo 测试场景矩阵

[COMPUTED][HIGH] 2026-08-03 WA8.5 Work/Timer/Spatial 切片：Development Editor DisableUnity、`MassCrowd.RuntimeV2.Complexity` 3/3 与完整 `MassCrowd.RuntimeV2` 32/32 通过。Work Ring 分别 Drain 1k/2k/5k/10k，累计 Bucket Probe 不超过 `W × 固定Bucket数`；Sparse Time Wheel 安排 10k 未来 Tick，提前 Drain 扫描为 0，只累计实际到期的 7 个 Bucket。

[COMPUTED][HIGH] Spatial 回归执行 10k×1% 与 10k×10% Movement Dirty：两组均保留 10k 成员、完成 20k 次增量 Spawn/Update、full rebuild=`0`，跨 Cell migration 分别精确为 100 和 1000。

[INFERRED][HIGH] 下一测试顺序：Target 受影响 Cohort + 128 Guidance Shard → Particle Island/Cell Pair Owner → 普通 Epoch 零完整序列化 → Target >900 Tick → WA9 三个 10k 场景。

[COMPUTED][HIGH] 2026-08-03 客户端 Legacy Round 中间诊断删除切片：Development Editor DisableUnity、`CrowdDemo.Architecture.PostFinalizeMinimalQuery` 1/1 和 `CrowdDemo.SF.Transport.RoundCheckpoint` 2/2 通过。结构门确认 Legacy unavailable 日志、Dynamic Flow/Particle/Projectile 客户端中间比较、状态标志和两个无消费者 Compare 字段均为零，同时 Checkpoint 状态误差比较仍存在。

[COMPUTED][HIGH] 9809 Correction-off 双进程 6 秒 Round/24 秒持有门完成 `pipeline_queued=1/boundary_applied=1`，Position/Velocity/Yaw 误差为 0，客户端继续到 Epoch/Input=`221/307`、`runtime_v2_failure=0`；Legacy 客户端日志及硬失败匹配数为 0。

[INFERRED][HIGH] 下一测试顺序：Work Ring/Time Wheel 扫描复杂度微基准 → Spatial 1%/10% 跨 Cell 迁移 → Target 128 分片/Particle Island → Target >900 Tick 长窗口 → WA9 三个 10k 场景。

[COMPUTED][HIGH] 2026-08-03 专用 Checkpoint/Correction Revision 切片：Development Editor DisableUnity 通过；`RoundCheckpointChunks`、`RoundCheckpointTerminalTolerance`、`PostFinalizeMinimalQuery`、`WorkerPacketTransport`、`WorkerCodec` 与 `SparseCorrectionWithoutWorldRebuild` 通过。可靠 49 KiB 载荷固定拆为 13 个不超过 4 KiB 的块。

[COMPUTED][HIGH] 9803 Correction-off 双进程 6 秒门：Worker bootstrap 后持续本地预测，终局 Checkpoint `pipeline_queued=1/boundary_applied=1`，Position/Velocity/Yaw 误差为 0，Legacy Round diagnostics 明确为 unavailable，硬失败为 0。

[COMPUTED][HIGH] 9807 默认 Correction 双进程 30 秒门：Digest 16/19/20/27/28 等发现 Combat Scope mismatch；每次可靠修补后客户端继续推进，最终 Epoch/Input=`905/991`、Runtime failure=`0`，终局 Checkpoint 正常应用且无世界重建。

[INFERRED][HIGH] 下一测试顺序：Legacy Round 中间诊断物理删除/符号门 → WA8.5 的 1k/2k/5k/10k 微基准 → Target >900 Tick 长窗口 → WA9 三个 10k 场景。20 实体网络门只证明合同和恢复路径，不替代规模性能门。

[COMPUTED][HIGH] 2026-08-03 Legacy 普通完整 Correction 关闭切片：Development Editor DisableUnity 成功；`PostFinalizeMinimalQuery`、`WorkerCodec` 与 `WorkerResultApply.LifecycleOwnerAndEvents` 3/3 通过。结构门验证旧完整纠错只允许显式诊断开启，不能因 Authority Mode 自动恢复。

[COMPUTED][HIGH] 默认 `CrowdDemo.PersistentWorker.SingleProcessDualPIE` 1/1 通过 300 Epoch 无纠错预测、单 Scope 稀疏修复和无 Runtime restart/resnapshot 回归；同一日志中旧 server `CrowdDemoCorrectionFrame` 与 client full-frame header 均为 0。

[COMPUTED][HIGH] 9790 全 Production 移动目标 T5 达到 batch 600：Target mode/verified=`2/20`、stale lifecycle=`0`、旧完整 Correction=`0`、硬失败=`0`。9791 全 Production T8 达到 batch 900：event=`150`、stale=`0`、acquire/windup/spawn/impact/damage=`50/50/50/50/50`、Golden Hash=`439379904/1411313634/6141440`；整轮只产生 1 个 `plan_phase=1.000` RoundResult Checkpoint。

[INFERRED][HIGH] 下一测试顺序：专用 Checkpoint 载荷与 Legacy Correction 符号零门 → Target >900 Tick 长窗口 → WA8.5 微基准 → 三个 WA9 10k 场景。20 实体 T5/T8 不替代规模和视觉验收。

[COMPUTED][HIGH] 2026-08-03 唯一 Commit/按需 Checkpoint 切片：Development Editor DisableUnity 成功；`PostFinalizeMinimalQuery`、`WorkerResultApply.LifecycleOwnerAndEvents`、`GatherMergeCommit` 与 T8 组共 17 项自动化通过。结构门验证三个完整副本类型不存在、Final Business 按 Dirty 数量分配、Checkpoint State 构造位于发布门之后。

[COMPUTED][HIGH] 9785 移动目标 T5 到 batch 600：Result Apply batch/stale=`600/0`、Dirty Mass batch=`600`，未出现 `VIOLATION`。9786 T8 到 batch 900：event=`150`、stale lifecycle=`0`，Projectile acquire/windup/spawn/impact/damage=`50/50/50/50/50`、duplicate/expired=`0/0`，Golden Hash=`439379904/1411313634/6141440`。

[COMPUTED][HIGH] 9786 同时证明旧 Round 网络出口尚未收敛：第一轮结束前 Correction publish_count=`55`，每帧仍携带 20 个 AgentStates。下一测试门必须验证普通 Epoch 完整 Correction bytes/state records 为零，而不是只重复 20 实体 Golden。

[INFERRED][HIGH] 下一测试顺序：Legacy 完整 Correction 删除/稀疏 Scope Correction 门 → T5/T8 与双 PIE Correction 门 → Demo DAG/Boundary 符号门 → Target >900 Tick → WA8.5 微基准 → WA9 三个 10k 场景。

[COMPUTED][HIGH] 2026-08-03 Dirty Mass Apply 切片：Development Editor DisableUnity、`CrowdDemo.Architecture.PostFinalizeMinimalQuery`、`CrowdDemo.FriendlyLogistics.Architecture` 与 `MassCrowd.Runtime.WorkerResultApply.LifecycleOwnerAndEvents` 通过。结构门验证持久 Handle 生命周期、Dirty Collection、写前完整匹配和无无界 ResultCommit traversal。

[COMPUTED][HIGH] 9782 移动目标 T5 到 600 Tick：batch/stale=`600/0`、Guidance=`20/20`、full-publish/hash/token=`1/3/598`，无 `VIOLATION/Rejected`。

[COMPUTED][HIGH] 9784 全 Production T8 到 900 Tick：acquire/windup/spawn/impact/damage=`50/50/50/50/50`、duplicate/expired=`0/0`、Hash=`439379904/1411313634/6141440`、stale lifecycle=`0`。Dirty Mass 在 batch 300/600/900 均为 20/20；该场景所有实体持续运动/战斗，不能用它证明稀疏比例，只证明 Dirty Collection 提交没有业务回归。

[INFERRED][HIGH] 下一测试顺序：普通帧完整 CPU 数组删除的结构/行为门 → T5/T8 → Target >900 Tick 长窗口 → WA8.5 微基准 → 三个 WA9 10k 场景。

[COMPUTED][HIGH] 2026-08-03 Dirty Proxy/Gather 删除切片：Development Editor DisableUnity、`MassCrowd.Networking.Replication.WorkerCodec`、`MassCrowd.Runtime.WorkerResultApply.LifecycleOwnerAndEvents`、`MassCrowd.Runtime.GatherMergeCommit` 与 `CrowdDemo.Architecture.PostFinalizeMinimalQuery` 通过。

[COMPUTED][HIGH] 9779 全 Production T8/900 Tick Golden 通过：attack/projectile/event=`439379904/1411313634/6141440`，step 600 的 batch/event/stale=`600/150/0`，full-publish/hash/token=`1/3/598`。

[COMPUTED][HIGH] 9781 移动目标 T5/600 Tick 通过：Guidance=`20/20`，batch/stale=`600/0`，full-publish/hash/token=`1/3/598`，零 `VIOLATION/Rejected`。

[COMPUTED][HIGH] 9780 是有意延长到 600 Tick 之后的诊断运行；step 886 出现 Target Demand 可行区不足拒绝。该日志不是本切片 600 Tick 通过证据，也不得忽略；它是 WA9 前新增的长窗口 Target 回归门。

[COMPUTED][HIGH] 该切片后的 Dirty Mass Apply 单元/结构门、T5 600 与 T8 900 已由 9782/9784 完成；当前下一测试门是完整 CPU 数组删除回归，然后执行 Target 长窗口 >900 与 WA8.5 微基准。三个 WA9 10k 场景继续后置。

[COMPUTED][HIGH] 2026-08-03 Production Snapshot Hash 降频切片：Development Editor DisableUnity、`MassCrowd.Runtime.GatherMergeCommit`、`CrowdDemo.Architecture.PostFinalizeMinimalQuery` 通过。RuntimeBridge 回归验证 Epoch Token 确定性、输入水位参与、零 baseline/水位拒绝及 record allocation 不变。

[COMPUTED][HIGH] 9772 全 Production T8/900 Tick 通过：acquire/windup/spawn/impact/damage=`50/50/50/50/50`，三哈希=`439379904/1411313634/6141440`，fixed-step p95=`34.058ms`，realtime=`0.998`；step 600 的 full-publish/hash/token=`1/3/598`，stale lifecycle=`0`。

[COMPUTED][HIGH] 9773 移动目标 T5 到 step 600：普通 Intent resources=`1`，full-publish/hash/token=`1/3/598`，stale/block wait=`0/0`，无 `VIOLATION/Rejected`。9771 首跑在 step 0 暴露无 Target topology 时错误要求 Target state；增加非空 topology 门后由 9772 复跑关闭。

[INFERRED][HIGH] 下一测试门是普通 Tick stable dense Proxy view/Dirty Batch 的结构与行为回归；20 实体 T5/T8 不替代 WA8.5 的 1k/2k/5k/10k 微基准或 WA9 三个 10k 场景。

[COMPUTED][HIGH] 2026-08-03 Snapshot 原位刷新切片：Development Editor DisableUnity、`MassCrowd.Runtime.GatherMergeCommit`、`CrowdDemo.Architecture.PostFinalizeMinimalQuery` 通过。单元门验证原位刷新不更换 record allocation、Hash 与相同状态的完整 Build 一致，并拒绝乱序成员。

[COMPUTED][HIGH] 9766 全 Production T8/900 Tick 通过：50/50/50/50/50、duplicate/expired=`0/0`、三哈希一致、p95=`34.192ms`、realtime=`0.998`；step 600 的 in-place/full-publish=`600/1`。

[COMPUTED][HIGH] 9770 移动目标 T5 到 step 600：普通 Intent resources=`1`，Worker input/result 与 Round transaction 均闭合，in-place/full-publish=`600/1`，无 `VIOLATION/Rejected`。9767 的脚本性能门失败发生在第一轮 step 600 之后：脚本继续等待并进入第二轮，不能作为本切片失败证据；9770 使用有界运行窗口取得了干净的第一轮证据。

[INFERRED][HIGH] 下一门是 Production dirty-view 替换普通 Tick 完整 Snapshot 遍历/Hash；随后才进入 WA8.5 微基准。20 实体 T5/T8 仍不代表 10k 规模通过。

[COMPUTED][HIGH] 2026-08-03 公共 Legacy API 删除切片：Development Editor DisableUnity 与 `CrowdDemo.Architecture.PostFinalizeMinimalQuery` 通过。9765 全 Production T8/900 Tick 得到 acquire/windup/spawn/impact/damage=`50/50/50/50/50`、duplicate/expired=`0/0`、三哈希=`439379904/1411313634/6141440`、fixed-step p95=`34.230ms`、realtime=`0.998`。

[COMPUTED][HIGH] 结构门现在验证 Orchestrator/WorkGraph 六个公共头、实现和 Legacy 测试文件不存在，Round 无公共 include，且无 `FCrowdMassBoundaryWorkGraph` 生产符号。该门是源码/注册前置证据，不等价于普通帧 Mass Query 为零。

[INFERRED][HIGH] 下一测试顺序是实际 Processor/Query 注册审计 → Production 正常 Tick 完整 `BoundarySnapshot`/Mass Gather 零门 → WA8.5 的 Work Ring/Time Wheel/Spatial/Target/Particle 微基准；WA9 10k 场景继续后置。

[COMPUTED][HIGH] 2026-08-02 Round Runner 删除切片：Development Editor DisableUnity 和 `CrowdDemo.Architecture.PostFinalizeMinimalQuery` 通过；9724 全 Production T8 到 step 300 时 tasks=`5`、pending=`301`、stale=`0`、block wait=`0`，证明 Demo 本地 Work Batch 与直接 Apply Plan 生命周期闭合。

[COMPUTED][HIGH] 9720/9722 稳定复现并修复 Projectile Shadow 计数断言：Resolved Hit 输入数应比较 `InputHitCount`，不能比较经过生命周期拒绝后的 `AppliedHitCount`。9723 越过原 step 76 阻塞。

[COMPUTED][HIGH] 9724 的 Particle replay 阻塞已由精确安全闭包替代；后续 ImpactId 与 Movement Guidance 所有权缺陷也已分别用定向回归锁定。9764 完成全 Production T8/900 Tick，完整功能与 realtime 门 PASS。

[INFERRED][HIGH] 下一测试顺序为 Round WorkGraph/Orchestrator 最后消费者迁移 → AST/注册与零消费者审计 → WA8.5 微基准；WA9 10k 场景继续后置。

[COMPUTED][HIGH] 2026-08-02 Demo transaction shell 脱钩验证：Development Editor DisableUnity、`CrowdDemo.MixedSandbox.J.Architecture`、`CrowdDemo.FriendlyLogistics.Architecture` 通过。9706 在 Behavior Production + Movement fallback 下完成 600 Tick，source=`MixedLegacyKernels+MassCrowdNavRuntime+ApplyFrame`、fixed-step p95=`17.848ms`。

[COMPUTED][HIGH] 9707 Friendly Movement/Behavior Production 双端门通过：20 实体、delivered=`5`、competition/death-recovery/fallback/cancellation=`1/1/1/1`、cargo attach/detach=`2/2`、presentation instances=`20`、state hash=`3180435972084878253`。

[COMPUTED][HIGH] 9709 Mixed 六 Domain Production 双端门通过：step=`600`、active=`20`、pickups/deliveries=`7/7`、combat quantity=`150`、spawn/despawn=`1/1`、projectile spawn/impact/damage=`4/4/4`、duplicate=`0`、stale lifecycle=`0`、server p95=`18.191ms`。client frame p95=`35.949ms`，故功能 PASS、WA9 性能 FAIL/OPEN。

[COMPUTED][HIGH] 9705 在 step 2 暴露 GT committed kinematics 与 Worker predicted kinematics 水位不同；9710 关闭该误判后继续到 expectation 63，锁定 `MaintainDistance(11006)` 因不同预测位置进入 Approach/Hold，而控制载荷、timeline、cursor 与 target context 相同。最终合同仅对 bootstrap 要求状态逐字一致，普通 Intent 要求控制/命令一致。

[COMPUTED][HIGH] 9714 全默认 Shadow 双端门通过：Behavior expectation=`600/600`、server/client p95=`17.748/12.224ms`、active/visible=`20/20`、projectile spawn/impact/damage=`4/4/4`、duplicate=`0`、零 violation。

[INFERRED][HIGH] 下一测试顺序固定为 Round 迁移结构门/真实 T8 → 公共 API 零消费者审计。WA8.5 与 10k WA9 场景继续后置。

[COMPUTED][HIGH] 2026-08-02 Friendly direct apply 9703：Movement/Behavior Production，服务端 `direct_worker_apply=1`；20 实体完成竞争、取货、死亡复用、fallback、退避、取消与交付，客户端 cargo attach/detach=`2/2`，双端 state hash=`3180435972084878253`，硬失败 0。普通 step 300/600 无资源或实体增量，step 606 仅有 spawn/despawn/profile=`1/1/1`，resnapshot 保持 1。

[INFERRED][HIGH] 下一场景门不是 WA9 10k，而是 Runner/WorkGraph parity 脱钩后的 Friendly/Mixed Shadow/Canary 定向回归与结构审计；Round shell 删除完成后才进入 WA8.5。

[COMPUTED][HIGH] 2026-08-02 full-Worker Mixed direct apply 9701：Movement/Behavior/Target/Particle/Projectile/Combat 全 Production，Mixed 不创建 Legacy Runner/WorkGraph；step 600 PASS，population/alive=`20/7`，intent=`169`（`99/27/43`），impact/damage/death=`66/66/13`，target switch=`189`，projectile spawned/impacted/expired/active=`43/13/27/3`，duplicate/stale=`0/0`，min separation=`70.04cm`，fixed-step p95=`17.641ms`，硬失败=`0`。

[COMPUTED][HIGH] 9699 是根因诊断证据：Behavior desired 非零且未锁定，但有 Goal、无 Target/Flow 时旧 MovementPlanning 输出零速度。goal fallback 单元门已补齐。9700 证明 fallback 能恢复推进和命中，但启用无路由 TargetControl 的 LocalPredictive 会在密集交战中永久 yield，故不计正式 PASS；9701 由 Worker Particle + safety commit 替代该 Mixed 局部预测配置后通过。

[INFERRED][HIGH] 下一真实场景门是 Friendly Production direct apply；Mixed TargetControl/LocalPredictive 恢复属于后续独立门。WA8 结构删除和 WA9 三个 10k 场景仍 OPEN。

[COMPUTED][HIGH] 2026-08-02 full-Worker Mixed 9689：六 Domain 全 Production，step 600 PASS；population/alive=`20/18`，attack intent=`82`，impact/damage/death=`21/20/2`，target switch=`79`，projectile spawned/impacted/expired/active=`43/18/23/2`，duplicate=`0`，conserved=`1`，stale reject=`0`，fixed-step p95=`17.624ms`。运行继续到 step 1055，零 `VIOLATION/Rejected`。

[COMPUTED][HIGH] 9688 是被替代的失败证据：step 6 Legacy/Worker target-switch 摘要 `1/0` 触发 Production parity 拒绝，随后产生 stale Behavior sequence；不得把它归因于独立 Behavior command bug。

[INFERRED][HIGH] 下一场景门在删除 Mixed Production Legacy combat/projectile prepare 后复跑同一 600 Tick 参数；当前结果只关闭所有权否决门，不关闭 WA8 结构门。

[COMPUTED][HIGH] 9690 已完成该复跑：Production 不执行 Legacy projectile/impact/health prepare，step 600 仍为 alive=`18`、intent=`82`、impact/damage/death=`21/20/2`、target switch=`79`、projectile=`43/18/23/2`、duplicate=`0`，entity/membership/commit hash 与 9689 一致，p95=`17.821ms`，零 `VIOLATION/Rejected`。

[COMPUTED][HIGH] 9691 删除 Production GT Attack Planner 后仍在 step 600 PASS：alive=`18`、intent=`82`、impact/damage/death=`21/20/2`、target switch=`79`、projectile=`43/18/23/2`、duplicate=`0`、stale reject=`0`、p95=`17.880ms`。Generic Combat movement-lock、MovementPlanning parity 与 Mixed structure 定向门通过。

[COMPUTED][HIGH] 2026-08-02 全Worker T8 9687：两轮各900 Tick，fixed-step p95=`33.999/33.981ms`、realtime=`0.998/1.000`、boundary pending=`902/901`；Round 1 acquire/windup/spawn/impact/damage=`50/50/50/50/50`、duplicate=`0`、Hash=`439379904/1411313634/6141440`，零Violation/Rejected。T8业务、跨Round与realtime门均PASS。

[COMPUTED][HIGH] 9686中间证据：同步Production tail后两轮p95约`51ms`、realtime约`0.669`、pending约`1800`，证明只移除一个串行等待；9687提前普通Clock Intent后再移除一个等待。首次/换Plan/Target/非Production帧不使用该快路径。

[INFERRED][HIGH] WA8当前下一场景门是全Worker Mixed 600 Tick完整业务覆盖；最终结构删除与WA9规模矩阵仍OPEN。

[COMPUTED][HIGH] 2026-08-02 全 Worker T8 9685：Round 1 完成900 Tick后，同一 Generation、`resnapshots=1` 进入 Round 2 并推进到 step 300，无 Domain rejection；跨 Round continuation PASS。Round 1 性能仍为 p95=`67.854ms`、realtime=`0.500`、boundary pending=`2698`，所以性能门仍 OPEN。

[COMPUTED][HIGH] 2026-08-02 全 Worker T8 9680：900 Tick 完成 acquired/windup/spawned/impacted/damage=`50/50/50/50/50`、duplicate=`0`，ProjectileControl=`published 1/reused 899`，确定性 Hash=`439379904/1411313634/6141440`。业务门 PASS；性能门 OPEN（p95=`67.871ms`、realtime=`0.500`）。该次运行的Round 2 failure已由9685关闭。

[INFERRED][HIGH] 每个场景分别记录自动化、能力、性能和人工视觉；低层通过不能替代高层结论。

[INFERRED][HIGH] 规模结果必须标明生产路径。旧20实体Mixed、100实体SoftPressure和500实体Obstacle仍是历史分路径证据；当前PJ6结果来自同一Mixed Source/Resolver/Boundary/Networking/Projectiles生产路径。

[COMPUTED][HIGH] pre-T9提交`5b947389`固定DP0–DP6证据；T9现已由8517双端真实门关闭，不能再沿用“尚未执行”的历史断言。

[COMPUTED][HIGH] 2026-08-02 Projectile clock切片更新：9664 Mixed兼容路径step600 PASS；9680全Worker T8业务终局PASS并解除step9阻塞，9685又关闭Round 2 continuation。WA8仍因Mixed业务覆盖和realtime性能门为OPEN。

## WA全面Worker权威验证矩阵

| 门 | 目标条件 | 当前状态 |
|---|---|---|
| Ownership结构 | [INFERRED][HIGH] 每字段唯一Production Writer；最终只注册Input Sync/Result Apply两个模拟Processor | [COMPUTED][HIGH] WA0矩阵已冻结；当前四节点仍是Legacy Adapter，WA8前不满足最终结构 |
| Runtime v2内核 | [INFERRED][HIGH] Epoch去重、Current/Next传播、Time Wheel排序/取消、Dependency闭合、Resource原子交换、Dirty latest-wins、Event不可覆盖 | [COMPUTED][HIGH] WA1 PASS：RuntimeV2 11/11与MassCrowd 96/96通过；覆盖非阻塞Shard Host、固定Domain屏障、Dependency漏标、Correction/Generation失效、in-flight Invalidate/teardown、Work/Event背压和传播上限延期 |
| WA2 Movement切换 | [INFERRED][HIGH] 版本化环境、Worker Local Predictive、Time Wheel自主调度、封闭Canary后全实体Production | [COMPUTED][HIGH] PASS：RuntimeV2 19/19；Obstacle Canary日志`WA2_CanaryObstacleFull3.log`、Obstacle Production日志`WA2_ProductionObstacle.log`、SoftPressure Production日志`WA2_ProductionSoftPressure.log`均为20实体且硬失败0 |
| WA3 Particle切换 | [INFERRED][HIGH] 闭合集合、唯一Pair、双向约束、NeedsRecompute/OutputDirty分离、最终状态反馈及封闭Canary后全实体Production | [COMPUTED][HIGH] PASS：RuntimeV2 19/19；`WA3_ParticleCanaryFinalBaseline.log`和`WA3_ParticleProductionDependency.log`均为20实体SoftPressure且硬失败0，Production checkpoint为`particle_mode=2 particle_domain_tail=1` |
| WA4 Target切换 | [INFERRED][HIGH] Membership/Demand/Plan/Quota/Revision与资源订阅进入Worker，缓存未变Topology/Plan，封闭Canary后关闭GT Target/Guidance Writer | [COMPUTED][HIGH] PASS：RuntimeV2 20/20；9424 Shadow、9425 Canary、9431 Production通过。9431到step 300为`mode=2 verified=20`，无Violation；Production停止Legacy Target资源发布并由Worker proxy构建Target Guidance |
| WA5 Combat/Projectile切换 | [INFERRED][HIGH] Attack/Cooldown、Projectile/Impact、Damage/Death/Hit React在同一Worker stage推进，Combat结果在Movement前可见，Commit不借用Legacy摘要 | [COMPUTED][HIGH] PASS：T8 9451/9452/9453完成Shadow/闭合Canary/Production到step 300；T9 9467/9468/9469完成三模式到step 600且业务计数和entity/membership/commit hash一致。Demo Combat + active Projectile checkpoint + ordered-event baseline下一步逐字重放、Codec专项及RuntimeV2 21/21通过 |
| WA6 Lifecycle/Behavior切换 | [INFERRED][HIGH] Spawn完整初始化、Despawn先失效旧Lifecycle、Capability/Source/Command/Business Ledger由Worker持有，Production Commit只消费Worker prepared records | [COMPUTED][HIGH] PASS：RuntimeV2 24/24覆盖同批Lifecycle复用、Behavior Domain与Command ownership；Behavior prepared commit专项通过；T6M 9531、Friendly 9532、Mixed 9533 Production正式门通过，旧Lifecycle/重复Business Event均未进入提交 |
| 确定性 | [INFERRED][HIGH] 正序、逆序、随机Shard完成顺序的Entity/Resource/Event/Checkpoint Hash一致 | [COMPUTED][HIGH] 基础Planner/Merge已验证正序、逆序和乱序完成Hash一致，异步Host已验证跨Domain屏障；各真实Domain的数据Hash仍待WA2–WA7逐层补证 |
| Domain切换 | [INFERRED][HIGH] Particle、Target、Combat、Lifecycle、Behavior分别通过Shadow→封闭Canary→Production并关闭旧Writer | [COMPUTED][HIGH] Movement、Particle/Interaction、Target/Cohort、Combat/Projectile及Lifecycle/Behavior均已完成Production Owner切换；剩余网络权威来源与Legacy事务外壳分别由WA7/WA8收口 |
| 网络与Correction | [INFERRED][HIGH] Checkpoint→Resource Baseline→Event Baseline→Intent；Correction barrier取消旧Revision结果 | [COMPUTED][HIGH] WA7-R PASS：正式双PIE完成300 Epoch无纠错预测、单Movement Cell摘要失配与稀疏Correction恢复；无Checkpoint、Resnapshot或Runtime restart |
| 最终结构删除 | [INFERRED][HIGH] 四节点类、Frame Transaction、完整Mass Gather、Boundary Commit、`CallExecute()`、阻塞Wait和双Writer均为0 | [COMPUTED][HIGH] WA8进行中：Round/Friendly/Mixed普通Worker输入已为Intent；Mixed 9651跨Despawn→Spawn运行600 step并PASS。Target/Projectile剩余输入、Friendly/Mixed Legacy Runner与Round四阶段尚未删除 |
| 完整规模/性能 | [INFERRED][HIGH] 20/100/500与1k/2k/5k/10k；simulation lag p95≤66.667ms、GT apply不回退、propagation/event loss=0 | [INFERRED][HIGH] 待WA9；9321/9322仅Legacy功能基线 |
| 视觉与生命周期 | [INFERRED][HIGH] 双PIE、Correction、Late Join、地图切换、teardown、T7录像/FFmpeg无冻结/重影 | [INFERRED][HIGH] 待WA9新证据 |

## PW持久Worker目标验证矩阵

[COMPUTED][HIGH] PW0–PW8已完成。Production Movement由`PersistentRuntimeAuthority`单独拥有；Particle、Target、Combat因fail-closed证据不足继续留在强一致Boundary。最终自动化为MassCrowd 83/83、CrowdDemo 135/135，Development/DebugGame Editor `-DisableUnity`均通过。

| 门 | 目标条件 | 当前状态 |
|---|---|---|
| Input/Mirror合同 | [INFERRED][HIGH] 首次全量后只同步Lifecycle/Command/Resource/Dirty输入；Sequence缺口触发Resnapshot；Worker不读取Mass/World/UObject | [COMPUTED][HIGH] PASS：Round/Mixed/Friendly已接入；命令只在Prepared Commit成功后进入有界Journal并在Worker Batch接受后ACK；定向2/2与Runtime缺序列/Resnapshot专项通过 |
| 可变Batch交换 | [INFERRED][HIGH] GT每帧只交换一次冻结Published Batch并完整消费0/10/9999项；不追逐实时尾部、不固定实体配额 | [COMPUTED][HIGH] PASS：0/1/10/9999、同实体latest-wins、有序Event、三槽不可变和并发压力均通过；生产GT每帧一次Exchange |
| State/Event背压 | [INFERRED][HIGH] 同实体State latest-wins；Spawn/Despawn/Combat Event/Correction不丢失；有界队列满时fail-closed | [COMPUTED][HIGH] PASS：输入/Event容量和Violation锁存专项通过；1k–10k持续运行的Input Queue及ordered event depth均为0 |
| Worker所有权 | [INFERRED][HIGH] 已迁移字段只有Worker Writer；Mass为代理；无输入Echo、双写或旧Generation提交 | [COMPUTED][HIGH] PASS：Production Movement只消费`PersistentRuntimeAuthority` Domain Tail，普通Movement输入Echo被拒绝；Shadow/Canary不与Production并行写 |
| Shadow等价 | [INFERRED][HIGH] Mirror实体/Lifecycle/资源Hash与Mass一致；低耦合Kernel在任务乱序、Shard变化下与现行Boundary结果一致 | [COMPUTED][HIGH] PASS：SharedFlow/Facing/Business乱序与Shard 1–64稳定，Dynamic Flow Hash改为每Fixed Step一次并进入rollback snapshot |
| 生命周期 | [INFERRED][HIGH] Worker在执行时Correction、Plan替换、PIE停止、地图切换和Subsystem Deinitialize均无悬空访问或旧结果应用 | [COMPUTED][HIGH] PASS：跨Round绝对Simulation Time、Correction Revision、单进程双PIE独立Runtime和双World teardown通过 |
| 规模吞吐 | [INFERRED][HIGH] 1k/2k/5k/10k逐级记录Mirror lag、Simulation lag、scan coverage、Task critical、publish-to-consume和GT apply；无持续积压 | [COMPUTED][HIGH] PASS：9174/9175/9177/9179均到step 300且队列为0；10k Worker lag=`9.677ms`、scan=`1.549ms`、owner pump=`2.988ms`、GT apply=`0.403ms`。完整强一致Demo Boundary约145ms/step，不标记10k实时 |
| 视觉连续性 | [INFERRED][HIGH] 可变Batch下无长冻结、批次跳变、错误状态或Correction闪回；录屏与FFmpeg门通过 | [COMPUTED][HIGH] PASS：9180 T7 Production录屏20实体、58状态事件、0 mismatch、0 freeze，knockback/knockup/death三段切片已生成 |
| 单主体ISM闪色 | [INFERRED][HIGH] PICD slot 2只改变受击目标；无第二ISM、红色副本、重影或Z-fighting；0/1/10/9999与swap-remove守恒 | [COMPUTED][HIGH] 表现PASS、最终切片BLOCKED：9208逐帧显示Knockback 2个、KnockUp 2个约5帧衰减、Death 4个目标闪白，实例始终20；自动化与构建全过。9207/9213 Mixed 500在fixed-step 93触发既有PW `RequiresResnapshot`，未把该失败误记为材质回退 |

## AB异步Boundary验证矩阵

| 门 | 目标条件 | 当前状态 |
|---|---|---|
| 非阻塞Mailbox | [INFERRED][HIGH] 每World最多一个InFlight；Pending立即返回；普通Tick `Wait/Get/Event.Wait`计数为0 | [COMPUTED][HIGH] PASS：生产源码已无阻塞入口，Boundary定向自动化7/7通过；T5/T6五个双端场景的`ordinary_block_wait_count`均为0 |
| 事务原子性 | [INFERRED][HIGH] Round/Generation/Plan/Step/Sequence/Snapshot任一不匹配均整批拒绝且零写入 | [COMPUTED][HIGH] UNIT PASS：显式事务身份、snapshot mismatch和完整集合拒绝已覆盖；Correction跨帧专项仍待运行 |
| Worker所有权 | [INFERRED][HIGH] Worker只访问不可变POD/SoA；不访问Mass/World/UObject；GT是唯一持久writer | [COMPUTED][HIGH] CODE PASS：跨帧闭包只持有Snapshot、WorkState和Nav只读资源；GT continuation执行最终提交 |
| 调度确定性 | [INFERRED][HIGH] Task完成顺序、输入反序和1/7 Cohort产生相同Stage/Commit Hash | [INFERRED][HIGH] 待补异步完成顺序专项 |
| T5/T6功能 | [INFERRED][HIGH] T5S/T5M/T6A/T6S/T6M保持现有安全、inside/coverage、稳定和双端Hash合同 | [COMPUTED][HIGH] 四节点切换后 T5S 9316 与 T6M 9318 通过。capability切换后的Base+Target T5S 9321/9322进一步通过20实例、inside/coverage=`20/16`、Correction零误差和双端hash；但backlog p95=`170.807/136.398ms`未过性能门，T5M/T6A/T6S亦待新版本重跑 |
| 独立双端性能 | [INFERRED][HIGH] `ordinary_block_wait_count=0`、稳态catch-up budget hit=0、backlog p95≤66.667ms、client frame p95≤33.333ms | [COMPUTED][HIGH] PASS：五图fixed-step p95为17.980/17.947/17.825/17.857/18.338ms，backlog p95为31.284/31.809/31.668/31.788/32.391ms，realtime均约0.999，阻塞/stale/catch-up均为0 |
| 单进程双PIE性能 | [INFERRED][HIGH] 与独立双端使用相同门槛，且不能由独立进程结果替代 | [COMPUTED][HIGH] `CrowdDemo.PersistentWorker.SingleProcessDualPIE` 在四节点切换后定向 1/1 通过；完整性能窗口与 AB6 组合门仍待运行 |
| 生命周期 | [INFERRED][HIGH] Correction/新Plan/PIE停止/地图切换均无stale commit、悬空Event或Worker回调已销毁World | [COMPUTED][HIGH] 单元合同已实现；真实Correction/PIE停止/地图切换专项待运行 |
| 视觉连续性 | [INFERRED][HIGH] Pending帧继续插值；FFmpeg无长冻结、非Correction跳变或错误状态切换 | [INFERRED][HIGH] 待异步生产路径录制 |

[INFERRED][HIGH] AB矩阵关闭顺序固定为Mailbox单元测试→T5S首图→全部T5/T6→Round T1–T9/Mixed/Continuous/Friendly回归；不得用独立进程性能替代单进程双PIE门。

## T9 Mixed Combat Integration当前门

| 门 | 当前状态 | 最新证据 |
|---|---|---|
| 角色与攻击 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] 20实体10对10；每方4 Melee、2 MidRange、4 Ranged；三类intent=`61/31/21`。 |
| Impact/Damage/Death | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] 8517 impact/damage/death=`62/61/9`，friendly fire和重复提交由Prepared Adapter拒绝，运行无VIOLATION。 |
| 目标失效与重建 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] target switch/TargetRegion rebuild=`227/227`，referenced dead=`0`，死亡实体从即时Spatial目标集合排除。 |
| Projectile守恒 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] spawn/impact/expire/active=`21/5/15/1`，duplicate=`0`，守恒成立。 |
| 网络与性能 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] 双端entity hash=`7972685099634826285`、membership hash=`6035199850779644907`；resync/安全违规=`0`，服务端fixed-step p95=`2.910ms`。 |
| 自动化 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] MassCrowd 64/64、CrowdDemo 133/133。 |
| 构建矩阵 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] Development/DebugGame × ForceUnity/DisableUnity四构建成功。 |
| 旧Mixed回归 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] 8520双端旧Mixed生产门通过，4发Projectile守恒且Hash=`1098769576993558422`。 |

## DP0–DP6 Demo业务规划当前门

| 门 | 当前状态 | 完成要求 |
|---|---|---|
| DP0 基线 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] `07359ed`、65/65、125/125、四构建与Mixed 20/100/500结果已冻结。 |
| Planner Core | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] Registry/冻结、NoBusiness、反序、缺事实、容量、Host Intent和Stable Hash专项通过。 |
| Mixed角色 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] 五Planner、Reaction、目标丢失、Source精确恢复和Coordinator结构门通过。 |
| Friendly | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] 8303通过Claim/Pickup/Deliver/Requeue/fallback/backoff/cancel、守恒和失败零写入。 |
| Round T7/T8 | [COMPUTED][HIGH] PARTIAL | [COMPUTED][HIGH] T7历史门通过；9680全Worker T8业务50/50/50、duplicate=0，canonical SimulationTick Hash为439379904/1411313634/6141440；9685关闭Round 2 continuation。性能仍OPEN。 |
| NoBusiness | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] 8350 Continuous、8353–8363 T1–T6与8351 NavFlow通过统一入口且保持专项结果。 |
| 最终门 | [COMPUTED][HIGH] PASS | [COMPUTED][HIGH] MassCrowd 64/64、CrowdDemo 131/131、四构建、全部真实入口及Mixed 8311/8314/8315的20/100/500通过。 |

| 场景 | 核心能力 | 最新20实体技术/能力结果 | fixed-step p95 | 视觉状态 |
|---|---|---|---:|---|
| T1 | 测试参与集切换、压力传播、staging reset、新平衡 | [COMPUTED][HIGH] 6阶段、layer3、settling通过；全部 Mass 实体始终存在，不是 spawn/despawn；普通不连续=0，测试reset单列 | [COMPUTED][HIGH] 1.131ms | [INFERRED][HIGH] 当前版人工审片待补 |
| T2 | 开放cohort移动与目标handoff | [COMPUTED][HIGH] handoff/band/settled=20，coverage=16/16 | [COMPUTED][HIGH] 3.073ms | [INFERRED][HIGH] 当前版人工审片待补 |
| T3 | 开放双向交换 | [COMPUTED][HIGH] 10/10完成，deadlock=0 | [COMPUTED][HIGH] 2.938ms | [INFERRED][HIGH] 当前版人工审片待补 |
| T4 | 障碍走廊与汇入 | [COMPUTED][HIGH] wall/corridor/completed/settled=20 | [COMPUTED][HIGH] 3.376ms | [INFERRED][HIGH] 当前版人工审片待补 |
| T5S | 静态目标Region分布与稳定落位 | [COMPUTED][HIGH] inside20、coverage16/16；收敛后性能/技术门通过 | [COMPUTED][HIGH] 5.362ms | [INFERRED][HIGH] 当前版人工审片待补 |
| T5M | 移动目标Region分布 | [COMPUTED][HIGH] 8785安全/同步/Transport通过；稳定诊断valid=1、merge/chatter=0 | [COMPUTED][HIGH] 6.196ms；client Game/Render/GPU=4.234/5.536/5.073ms | [INFERRED][HIGH] 移动追随审片待补；不宣称静态settled |
| T6A | 异构走廊后按能力自然落位 | [COMPUTED][HIGH] corridor/completed/inside/coverage=20，7 profiles通过 | [COMPUTED][HIGH] 3.114ms | [INFERRED][HIGH] Region标记与朝向审片待补 |
| T6S/T6M | 异构静态/移动目标 | [COMPUTED][HIGH] T6S通过；T6M 8790 Round末inside/coverage=20/20，AcquireThenHold资格保持合同技术放行；90步最低18/17保留为过程诊断 | [COMPUTED][HIGH] T6S 4.261ms / T6M 12.137ms；client phases 10.332/6.852/5.802ms | [INFERRED][HIGH] 当前版人工审片待补 |
| T7 | VAT、受击、击退、死亡 | [COMPUTED][HIGH] 新阶段证据下8781/8783连续普通运行通过；历史8777失败未唯一归因 | [COMPUTED][HIGH] fixed-step约1.95–2.12ms；client frame p95 6.016/5.820ms | [INFERRED][HIGH] 当前版人工审片待补 |
| T8 | 远程攻击、Projectile、swept hit | [COMPUTED][HIGH] spawn/impact/damage=50，duplicate=0 | [COMPUTED][HIGH] 1.598ms | [INFERRED][HIGH] 当前版人工审片待补 |

## 当前版人工验收矩阵（2026-07-29）

| 场景组 | 无录屏性能轮 | 可视化/FFmpeg轮 | 人工判定重点 |
|---|---|---|---|
| T1 | [INFERRED][HIGH] 独立双端、20实体、性能门；test boundary reset单列 | [INFERRED][HIGH] 完整录像与连续性检查 | [INFERRED][HIGH] 参与集切换可见且新平衡成立；只允许已标记reset，不允许普通帧瞬移 |
| T2/T5/T6S/T6M | [INFERRED][HIGH] 独立双端、稳定窗口与性能门 | [INFERRED][HIGH] Region标记、完整录像、contact sheet | [INFERRED][HIGH] inside/coverage只是前提；必须继续检查目标相对速度、位置抖动、状态chatter和连续稳定落位 |
| T3/T4/T6A | [INFERRED][HIGH] 独立双端、完成/安全/性能门 | [INFERRED][HIGH] 通道入口、窄口、出口连续录像 | [INFERRED][HIGH] 验收安全通过和稳定离开出口；除非后续显式进入Target场景，否则不要求出口形成Region站位 |
| T7 | [INFERRED][HIGH] 不开录屏和标签，核对20实体、五态、Hit/Reactive计数、双端hash和性能 | [COMPUTED][HIGH] `-T7StateAcceptance`已提供expected/actual标签、JSONL sidecar、step 30/60/90自动短片和冻结诊断 | [INFERRED][HIGH] 分别核对Knockback进入/退出、KnockUp上升/apex/landing/recovery、Death与VAT；短暂复制延迟差异保留诊断 |
| T8 | [INFERRED][HIGH] 不开录屏，核对windup/spawn/impact/damage守恒、重复数和性能 | [INFERRED][HIGH] 后续复用T7 sidecar/切片机制，按Fire/Impact/Death事件切片 | [INFERRED][HIGH] 攻击VAT、发射、弹道、impact、HitFlash和受击响应时序一致 |

[INFERRED][HIGH] 每次人工验收必须保存五类产物：Server/Client日志、完整视频、完整contact sheet、权威状态/事件sidecar、专项事件短片。缺少权威sidecar时，视频只能证明“看起来发生了”，不能证明实体真实业务状态。

## 公共门

[COMPUTED][HIGH] 8790无Fatal、Assertion、Ensure、`LogWindows: Error`、VIOLATION或Native NetSerialize Warning，双端correction误差为0；T6M按AcquireThenHold资格保持合同技术放行，18/17严格窗口只保留为过程诊断。

[INFERRED][HIGH] 异步`fixed_step_ms`表示Request Submit到Result Commit端到端延迟，性能门为fixed-step p95≤66.667ms、client frame p95≤33.333ms、visual p95≤16.667ms、realtime≥0.95、step-limit hit=0；启动max、Round reset、catch-up和steady discontinuity必须单列。

| 规模 | 当前结论 |
|---|---|
| 20 | [COMPUTED][HIGH] 8402在step600通过同一Source/Resolver/Boundary/Networking/Projectiles路径；active/visible=`20/20`，4发Projectile的spawn/impact/damage=`4/4/4`、duplicate=`0`，服务端/客户端p95=`2.152/4.963ms`，最小间距=`70.11cm`，双端实体/成员/Projectile Hash一致且无resync或违规。 |
| 100 | [COMPUTED][HIGH] 8403在step630通过同一路径；active/visible=`100/100`，20发Projectile的spawn/impact/damage=`20/20/20`、duplicate=`0`，服务端/客户端p95=`9.675/4.938ms`，最小间距=`70.03cm`，双端实体/成员/Projectile Hash一致且无resync或违规。 |
| 500 | [COMPUTED][HIGH] 8401在step630通过同一路径；active/visible=`500/500`，100发Projectile的spawn/impact/damage=`100/100/100`、duplicate=`0`，服务端/客户端p95=`30.016/5.171ms`，最小间距=`70.00cm`，双端实体/成员/Projectile Hash一致且无resync或违规。 |

## 生产生命周期与复制场景

| 场景 | 当前状态 |
|---|---|
| StableEntityRef/Capability/Behavior POD 与 Runtime 映射 | [COMPUTED][HIGH] 阶段 B 已通过 Core AgentFacts 与 Runtime AgentFactsMapping 自动化；覆盖 lifecycle 区分、能力门、Faction/Capability 解耦、非法位与可选引用、Runtime 往返映射。 |
| Relevant Snapshot header/chunks 与 assembly | [COMPUTED][HIGH] 阶段 C 纯协议3/3与阶段 D Demo adapter 3/3通过；8773客户端经真实网络组装20 agents、1 chunk、3720 bytes并进入现有bootstrap消费入口。覆盖重分块、任意顺序、重复/冲突、stale、损坏hash、bounds、empty、缺块、timeout与合成500实体。 |
| 分批 spawn/despawn、死亡移除、LifecycleSerial 复用 | [COMPUTED][HIGH] E协议与F最小Mass World通过；G的8777真实双端路径从10增至20上限并持续Membership/Despawn/Respawn。序列12明确slot 2 serial 1以Death移除，序列13以serial 2重生；T1仍不覆盖生命周期。 |
| Spawn/Despawn 乱序与 stale Lifecycle 拒绝 | [COMPUTED][HIGH] E覆盖严格sequence/重复/缺序列/原子拒绝；F真实World覆盖stale correction/despawn不改变active entity与完整集合hash。 |
| late join snapshot + 后续 delta | [COMPUTED][HIGH] P3公共channel已通过真实延迟加入：J 7977 baseline=`20 entities/3 chunks`后连续消费state/correction至step600；Continuous 7975从当前19实体baseline恢复并继续可靠序列，双端在sequence 32集合hash一致。 |
| 动态 Relevancy 与 Membership Delta | [COMPUTED][HIGH] `FCrowdSpatialGridRelevantSetProvider`已通过稳定排序与关系闭包自动化；J/Continuous已通过Membership可靠序列。真实视区移动触发enter/exit的双客户端场景尚未单独保存证据。 |
| 客户端视觉实例增量创建/回收 | [COMPUTED][HIGH] 公共Presentation slot table与Demo ISM sink已接管J/Continuous；7975 active/visible恒等，7977 step600 active/visible=`20/20`，swap-remove和重复/stale由定向测试覆盖。 |
| Cargo/Combat 跨 Source 组合 | [COMPUTED][HIGH] Demo Provider覆盖领域Source，控制器对期望持久集合执行Start/Update/Stop Diff；临时压制结束后原Source实例与持久状态继续存在。Mixed 20/100/500均走同一Resolver/安全链。 |
| NavMesh Surface Graph / Shared Flow | [COMPUTED][HIGH] I的合成图测试覆盖确定性、桥上桥下XY重叠、窄门/落差拒绝、layer attach、动态目标rebind；`CrowdDemo_NavSurfaceGraphVerticalSmall`的8800真实Recast运行得到98 nodes、234 directed edges、4 layers、13 overlap、76 reachable sloped edges、8/8 reachable markers与drop unreachable。视觉证据为`Saved/StageI_NavSurfaceGraph_Visual.png`。 |
| continuous lifecycle / Sandbox | [COMPUTED][HIGH] 当前Mixed双端路径组合LifecycleWorld、Source World Store、Combat、Logistics、NavMesh Flow与增量ISM；Movement/Facing/Constraint消费Resolver结果并进入`FCrowdMassMovementPipelineWork → Particle Constraint → Facing Finalize`。StandardSources升级后的20/100/500均已通过同一路径复测。 |

## Behavior Source专项门

| 专项 | 当前状态 |
|---|---|
| Core Source状态机 | [COMPUTED][HIGH] 已覆盖Profile/Modifier、16 Source、命令幂等/冲突/缺口、过期、Capability撤销和反序Hash。 |
| Resolver | [COMPUTED][HIGH] 六通道排序、Q15 Blend、Override/Additive、Constraint、冲突、容量、输入反序和稳定Hash由Core/Fixture自动化覆盖；Mixed生产Movement/Facing直接消费Resolved结果。 |
| Runtime原子性 | [COMPUTED][HIGH] Source staging、Prepared Hash、Patch稳定排序、篡改拒绝与失败零写入自动化存在；Mixed只在全部Prepare/Validate成功后Final Apply。 |
| Behavior网络 | [COMPUTED][HIGH] v3 Codec携带Registry/Context/State Schema与实例状态，已接入生产可靠状态、late join、分批发送和Hash resync；Fixture覆盖Predictable/ResolvedOnly回放。 |
| StateTree | [COMPUTED][HIGH] Adapter已拆为默认禁用兄弟插件并通过独立构建Smoke；真实业务Task已从现行框架门移除。 |
| Mass Projectile | [COMPUTED][HIGH] T8专项13/13继续覆盖Mass权威生命周期、相对/环境Sweep、Broadphase、墙体优先、Faction/NavLayer、Pierce、通用Impact/Hit与唯一宿主伤害；PJ6新增Spatial/Combat/Projectiles公共专项与结构门，禁止公共模块引用Demo、持久Projectile数组、Projectile×Agent全扫描和Runtime反向依赖Projectiles。 |
| 同路径规模 | [COMPUTED][HIGH] PJ6 Mixed Source/Resolver/Boundary/Networking/Projectiles路径已依次通过20/100/500；每种规模全部实体执行Standard Source，并并发4/20/100发公共Mass Projectile，spawn/impact/damage守恒、零重复、双端Hash一致、零安全违规。 |

## Standard Sources S0–S6专项门

| 专项 | 当前状态 |
|---|---|
| 模块边界 | [COMPUTED][HIGH] `MassCrowdStandardSources`已作为随包Runtime模块加载，只单向依赖Core/Runtime；Runtime/Core无反向依赖或Standard TypeId分支。 |
| Target Context | [COMPUTED][HIGH] `TargetKinematicsV1`与`FormationAnchorV1`均为不超过96字节的trivially-copyable POD；缺失、版本、Ref、Revision和非有限值均有拒绝专项。 |
| 基础Source库 | [COMPUTED][HIGH] 13种标准Source自主Evaluator已实现；定向8/8覆盖位置Goal、目标预测、Flee、Distance迟滞、Facing/Constraint、Wander回放、Formation和Impulse。 |
| 组合Recipe | [COMPUTED][HIGH] Mixed五Controller稳定Diff专项5/5覆盖无变化零命令、Escort、Pursue+Attack、显式一帧Lock、目标丢失Stop和HitReaction持久Source精确恢复。 |
| 完整运动安全链 | [COMPUTED][HIGH] 20/100/500均执行Resolved Goal/Movement/Facing/Constraint → Local Predictive → Particle/Bounds → Facing → Final Safety/Prepared Apply；PJ6修复候选NavLayer与安全Hold的原子提交缺口后，8402/8403/8401最小同层间距分别为`70.11/70.03/70.00cm`且零违规。 |
| 网络与规模 | [COMPUTED][HIGH] Codec v3覆盖Registry/Context/State、旧版本拒绝和Hash不符；第三方Fixture覆盖三复制策略。PJ6 20/100/500双端late join均达到全集active/visible、实体/成员/Projectile Hash一致和零resync；服务端p95=`2.152/9.675/30.016ms`，客户端p95=`4.963/4.938/5.171ms`。 |

## P0–P5 产品化验证矩阵

| 阶段 | 当前证据与关闭条件 |
|---|---|
| P0 合同与事实 | [COMPUTED][HIGH] 文档状态、查询所有权、J直接所有权、模块加载状态与公共API缺口已交叉核对；只需全文扫描、反向依赖扫描和`git diff --check`，不以编译替代文档闭环。 |
| P1 Boundary Orchestrator | [COMPUTED][HIGH] 历史P1覆盖依赖、Worker执行、失败、稳定hash和两阶段patch；2026-07-30已迁移为非阻塞Poll与事务身份，定向自动化7/7通过。 |
| P2 Nav Runtime | [COMPUTED][HIGH] provider/resource/Flow key/refcount/LRU/budget定向测试已通过；8156 `NavFlowProductSmall`通过98 nodes、234 directed edges、4 layers、2个Flow资源与20实体boundary。 |
| P3 Networking/Presentation | [COMPUTED][HIGH] Networking 9/9、Presentation 1/1及累计MassCrowd 36/36通过；真实J/Continuous late join、可靠序列、实例恒等通过。真实移动视区enter/exit仍是保留风险，但不再是公共API缺失。 |
| P4 FriendlyLogisticsSmall | [COMPUTED][HIGH] 8154专用地图通过20实体竞争、数量守恒、幂等、死亡恢复、fallback、不可达退避和late join；双端hash=`3180435972084878253`，Cargo attach/detach=`2/2`、实例=`20`并保存近景证据。 |
| P5 统一路径 | [COMPUTED][HIGH] 8151旧Round公共baseline/state/correction/ResultHeader通过；8153 J step600双端通过；8157常驻与延迟客户端分别从resume=`1766/4508`恢复并通过。实体Presentation profile所有权固定为单一公共路径。 |

## 2026-07-23 产品化续跑

| 入口 | 结果 |
|---|---|
| `NavFlowProductSmall` 8122 | [COMPUTED][HIGH] 双端通过；98节点、234有向边、4层，Flow resource/ref=`2/2`、cache hit/miss=`1/2`、9504字节；20实体P1 boundary提交hash=`9514377555178196070`，无硬错误。 |
| `FriendlyLogisticsSmall` 8125 | [COMPUTED][HIGH] 延迟客户端通过公共baseline/reliable state恢复；20实体、source/sink=`35/5`、in-transit=0、竞争/死亡恢复/fallback/取消=`1/1/1/1`、退避=2，双端hash=`3180435972084878253`，无硬错误。 |
| J Mixed 8126 | [COMPUTED][HIGH] 删除O(N)安全旁路后的step600双端通过；active/visible=`20/20`、transitions=29、pickup/delivery=`4/1`、combat=500、spawn/despawn=`3/3`、membership=7、最小间距=`71.51cm`、p95=`1.763/4.640ms`，双端hash一致。 |
| 累计自动化 | [COMPUTED][HIGH] Development/DebugGame `-DisableUnity`通过，MassCrowd=`40/40`，CrowdDemo=`115/115`。 |
| P1 Round 8132/8137/8138/8139 | [COMPUTED][HIGH] T2/T6/T7/T8双端通过，fixed-step p95=`2.581/5.140/1.853/1.525ms`，客户端frame p95均低于门限；T6首轮旧同步预Wait验证误报已修复并重跑，无硬错误。 |
| P5 Round/J/P4/Nav/late join 8151/8153/8154/8156/8157 | [COMPUTED][HIGH] Round公共ResultHeader=`1146 bytes`且correction=`20/20`；J active/visible=`20/20`、业务与hash无回退；P4 Cargo视觉通过；Nav graph/boundary通过；双客户端baseline resume连续。全部场景零硬错误。 |

[RULES I BROKE]：[COMPUTED][HIGH] P1未关闭时继续实施了P2/P3/P5切片，违反“失败留在当前阶段、不得跨阶段规避”的阶段顺序；修改本身保持模块边界，但阶段门没有被遵守。

## 2026-07-28 当前工作树回归

| 场景/门 | 当前证据 |
|---|---|
| P5 J 7939 | [COMPUTED][HIGH] step600 active/visible=`20/20`，transition=29，pickup/delivery=`4/1`，spawn/despawn=`3/3`，membership=7，最小间距=`71.51cm`，服务端fixed-step p95=`1.972ms`；双端无resync和硬错误。隐藏客户端Actor Tick p95=`400ms`不作为渲染性能通过证据。 |
| P4 FriendlyLogistics 7953 | [COMPUTED][HIGH] 20实体、总量40、交付5、竞争/死亡恢复/fallback/取消=`1/1/1/1`、退避2、最大单步位移=`8.667cm`、双端hash=`3180435972084878253`；客户端实例20、Cargo attach/detach=`1/1`。 |
| Continuous 7946 | [COMPUTED][HIGH] late join后active/visible保持恒等，最终sequence 53、entity-set hash=`7875336925641762435`，无stale或硬错误。 |
| Round 7948–7951 | [COMPUTED][HIGH] T2/T6/T7/T8双端通过，服务端fixed-step p95=`2.869/5.628/2.079/1.628ms`，correction与输入hash门通过，无硬错误。 |
| 累计自动化/构建 | [COMPUTED][HIGH] Development/DebugGame Editor `-DisableUnity`通过；MassCrowd 43/43、CrowdDemo 115/115通过。测试发现前两条既有`Condition failed`启动噪声保留记录，但没有失败测试。 |

## 2026-08-02 WA8 T8 局部预测失效回归

| 场景/门 | 当前证据 |
|---|---|
| LocalPredictive 精确失败重放 | [COMPUTED][HIGH] 9741 在 fixed step 416 捕获候选 hash=`2662689854`，重放 hash 相同，`attempted/matched/valid=1/1/1`；20 个 initial/completed result 均来自同一不可变 Worker 输入。 |
| 继承重叠合同 | [COMPUTED][HIGH] `MassCrowd.Core.LocalPredictive.InheritedOverlapRecovery` 验证 80cm 起始间距、94cm 要求下的单调分离通过、继续深入失败；Core LocalPredictive 4/4 和 Demo/Core/Runtime 等价 1/1 通过。 |
| T8 原失效窗口 | [COMPUTED][HIGH] 9742 使用 Movement/Behavior/Particle/Projectile/Combat 全 Production，越过原 step 416 且脚本退出 0；9743 延长至 Worker epoch 771，Runtime failure=0，LocalPredictive/Particle/Projectile/全部 `VIOLATION` 为零，覆盖原 474/616 后续失败窗口。 |
| Projectile 时序观察 | [COMPUTED][MED] 9737 曾在 step 39 出现一次 Worker/Legacy Projectile 状态 hash 不同但状态/事件数量相同；9740/9741/9742/9743 未复现。失败分支现会按 ProjectileId 输出 Age/Position/Velocity/生命周期字段；该观察保持 OPEN。 |
| 验收边界 | [INFERRED][HIGH] 上述证据关闭 20 实体 T8 的确定性局部预测→Particle 传播缺陷；它不替代双 PIE、Late Join、Correction 或 WA9 三个 10k 场景门。 |

## 2026-08-03 WA8 T5 Target Objective → Particle 回归

| 场景/门 | 当前证据 |
|---|---|
| Objective 动态外部实体 | [COMPUTED][HIGH] `MassCrowd.RuntimeV2.ParticleUsesLiveTargetObjective` 以 10000cm 外的冻结模板和 50cm 的 live Objective 构造反证；Worker Particle 产生非零位移，并声明/观察 Objective Resource 依赖。 |
| Particle 精确闭包 | [COMPUTED][HIGH] `MassCrowd.Core.ParticleConstraint` 6/6 通过；其中 `QuantizedSweptClosureRegression` 精确覆盖 9748、9750、9753、9754 的四个 21-agent fixture，并在 1000 节点/稳定顺序的有界搜索下通过。 |
| T5 Production 9756 | [COMPUTED][HIGH] Movement/Behavior/Target/Particle/Projectile/Combat 全 Production 达到 step 600；step 300/600 task=`5`，Runtime failure、simulation lag、stale lifecycle 均为 0，日志无 `VIOLATION/Rejected`，无 Particle failure fixture。 |
| 增量输入 | [COMPUTED][HIGH] step 300/600 的 Intent 均为 `resources=1`、spawn/despawn/profile=`0/0/0`；唯一普通资源是移动 Objective Revision，没有完整 Target cohort 或实体状态载荷。 |
| 验收边界 | [INFERRED][HIGH] 9756 是 20 实体服务器 T5 合同门，不是双 PIE、视觉连续性或 10k WA9 性能门。 |

## 2026-08-03 WA8 Round 四阶段删除回归

| 场景/门 | 当前证据 |
|---|---|
| 结构门 | [COMPUTED][HIGH] 四个 `Execute*Stage` 符号和 `PollRoundWorkBatch` 为零；Input Sync 直接 Plan Apply，Result Apply 只有一个 `AdvanceRoundWorkerFrame`；Processor header 中恰有两个 `UMassProcessor`。Development DisableUnity 与 PostFinalizeMinimalQuery 通过。 |
| T5 9757 | [COMPUTED][HIGH] 全 Production 达到 step 600；task=`5`、stale=`0`、ordinary block wait=`0`、simulation lag=`0`，无硬错误或 Particle fixture。 |
| T8 配置反例 9758 | [COMPUTED][HIGH] T8 无 Target cohort，额外强制 Target Production 会在 bootstrap 正确 fail-closed 为 `WorkerTargetMissing`；该运行不作为阶段合并回归。 |
| T8 修复与 9764 | [COMPUTED][HIGH] ImpactId 改为 Tick-major；Movement Profile v2 / Control v9 保留权威 Guidance 所有权。14 项 T8、两个 Movement 定向门及全 Production 900 Tick 通过：50/50/50/50/50，duplicate/expired=`0/0`，三哈希=`439379904/1411313634/6141440`，p95=`34.181ms`、realtime=`0.998`。 |
| 验收边界 | [INFERRED][HIGH] 四阶段结构删除、T5 与 T8 黄金门通过；WA8 总关闭仍为 OPEN，因为 Round 仍消费 WorkGraph/Orchestrator。下一门是迁移最后消费者并执行 AST/注册零消费者审计。 |
