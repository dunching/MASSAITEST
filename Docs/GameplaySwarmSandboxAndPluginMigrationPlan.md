# 类游戏虫群 Sandbox 与插件化迁移前置计划

## 1. 文档职责

[INFERRED][HIGH] 本文件定义现有独立测试收敛、插件化提取、主工程迁移验证与最终类游戏虫群 Sandbox 的强制顺序；它描述目标和门控，不表示这些功能已经实现。

[COMPUTED][HIGH] 当前工程仍以 RoundPlan 和固定 testcase 驱动技术验证，不具备持续玩家输入、动态刷怪、运行时群体业务 Planner、NavMesh 分层 Flow 或完整类游戏业务循环。

## 2. 强制实施顺序

```text
T1-T8 独立 Small 归因场景大致全部通过
→ 清理已知同步、rollback、视觉和文档硬失败
→ 提取可复用 Runtime/Navigation/Visual 插件
→ 在独立最小宿主工程回归插件
→ 回到 Demo 执行 T1-T8 插件版回归
→ 设计并运行 T9 Mixed Combat Integration
→ 最后实施 T10 Gameplay Swarm Sandbox
```

[INFERRED][HIGH] “大致全部通过”的最低定义为：T1-T8 各自的纯自动化、20实体真实 Small 技术门与能力门无已知硬失败；依赖表现的场景完成有效录像和人工审片；Development 与 DebugGame Editor 均通过；无 Fatal、Assertion、Ensure、`LogWindows: Error` 或 VIOLATION。

[INFERRED][HIGH] 100/500 不要求在首次插件提取前覆盖每一种业务场景，但插件迁移到主工程前必须至少对核心 Shared Flow、Particle、correction、完整实例显示和代表性业务事件执行 20/100/500 回归，并记录真实 WORK/GT 边界与性能预算。

[COMPUTED][HIGH] 当前尚未满足此前置门：T3已在8455/8456完成双向交换门，T4已在8460/8461完成有效通道技术、能力与审片门，但T6A/T6S/T6M尚未运行。T7细动作近景审片和T8静止目标投射物Small门已关闭，但T8尚未覆盖移动目标、100/500或DebugGame完整阶段回归。

## 3. 插件化目标与依赖方向

[INFERRED][HIGH] 插件化的目标不是把整个 Demo 原样复制回主工程，而是提取不依赖测试地图、端口、Round脚本、具体Player类或项目资产路径的稳定能力。

[INFERRED][HIGH] 第一轮候选边界为：

```text
MassCrowdCore Runtime Plugin
├── 稳定 POD/SoA 数据合同
├── Shared Flow、Particle、Target Transport 等纯 kernel
├── 最小 Mass fragments/processors
├── fixed-step 输入/输出接口
└── 确定性 hash 与基础自动化

MassCrowdVisual Runtime/Content Plugin
├── VAT playback 纯合同
├── ISM custom-data 提交适配
├── 可配置 mesh/material/profile
└── 不依赖 Demo 固定资产路径

MassCrowdNavigation Runtime Plugin（NavMesh阶段新增）
├── NavMesh/Recast 只读拓扑提取适配
├── 稳定 Surface Graph Asset/SoA
├── 分层 Flow 构建
└── 坡面投影与 NavLayer 合同

MassAICrowdDemo Project Module
├── Round Coordinator与readiness
├── 测试场景、地图、端口和CLI
├── fixture、录像与验收指标
└── Gameplay Sandbox专用Director/Pawn
```

[INFERRED][HIGH] 依赖方向必须保持 `Demo/主工程 Adapter → 插件公开接口 → 纯 kernel`；插件不得反向 include Demo Coordinator、ScenarioConfigActor、测试地图或Saved诊断路径。

[INFERRED][HIGH] Coordinator、关卡生成脚本、测试端口、录像工具、历史fixture和场景专用hard-coded fixed step不进入Runtime插件。

[INFERRED][HIGH] 插件公开配置不得硬编码`/Game/Maps`或当前Demo资源路径；资产、Nav数据和表现profile通过DataAsset、软引用或宿主Adapter注入。

[INFERRED][HIGH] 网络包、rollback snapshot、hash合同和DataAsset需要显式版本；主工程不得直接依赖插件private结构或Demo内部数组布局。

## 4. 插件迁移验收

[INFERRED][HIGH] 插件提取后必须在一个不包含Demo地图和Coordinator的最小宿主工程中验证：模块可加载、纯自动化可发现、公开接口可构建、无Demo反向依赖、资产软引用可替换。

[INFERRED][HIGH] 回到当前Demo后，T1-T8必须使用插件实现重新通过；禁止保留一套Demo旧实现和一套插件新实现并通过运行时开关长期并存。

[INFERRED][HIGH] 只有插件宿主回归和Demo回归均通过，才允许将插件接入`E:\Projects\SuperInvincibleTank_BugFix`；主工程接入必须通过Adapter消费，不能整体复制Demo Source目录。

## 5. T10 Gameplay Swarm Sandbox目标

