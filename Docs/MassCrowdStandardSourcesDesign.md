# MassCrowd Standard Sources 设计

## 1. 设计目标

[INFERRED][HIGH] 本设计采用类似 UE `FRootMotionSource` 的扩展方式，但不实现动画 Root Motion：稳定 Source 协议负责实例身份、生命周期、优先级、混合、复制和恢复；可注册 Evaluator 负责把只读 POD Context 转换为一个或多个 Contribution。

[INFERRED][HIGH] “可扩展”不等于每个高层行为都建立一个互斥派生类。基础运动和约束应实现为可组合 Source；追击攻击、护送、物流等高层行为应由 Recipe/Controller 维护期望 Source 集合。

[INFERRED][HIGH] 插件必须提供一组开箱即用的通用 Source，但纯机制层不得因此理解敌人、攻击、取货、交付、阵营策略或 Demo 场景。

## 2. 模块边界

```text
MassCrowdCore
├── 稳定ID、Source POD、Contribution、Resolver、Hash
└── 不包含具体行为语义

MassCrowdRuntime
├── Provider/Registry、World Store、Context、Boundary
├── 完整Movement安全流水线
└── 不按具体SourceTypeId分支

MassCrowdStandardSources
├── 通用Movement/Facing/Constraint Source
├── 通用Context Schema与Evaluator
└── 单向依赖Core/Runtime

MassAICrowdDemo Provider
├── Attack/Pickup/Deliver/CarryCargo等产品Source
├── Escort/Combat/Logistics等Recipe
└── 目标选择、阵营、业务与表现解释
```

[INFERRED][HIGH] `MassCrowdStandardSources`是`MassCrowdSimulation`插件随包交付的模块。它可以默认启用，但必须保持为Runtime的单向消费者；Runtime不得反向依赖或按其TypeId特判。

[INFERRED][HIGH] Standard Source使用`TypeId + Spec + ICrowdBehaviorSourceEvaluator + Payload/State`注册，不在实体热路径保存多态对象、UObject或字符串。Evaluator对象可以共享；每实体差异只存在于有界Instance Payload/State与只读Context。

## 3. Source粒度与所有权

| Source/能力 | 所有者 | 合同 |
|---|---|---|
| `MoveToLocation` | [INFERRED][HIGH] StandardSources | 静态世界位置、到达半径、最大速度；输出Movement。 |
| `ArriveAtLocation` | [INFERRED][HIGH] StandardSources | 在减速半径内确定性降速；输出Movement。 |
| `FollowEntity` | [INFERRED][HIGH] StandardSources | 读取目标运动学Context并保持跟随偏移；不选择目标。 |
| `PursueEntity` | [INFERRED][HIGH] StandardSources | 读取目标位置/速度并计算有界预测截获；不判断敌我或发起攻击。 |
| `FleeFromEntity` | [INFERRED][HIGH] StandardSources | 从目标运动学事实生成反向Movement；不决定恐惧/威胁规则。 |
| `MaintainDistance` | [INFERRED][HIGH] StandardSources | 维持最小/最大距离并提供滞回；不解释战斗距离语义。 |
| `FaceMovement`/`FaceEntity` | [INFERRED][HIGH] StandardSources | 独立输出Facing。 |
| `FormationOffset` | [INFERRED][MED] StandardSources | 读取宿主提供的锚点/槽位Context并输出Additive Movement。 |
| `MovementLock`/`SpeedLimit` | [INFERRED][HIGH] StandardSources | 输出通用Constraint。 |
| `TimedImpulse`/`Knockback` | [INFERRED][MED] StandardSources | 输出有期限高优先级运动贡献；最终结果仍经过安全流水线。 |
| `WanderSteering` | [INFERRED][MED] StandardSources | 只负责确定性漫游状态与运动；可通行候选/Nav采样由宿主Context提供。 |
| `AttackTarget` | [INFERRED][HIGH] Demo/产品Provider | 攻击合法性、Commit和伤害属于宿主业务。 |
| `Pickup`/`Deliver` | [INFERRED][HIGH] Demo/产品Provider | 依赖物流账本和宿主原子提交。 |
| `Escort` | [INFERRED][HIGH] Demo/产品Recipe | 由FollowEntity、MaintainDistance、FormationOffset和Facing组合。 |
| `Death` | [INFERRED][HIGH] 分层 | 通用永久MovementLock可复用；死亡、掉落和销毁语义属于宿主。 |

