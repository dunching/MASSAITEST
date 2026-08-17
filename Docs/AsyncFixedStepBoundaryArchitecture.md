# Async Fixed-Step Boundary（历史阶段）

> 状态：**Retired / Historical**

本文记录系统从“GT 派发后同步等待”迁移到“Mailbox + 跨帧 Poll + 原子 Commit”的过渡架构。

该架构已经被 Persistent Full Worker Authority 取代，不再定义当前或最终生产结构。

现行事实源：

- 当前生产结构：[`CurrentArchitecture.md`](CurrentArchitecture.md)
- 最终目标架构：[`TargetArchitecture.md`](TargetArchitecture.md)
- Worker/GT Writer 边界：[`Reference/WorkerOwnershipMatrix.md`](Reference/WorkerOwnershipMatrix.md)
- 当前实施顺序：[`PhasePlan.md`](PhasePlan.md)

## 历史价值

这份文档解释了为什么项目先消除 GT 阻塞 Wait、引入深度 1 Mailbox 和跨帧 Poll，再进一步演化为 Persistent Worker。

旧 Mailbox、Boundary Request/Result/Commit、四节点 Processor、Round Transaction 等不得继续被解释为终态架构。

完整历史正文保留在 Git 历史中。