# Reference 文档索引

`Docs/Reference/` 保存需要长期查阅的**精确合同、所有权矩阵和模块边界**。

Reference 不是项目总架构入口，也不负责宣布阶段完成状态。

事实优先级：

```text
当前源码 / CurrentArchitecture.md
        ↓
TargetArchitecture.md（最终方向）
        ↓
Reference（精确边界）
```

## 当前 Reference

| 文档 | 职责 |
|---|---|
| `WorkerOwnershipMatrix.md` | Simulation field、Host business、Mass proxy、Network、Presentation 的 owner / writer 边界。 |
| `PluginModuleBoundary.md` | 插件各模块当前编译依赖方向、长期职责与 Demo/Host 边界。 |
| `../MassCrowdUnifiedRuntimeAndReplicationContract.md` | Agent、Lifecycle、Behavior Source、Replication、Result Commit 的长期详细合同。 |

## 使用规则

- `WorkerOwnershipMatrix.md` 解决“这个状态到底谁有权推进”。
- `PluginModuleBoundary.md` 解决“这段能力应该放在哪个模块、允许依赖谁”。
- Runtime/Replication Contract 解决“跨模块与跨网络的数据合同是什么”。
- 当前是否已经实现，以 `../CurrentArchitecture.md` 和 `../FeatureChecklist.md` 为准。
- 当前下一步，以 `../PhasePlan.md` 为准。
- 旧 `FullWorkerAuthorityOwnershipMatrix.md`、`MassCrowdSimulationPluginArchitecture.md` 等名称已经退出 active tree；需要追溯时使用 Git 历史。
