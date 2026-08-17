# MASSAITEST / MassCrowdSimulation

MASSAITEST 是一个基于 **Unreal Engine 5.7 + Mass** 的大规模 Agent Simulation 验证工程。

```text
MassCrowdSimulation
= 可复用的大规模 Agent Simulation Runtime

MassAICrowdDemo
= Runtime 的验证宿主 / 测试场
```

最终产品不是“虫群 Demo”。Demo 使用虫群移动、目标围攻、异构实体、战斗、Projectile、VAT、网络和视觉场景验证同一套可复用 Runtime。

## 架构摘要

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

Production-capable Worker Owner 已覆盖主要模拟 Domain；但当前 Demo **普通无参数启动默认仍使用 Shadow authority mode**，正式 Production 路径由 runner/命令行显式开启。不要把“Production 实现已存在”误解成“任何启动默认 Full Production”。

群体运动分层：

```text
Shared Flow
    ↓
可选 Target-relative Polar Transport Field
    ↓
Local Predictive Interaction
    ↓
Particle Safety
```

Target Region Transport 只在需要目标附近区域人口分布时启用，不是所有移动的固定必经层。

## 当前状态

项目尚不能宣称完整 10k Production Ready。

当前主要 Gate：

```text
WA8 Legacy Removal
    ↓
T5 Long-Window Correctness
    ↓
Particle Scaling
    ↓
WA9 Full 1k → 2k → 5k → 10k Acceptance
```

其中 Particle 当前已经能分解多个 Interaction Island，但多个 Island 仍在一个 Particle Work 中顺序 sub-solve；Island-level Task parallelism 与大型单 Island 内部分片都还没有关闭。

## 文档入口

第一次理解项目：

1. [`Docs/README.md`](Docs/README.md) — 文档地图与事实优先级。
2. [`Docs/CurrentArchitecture.md`](Docs/CurrentArchitecture.md) — 当前 `main` 实际是什么。
3. [`Docs/TargetArchitecture.md`](Docs/TargetArchitecture.md) — 最终决定做成什么。
4. [`Docs/PhasePlan.md`](Docs/PhasePlan.md) — 接下来按什么顺序收敛。
5. [`Docs/FeatureChecklist.md`](Docs/FeatureChecklist.md) — DONE / PARTIAL / OPEN。
6. [`Docs/TestScenarioMatrix.md`](Docs/TestScenarioMatrix.md) — 当前有效测试证据。

准备读源码时，不要直接进入巨型 `CrowdDemoRoundSimProcessors.cpp`。先读：

- [`Docs/SourceReadingMap.md`](Docs/SourceReadingMap.md) — 源码阅读顺序和搜索入口。
- [`Docs/SourceConsistencyAudit.md`](Docs/SourceConsistencyAudit.md) — 文档 ↔ 源码当前冲突。
- [`Docs/LegacyCodeInventory.md`](Docs/LegacyCodeInventory.md) — 哪些旧代码仍有消费者、哪些只是候选删除。

需要快速恢复 AI / 开发上下文时，从 [`Docs/AI_ENTRY/README.md`](Docs/AI_ENTRY/README.md) 开始。

## 主要代码目录

```text
Plugins/MassCrowdSimulation/
├── MassCrowdCore
├── MassCrowdSpatial
├── MassCrowdCombat
├── MassCrowdRuntime
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

模块精确职责和 Build.cs 依赖方向见 [`Docs/Reference/PluginModuleBoundary.md`](Docs/Reference/PluginModuleBoundary.md)。

## Demo 与产品边界

Demo 可以拥有：Scenario、测试地图、固定窗口、故障注入、Golden Hash、性能指标、录像与人工审片。

Demo 不应长期拥有：第二套 Worker Runtime、第二套 Commit Barrier、重复 Networking/Presentation state machine、重复通用 Flow/Target/Particle kernel 或 Legacy Round Transaction。

当前旧 RoundSim shell 仍有真实消费者；例如 Worker Input 仍从 RoundSimPipeline 获取部分 Shared Flow resource，因此不能把整个 Demo old shell 直接删除。清理顺序以 `LegacyCodeInventory.md` 和 `PhasePlan.md` 为准。

## 测试说明

仓库记录了大量本地 Unreal 自动化、真实地图和 runner 证据。这些是**仓库记录的本地测试结果**，不等同于当前 GitHub `main` 有必需 CI status checks，也不等同于 ChatGPT 本轮独立运行过 UE。

任何“已经完成”“已经通过”“10k 可用”的判断，都应同时核对：

```text
FeatureChecklist.md
+
TestScenarioMatrix.md
+
当前源码
```
