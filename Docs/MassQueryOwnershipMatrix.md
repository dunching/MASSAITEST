# RoundSim Mass 查询与数据所有权矩阵

## 1. 文档职责

[COMPUTED][HIGH] 本文件记录当前 RoundSim fixed-step 中仍存在的 Mass 查询、持久 fragment、boundary prepared SoA 及其唯一生产者/消费者。

[INFERRED][HIGH] 本矩阵是继续拆分 archetype 前的约束：中间数据所有权未收敛前，不把同一份兼容债务复制到多个 archetype。

## 2. Fixed-step 查询矩阵

| 阶段 | 当前 Mass 遍历 | 所有权与保留理由 | 状态 |
|---|---:|---|---|
| `RoundPlanApply` | 多次、仅 Round 激活边界 | [COMPUTED][HIGH] 原子重置并同步身份、稳定初态、业务状态和 Runtime 持久 fragment。 | [INFERRED][HIGH] 暂保留；后续应单独收敛 Round 激活事务。 |
| `BoundaryGather` | 1 | [COMPUTED][HIGH] 每个 fixed-step 唯一完整基础事实读取，生成按StableEntityRef排序的不可变boundary snapshot，并组合版本化Behavior/Combat/Business overlay。 | [COMPUTED][HIGH] P1已完成。 |
| `SharedFlowFieldBuild` | 0 | [COMPUTED][HIGH] 只消费规则、环境和缓存的 Flow 资源。 | [COMPUTED][HIGH] 已收敛。 |
| `FlowPreferredVelocity` | 1 | [COMPUTED][HIGH] 算法读取 boundary snapshot；唯一遍历只写需要被 correction rollback 恢复的 `RoundFlowSample`。 | [COMPUTED][HIGH] 第24切片由2次降为1次。 |
| `TargetFact/Topology/Demand/Plan` | 0 | [COMPUTED][HIGH] 只消费 Pipeline 中的稳定 POD/SoA 和 Round 资源。 | [COMPUTED][HIGH] 已收敛。 |
| `TargetRegionGuidance` | 0 | [COMPUTED][HIGH] 使用 boundary snapshot 的位置、Yaw、身份构造 Guidance candidate。 | [COMPUTED][HIGH] 第24切片删除重复查询。 |
| `CombatBoundary` | 2 | [COMPUTED][HIGH] canonical gather读取持久业务事实，Worker纯Prepare生成不可变patch，完整envelope验证后由唯一GT writer提交Stats/Business/Attack/Reactive/HitFlash/Visual与projectile/event。 | [COMPUTED][HIGH] P1两阶段业务adapter已完成。 |
| `MovementWork` | 0 | [COMPUTED][HIGH] SF1与SoftPressure均消费snapshot/prepared SoA并发布`PreparedRuntimePredictedMovements`；不再写阶段中转fragment。 | [COMPUTED][HIGH] 第25切片已收敛。 |
| `ParticleConstraintSolve` | 0 | [COMPUTED][HIGH] 全局粒子安全只消费不可变 POD 并发布 prepared 结果。 | [COMPUTED][HIGH] 已收敛。 |
| `ObstacleConstraint` | 0 | [COMPUTED][HIGH] SF1按boundary snapshot校验并消费prepared predicted movements，调用原Shared Flow障碍kernel后发布prepared final kinematics。 | [COMPUTED][HIGH] 第25切片已收敛。 |
| `FacingFinalize` | 2 | [COMPUTED][HIGH] settle/Facing跨boundary历史已并入唯一`BoundaryGather`并发布为按AgentId排序的prepared fact；FacingFinalize只保留完整身份/Lifecycle/结果预验证和唯一原子提交。 | [COMPUTED][HIGH] 第26切片已收敛；预验证与写回继续分离，避免集合不完整时产生部分提交。 |
| `PostFinalizeMetrics` | 0 | [COMPUTED][HIGH] 消费 prepared post-finalize records，不再回读 Mass。 | [COMPUTED][HIGH] 已收敛。 |
| `Authority/ClientPrediction/Checkpoint` | 0 | [COMPUTED][HIGH] 消费 finalized prepared records和checkpoint states。 | [COMPUTED][HIGH] 已收敛。 |

## 3. Fragment 所有权

| Fragment | 当前语义 | 决定 |
|---|---|---|
| `MassIdentity`、`RoundFormation`、`RoundSimState` | [COMPUTED][HIGH] Demo稳定身份、阵型事实和权威/预测模拟状态。 | [COMPUTED][HIGH] 保留。 |
| `RoundFlowSample` | [COMPUTED][HIGH] Flow诊断及 correction rollback 所需的持久采样状态。 | [COMPUTED][HIGH] 保留一次写回。 |
| `ParticleProperties` | [COMPUTED][HIGH] 半径、HardGap、SoftMargin、Mobility和CapabilityProfile的正式输入。 | [COMPUTED][HIGH] 保留。 |
| Runtime `Agent/SimulationState/Properties/Facing/MovementOutput` | [COMPUTED][HIGH] 插件 Runtime 的持久公共合同与最终输出。 | [COMPUTED][HIGH] 保留。 |
| `Stats/Business/RangedAttack/ReactiveMotion/HitFlash/Visual` | [COMPUTED][HIGH] T7/T8业务、复制和表现跨boundary状态。 | [COMPUTED][HIGH] 保留在Demo业务层。 |

## 2026-07-23 Boundary 与产品场景补充

