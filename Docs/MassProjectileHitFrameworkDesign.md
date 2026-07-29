# Mass Projectile、空间碰撞与通用命中接口设计

[COMPUTED][HIGH] 文档状态：本文件的合同已经在当前Demo生产路径实现；2026-07-17的“插件未开始、数组权威、固定32槽、全量扫描”全部是历史审计，不是当前源码状态。Behavior Source状态由`EntityBehaviorSourceArchitecture.md`单独管理。

## 1. 文档职责

[INFERRED][HIGH] 本文件是“大量远程敌人、真正的 Mass projectile entity、稳定空间查询、通用命中/被命中接口和插件迁移”的权威设计事实源。

[INFERRED][HIGH] `RangedCombatVatAndHitResponseDesign.md`继续记录T7/T8已经通过的业务、VAT和静止目标证据；本文件定义其后的可扩展生产架构。T8现有Small结果是回归基线，不是最终大量投射物架构已经成立的证据。

## 2. 已确认的当前事实与修正原因

[COMPUTED][HIGH] 当前Demo的Projectile位置、速度、发射者、生命周期、NavLayer、Collision/Effect Profile、Pierce与状态只保存在`FCrowdDemoMassProjectileFragment`并由Mass Subsystem gather/apply；容量通过配置动态准备。`PreparedProjectiles`、`MirrorProjectileStates()`和固定32槽路径已从Source删除。

[COMPUTED][HIGH] 当前Projectile kernel按量化网格构建移动目标Swept Bounds，查询候选后执行相对Sweep；环境体使用稳定SurfaceId和扩张AABB Sweep。相同TOI按稳定目标引用决胜，并显式过滤Faction、NavLayer和已命中目标；专项断言候选数而不是执行Projectile×Agent全扫描。

[COMPUTED][HIGH] `MassAIExample/BulletHellExample`证明了轻量Mass bullet entity、Signal生命周期和二维层级HashGrid候选查询；它的当前点半径命中可能高速穿透，不能直接作为最终窄相算法。

[COMPUTED][HIGH] `MassSample/ProjectileSim`证明了Mass projectile entity与previous→current世界trace、HitResult observer和表现分离；它依赖UE世界碰撞查询，且没有本Demo所需的fixed-step rollback、稳定hash和大规模Mass目标业务合同。

[COMPUTED][HIGH] 原工程`E:\Projects\SuperInvincibleTank_BugFix`同时存在两条Projectile路径：玩家/装备侧`AGameplayProjectileActor + UProjectileMovementComponent + Sphere overlap/hit`，以及敌群侧真正的`FProjectMassProjectileFragment/FProjectMassProjectileEffectFragment`与批量Mass spawn/move/destroy processor。

[COMPUTED][HIGH] 原工程Mass projectile目前按锁定TargetLocation和固定TravelSeconds计算解析抛物线，到达Alpha=1后按EffectType直接生成MudZone或对平台/目标点结算；它证明了entity-native生命周期和批量视觉事件，但没有对移动实体或环境执行通用Broadphase+Swept碰撞。

[COMPUTED][HIGH] 原工程最终伤害/被命中出口仍以`FCombatDamageSpec`、`FCombatDamageEvent`、`AActor::TakeDamage()`、`VisualId`和`AMassEnemyTargetProxyActor + UBusinessMassEnemyTargetSubsystemBase`为主要桥；Actor projectile与Mass projectile还没有共享一个无Actor依赖的HitFact入口。

[INFERRED][HIGH] 原工程的Mass projectile生命周期、伤害、防御、状态、掉落和表现语义必须保留，但Actor指针、VisualId代理、EffectType业务分支和逐Projectile Actor不能成为插件底层合同。

## 3. 修正后的总原则

[INFERRED][HIGH] 最终架构固定采用：

```text
真实 Mass Projectile Entity
→ 稳定空间Broadphase
→ fixed-step相对Swept Narrowphase
→ 与项目无关的ImpactFact/HitFact
→ 宿主Combat Adapter
→ 原工程伤害/状态/击退/击飞/死亡与表现
```

[INFERRED][HIGH] 插件只负责“谁在何时以什么几何和效果事实命中了谁”，不直接拥有原工程护甲公式、属性组件、掉落、任务、ItemTag分支或具体VAT资产。

[INFERRED][HIGH] 宿主只负责把稳定HitFact翻译成自己的伤害结算和业务结果，不重新执行projectile trajectory、空间候选查询或命中几何。

[INFERRED][HIGH] 线形Capsule、圆形Cylinder、近战Sweep和Projectile都必须复用同一TargetRef、空间索引和HitFact出口；不得为每种武器再建一套“命中/被命中”接口。

## 4. 通用POD合同

