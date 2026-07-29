# Demo 业务规划独立模块架构

## 1. 文档职责与当前基线

[INFERRED][HIGH] 本文件是 Demo 产品业务规划、Planner 扩展合同和宿主接入边界的现行事实源；公共 Behavior Source、Projectile 和 Boundary 合同继续分别以 `EntityBehaviorSourceArchitecture.md`、`MassProjectileHitFrameworkDesign.md` 和 `CurrentArchitecture.md` 为准。

[COMPUTED][HIGH] DP0 基线提交为 `07359ed`。该基线中 `ACrowdDemoMixedSandboxCoordinator` 约 3769 行，并在 `EvaluateSlotBehavior` 内同时解释每 20 实体角色、选择目标、组合 Source、构建 Context 和提交业务；`MassCrowdRuntimeBehavior` 仍公开 `CargoPickup`、`CargoDeliver` 和 `CombatHit` 领域枚举。

[COMPUTED][HIGH] DP0 回归基线为 `MassCrowd 65/65`、`CrowdDemo 125/125`，Development/DebugGame × ForceUnity/DisableUnity 四构建通过；Mixed 20/100/500 同路径分别并发 4/20/100 发 Projectile，服务端 fixed-step p95=`2.152/9.675/30.016ms`。

## 2. 锁定模块边界

[INFERRED][HIGH] 新增项目 Runtime 模块 `MassCrowdDemoBusiness`，只依赖 `Core`、`MassCrowdCore`、`MassCrowdRuntime` 和 `MassCrowdStandardSources`。该模块不得引用 Engine World/Actor、MassEntity、Networking、Spatial、Combat、Projectiles、Presentation 或 `MassAICrowdDemo`。

[INFERRED][HIGH] `MassCrowdDemoBusiness` 拥有 Demo 产品 Source Provider、Source Set Diff、业务 Planner、业务 Ledger、Host Intent 与 Prepared Business Patch；主 Demo 模块负责 World Gather、Nav/Projectile/Movement/Network/Presentation Adapter 和场景验收。

[INFERRED][HIGH] Provider/Capability/Source/Schema/Adapter 数值 ID 保持不变，因此模块迁移不得改变 Behavior Registry Hash 或 Codec v3。

## 3. Planner 合同

[INFERRED][HIGH] Planner 只读取按 StableEntityRef 排序的不可变 POD Snapshot，并通过有界 Writer 输出 Desired Sources、Context Requests、Host Intents、诊断 Label 和 Stable Hash。Planner 不得直接读取 Source Runtime、业务 Store、Actor、Mass、Nav 或网络对象。

[INFERRED][HIGH] Planner Registry 使用稳定 PlannerId，注册后冻结；重复 ID、未知 Planner、重复实体、缺失目标/Objective、无效 Revision、超过 16 个 Source、超过 8 个 Context 或 Host Intent 冲突拒绝完整 Decision Batch。

[INFERRED][HIGH] Mixed 的业务 Planner 固定为 Logistics、PursueAttack、GuardFlee、Roam 和 Escort；Reaction 作为正交层叠加 TimedImpulse 或 Death MovementLock，不删除持久 Source。

[INFERRED][HIGH] Friendly Logistics 使用同一 Logistics Planner；Round T7/T8 分别使用 VatShowcase/RangedAttack Planner；Round T1–T6 与 Continuous 显式使用 NoBusiness，保持专项归因性。

## 4. Boundary 数据流

[INFERRED][HIGH] 生产顺序固定为：GT Gather 不可变业务/目标/Objective 事实 → Planner WORK → 稳定 Merge 与完整验证 → 在本地构建 Source Command/Context Batch → Behavior Prepare → Business/Movement/Projectile/Presentation Prepare → 全量 Validate → 一次不可失败 Final Apply。

[INFERRED][HIGH] Pickup/Deliver/Attack 继续由一帧 ServerOnly Source 产生 Resolved Business；Claim/Requeue/Cancel、Round Fire 和测试 Hit 注入使用通用 Host Intent。所有 Host Intent 必须先形成 Prepared Patch，不允许 Planner 或 Coordinator 直接写 Ledger、Health、Cooldown、Cargo 或 Projectile。

## 5. DP0–DP6

DP0. [x] [COMPUTED][HIGH] 文档、提交、角色比例、ID、测试和性能基线已冻结；未修改生产代码。

DP1. [x] [COMPUTED][HIGH] 已建立独立模块、Scenario/Planner/Snapshot/Decision/Registry/Writer/Runner 合同和结构测试。

DP2. [x] [COMPUTED][HIGH] 已实现 Mixed 五类 Planner、Reaction 叠加、稳定角色表、Context Request 和反序等价专项。

DP3. [x] [COMPUTED][HIGH] 已迁移 Demo Provider、Diff、Ledger、Combat/RangedAttack 规划与 Prepared Business Adapter；Runtime 领域业务 API 已删除。

DP4. [x] [COMPUTED][HIGH] Mixed、Friendly、Round T7/T8 已迁移；T1–T6 与 Continuous 已接入 NoBusiness；旧 Planner 双路径已删除。

DP5. [x] [COMPUTED][HIGH] 已收口共享 Planning Host 与 Source 状态发布；结构、零写入和 Hash 约束专项通过。

DP6. [x] [COMPUTED][HIGH] MassCrowd 64/64、CrowdDemo 131/131、四构建、Continuous/Friendly/NavFlow/T1–T8及Mixed 20/100/500真实门通过，事实源已更新。

[COMPUTED][HIGH] T8现行功能结果为spawn/impact/damage=50/50/50、duplicate=0且双端一致。真实StableEntityRef替换旧Demo AgentId伪引用后，版本化attack/projectile/event Hash为3512277419/488896174/4204062592；旧身份布局Hash保留为DP0历史证据。

[COMPUTED][HIGH] 生产Behavior Registry/Context Schema黄金值分别为17037152232310596158/7449648488286461483；`CrowdDemo.BehaviorAdapters.RegistryGolden`执行精确断言，T8脚本通过`-RangedProjectileGolden`执行功能计数、双端一致及三个Hash的精确断言。

## 6. 明确范围外

[INFERRED][HIGH] T9/T10、玩家 GameplayCommand、DataAsset 编排、真实 StateTree 业务 Task、动态 NavMesh topology 和原工程迁移不属于 DP0–DP6。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
