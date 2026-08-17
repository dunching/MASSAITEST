# MASSAITEST 历史文档索引

`Docs/History/` 只保存**架构演进、退役方案、恢复快照和旧实验背景**。

历史文档没有当前事实优先级。任何历史内容与当前源码或核心事实源冲突时，以当前事实源为准。

## 当前核心事实源

```text
../CurrentArchitecture.md
../TargetArchitecture.md
../PhasePlan.md
../FeatureChecklist.md
../TestScenarioMatrix.md
```

## 当前已有历史归档

| 文档 | 内容 |
|---|---|
| `ArchitectureStatusBeforeConvergence_20260718.md` | 文档/架构第一次大规模收敛之前的状态快照。 |
| `LegacyBusinessAndRoundSim.md` | 旧业务、旧 RoundSim、旧 Movement 实验路线的简要归档。 |
| `RecoverySnapshotsBeforePJ0_20260729.md` | PJ0 之前的恢复快照与阶段上下文。 |

此外，以下根目录文件已经被降级为历史入口或兼容入口，不再拥有架构事实权：

```text
../FullWorkerAuthorityArchitecture.md
../PersistentWorkerSimulationArchitecture.md
../AsyncFixedStepBoundaryArchitecture.md
../AB5FourNodeBoundaryContract.md
../GameplaySwarmSandboxAndPluginMigrationPlan.md
../MassQueryOwnershipMatrix.md
../HistoricalNotes.md
```

## 架构演进主线

可以把项目演进粗略理解为：

```text
早期 RoundSim / 多 Processor
        ↓
Async Fixed-Step Boundary
        ↓
四节点 Boundary
        ↓
Persistent Worker（部分 Domain Authority）
        ↓
Full Worker Authority
        ↓
Worker Result Dirty Apply / Owner Commit Barrier
        ↓
当前：WA8 Legacy Removal
        ↓
目标：完整 10k Production Runtime
```

这些阶段的存在用于解释“为什么现在是这样”，不能作为“现在仍应该保留什么”的理由。

## 历史保存规则

以后只有以下内容进入 History：

- 已被替代的架构方案。
- 已完成且不再执行的迁移计划。
- 有长期归因价值的重大失败/恢复总结。
- 需要保留的旧产品/算法实验背景。

以下内容不应继续进入 History 文档：

- 每次端口号。
- 每次 PID。
- 每个临时命令输出。
- 已被后续成功覆盖且没有归因价值的重复 runner 日志。

这些细节由 Git 历史、Saved 日志和 CI/runner 产物保存即可。

## Git 历史

文档收敛会主动删除核心文档中的大段旧正文，但不会造成不可恢复的信息丢失；旧版本始终可以从 Git commit 历史查看。
