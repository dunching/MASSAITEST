# 历史提示词：WA8 Runtime Owner Commit Barrier 迁移

> 状态：**已完成，禁止作为当前执行提示词继续使用。**

本文件曾用于实施“将 Worker Result Owner Commit Barrier 从 `MassAICrowdDemo` 下沉到 `MassCrowdRuntime`”这一迁移切片。

该目标已经完成：

- 通用 Commit Token / Result / Owner Commit Barrier 已位于 `MassCrowdRuntime`。
- Demo 只保留 Host-specific Prepared Plan 与 FinalValidate/NoFailApply adapter。
- 旧 Demo Barrier 文件、类型和消费者已经物理删除。
- 当前正式架构和剩余任务已经由新的核心事实源接管。

当前继续开发时请阅读：

```text
Docs/AI_ENTRY/README.md
Docs/AI_ENTRY/02_状态恢复.md
Docs/CurrentArchitecture.md
Docs/TargetArchitecture.md
Docs/PhasePlan.md
Docs/FeatureChecklist.md
Docs/TestScenarioMatrix.md
```

当前 WA8 下一项不是再次迁移 Barrier，而是：

```text
删除普通帧完整 rollback 旧数据源
→ 删除 TryPrepareRoundApply / RoundWorkBatch / BeginBoundaryTransaction
→ 删除 Demo-local Round Transaction
```

历史完整提示词可通过 Git 历史查看；核心 Docs 不再保存已完成任务的大段执行 prompt。
