# MASSAITEST Legacy Code Inventory

## 1. 文档职责

本文不是“看到旧代码就删”的清单。

它用于回答：

> 当前源码中哪些内容属于目标 Runtime，哪些是 WA8 迁移耦合，哪些是重复通用实现，哪些其实只是合法测试 Fixture；每类代码在删除前还缺什么证据。

状态定义：

```text
KEEP
当前产品/插件/宿主职责仍然成立。

MIGRATE THEN DELETE
当前仍有生产或运行时消费者，先迁出依赖，再物理删除。

DELETE CANDIDATE
没有看到必要的生产职责，但必须完成 repo-wide 引用/测试审计后再删。

TEST FIXTURE
场景或专项测试代码，不能因为不属于产品 Runtime 就当 Legacy 删除。

RENAME / CLEANUP
功能可以保留，但名称、注释、文件组织或构建方式携带明显迁移债务。
```

当前审计基于 `main` 源码静态阅读；未在本轮重新编译或运行 UE。

---

## 2. A 类：WA8 运行时耦合 — MIGRATE THEN DELETE

这是优先级最高的一类，因为新 Worker Production 路径仍直接或间接依赖旧 Demo Round 架构。

### `CrowdDemoRoundSimPipelineSubsystem.*`

当前角色仍然混杂：

```text
旧 Round 事务状态
Prepared data
rollback / transaction
metrics / diagnostics
Target / Flow / Particle 历史/验收数据
```

已完成的 P0 子切片：

```text
Primary FCrowdMassSharedFlowResource owner
RoundSimPipeline → UMassCrowdRuntimeSubsystem

CrowdDemoWorkerInputSync
不再 include/query RoundSimPipeline
直接读取 RuntimeSubsystem SharedFlowResource

Target Prepared ResourceId
pointer address → CrowdWorkerResourceIds::Environment
```

结论：**Pipeline 仍不能直接删除。** Shared Flow 的 Worker Input ownership 耦合已关闭，但 Result Apply 后的 Round frame、rollback/transaction、Target prepared state 与 diagnostics 仍是真实运行时消费者。

剩余删除门：

- Result Apply 不再依赖 Pipeline transaction/rollback source；
- 其他仍由 Pipeline 持有的生产输入/Prepared state 迁到明确 owner；
- 必需 diagnostics 有长期 owner；
- tests 不再用 Pipeline 伪装生产状态。

### `CrowdDemoRoundSimProcessors.*`

当前 `.h` 仍声明大量旧 Stage：

```text
RoundPlanApply
SharedFlowFieldBuild
OpenSpawnRelaxationPrepare
FlowPreferredVelocity
TargetFactApply
TargetPolarTopologyBuild
TargetRegionPopulationBuild
TargetRegionTransportSolve
TargetRegionGuidance
MovementWork
ParticleConstraint
ObstacleConstraint
FacingFinalize
PostFinalizeMetrics
AuthorityCommit
ClientPredictionCommit
CheckpointPublisher
```

但当前真正注册的 Simulation UMassProcessor 只有：

```text
UCrowdDemoWorkerInputSyncProcessor
UCrowdDemoWorkerResultApplyProcessor
```

因此 Stage struct 的存在主要代表**迁移壳/宿主逻辑残留**，不是当前 Processor DAG。

结论：MIGRATE THEN DELETE。

不要做的事：仅因为 `legacy_round_processors=0` 就整文件删除；`.cpp` 内仍有诊断、metrics、Host apply 与旧数据路径消费者。

### `CrowdDemoRoundSimCoordinator.*`

大体量 Demo Round 管理器。

当前合理职责可能包括：

```text
测试窗口
Scenario orchestration
验收与结果汇总
```

不合理长期职责包括：

```text
产品模拟 authority
第二套状态推进
第二套 transaction
第二套 rollback owner
```

目标不是“Demo 不许有 Round”，而是：

> Round 只能是测试窗口，不能继续成为产品 Runtime 的事务模型。

### `FCrowdDemoRoundWorkBatch` / `TryPrepareRoundApply` / `BeginBoundaryTransaction`

这些属于已明确列入 WA8 的结构债务。

状态：MIGRATE THEN DELETE。

删除门以 `PhasePlan.md` 为准。

---

## 3. B 类：重复通用 Kernel

当前 Plugin 与 Demo 同时存在多组功能高度重叠的实现。

### Plugin canonical candidates

