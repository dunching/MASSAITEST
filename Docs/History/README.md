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

## 当前归档

| 文档 / 目录 | 内容 |
|---|---|
| `ArchitectureStatusBeforeConvergence_20260718.md` | 第一次大规模文档/架构收敛前的状态快照。 |
| `LegacyBusinessAndRoundSim.md` | 旧业务、旧 RoundSim、旧 Movement 实验路线简要归档。 |
| `RecoverySnapshotsBeforePJ0_20260729.md` | PJ0 之前的恢复快照和阶段上下文。 |
| `Prompts/README.md` | 已完成执行 Prompt 的名称与 Git 追溯入口。 |

## 架构演进主线

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
Worker Dirty Apply / Owner Commit Barrier
        ↓
当前：WA8 Legacy Removal
        ↓
目标：完整 10k Production Runtime
```

这些阶段只用于解释“为什么现在是这样”，不能作为“现在仍应该保留什么”的理由。

## Active tree 清理原则

以下旧名称不再为了兼容链接长期保留在 `Docs/` 根目录：

```text
AB5FourNodeBoundaryContract
AsyncFixedStepBoundaryArchitecture
PersistentWorkerSimulationArchitecture
FullWorkerAuthorityArchitecture
FullWorkerAuthorityOwnershipMatrix
GameplaySwarmSandboxAndPluginMigrationPlan
MassQueryOwnershipMatrix
MassCrowdSimulationPluginArchitecture
TargetInfluenceDistanceBandDesign
CrowdTransitCapabilityDesign
DemoPurposeAndTargetEffect
HistoricalNotes
```

需要查看这些文件的完整历史正文时，直接使用 Git commit 历史。

这比在 active tree 留几十个 0.5–1KB retirement stub 更清楚，也避免代码搜索或 AI 检索继续命中过时文件名。

## 历史保存规则

以后只有以下内容进入 History：

- 已被替代、但有长期架构解释价值的方案。
- 已完成且值得保留背景的迁移计划总结。
- 有长期归因价值的重大失败 / 恢复总结。
- 旧产品 / 算法实验的关键背景。
- 已完成 Prompt 的索引。

以下内容不再沉淀到长期文档：

```text
每次端口号
每次 PID
每个临时命令输出
重复 runner 日志
已经被后续成功覆盖且没有归因价值的失败
```

这些细节由 Git 历史、Saved 日志和 runner / CI 产物保存。

## Git 历史

文档收敛会主动删除 active tree 中的过时正文和 stub，但不会造成不可恢复的信息丢失；旧版本始终可以从 Git commit 历史查看。