### 4.1 稳定目标引用

```cpp
struct FCrowdCombatEntityRef
{
    uint16 ProviderId;
    uint64 StableEntityId;
    uint32 LifecycleSerial;
    uint32 TeamMask;
    uint32 CollisionLayerMask;
};
```

[INFERRED][HIGH] `FCrowdCombatEntityRef`不得保存`AActor*`、`UObject*`、`FMassEntityHandle`或Demo的AgentId假设。`ProviderId + StableEntityId + LifecycleSerial`由宿主Adapter映射到本地Actor或Mass Entity；Team/Collision mask只负责候选过滤，不承载具体阵营业务。

### 4.2 碰撞快照

```cpp
struct FCrowdCollisionBodySnapshot
{
    FCrowdCombatEntityRef Entity;
    FVector StartPosition;
    FVector AppliedEndPosition;
    float RadiusCm;
    uint16 NavLayer;
    uint32 ResponseMask;
    bool bAlive;
};
```

[INFERRED][HIGH] 每个fixed-step只向WORK侧发布稳定排序的POD碰撞快照。移动目标必须提供Start与已经通过局部安全约束的AppliedEnd，不能继续把目标当作静态球心。

### 4.3 Projectile Entity最小fragment

```text
ProjectileIdentity       ProjectileId / SpawnSequence / Lifecycle
ProjectileSource         SourceRef / OptionalLockedTargetRef
ProjectileKinematics     Previous / Current / Velocity / TrajectoryModel
ProjectileCollision      Radius / QueryMask / ResponseMask / NavLayer
ProjectileEffect         EffectProfileKey / QuantizedMagnitudeOverrides
ProjectileLifetime       SpawnStep / AgeSteps / MaxSteps
ProjectileState          Active / Impacted / Expired / PendingDestroy
```

[INFERRED][HIGH] 这些fragment才是权威状态。可使用entity pool或批量spawn降低结构变更成本，但pool必须直接由projectile processors读写；禁止继续使用普通数组求解后再镜像到Mass实体。

[INFERRED][HIGH] 第一版轨迹模型至少显式区分`Linear`；后续`Homing`、`Ballistic`、`Piercing`或`Bounce`必须作为版本化profile/fragment能力加入，不能通过武器名或AgentId隐式分支。

### 4.4 ImpactFact与HitFact

```text
ImpactFact（几何事实）
├── ProjectileId / ImpactEventId / FixedStepIndex / QuantizedTime
├── SourceRef / CauserRef / TargetRef
├── HitPosition / HitNormal / RelativeVelocity
├── EnvironmentId或TargetRef
└── EffectProfileKey

HitFact（业务请求）
├── StableHitEventId / ApplyFixedStep
├── SourceRef / CauserRef / TargetRef
├── HitPosition / HitDirection
├── EffectProfileKey
├── QuantizedMagnitudeOverrides
└── HitFlags
```

[INFERRED][HIGH] `ImpactFact`只描述碰撞；`HitFact`是可供伤害、状态、击退、击飞和视觉共同消费的稳定请求。两者均不得包含Actor指针、属性组件或宿主私有fragment地址。

[INFERRED][HIGH] `EffectProfileKey`由宿主解析为伤害、穿甲、状态、水平/垂直Impulse和表现profile。插件可提供最小默认resolver供Demo测试，但原工程必须通过Adapter接入自己的`FCombatDamageSpec`、属性、防御和状态系统。

[INFERRED][HIGH] `HitResolveResult`应返回AppliedDamage、bKilled、实际状态变化、requested/realized impulse和VisualEventKey；Projectile/Shape查询不能通过读取宿主HP来反推是否命中成功。

## 5. 空间索引与碰撞合同

[INFERRED][HIGH] 每个fixed-step从排序后的`FCrowdCollisionBodySnapshot`构建或确定性更新碰撞索引。第一版采用稳定Uniform/Hierarchical Grid；cell key、cell内实体顺序和候选pair最终顺序必须稳定，禁止依赖`TMap/TSet`迭代顺序。

[INFERRED][HIGH] 每个目标使用`Start→AppliedEnd`的swept AABB注册到所有覆盖cell；Projectile使用自身previous→proposed swept AABB查询候选，再执行移动球—移动球相对sweep。复杂度目标从全量`O(Projectiles×Agents)`改为`O(BuildBodies + Projectiles + CandidateTests)`。

[INFERRED][HIGH] 窄相决胜固定为最早量化impact time → Target stable ref → ProjectileId。生命周期、Team mask、Collision layer、已命中过的Target和pierce policy在窄相前后分别执行稳定过滤。

