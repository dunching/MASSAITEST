# MASSAITEST 文档入口

本目录采用 **单一职责 + 明确事实优先级 + active tree 不保留旧架构 stub** 的方式维护文档。

第一次阅读项目，不要从旧阶段名称、历史 Prompt 或 Git 中的退休文件推断当前架构。

---

## 1. 五个核心事实源

| 文档 | 唯一职责 |
|---|---|
| `CurrentArchitecture.md` | 当前 `main` 实际存在的生产结构、运行链、运行模式与迁移债务。 |
| `TargetArchitecture.md` | 已经确定的最终产品方向与终态架构。 |
| `PhasePlan.md` | 从 Current 收敛到 Target 的当前实施顺序。 |
| `FeatureChecklist.md` | 功能 / 结构能力的 DONE / PARTIAL / OPEN 状态。 |
| `TestScenarioMatrix.md` | 当前仍有效的 PASS / DIAGNOSTIC / FAIL / NOT RUN 证据。 |

事实冲突时：

```text
当前源码事实
    ↓
CurrentArchitecture.md
    ↓
FeatureChecklist / TestScenarioMatrix 最新有效证据

最终方向
    ↓
TargetArchitecture.md

实施顺序
    ↓
PhasePlan.md
```

---

## 2. 源码审计与阅读入口

项目经过多轮实验性迭代，新 Worker 架构与旧 Demo RoundSim shell 仍有共存。为了避免“从最大文件开始读，结果把 Legacy 当主架构”，源码阅读统一增加三个入口：

| 文档 | 职责 |
|---|---|
| `SourceReadingMap.md` | 当前源码从哪里开始读；Production / Demo Adapter / Legacy / Diagnostic / Test 如何区分。 |
| `LegacyCodeInventory.md` | 旧代码、重复 Kernel、巨型 Coordinator 的风险分类与删除前置条件。 |
| `SourceConsistencyAudit.md` | 文档 ↔ 源码逐项对照，记录 EXACT / DOC BUG / SOURCE DEBT / NEEDS CLARIFICATION。 |

推荐源码阅读路径：

```text
SourceReadingMap.md
    ↓
MassCrowdWorkerRuntimeV2
    ↓
MassCrowdAsyncSimulationRuntime
    ↓
Worker Domain Executors
    ↓
MassCrowdWorkerResultApply
    ↓
CrowdDemoWorkerInputSync / Host Apply
    ↓
Core Kernels
    ↓
最后才进入 RoundSim legacy shell
```

任何计划删除旧代码前，先查 `LegacyCodeInventory.md`，不要靠文件名判断可删除性。

---

## 3. 专项设计文档

专项 Design 只回答“这个机制怎么设计、边界是什么”，不重新定义项目全局状态。

### Behavior / Business

- `EntityBehaviorSourceArchitecture.md` — Capability / Source / Registry / Command / Resolver。
- `MassCrowdStandardSourcesDesign.md` — 通用 Movement / Facing / Constraint Sources。
- `DemoBusinessPlanningArchitecture.md` — Demo Planner / Provider / Host Intent / Business Adapter。

### Movement / Crowd

```text
Shared Flow
    ↓
TargetRegionTransportFieldDesign.md   （可选 Macro Guidance）
    ↓
LocalPredictiveInteractionDesign.md
    ↓
ParticleSafetyDesign.md
```

- `TargetRegionTransportFieldDesign.md` — Target-relative Polar Topology、Distance Band、Demand、Transport Plan、Edge Quota、Guidance。
- `LocalPredictiveInteractionDesign.md` — Pair prediction、half-plane、conflict component、公平让行。
- `ParticleSafetyDesign.md` — Soft / Hard / Swept / Obstacle / Bounds / Interaction Island 最终安全闭环。

### Combat / Projectile / Presentation

- `MassProjectileHitFrameworkDesign.md` — 插件级 Worker Projectile Simulation、Broadphase、Sweep、ImpactFact / HitFact。
- `RangedCombatVatAndHitResponseDesign.md` — Demo T7/T8 Attack、HitResponse、Reactive Motion、VAT / HitFlash 验收语义。
- `VatAssetProductionPipeline.md` — Demo VAT 资产可重复生成管线。

---

## 4. Reference

从：

```text
Reference/README.md
```

进入。

当前 Reference：

- `Reference/WorkerOwnershipMatrix.md` — Simulation / Host Business / Mass Proxy / Network / Presentation 的字段 Owner。
- `Reference/PluginModuleBoundary.md` — 实际 Build.cs 主干、模块职责与依赖禁则。
- `MassCrowdUnifiedRuntimeAndReplicationContract.md` — 持续 Agent、Lifecycle、Behavior、Result Commit、Replication 的长期详细合同。

Reference 不覆盖 `CurrentArchitecture.md` / `TargetArchitecture.md`。

---

## 5. AI 恢复入口

新的 AI 或开发者需要快速恢复上下文时：

```text
AI_ENTRY/README.md
    ↓
AI_ENTRY/02_状态恢复.md
    ↓
CurrentArchitecture.md
TargetArchitecture.md
PhasePlan.md
FeatureChecklist.md
TestScenarioMatrix.md
    ↓
SourceReadingMap.md
```

`AI_ENTRY` 只做快速恢复，不拥有架构事实优先级，也不保存已完成执行 Prompt。

---

## 6. History

历史统一从：

```text
History/README.md
```

进入。

History 用于保存被替代的架构方案、重大迁移背景、有归因价值的恢复快照和旧实验总结。

已完成执行 Prompt 只在 `History/Prompts/README.md` 留索引，完整正文通过 Git 历史追溯。

Active `Docs/` 根目录不再保留 AB5、Async Boundary、Persistent Worker 中间态、旧 Ownership Matrix、旧 Plugin Architecture、旧 Distance Band 文档等兼容 stub。

---

## 7. 推荐阅读顺序

第一次理解项目：

```text
../README.md
    ↓
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

准备修改源码：

```text
SourceReadingMap.md
→ SourceConsistencyAudit.md
→ LegacyCodeInventory.md
→ 对应 Design / Reference
```

---

## 8. 文档维护规则

1. `CurrentArchitecture.md` 不写历史流水账，也不写未实现目标。
2. `TargetArchitecture.md` 不把目标写成已经完成。
3. `PhasePlan.md` 只保留当前有效 Gate，不累积已完成时间线。
4. `FeatureChecklist.md` 只维护当前能力状态。
5. `TestScenarioMatrix.md` 只保留当前有效证据；端口、PID、临时日志留给 Git/Saved/runner artifact。
6. Design 文档只维护机制合同，不保存阶段编号和逐次性能流水账。
7. Reference 只维护精确边界，不抢总架构事实权。
8. `SourceConsistencyAudit.md` 只记录当前仍有价值的不一致；已修复历史通过 Git 追溯。
9. `LegacyCodeInventory.md` 中没有完成 consumer audit 的条目，不得直接宣布“安全删除”。
10. `AI_ENTRY` 不追加几十 KB 的迁移日志，也不保留已完成 Prompt。
11. `History` 不充当当前状态页面。
12. 被新机制替代的旧架构文档直接退出 active tree；需要追溯使用 Git 历史。
13. 文档与源码冲突时，以源码为起点修正文档；不得为了保护旧文档而保留 Legacy 代码。