```text
Plugins/MassCrowdSimulation/Source/MassCrowdCore/
CrowdLocalPredictiveInteractionKernel.*
CrowdParticleConstraintKernel.*
CrowdSharedFlowFieldKernel.*
CrowdTargetRegionTransportKernel.*
CrowdFacingKernel.*
CrowdGuidanceComposeKernel.*
CrowdVelocityHalfPlaneKernel.*
```

这些位于通用 Core，是长期 canonical 方向。

### Demo duplicates

```text
Source/MassAICrowdDemo/Mass/
CrowdDemoLocalPredictiveInteractionKernel.*
CrowdDemoParticleConstraintKernel.*
CrowdDemoSharedFlowFieldKernel.*
CrowdDemoTargetRegionTransportKernel.*
CrowdDemoFacingKernel.*
CrowdDemoGuidanceComposeKernel.*
CrowdDemoVelocityHalfPlaneKernel.*
```

不能整组一次删除，当前消费者并不相同。

#### Demo Particle Kernel — MIGRATE THEN DELETE

已确认剩余消费者包括：

```text
BuildFailureFixture
HashAppliedRoundSimState
```

普通/诊断 Particle 求解已经大量通过插件 `FCrowdMassParticleWork::Solve()`。

建议：

1. 把纯诊断 Fixture/Hash 帮助函数迁到 Test/Diagnostic utility；
2. 确认不再执行第二套 solver；
3. 删除 Demo kernel 实现。

#### Demo Shared Flow Kernel — MIGRATE THEN DELETE

当前旧 RoundSim 仍调用：

```text
Sample
IsInsideInflatedObstacle
```

用于 metrics / penetration 等诊断。

建议把这些诊断消费改到 Plugin Core API 或专用 Test Adapter 后删除 Demo 重复实现。

#### Demo Local Predictive Kernel — DELETE CANDIDATE

在本轮对 `CrowdDemoRoundSimProcessors.cpp` 的直接调用检查中没有发现 `FCrowdDemoLocalPredictiveInteractionKernel::...` 调用。

但这不足以宣布安全删除：

- 仍可能被专项测试或其他 Coordinator 调用；
- plugin-equivalence tests 可能依赖它作为旧参考实现。

下一步必须做 repo-wide 引用审计，并区分“生产消费者”和“parity fixture”。

#### Demo Target Region Kernel — DELETE CANDIDATE

同样在 `CrowdDemoRoundSimProcessors.cpp` 中没有发现直接 `FCrowdDemoTargetRegionTransportKernel::...` 调用，但仓库有 Target plugin equivalence / legacy tests。

先完成引用图，再决定：

```text
完全删除
或
迁入 History/Test-only fixture
```

#### Demo Facing / Guidance / HalfPlane — DELETE CANDIDATE

原则相同。

这些重复实现若只剩 equivalence test，应考虑：

- 在迁移验收结束后删旧实现和 parity test；
- 或保留最小 frozen fixture，而不是完整第二套生产 kernel。

---

## 4. C 类：合法 Scenario/Test Kernel — TEST FIXTURE

以下名字看起来“很 Demo”，但它们本来就是专项测试场景，不应该因为插件化而自动删除：

```text
CrowdDemoOpenSpawnRelaxationKernel
CrowdDemoOpenCohortMovementKernel
CrowdDemoBidirectionalSwapKernel
CrowdDemoValidCorridorTransitKernel
CrowdDemoSoftPressureRouteDiagnosticKernel
Target stability / plan lifecycle diagnostic kernels
```

它们的长期合法性取决于一个边界：

> 它们可以生成测试输入、判定验收、构建 failure fixture；不能成为产品 Runtime 对特定地图/Agent 的生产特判。

状态：TEST FIXTURE。

如果某个 Fixture 开始直接修改 Production Worker State，应重新分类为架构问题。

---

## 5. D 类：Demo Coordinator 体量债务

当前几个文件已经大到显著增加阅读成本：

```text
CrowdDemoRoundSimPipelineSubsystem.cpp  ~390 KB
CrowdDemoRoundSimProcessors.cpp         ~330 KB
CrowdDemoMixedSandboxCoordinator.cpp    ~204 KB
CrowdDemoRoundSimCoordinator.cpp        ~128 KB
```

这些文件“大”本身不是 bug，但当前同时存在：

```text
旧架构
迁移适配
业务
诊断
测试
网络/表现 glue
```

导致开发者无法通过文件边界判断职责。

状态：RENAME / CLEANUP + MIGRATE THEN DELETE 部分内容。

建议未来拆分原则：