[INFERRED][HIGH] 静态环境通过独立`IEnvironmentCollisionSnapshotProvider`提供稳定Obstacle/Surface facts；Projectile obstacle sweep与Agent query使用相同时间量化，但环境命中不得伪装成Target HitFact。

[INFERRED][HIGH] 高低差阶段必须让碰撞快照携带NavLayer并使用三维或layer-aware broadphase；桥上桥下XY重叠但NavLayer不同的实体不得互相成为普通地面命中候选，跨层Projectile则必须由显式profile允许。

## 6. Processor、WORK与GT边界

```text
FixedStepBoundaryBegin
→ HostCombatSnapshotPrepare              GT，只读宿主Actor/Mass事实
→ AttackIntent/PhaseAdvance              POD/Mass
→ ProjectileSpawnRequestBuild            POD
→ MassProjectileBatchSpawn               GT deferred commands
→ AgentMovement/Particle/MovementFinalize
→ CollisionBodySnapshotBuild             GT gather → immutable POD
→ ProjectileMovementPredict              WORK-capable
→ ProjectileBroadphaseQuery               WORK-capable
→ ProjectileRelativeSweptNarrowphase      WORK-capable
→ ImpactFact/HitFactBuild                 WORK-capable
→ ProjectileFinalize/Destroy              GT commit
→ HostHitResolveCommit                    GT，唯一业务写入边界
→ VisualEventCommit                       GT/网络表现
```

[INFERRED][HIGH] Projectile collision消费Agent本步Start与AppliedEnd，命中在本boundary末提交；伤害可立即写入combat事实，但击退/击飞运动从下一fixed-step进入ReactiveIntent与Particle，避免在已经Finalize的同一步回写Transform。

[INFERRED][HIGH] WORK processor只消费不可变POD并输出POD，不访问`UWorld`、Actor、UObject、Mass fragments或宿主属性系统。GT processor负责快照、Mass fragment提交、宿主Combat Adapter和网络事件。

[INFERRED][HIGH] 同一HitFact只能由一个HostHitResolve writer消费；Demo resolver、原工程`TakeDamage()`兼容桥和Mass直接写入不能在同一运行中重复生效。

## 7. 插件模块与依赖方向

```text
CrowdRuntimeCore
├── StableEntityRef / HitFact / versioned settings / hashes
CrowdSpatialRuntime
├── collision snapshots / grid / broadphase / shape queries
CrowdProjectileRuntime
├── projectile fragments / traits / spawn / trajectory / hit processors
CrowdCombatBridge
├── hit queue / resolver interfaces / minimal default resolver
CrowdPresentationRuntime
└── projectile visual event / VAT adapter contracts

Demo Adapter或原工程 Adapter
→ 插件Public API
→ 插件纯kernel
```

[INFERRED][HIGH] 实际可先合并为较少UE模块，但公开依赖必须保持上述单向关系。插件不得include Demo Coordinator、RoundPlan、Scenario枚举、测试地图、Saved fixture路径、原工程`Business`类或具体ItemTag。

[INFERRED][HIGH] 原工程Adapter负责：

- [INFERRED][HIGH] Actor目标：`FCrowdCombatEntityRef → AActor`，再构造`FCombatDamageSpec/FCombatDamageEvent`并进入现有`UCombatResolutionLibrary`。
- [INFERRED][HIGH] Mass目标：`FCrowdCombatEntityRef → FMassEntityHandle + Lifecycle`，由GT批量processor直接写入Mass combat fragments；不要求为每个Mass敌人生成`AMassEnemyTargetProxyActor`。
- [INFERRED][HIGH] 旧Actor projectile：仅保留给低频、强Actor语义或尚未迁移的武器；大量Mass远程敌人统一走Mass Projectile Runtime。两条路径必须共享HitFact/EffectProfile语义，不能共享同一发射请求后重复结算。

## 8. 网络、Rollback与视觉

[INFERRED][HIGH] 服务端始终是gameplay hit与damage权威。客户端不为每颗Projectile创建持续复制Actor；客户端通过Spawn/Impact/Expire/Correction事件和同步ServerTime重建ISM/Niagara表现。

[INFERRED][HIGH] 插件合同必须版本化保存Projectile fragment状态、spawn/destroy command cursor、空间快照revision、HitFact queue、LastConsumedHitEventId、visual event cursor和hash。rollback先恢复这些事实，再重放；不得重复spawn、impact、damage或visual event。

[INFERRED][HIGH] Demo可以继续运行双端同构模拟验证确定性；原工程可以采用“Server gameplay simulation + Client visual prediction/event correction”。这属于宿主策略差异，不得改变HitFact ID和表现事件守恒合同。

## 9. 分阶段迁移顺序

