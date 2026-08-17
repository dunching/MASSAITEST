# Full Worker Authority Ownership Matrix（已收敛）

> 状态：**Superseded**

旧矩阵包含多个迁移日期的字段 Owner 快照，其中部分描述已经与当前源码不一致，因此不再作为现行 Ownership 事实源。

请使用：

[`Reference/WorkerOwnershipMatrix.md`](Reference/WorkerOwnershipMatrix.md)

该文档统一维护：

- Worker / Mass / Network / Presentation 的字段 Owner。
- StableEntityRef / Lifecycle 身份边界。
- Entity → Work → Shard → Task 的写入规则。
- Result Apply 原子提交顺序。
- Shadow / Canary / Production 迁移禁则。
- Demo 与通用 Runtime 的 Writer 边界。

当前代码是否已经完成迁移，以 `CurrentArchitecture.md` 为准；最终终态以 `TargetArchitecture.md` 为准。

旧矩阵完整正文可通过 Git 历史追溯。