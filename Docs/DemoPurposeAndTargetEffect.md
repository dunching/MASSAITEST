# Demo Purpose / Target Effect（已收敛）

> 状态：**Superseded as a top-level source**

本文曾同时承担“项目目的、最终产品角色、运动目标效果、场景验收”等多种职责。为避免与新的核心事实源重复，这些职责已经拆分：

- 项目最终产品定义与架构原则：[`TargetArchitecture.md`](TargetArchitecture.md)
- 当前实际生产结构：[`CurrentArchitecture.md`](CurrentArchitecture.md)
- 场景与验收证据：[`TestScenarioMatrix.md`](TestScenarioMatrix.md)
- 功能完成状态：[`FeatureChecklist.md`](FeatureChecklist.md)
- Target Region 详细算法：[`TargetRegionTransportFieldDesign.md`](TargetRegionTransportFieldDesign.md)

## 保留结论

Demo 的长期角色不变：

```text
MassCrowdSimulation = 可复用产品
MassAICrowdDemo     = 生产架构验证宿主
```

Demo 可以拥有 Scenario、T1-T8、地图、Round 测试窗口、故障注入、Golden Hash、指标、录像与人工审片，但不得长期维护第二套 Runtime、Networking、Presentation、Commit Barrier 或通用 Crowd Kernel。

“群体驱动 + 个体修正”、持续 Agent population、Target Region、Local Predictive、Particle Safety、异构 Agent 等最终原则已经进入 `TargetArchitecture.md` 与对应专项 Design，不再在本文重复维护。

需要查看旧的完整目标效果、场景描述和历史验收流程时，请使用 Git 历史。