[INFERRED][HIGH] 不允许为“复用代码”把已经在上层计算完成的领域结果统一塞入一个无语义`Vector` Payload，然后用同一Evaluator冒充完整Source实现。`ExternalGuidance`可以作为明确命名的桥接Source，但不得代替Pursue、Wander、Follow等自主Evaluator的验收。

## 4. 标准Context与状态合同

[INFERRED][HIGH] `FollowEntity`、`PursueEntity`、`FleeFromEntity`和`FaceEntity`共享版本化`TargetKinematics` Context，至少包含目标`StableEntityRef`、位置、速度、Facing、NavLayer和事实Revision；Gather必须验证Payload目标与Context目标一致。

[INFERRED][HIGH] `FormationOffset`共享版本化`FormationAnchor` Context，包含锚点Ref、锚点Transform/Velocity、槽位偏移与Revision。Evaluator不得自行查询Actor、Mass EntityManager、World或NavMesh。

[INFERRED][HIGH] `WanderSteering`的持久State至少覆盖确定性随机状态、当前候选/方向和下一次重选固定步；回放、late join和临时压制后必须从同一State继续，不得依赖墙钟时间或全局随机数。

[INFERRED][HIGH] Payload与State继续受96字节上限约束；扩展Context继续受每项96字节、每次Evaluate最多8项约束。任何Schema、目标Ref、Revision或容量错误必须在Final Apply前整批拒绝。

## 5. 组合合同

[INFERRED][HIGH] 追击攻击的期望集合为`PursueEntity + MaintainDistance + FaceEntity + AttackTarget`；其中前三项是通用运动/朝向Source，最后一项是产品业务Source。

[INFERRED][HIGH] 护送的期望集合为`FollowEntity + MaintainDistance + FormationOffset + FaceMovement`；遭遇战、受击或短暂交互只增加临时Source，不删除护送集合。

[INFERRED][HIGH] HitReaction、Stun或Knockback到期后，原Pursue/Follow/Wander实例、Handle、Payload和持久State必须仍然存在；恢复不得通过Stop-All/Start-All重新创建实例。

[INFERRED][HIGH] 产品Controller直接维护期望Source集合并生成稳定Start/Update/Stop Diff。`ECrowdActiveBehavior`最多保留为迁移输入或诊断Label，不得长期拥有生产控制权。

## 6. 生产消费合同

[INFERRED][HIGH] 所有Standard Movement Source只输出Movement/Facing/Constraint Contribution；Resolver结果必须进入同一`Movement Pipeline Builder`，依次执行Guidance适配、Local Predictive、Movement Predict、Particle、Obstacle/Bounds、量化与Prepared Patch。

[INFERRED][HIGH] Business或Presentation通道非空不得隐式停止移动。只有Resolved Movement为零、显式Movement Lock/Speed Limit或安全阶段拒绝候选时，移动消费者才允许停止/限制运动。

[INFERRED][HIGH] Production Adapter不得扫描SourceSet、判断Standard Source TypeId或重新解释Payload；消费入口只能是Resolved Channels和通用Context/Patch合同。

## 7. 现状与偏差（2026-07-28）

[COMPUTED][HIGH] 当前开放Provider、冻结Registry、Source Instance、Context/State、六通道Resolver、Start/Update/Stop与第三方Fixture已经实现，足以承载StandardSources模块。

[COMPUTED][HIGH] `MassCrowdStandardSources`现注册13个稳定TypeId和自主Evaluator；TargetKinematics、FormationAnchor、MaintainDistance State与Wander State均为有界POD。第三方Fixture只依赖Public API，并注册仓库核心未知的TypeId。

[COMPUTED][HIGH] Demo Provider现只保留SharedFlow桥、CarryCargo和Pickup/Deliver/Attack产品语义；Move/Arrive/Follow/Pursue/Flee/Distance/Facing/Constraint/Wander/Formation/Impulse全部来自StandardSources。Escort由Follow+MaintainDistance+FormationOffset+FaceMovement+SpeedLimit组合。

