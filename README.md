# MASSAITEST / MassCrowdSimulation

MASSAITEST 是一个基于 **Unreal Engine 5.7 + Mass** 的大规模 Agent Simulation 验证工程。

这个仓库包含两个不同层次：

```text
MassCrowdSimulation
= 可复用的大规模 Agent Simulation 插件

MassAICrowdDemo
= 插件的验证宿主 / 测试场
```

最终产品不是“虫群 Demo”。Demo 使用虫群移动、目标围攻、通道、异构实体、战斗、Projectile、VAT、网络和视觉场景，验证同一套可复用 Runtime 是否能够长期承载持续 Agent population。

## 架构摘要

当前和最终方向都围绕 Persistent Worker Authority 展开：

```text
External Facts
Spawn / Despawn / Command / Resource / Correction
                    │
                    ▼
             Worker Input Sync
                    │
                    ▼
        Persistent Worker Runtime
                    │
      Entity → Work → Shard → UE::Task
                    │
          Deterministic Owner Merge
                    │
          Dirty State / Ordered Event
                    │
                    ▼
             Worker Result Apply
                    │
          ┌─────────┼─────────┐
          ▼         ▼         ▼
        Mass     Network   Presentation
        Proxy     Adapter      Proxy
```

普通模拟状态由 Worker 持有唯一权威；Mass、Network 和 Presentation 只消费版本化结果，不建立同字段的第二套模拟权威。

群体运动使用分层结构：

```text
Shared Flow
    ↓
可选 Target-relative Polar Transport Field
    ↓
Local Predictive Interaction
    ↓
Particle Safety
```

Target Region Transport 只在需要围绕目标进行区域人口分布时启用，不是所有移动的固定必经层。

## 当前状态

当前架构主体已经建立，但项目**尚不能宣称完整 10k Production Ready**。

当前主线仍有四个顺序 Gate：

```text
WA8 Legacy Removal
    ↓
T5 Long-Window Correctness
    ↓
Large Single Particle Island Scaling
    ↓
WA9 Full 1k → 2k → 5k → 10k Acceptance
```

详细状态不要从旧阶段文档推断，统一查看 `Docs/` 下的核心事实源。

## 文档入口

第一次理解项目，请按这个顺序阅读：

1. [`Docs/README.md`](Docs/README.md) — 文档地图与事实优先级。
2. [`Docs/CurrentArchitecture.md`](Docs/CurrentArchitecture.md) — 当前 `main` 实际是什么。
3. [`Docs/TargetArchitecture.md`](Docs/TargetArchitecture.md) — 最终已经决定做成什么。
4. [`Docs/PhasePlan.md`](Docs/PhasePlan.md) — 接下来按什么顺序收敛。
5. [`Docs/FeatureChecklist.md`](Docs/FeatureChecklist.md) — 哪些能力 DONE / PARTIAL / OPEN。
6. [`Docs/TestScenarioMatrix.md`](Docs/TestScenarioMatrix.md) — 当前有效测试证据。

需要快速恢复 AI / 开发上下文时，从 [`Docs/AI_ENTRY/README.md`](Docs/AI_ENTRY/README.md) 开始。

## 主要代码目录

```text
Plugins/MassCrowdSimulation/
├── MassCrowdCore
├── MassCrowdRuntime
├── MassCrowdSpatial
├── MassCrowdCombat
├── MassCrowdProjectiles
├── MassCrowdNetworking
├── MassCrowdPresentation
├── MassCrowdStandardSources
└── MassCrowdTests

Plugins/MassCrowdStateTreeAdapter/

Source/
├── MassCrowdDemoBusiness
└── MassAICrowdDemo
```

模块精确职责和依赖方向见 [`Docs/Reference/PluginModuleBoundary.md`](Docs/Reference/PluginModuleBoundary.md)。

## Demo 与产品边界

Demo 可以拥有：Scenario、测试地图、固定窗口、故障注入、Golden Hash、性能指标、录像与人工审片。

Demo 不应长期拥有：第二套 Worker Runtime、第二套 Commit Barrier、重复 Networking/Presentation state machine、重复的通用 Flow/Target/Particle kernel 或 Legacy Round Transaction 兼容框架。

## 测试说明

仓库文档记录了大量本地 Unreal 自动化、真实地图和 runner 证据。这些是**仓库记录的本地测试结果**，不等同于当前 GitHub `main` 上存在必需 CI status checks。

任何“已经完成”“已经通过”“10k 可用”的判断，都应同时核对：

```text
FeatureChecklist.md
+
TestScenarioMatrix.md
+
当前源码
```

而不是引用旧端口、旧 Round 或历史阶段日志。
