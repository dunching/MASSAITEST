# MassCrowdSimulation Plugin Architecture（已收敛）

> 状态：**Superseded as a top-level source**

本文曾混合当前模块状态、历史 P0-P5/PW 阶段和长期产品边界，部分日期快照已经与当前源码冲突，因此不再作为插件边界事实源。

现行模块参考：

[`Reference/PluginModuleBoundary.md`](Reference/PluginModuleBoundary.md)

全局事实源：

- 当前生产结构：[`CurrentArchitecture.md`](CurrentArchitecture.md)
- 最终产品架构：[`TargetArchitecture.md`](TargetArchitecture.md)
- Worker 字段所有权：[`Reference/WorkerOwnershipMatrix.md`](Reference/WorkerOwnershipMatrix.md)
- Behavior 机制：[`EntityBehaviorSourceArchitecture.md`](EntityBehaviorSourceArchitecture.md)
- 持续运行/复制详细合同：[`MassCrowdUnifiedRuntimeAndReplicationContract.md`](MassCrowdUnifiedRuntimeAndReplicationContract.md)

## 保留原则

插件不得反向依赖 Demo；Core 保持纯算法/数据边界；Runtime 拥有通用 Persistent Worker 与 Result Commit 机制；Networking / Presentation 是 Worker 输出消费者；StandardSources 提供通用 Movement/Facing/Constraint；Demo 只提供 Scenario、项目业务 Adapter 和验收设施。

旧 P0-P5、PW0 等阶段状态和日期快照只具有历史价值。完整旧正文可通过 Git 历史追溯。