[INFERRED][HIGH] T10用于验证接近真实游戏的持续业务循环，而不是新的30秒预写事件表：玩家控制Pawn移动；地图按稳定规则持续生成敌群；敌群根据玩家和世界事实建立cohort目标；玩家通过命令释放线形或圆形伤害；命中产生统一HitFact，并驱动伤害、HitFlash、击退、击飞、落地、死亡和成员移除。

[INFERRED][HIGH] Interactive输入必须量化为带`CommandId/ApplyFixedStep`的GameplayCommand，由Server验证后在fixed-step boundary发布；自动化使用相同Command序列回放，不能另写一套测试专用伤害路径。

[INFERRED][HIGH] 线形伤害首版定义为有限长度Capsule，圆形伤害定义为带NavLayer/高度过滤的地面圆柱；候选由稳定spatial grid生成，精确命中集合按AgentId排序，客户端DebugDraw不参与gameplay判定。

[INFERRED][HIGH] 持续生成必须具有实体上限、稳定SpawnId/LifecycleSerial、fixed-step原子spawn/despawn和尸体回收规则，避免无限增长把生命周期错误误报为Mass性能问题。

## 6. NavMesh分层Flow与高低差地图

[COMPUTED][HIGH] 当前Shared Flow和Particle主要是XY平面合同，当前代码没有正式NavMesh/Recast生产接入；高低差能力不能通过只增加坡道模型宣称完成。

[INFERRED][HIGH] 新导航层应从静态烘焙NavMesh提取稳定Surface Graph：每个节点保存StableNodeId、NavLayerId、Center XYZ、SurfaceNormal、邻接边、坡度、宽度和整数通行成本；群体目标只重建attachment与Integration/Direction，不让每个实体独立执行完整Nav查询。

[INFERRED][HIGH] 第一张高低差验收地图至少包含连续上下坡、桥上桥下XY重叠、双路线高台、窄坡/窄桥、不可通行落差、墙体转角和不同Nav层刷怪区。

[INFERRED][HIGH] 桥上与桥下实体不得因为XY接近而形成Particle pair；贴地速度必须投影到Nav表面切平面；KnockUp离地后按固定步弹道运行，Landing只能回到合法Nav层并重新加入Flow。

## 7. 分阶段能力门

[INFERRED][HIGH] T10不得作为一次性综合实现，必须依次通过：平面GameplayCommand/HitFact门；动态spawn/despawn与玩家追逐门；无战斗的NavMesh分层Flow门；最后才合并玩家、刷怪、线/圆伤害、击退/击飞、死亡与高低差导航。

[INFERRED][HIGH] 每阶段先运行20实体确定业务正确性；插件版Small通过后再设计100/500，不得从20实体直接宣称真实业务WORK预算成立。

## 8. 当前停止边界

[COMPUTED][HIGH] 本轮只对齐Markdown文档，不实施插件、Player Pawn、持续刷怪、GameplayCommand、NavMesh Flow、高低差地图或主工程迁移。

[INFERRED][HIGH] 当前下一步仍是关闭T1-T8现有独立测试缺口；在前置门完成前，不应新增T10 Source、地图或资产。

## 9. 大量远程敌人的插件前置修正（2026-07-17）

[COMPUTED][HIGH] 当前T8不是最终可迁移Projectile实现：权威状态仍在Pipeline数组，Mass projectile pool仅镜像状态，命中仍是每Projectile全量扫描Agent。

[INFERRED][HIGH] 插件提取必须新增`CrowdRuntimeCore / CrowdSpatialRuntime / CrowdProjectileRuntime / CrowdCombatBridge / CrowdPresentationRuntime`职责边界；实际UE模块可合并，但依赖必须保持`宿主Adapter → 插件Public API → 纯kernel`。

[INFERRED][HIGH] 插件HitFact使用不含Actor/UObject/FMassEntityHandle的StableEntityRef和EffectProfileKey；原工程Adapter分别把Actor目标接入`FCombatDamageSpec/UCombatResolutionLibrary`，把Mass目标接入Lifecycle校验后的批量GT combat commit。`AMassEnemyTargetProxyActor + VisualId`只作为旧兼容入口，不是插件要求。

[COMPUTED][HIGH] 原工程已经有真正的Mass ballistic projectile entity和另一条Actor projectile路径；迁移时应保留其业务profile与表现证据，但用统一HitFact收敛两条命中出口。原Mass projectile“到达锁定落点后按EffectType直接结算”不能冒充移动目标/环境碰撞已经成立。

[INFERRED][HIGH] T10线形Capsule、圆形Cylinder、近战Sweep与Mass projectile必须共享稳定空间索引和HitFact出口。大量远程敌人不得依赖逐Projectile Actor、逐Mass目标常驻Proxy Actor或`O(Projectiles×Agents)`全量扫描。

[INFERRED][HIGH] 迁移前后门、移动目标/环境命中、20/100/500规模指标和删除条件以`MassProjectileHitFrameworkDesign.md`为准。

[COMPUTED][HIGH] 2026-07-17已执行Mass Projectile插件前置核对；T3停止项已通过8455/8456，T4已通过8460/8461，当前仅T6异构前置门仍开放；插件Module、最小宿主、T8迁移和旧路径删除均未执行。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
