# 通用持续 Agent Sandbox 与插件化迁移前置计划

## 1. 文档职责

[INFERRED][HIGH] 本文件定义现有独立测试收敛、插件化提取、原工程迁移验证与最终通用持续 Agent Sandbox 的强制顺序；它描述目标和门控，不表示这些功能已经实现。长期运行与复制合同以`MassCrowdUnifiedRuntimeAndReplicationContract.md`为准。

[COMPUTED][HIGH] 当前工程仍以 RoundPlan 和固定 testcase 驱动大部分技术验证，但已具备独立continuous lifecycle和静态Recast分层Flow验收入口；持续玩家输入、运行时混合业务Planner及完整类游戏业务循环仍未实现。

## 2. 强制实施顺序

[INFERRED][HIGH] 唯一后续顺序为：合同冻结 → StableEntityRef/Capability/Behavior POD → Relevant Snapshot → Demo Bootstrap 适配 → Spawn/Despawn/Membership Delta → 最小 Mass World 真实生命周期 → Demo continuous lifecycle → Logistics/Combat 共用 Behavior API → NavMesh Surface Graph/Shared Flow → 混合行为 Sandbox → 20/100/500 → 原工程最小宿主与生产迁移。

[COMPUTED][HIGH] 当前已完成A–J：合同冻结、AgentFacts POD/Runtime映射、Relevant Snapshot、Demo Bootstrap、lifecycle batches、最小Mass World真实生命周期、Demo continuous lifecycle、统一Behavior接口、NavMesh Surface Graph/Shared Flow及20实体混合行为Sandbox。

## 3. 插件化目标与依赖方向

[INFERRED][HIGH] 插件化的目标不是把整个 Demo 原样复制回主工程，而是提取不依赖测试地图、端口、Round脚本、具体Player类或项目资产路径的稳定能力。

[COMPUTED][HIGH] 当前插件边界为：

```text
MassCrowdCore
├── 通用 POD、纯 kernel、排序、量化、hash
MassCrowdRuntime
├── Mass lifecycle、Gather、WORK、Merge、Commit、Capability 注册
MassCrowdNetworking
├── Snapshot/Delta/Correction/Event、assembly、revision、rollback 调度
MassCrowdPresentation
├── ISM/VAT、插值、Cargo 视觉、correction offset、调试绘制
MassCrowdTests
└── POD fixture、最小 Mass World、Networking loopback 与边界测试

MassAICrowdDemo Project Module
├── Round Coordinator与readiness
├── 测试场景、地图、端口和CLI
├── fixture、录像与验收指标
└── continuous Sandbox专用Director/Pawn
```

[COMPUTED][HIGH] 当前 Core/Runtime 运动、统一Behavior合同、静态Recast分层Surface Graph/Shared Flow、Networking Snapshot/lifecycle/late-join/relevancy协议、公共Presentation、Demo continuous lifecycle与20实体混合Sandbox已实现。旧Round统一、真实移动视区enter/exit、独立NavFlow与FriendlyLogistics产品场景仍未完成；上表后续职责不是当前完成状态。

[INFERRED][HIGH] 依赖方向必须保持 `Demo/主工程 Adapter → 插件公开接口 → 纯 kernel`；插件不得反向 include Demo Coordinator、ScenarioConfigActor、测试地图或Saved诊断路径。

[INFERRED][HIGH] Coordinator、关卡生成脚本、测试端口、录像工具、历史fixture和场景专用hard-coded fixed step不进入Runtime插件。

[INFERRED][HIGH] 插件公开配置不得硬编码`/Game/Maps`或当前Demo资源路径；资产、Nav数据和表现profile通过DataAsset、软引用或宿主Adapter注入。

[INFERRED][HIGH] 网络包、rollback snapshot、hash合同和DataAsset需要显式版本；主工程不得直接依赖插件private结构或Demo内部数组布局。

## 4. 插件迁移验收

[INFERRED][HIGH] 插件提取后必须在一个不包含Demo地图和Coordinator的最小宿主工程中验证：模块可加载、纯自动化可发现、公开接口可构建、无Demo反向依赖、资产软引用可替换。

[INFERRED][HIGH] 回到当前Demo后，T1-T8必须使用插件实现重新通过；禁止保留一套Demo旧实现和一套插件新实现并通过运行时开关长期并存。

[INFERRED][HIGH] 只有插件宿主回归和Demo回归均通过，才允许将插件接入`E:\Projects\SuperInvincibleTank_BugFix`；主工程接入必须通过Adapter消费，不能整体复制Demo Source目录。

## 5. 通用持续 Agent Sandbox 目标

[INFERRED][HIGH] continuous Sandbox 用于验证接近真实产品的持续业务循环，而不是新的 30 秒预写事件表：世界持续生成、销毁与迁移通用 Agent population；Agent 根据世界事实和当前 Behavior 建立 cohort 目标；Combat、Logistics 与其他业务通过统一事件接口驱动状态变化。

[INFERRED][HIGH] 敌方追逐、友方搬运和中立游荡只是三种业务配置，不是三套 Movement/Replication 类型。搬运不属于友方专用能力，追逐与攻击也不属于敌方专用能力。

