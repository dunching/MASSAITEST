# Persistent Worker Simulation Runtime（历史阶段）

> 状态：**Retired / Historical**

本文记录 PW0–PW8 的 Persistent Worker 迁移阶段。该阶段的重要成果——持久 Worker 状态、增量输入、短生命周期 Task、可变 Published Batch、Worker Authority——已经进入后续 Full Worker Authority 终态，因此本文不再作为现行架构事实源。

现行事实源：

- 当前生产结构：[`CurrentArchitecture.md`](CurrentArchitecture.md)
- 最终终态：[`TargetArchitecture.md`](TargetArchitecture.md)
- Worker 字段所有权：[`Reference/WorkerOwnershipMatrix.md`](Reference/WorkerOwnershipMatrix.md)
- 当前任务：[`PhasePlan.md`](PhasePlan.md)

## 历史价值

PW 阶段解释了系统为什么从 Fixed-Step Boundary 转向跨帧持久 Worker，以及为什么采用 Worker Mirror、Dirty Result、Work/Task 分离等机制。

其中“部分 Consistency Domain 长期留在旧 Boundary”之类迁移期结论已经被 Full Worker Authority 取代，不得用于推断当前或最终设计。

完整历史正文保留在 Git 历史中。