# AB5 四节点 Round Boundary 合同

> [INFERRED][HIGH] **历史合同，已被取代。** 全面Worker权威方案已取代本文作为AB5终态；现有四节点仅作为WA0–WA8迁移期`Legacy Domain Adapter`原样保留。不得继续以本文扩建长期Boundary架构。现行目标以`FullWorkerAuthorityArchitecture.md`与`FullWorkerAuthorityOwnershipMatrix.md`为准。

## 1. 冻结范围

[INFERRED][HIGH] AB5 不注册全部子 Stage，也不保留单一顶层总控 Processor；只动态注册四个真正跨越 GT/Mass 边界的 Processor。Particle、Target、Combat 继续保持强一致 Boundary，不迁入 Worker。原工程迁移、T10、动态 NavMesh 和真实 StateTree Task 不属于本阶段。

[INFERRED][HIGH] 四个 Processor 均运行于 `PrePhysics`，均设置 `bAutoRegisterWithProcessingPhases=false`，由每 World `UCrowdDemoMassSubsystem` 成组注册并按逆序注销。生产顺序固定为：

```text
MassReplication
→ CrowdDemo.Round.AuthorityInput
→ CrowdDemo.Round.ResultCommit
→ CrowdDemo.Round.PostCommit
→ CrowdDemo.Round.RequestSubmit
→ Client Visual / Mass Visual
```

[INFERRED][HIGH] 四节点必须同时声明稳定 `ExecuteInGroup` 与显式 `ExecuteAfter/ExecuteBefore`，不得依赖 UObject 创建顺序或动态注册顺序。

## 2. 四节点职责

[INFERRED][HIGH] `UCrowdDemoRoundAuthorityInputApplyProcessor` 开始本 World 的 Frame Transaction，按 Plan、Correction、RoundResult 和 PW Published Result 的权威顺序应用输入。Correction 必须先递增现有 Generation、失效 Runner，再记录重新 Gather。

[INFERRED][HIGH] `UCrowdDemoRoundBoundaryResultCommitProcessor` 每帧最多 Poll 一次。Ready Result 必须验证 Generation、PlanRevision、FixedStep、Boundary Sequence、SnapshotHash、StableEntityRef/Lifecycle 和完整集合，随后进入唯一原子 Commit；Pending 立即返回。

[INFERRED][HIGH] `UCrowdDemoRoundPostCommitPublishProcessor` 只处理尚未发布的已提交事务，发布 Checkpoint、网络状态、指标、诊断和提交后的 Source/业务事实；成功后才推进单调 `LastPublishedCommitSequence`，且不得重新读取 Mass。

[INFERRED][HIGH] `UCrowdDemoRoundBoundaryRequestSubmitProcessor` 只在 Mailbox 空、无未处理 Correction、fixed step 到期且上一 Commit 已发布时执行 canonical gather、Request build 与 TrySubmit。失败必须清除半成品且不推进提交序列；Result N Commit 前禁止 Gather N+1。

## 3. Frame Transaction

[COMPUTED][HIGH] 通用阶段状态机现位于插件 `MassCrowdRuntime` 的 `FCrowdBoundaryFrameTransaction`；它保存 GameFrame、事务身份快照、最后 Poll/Commit/Publish/Submit 序列、四阶段显式结果和本帧幂等标志。项目侧 `FCrowdDemoRoundFrameTransactionState` 只继承该合同并附加 Round 性能采样字段；跨帧实例仍只属于每 World `UCrowdDemoRoundSimPipelineSubsystem`。

[INFERRED][HIGH] Transaction 的 Generation、Boundary Sequence 和 Revision 只引用现有 Pipeline/Runner 权威，不建立第二套可递增身份。Subsystem 暴露 `BeginFrame`、`ApplyAuthorityInputs`、`TryCommitResult`、`PublishCommittedResult`、`TrySubmitRequest` 五个阶段 API；每个入口验证前置阶段与本帧调用次数。

[INFERRED][HIGH] 阶段结果使用显式枚举表达 Empty、Pending、Ready、Committed、Failed、Stale、Invalidated、AlreadyProcessed。节点缺席、乱序或重复执行必须 fail-closed，并记录阶段、GameFrame、Generation 和事务 Sequence。