[COMPUTED][HIGH] Mixed不再使用`FCrowdDemoBehaviorRecipe`展开Source；Navigation/Facing/Interaction/Presentation/Reaction五Controller分别维护自己拥有的Handle和Command Sequence，稳定Diff只发必要Start/Update/Stop。目标丢失时同Boundary省略依赖Handle并生成Stop，不把缺Context交给Evaluator。

[COMPUTED][HIGH] S1后Mixed把Resolved Movement/Facing/Constraint送入`FCrowdMassMovementPipelineWork → Particle Constraint → Facing Finalize`生产链；Shared Flow只作为目标引导输入，Local Predictive、运动预测、Particle/Bounds约束、垂直运动和最终量化均由同一Prepared Boundary完成。Business数组非空不再隐式跳过移动，只有Resolved零速度、显式Constraint或安全阶段拒绝才会停止/限制候选。

[COMPUTED][HIGH] S1真实垂直地图复测暴露并修复了跨NavLayer的2D邻居误判：Local Predictive与Particle输入现携带`InteractionLayer`，不同层不参与成对约束；默认全零层继续保持旧Hash兼容。该修复已有Core专项和Mixed真实场景证据。

[COMPUTED][HIGH] Presentation Resolver已按Property保留最高优先级Override和稳定排序的全部Additive记录；Runtime不解释属性值语义。

## 8. 现行实施阶段（S0–S6）

S0. [x] [COMPUTED][HIGH] 文档已锁定RootMotionSource式扩展类比、StandardSources模块边界、通用/产品Source归属、组合原则和当前代码偏差。

S1. [x] [COMPUTED][HIGH] Mixed生产消费已修复：Resolved Movement/Facing/Constraint进入完整Movement Pipeline与Prepared Final Apply；Business非空不再停移；Local Predictive/Particle按InteractionLayer过滤。`MassCrowd.Core`定向13/13、`CrowdDemo.MixedSandbox.J` 3/3、Development Editor `-DisableUnity`及8010服务端/8011双端20实体真实门通过。

S2. [x] [COMPUTED][HIGH] `MassCrowdStandardSources`模块、稳定Provider/TypeId范围、`TargetKinematics`/`FormationAnchor`公共Context Schema和单向模块边界已实现。

S3. [x] [COMPUTED][HIGH] 第一批自主Evaluator、Goal、Context验证、MaintainDistance迟滞State及Facing/Constraint独立输出已实现并通过定向自动化。

S4. [x] [COMPUTED][HIGH] Demo重复运动薄Evaluator已删除，产品Provider边界和五Controller稳定Diff已落地；生产Movement/Facing只消费Resolved Channels。

S5. [x] [COMPUTED][HIGH] 确定性Wander、FormationOffset和有期限TimedImpulse已实现；Escort、Pursue+Attack、显式一帧Lock及HitReaction精确恢复均有专项证据。

S6. [x] [COMPUTED][HIGH] StandardSources 8/8、Mixed组合5/5、第三方三复制策略Fixture与20/100/500同一生产路径双端门均已关闭。PJ6最终回归更新为MassCrowd 65/65、CrowdDemo 125/125与四构建通过，并在相同路径加入4/20/100发公共Projectile；服务端p95=`2.152/9.675/30.016ms`、客户端p95=`4.963/4.938/5.171ms`，三种规模均双端Hash一致、零resync和零安全违规。目标丢失Stop、Schema拒绝、Wander回放、Distance迟滞、显式Lock、HitReaction精确恢复和完整安全链均有专项或真实门证据。

## 9. 验收边界

[INFERRED][HIGH] “注册一个未知TypeId的Fixture”只证明扩展API，不证明通用Source库完成。

[INFERRED][HIGH] “TypeId存在”或“通用Evaluator把Payload.Vector写入Contribution”只证明桥接，不证明对应Pursue/Wander/Formation算法已经实现。

[INFERRED][HIGH] StandardSources关闭必须同时证明：自主Evaluator、POD Context/State、Source生命周期、Resolver组合、完整生产安全链、网络回放以及临时压制后的精确恢复。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
