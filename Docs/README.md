# MASSAITEST 文档入口

本目录使用“单一职责 + 明确事实优先级”的方式维护文档。阅读项目时，不应再从旧阶段文档或日期流水账推断当前架构。

## 1. 核心事实源

| 文档 | 唯一职责 |
|---|---|
| `CurrentArchitecture.md` | 当前 `main` 实际存在的生产结构、模块边界、运行链与已知迁移债务。 |
| `TargetArchitecture.md` | 已经确定的最终产品方向与终态架构。 |
| `PhasePlan.md` | 从 Current 收敛到 Target 的实施顺序与当前下一步。 |
| `FeatureChecklist.md` | 功能/结构门是否已经完成。 |
| `TestScenarioMatrix.md` | 当前仍有效的测试、场景与验收证据。 |

事实冲突时按以下规则处理：

```text
当前源码事实
    ↓
CurrentArchitecture.md
    ↓
FeatureChecklist / TestScenarioMatrix 中最新有效证据

最终方向
    ↓
TargetArchitecture.md

实施顺序
    ↓
PhasePlan.md
```

旧设计、旧计划、旧 Boundary、旧 PW/WA 中间态文档不得覆盖以上核心事实源。

## 2. 详细设计文档

这些文档解释某个子系统“为什么这样设计、数据和算法边界是什么”，但不负责宣布整个项目当前完成状态：

- `EntityBehaviorSourceArchitecture.md`：Capability / Behavior Source / Registry / Resolver。
- `MassCrowdStandardSourcesDesign.md`：插件随包的通用 Movement / Facing / Constraint Sources。
- `TargetRegionTransportFieldDesign.md`：目标附近 Target-relative Polar Transport Field。
- `LocalPredictiveInteractionDesign.md`：局部预测与公平让行。
- `CrowdTransitCapabilityDesign.md`：Particle / Hard / Soft / Environment 安全历史设计，后续将收敛为 ParticleSafety 设计。
- `MassProjectileHitFrameworkDesign.md`：Projectile / Broadphase / Sweep / HitFact 公共机制。
- `DemoBusinessPlanningArchitecture.md`：Demo 业务 Planner / Provider / Host Intent。
- `RangedCombatVatAndHitResponseDesign.md`：Demo T7/T8 战斗、VAT 与表现验收。
- `VatAssetProductionPipeline.md`：VAT 资产生产管线。

## 3. Reference

`Reference/` 保存需要长期查阅、但不应该成为“总架构文章”的精确合同或矩阵：

- `Reference/WorkerOwnershipMatrix.md`：Worker / Mass / Network / Presentation 字段所有权与迁移禁则。
- `Reference/PluginModuleBoundary.md`：MassCrowdSimulation 插件模块职责与依赖方向。
- `MassCrowdUnifiedRuntimeAndReplicationContract.md`：持续 Agent、Behavior、Lifecycle、Replication 的详细长期合同；它属于深层参考，不覆盖 `CurrentArchitecture.md` 或 `TargetArchitecture.md`。

## 4. History 与 AI 恢复入口

历史统一从：

```text
History/README.md
```

进入。旧 AB5、Async Boundary、Persistent Worker PW0-PW8、Gameplay Sandbox Migration、旧 RoundSim Query Matrix、旧恢复点和旧实验都只用于理解演进过程。

新的 AI / 开发者需要恢复上下文时，从：

```text
AI_ENTRY/README.md
→ AI_ENTRY/02_状态恢复.md
```

开始。`AI_ENTRY` 不拥有架构事实优先级，它只是核心文档的快速入口。

根目录仍存在的 retirement stub 只用于旧链接兼容；完整历史仍可通过 Git 历史追溯。

## 5. 推荐阅读顺序

第一次理解项目：

```text
Docs/README.md
    ↓
CurrentArchitecture.md
    ↓
TargetArchitecture.md
    ↓
PhasePlan.md
    ↓
FeatureChecklist.md
    ↓
TestScenarioMatrix.md
```

需要深入实现时，再进入 Behavior、Target、Movement、Particle、Projectile、Networking、Presentation 等专项设计或 Reference。

会话恢复：

```text
AI_ENTRY/README.md
    ↓
AI_ENTRY/02_状态恢复.md
    ↓
回到上述核心事实源
```

## 6. 文档维护规则

1. `CurrentArchitecture.md` 不写历史流水账，也不写未实现目标。
2. `TargetArchitecture.md` 不把目标写成已完成事实。
3. `PhasePlan.md` 只保留当前有效实施顺序，旧阶段移入历史。
4. `FeatureChecklist.md` 只声明当前完成状态，不承担阶段时间线。
5. `TestScenarioMatrix.md` 只保留可复核的当前有效证据；端口、PID、临时失败日志由 Git/Saved/runner 产物保存。
6. 专项 Design 不得重新定义项目总权威、总模块边界或全局完成状态。
7. 被新机制替代的生产架构，不长期保留第二套“兼容事实源”。
8. `AI_ENTRY` 只做快速恢复，不再追加几十 KB 的迁移日志。
9. `History/` 只保存有长期归因价值的演进内容，不充当当前状态页面。
10. 若文档与源码冲突，先修正文档；不得为了保护旧文档而保留 Legacy 代码。