[INFERRED][HIGH] Interactive输入必须量化为带`CommandId/ApplyFixedStep`的GameplayCommand，由Server验证后在fixed-step boundary发布；自动化使用相同Command序列回放，不能另写一套测试专用伤害路径。

[INFERRED][HIGH] 线形伤害首版定义为有限长度Capsule，圆形伤害定义为带NavLayer/高度过滤的地面圆柱；候选由稳定spatial grid生成，精确命中集合按AgentId排序，客户端DebugDraw不参与gameplay判定。

[INFERRED][HIGH] 持续 population 必须具有实体上限、稳定 `FCrowdStableEntityRef`、fixed-step 原子 spawn/despawn、死亡与业务回收原因、membership 增量和视觉实例回收规则，避免无限增长把生命周期错误误报为 Mass 性能问题。

## 6. NavMesh分层Flow与高低差地图

[COMPUTED][HIGH] 阶段I已把静态Recast tile/poly/portal提取为稳定分层Surface Graph，并在真实地图验证坡道、桥上桥下XY重叠、高台、多路线、窄桥和不可通行落差；Particle仍主要是2.5D安全合同，桥上桥下Particle分层组合与完整业务运行留在J验收。

[INFERRED][HIGH] 新导航层应从静态烘焙NavMesh提取稳定Surface Graph：每个节点保存StableNodeId、NavLayerId、Center XYZ、SurfaceNormal、邻接边、坡度、宽度和整数通行成本；群体目标只重建attachment与Integration/Direction，不让每个实体独立执行完整Nav查询。

[INFERRED][HIGH] 第一张高低差验收地图至少包含连续上下坡、桥上桥下XY重叠、双路线高台、窄坡/窄桥、不可通行落差、墙体转角和不同Nav层刷怪区。

[INFERRED][HIGH] 桥上与桥下实体不得因为XY接近而形成Particle pair；贴地速度必须投影到Nav表面切平面；KnockUp离地后按固定步弹道运行，Landing只能回到合法Nav层并重新加入Flow。

## 7. 分阶段能力门

[INFERRED][HIGH] continuous Sandbox 不得作为一次性综合实现，必须在生产 Snapshot/Delta 与真实生命周期通过后，依次接入 Behavior API、Combat/Logistics、NavMesh 分层 Flow 和混合行为；不得用 testcase active 标志替代实体销毁。

[INFERRED][HIGH] 每阶段先运行20实体确定业务正确性；插件版Small通过后再设计100/500，不得从20实体直接宣称真实业务WORK预算成立。

## 8. 当前停止边界

[COMPUTED][HIGH] 阶段G已实现独立continuous lifecycle，阶段I已实现NavMesh Flow与高低差地图；Player Pawn、GameplayCommand、混合业务Sandbox和主工程迁移仍未实施。

[COMPUTED][HIGH] A–J历史能力阶段已完成；K/L继续冻结，不执行正式20/100/500或原工程迁移。[COMPUTED][HIGH] 当前产品化闭环P0已完成并停止在P1前，J的独立Demo组合不等于公共Runtime/Networking/Presentation已经闭环。

## 9. 大量远程敌人的插件前置修正（2026-07-17）

[COMPUTED][HIGH] 当前T8不是最终可迁移Projectile实现：权威状态仍在Pipeline数组，Mass projectile pool仅镜像状态，命中仍是每Projectile全量扫描Agent。

[INFERRED][HIGH] Projectile 与 Combat 必须遵守现有 `MassCrowdCore / MassCrowdRuntime / MassCrowdNetworking / MassCrowdPresentation / 宿主Business` 边界；不再发明另一套并行模块命名。依赖保持`宿主Adapter → 插件Public API → 纯kernel`。

[INFERRED][HIGH] 插件HitFact使用不含Actor/UObject/FMassEntityHandle的StableEntityRef和EffectProfileKey；原工程Adapter分别把Actor目标接入`FCombatDamageSpec/UCombatResolutionLibrary`，把Mass目标接入Lifecycle校验后的批量GT combat commit。`AMassEnemyTargetProxyActor + VisualId`只作为旧兼容入口，不是插件要求。

[COMPUTED][HIGH] 原工程已经有真正的Mass ballistic projectile entity和另一条Actor projectile路径；迁移时应保留其业务profile与表现证据，但用统一HitFact收敛两条命中出口。原Mass projectile“到达锁定落点后按EffectType直接结算”不能冒充移动目标/环境碰撞已经成立。

[INFERRED][HIGH] T10线形Capsule、圆形Cylinder、近战Sweep与Mass projectile必须共享稳定空间索引和HitFact出口。大量远程敌人不得依赖逐Projectile Actor、逐Mass目标常驻Proxy Actor或`O(Projectiles×Agents)`全量扫描。

[INFERRED][HIGH] 迁移前后门、移动目标/环境命中、20/100/500规模指标和删除条件以`MassProjectileHitFrameworkDesign.md`为准。

[COMPUTED][HIGH] 2026-07-17已执行Mass Projectile插件前置核对；T3停止项已通过8455/8456，T4已通过8460/8461，当前仅T6异构前置门仍开放；插件Module、最小宿主、T8迁移和旧路径删除均未执行。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