## 4. Query 与 Adapter

[INFERRED][HIGH] Authority Apply、Result Commit、Request Submit 只声明实际所需 Query；PostCommit 不读取 Mass。不得建立覆盖全部 Fragment 的统一超集 Query。

[COMPUTED][HIGH] 四个注册 `UMassProcessor` 直接拥有阶段入口和各自 Query。原内部 `UObject`/伪 Processor 已降为非反射、无调度权的纯 C++ Stage；它们不持有跨帧状态。只有 Authority、Gather 和原子 Commit Stage 接收所属注册节点显式传入的 Query，PostCommit Stage 不读取 Mass。

[INFERRED][HIGH] 切换后删除生产路径的子 Processor `UPROPERTY`、`MakeDynamicRoundProcessor`、`ROUND_DYNAMIC_FLAGS` 和手工 `CallExecute()`；不得保留未注册但仍手工执行的 `UMassProcessor`。

## 5. Mass 访问与 archetype

[INFERRED][HIGH] 普通无 Correction fixed step 的运行计数必须为 canonical gather-read=`1`、intermediate-read=`0`、commit-write=`1`。Authority/Correction 写入单独计数，不伪装为普通 step。

[INFERRED][HIGH] Agent Template 按 capability bitset 缓存：Base 必选，Target 与 Combat 为可选 bundle，Projectile 继续使用独立 Trait/archetype。Target 使用独立 capability tag；Combat bundle 包含 Stats、Business、Attack、Reactive 和 HitFlash fragments。Template 选择来自实体 capability，不按 T1–T9 名称硬编码。

## 6. 切换与关闭门

[COMPUTED][HIGH] 四节点已成为唯一生产路径；旧 `UCrowdDemoRoundSimFixedStepPipelineProcessor`、项目侧持有字段、初始化入口及查询转发器已经物理删除，没有保留 A/B 兼容开关。

[COMPUTED][HIGH] 2026-07-30 Runtime 前置已恢复：定向 WorkerRuntime 5/5 通过；Mixed 500 无客户端 9304/9305 与双端 9306/9307 各连续两轮通过。根因包含 admission/apply 序列混用以及到期 Behavior Command 未从 Worker Mirror 有界队列消费。

[COMPUTED][HIGH] 四节点直接执行切换后的 Development/DebugGame × ForceUnity/DisableUnity 四构建、MassCrowd 85/85、CrowdDemo 138/138、插件 `MassCrowd.Runtime.BoundaryFrameTransaction` 1/1、项目 `CrowdDemo.Architecture` 2/2 及单进程双 PIE 定向 1/1 通过。

[COMPUTED][HIGH] 新 T5S 双端 9316 通过：inside/feasible coverage=`20/16`，fixed-step/backlog p95=`34.137/36.949ms`，realtime=`0.998`，client frame/visual p95=`5.302/0.096ms`；ordinary wait、stale result、step-limit 和非 Correction 跳变均为 0。完整真实场景与 AB6 专项仍未完成，因此 AB5 尚不关闭。

[COMPUTED][HIGH] 新 T6M 双端 9318 通过：inside/feasible coverage=`20/20`，Target/Transport valid=`1/1`，fixed-step/backlog p95=`34.327/60.528ms`，realtime=`0.998`，client frame/visual p95=`5.347/0.096ms`；双端 input/flow hash 匹配，ordinary wait、stale result、step-limit 和非 Correction 跳变均为 0。

[COMPUTED][HIGH] capability实际运行切换后，Base+Target T5S 9321/9322均完成20实例复制、inside/feasible coverage=`20/16`、Correction零误差及双端hash匹配；完整CrowdDemo自动化更新为139/139，四构建通过。但两轮backlog p95=`170.807/136.398ms`均超过`66.667ms`，只能作为功能证据，不能关闭AB5性能门。

[INFERRED][HIGH] AB5 只有在四节点结构扫描、节点幂等/乱序/失败零部分写入、Query 1/0/1、capability archetype、完整自动化、四构建和真实场景门全部通过后才能关闭；AB6 必须使用新证据重跑，不复用 PW8 日志。