1. [INFERRED][HIGH] 冻结现有T8 8451/8450静止目标基线和纯fixture；明确其只验证20实体、静止目标与数组权威实现。
2. [INFERRED][HIGH] 在插件中先建立StableEntityRef、CollisionSnapshot、ImpactFact、HitFact和Host resolver接口，并用纯POD自动化验证Actor/Mass引用与Lifecycle失效。
3. [INFERRED][HIGH] 实现稳定spatial grid、移动目标相对sweep、环境sweep与线/Cylinder共享shape query；先通过纯kernel门。
4. [INFERRED][HIGH] 将T8替换为entity-native Mass Projectile；删除`PreparedProjectiles`权威数组、`MirrorProjectileStates()`和固定32镜像pool。迁移比较使用离线fixture，不保留长期运行时A/B双路径。
5. [INFERRED][HIGH] 在不包含Demo Coordinator和地图的最小宿主工程验证插件构建、spawn/hit/rollback和visual event。
6. [INFERRED][HIGH] Demo改用插件后重跑T1–T8；T8新增移动目标、墙体阻挡、阵营过滤和并发Projectile门。
7. [INFERRED][HIGH] 原工程先接入Adapter并验证现有Actor projectile、Mass ballistic projectile、命中、被命中、护甲、状态、击退/击飞、死亡、掉落和表现不回退，再把大量Mass远程敌人迁入通用Projectile/HitFact链；低频Actor武器分开记录迁移状态。
8. [INFERRED][HIGH] 最后执行20/100/500敌人与代表性并发Projectile规模门，通过后才进入T9/T10持续刷怪和完整类游戏业务。

## 10. 自动化、指标与验收

[INFERRED][HIGH] 纯测试至少覆盖：Mass entity spawn/despawn、pool复用Lifecycle、移动目标相对sweep、高速穿越、最早hit、墙体先于目标、桥上桥下layer过滤、友军过滤、pierce一次性、输入反序、rollback不重复命中和Actor/Mass Adapter等价HitFact。

[INFERRED][HIGH] 运行指标至少包括：active/spawned/impacted/expired、守恒、broadphase candidate p50/p95/max、narrowphase tests、grid build/query/solve ms p95、moving-target hits、environment hits、layer/team rejects、lifecycle rejects、duplicate hit/damage、HitFact queue backlog、GT commit count和client visual event守恒。

[INFERRED][HIGH] 大量远程敌人验收必须同时证明：没有全量`Projectile×Agent`扫描、没有每Projectile持续复制Actor、没有每Mass目标常驻碰撞Proxy Actor、WORK阶段无UObject访问、server gameplay hit唯一、客户端视觉完整、20/100/500下无命中漏失或重复伤害。

## 11. 当前实现边界

[COMPUTED][HIGH] 本仓库Demo已完成Mass权威Projectile、动态容量、网格Broadphase、相对/环境Sweep、`FCrowdImpactFact`、`FCrowdHitFact`、`ICrowdHostHitResolver`、宿主唯一伤害提交、Pierce与Faction/NavLayer过滤。T8专项当前13/13通过。

[INFERRED][HIGH] 原工程Actor/VisualId桥仍未修改，因为本轮范围明确限于本仓库及Demo。20/100/500 Mixed规模门也不等于代表性并发Projectile规模门；后者仍是R7未关闭组合项。

## 12. 2026-07-17 前置门执行结果

[COMPUTED][HIGH] 已完成计划指定的当前Demo、原工程Combat/Mass projectile和外部BulletHell/ProjectileSim/Octree只读核对。当前T8的数组权威、镜像pool、全Agent扫描、静态目标球心、单调HitEventId去重和Reliable-only视觉事件问题均由代码确认。

[COMPUTED][HIGH] `git diff --check`、Development Editor和当前完整`CrowdDemo.SF` 46/46通过；自动化日志没有Fatal、Assertion、Ensure、`LogWindows: Error`或VIOLATION。

[COMPUTED][HIGH] T3 8452首次真实单轮暴露生产合同缺失；该停止项现已修复。8455使用稳定10+10 cohort、两套相反Shared Flow和中心/完成平面达到20/20、throughput difference=0、final deadlock=0，安全、双端hash与rollback均通过；8456录像完成群体级人工审片。

[COMPUTED][HIGH] 这是2026-07-17历史快照：当时Projectile专项尚未开始，且当时尚无对应插件Module或最小宿主。当前代码已完成entity-native Mass权威状态、稳定Spatial Broadphase、移动目标/环境命中和宿主Hit Adapter；只有代表性并发Projectile规模门仍未执行。

[INFERRED][HIGH] 正确下一步是按顺序验收T6A、T6S和T6M；插件生产迁移只能在三个异构场景无已知硬失败后重新开始。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
