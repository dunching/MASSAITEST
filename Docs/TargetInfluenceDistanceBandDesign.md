# Target Influence Distance Band（已并入 Target Region Transport）

> 状态：**Superseded / Merged**

本文原先同时维护 Distance Band、Polar Region Transport 与历史验证记录。为避免 Target 设计出现两份事实源，现统一以：

[`TargetRegionTransportFieldDesign.md`](TargetRegionTransportFieldDesign.md)

作为 Target Region / Polar Transport 的专项设计文档。

## 保留概念

Distance Band 仍是 Target Region Transport 的重要输入合同：

```text
MinimumCombatCenterDistance
MaximumCombatCenterDistance
InfluenceBlendWidth
TargetHardDistance
```

它定义不同 Agent 在目标参考系中的有效径向区间；Polar Navigation Cell、Demand Region、Transport Plan 与 Guidance 则负责目标附近的人口分布和宏观运输。

Distance Band 不是永久 Slot，也不为每个实体分配固定 Region Owner；最终 Guidance 仍需经过 Local Predictive 与 Particle Safety。

当前生产接入以 `CurrentArchitecture.md` 为准，最终原则以 `TargetArchitecture.md` 为准，测试证据以 `FeatureChecklist.md` / `TestScenarioMatrix.md` 为准。

旧完整公式、历史运行证据和迁移记录可通过 Git 历史追溯。