```text
Scenario Driver
Host Input Adapter
Host Result Apply Adapter
Diagnostics
Acceptance / Metrics
Legacy Migration（最终归零）
```

不要再以“一个 Coordinator 方便访问所有东西”为理由继续加逻辑。

---

## 6. E 类：构建与命名债务

### `MassAICrowdDemo.Build.cs` 的 `bUseUnity = false`

源码注释明确说明：Demo module 中存在很多 translation-unit-local helper 同名，Unity amalgamation 会发生冲突，因此被迫关闭 Unity。

这不是 Runtime correctness bug，但它是非常强的结构债务信号：

```text
过多 anonymous namespace helper
同名 helper
职责堆叠
文件之间缺少稳定公共 utility 边界
```

状态：RENAME / CLEANUP。

完成 WA8 与重复 kernel 删除后，应重新评估是否仍需要整个 Demo module `bUseUnity=false`。

### `ShardEntityCount`

当前 Shard Planner 实际切的是 WorkItems。

状态：RENAME / CLEANUP。

建议未来兼容性允许时改成类似：

```text
MaxWorkItemsPerShard
```

当前先在文档明确真实语义，避免为了改名制造不必要协议/测试 churn。

### `AllowedPw5Mask`

`MassCrowdWorkerResultApply.h` 中仍有：

```text
CrowdWorkerResultFields::AllowedPw5Mask
```

PW5 已是迁移阶段编号，继续出现在长期 Runtime Public API 附近会误导读者。

状态：RENAME / CLEANUP。

功能本身不是问题；等 WA8 结构整理时改为语义名。

### WA5 / WA6 源码注释

Projectile Domain `GetDependencies()` 附近仍有 WA5/WA6 迁移时期注释，但 Lifecycle/Behavior executors 现在已经存在。

状态：RENAME / CLEANUP。

应把注释改成当前真实规则：

- Registry dependency 不是完整 DAG；
- runtime stage selection 使用 execution rank；
- `OutDependencies={Target}` 只表达该 Executor 声明的 prerequisite。

---

## 7. F 类：元数据债务

`MassAICrowdDemo.uproject` 当前 Description 仍写：

```text
Isolated 500-entity MassAI crowd replication and movement demo.
```

这已经与项目事实严重不符。

状态：RENAME / CLEANUP。

应改为“MassCrowdSimulation 的大规模 Agent Simulation 验证宿主”。

---

## 8. 当前删除优先级

按风险/收益建议：

| 优先级 | 内容 | 原因 |
|---|---|---|
| CLOSED slice | Primary SharedFlow → WorkerInput ownership | 已迁到 `UMassCrowdRuntimeSubsystem`；WorkerInput 不再读取 Pipeline |
| P0 | Round transaction / rollback / TryPrepareRoundApply | 阻止 Full Worker Authority 完整闭环 |
| P1 | Demo generic kernel 残留消费者迁出 | 双实现持续制造阅读和维护歧义 |
| P1 | 巨型 RoundSimProcessors 拆掉无消费者 Stage | 直接降低代码阅读复杂度 |
| P2 | Demo Coordinator 按职责拆分 | 降低未来新增功能继续堆积风险 |
| P2 | 迁移时代命名/注释 | 降低误读，不影响算法 |
| P3 | Unity 重新开启评估 | 应在结构债清完后进行 |

---

## 9. 删除代码前的标准流程

任何 Legacy 删除都走：

```text
1. repo-wide reference audit
2. 标注 production / diagnostic / test consumer
3. 若有 production consumer → 先迁移
4. 若只有 parity test → 判断迁移验收是否仍需要
5. 删除实现
6. 删除失效 Build.cs dependency / include / test
7. DisableUnity + ForceUnity 都编译
8. 定向自动化
9. 对应真实场景 runner
10. 更新本文与 FeatureChecklist
```

禁止：

```text
“看起来旧” → 直接删
“测试通过” → 保留永久双实现
“为了兼容” → 新建第三套 wrapper
```

---

## 10. 当前总体判断

[INFERRED][HIGH] 当前代码最主要的问题不是“架构方向错误”，而是：

> **第二代 Persistent Worker Runtime 已经建立，但第一代/迁移期 Demo RoundSim shell 仍然承担部分资源、诊断、事务和测试职责，导致新旧代码在同一目录和同一大文件中共存。**

因此下一轮代码治理的正确方向不是重写所有系统，而是：

```text
先断旧壳对生产路径的真实依赖
→ 再删除重复实现
→ 再拆巨型宿主文件
→ 最后做命名/Unity/目录结构清洁
```
