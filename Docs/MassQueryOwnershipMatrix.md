# RoundSim Mass Query / Ownership Matrix（历史）

> 状态：**Retired / Historical**

本文记录旧 RoundSim Fixed-Step Boundary 时期的 Mass Query 次数、Fragment Writer 和 Prepared SoA 所有权。随着 Worker Input Sync / Worker Result Apply 成为生产主边界，这份矩阵不再代表当前结构。

现行事实源：

- 当前 Mass/Worker 结构：[`CurrentArchitecture.md`](CurrentArchitecture.md)
- 最终 Worker Authority：[`TargetArchitecture.md`](TargetArchitecture.md)
- 字段 Owner / Writer：[`Reference/WorkerOwnershipMatrix.md`](Reference/WorkerOwnershipMatrix.md)

旧文中 `BoundaryGather`、`FlowPreferredVelocity`、`FacingFinalize`、Round Transaction 等 Query/Writer 数量只用于理解迁移过程，不能作为当前代码审计基线。

如需核对当前 Result Apply、Dirty Mass Apply 或 StableRef→Mass Handle 结构，应直接阅读当前源码与 `CurrentArchitecture.md`。

完整旧矩阵可通过 Git 历史追溯。