| 资源/查询 | 当前所有权 | 写入合同 |
|---|---|---|
| Round Business/Combat overlay | [COMPUTED][HIGH] Demo在canonical gather复制为不可变POD；Worker Business task独占prepared输出。 | [COMPUTED][HIGH] 只由最终GT writer在CommitEnvelope全量验证后提交。 |
| SharedFlow/Target cohort | [COMPUTED][HIGH] Runtime WORK；Target按稳定CohortKey形成独立Topology/Demand/Plan/Guidance任务。 | [COMPUTED][HIGH] 资源与诊断patch在同一事务最终发布。 |
| Obstacle/Movement/Particle/Facing | [COMPUTED][HIGH] 同一typed boundary DAG。 | [COMPUTED][HIGH] 无第二套同步Movement或Facing DAG。 |
| J空间安全 | [COMPUTED][HIGH] Runtime `FCrowdSpatialSafetyIndex`拥有索引；J只提供StableEntityRef/位置/半径POD。 | [COMPUTED][HIGH] 每步稳定Build，候选移动使用邻格查询，成功后增量Update。 |
| Friendly Logistics库存/Task/Cargo | [COMPUTED][HIGH] Runtime transaction store拥有通用事实与patch；Demo拥有planner/fault policy。 | [COMPUTED][HIGH] Prepare/Validate后ApplyPrepared；公共Networking只复制事实，不决定库存规则。 |
| `ClientAuthority/ClientVisualOffset` | [COMPUTED][HIGH] Networking/Presentation客户端持久状态。 | [COMPUTED][HIGH] 保留。 |
| `RoundProposedMovement/ObstacleConstraint` | [COMPUTED][HIGH] 曾仅服务SF1预测→障碍→Finalize的中间桥。 | [COMPUTED][HIGH] 第25切片迁入prepared SoA后已物理删除。 |
| `ReactiveMotionStep` | [COMPUTED][HIGH] 仅在同一boundary由Combat写、Movement读。 | [COMPUTED][HIGH] 第24切片已替换为prepared SoA并物理删除。 |
| `TargetCapability` | [COMPUTED][HIGH] 没有生产消费者；实际能力由`ParticleProperties.CapabilityProfileKey`、cohort snapshot和规则驱动。 | [COMPUTED][HIGH] 第24切片已物理删除。 |

## 4. P0冻结的下一收敛边界

[COMPUTED][HIGH] 当前“唯一canonical gather”准确限定为运动基础事实；不能把它外推成整个Demo boundary只有一次物理Mass遍历。Combat业务事务仍有独立两遍，FlowSample持久写回与Facing完整集合预验证/最终写回具有不同所有权。

[INFERRED][HIGH] P1完成标准不是强制一次物理遍历，而是“一次canonical gather、不可变Snapshot、显式POD overlay、依赖图WORK、稳定merge、完整集合预验证、唯一GT原子最终写回”。任何WORK访问UObject、World或EntityManager均违反该边界。

[INFERRED][HIGH] P1允许保留独立验证遍历和持久诊断写回；它必须删除的是WORK输入对基础fragments的重复读取、提交后立即等待造成的虚假并行，以及验证失败后的任何部分写入。

[COMPUTED][HIGH] 2026-07-23最终检查点：公共Runtime record/CommitPlan以StableEntityRef排序，旧Round不存在`Async/TFuture/Future.Get`，Business/Combat与Movement/Particle/Facing进入同一typed DAG，完整验证后由唯一writer提交。P1查询所有权合同已关闭。

[COMPUTED][HIGH] 2026-07-28复核：Round生产路径仍只有一次canonical gather、一次Dispatch、一次Wait和一个最终写入入口；J/P4通过`FCrowdMassBoundaryRunner`提交实体移动，场景Coordinator不拥有第二套Mass位置写入。7948–7951 Round与7939 J回归无VIOLATION。

[COMPUTED][HIGH] 第26切片把上一boundary的`ConsecutiveFinalSettleSteps`并入`BoundaryGather`，因此FacingFinalize由3次Mass遍历降为2次。剩余两次分别承担“写入前完整集合预验证”和“唯一原子提交”，当前没有证据支持在不削弱整批失败原子性的前提下继续合并。

[COMPUTED][HIGH] 8767再次复现SF1 Single的correction interval位置误差p95=`26.745cm`。代码审计确认SF1不进入SoftPressure专用历史snapshot/replay分支：客户端以当前状态比较旧`ServerTimeSeconds`的frame，并直接应用旧权威状态；误差量级与`800cm/s × 1/30s = 26.667cm`一致。该指标当前描述时间错位，不是已经证明的双端模拟发散。

[COMPUTED][HIGH] 8764的500实体启动失败载荷已定位为直接复制的`FCrowdDemoRoundBootstrapPacket::Agents`完整数组；它未使用现有100-agent correction/checkpoint chunks，因此在Round前触发`Ensure !IsBunchTooLarge`。该问题属于Networking/宿主bootstrap传输合同，不属于Core运动算法。

[COMPUTED][HIGH] 第二十七切片已让两个正式Flow场景共享fixed-step correction history/replay；8770 SF1 correction interval p95=`0.064cm`且snapshot miss/mismatch=0，T2/T6原路径无回退。

[INFERRED][HIGH] 该归因不能直接导向 Demo 专用 bootstrap chunks；现行顺序是先完成通用身份/行为 POD 与 Relevant Snapshot primitives，再适配 Demo RoundBootstrap。不得通过关闭Ensure、提高复制预算、移除客户端或把网络协议加入Core掩盖问题。

[COMPUTED][HIGH] 本文件不授权修改算法参数、网络预算、地图、Lighting或进入100/500。
