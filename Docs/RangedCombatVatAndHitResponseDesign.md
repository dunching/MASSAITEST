# MassAI 远程攻击、VAT 与受击响应设计

[COMPUTED][HIGH] 文档状态：本文件保留T7/T8业务、VAT和静止目标Projectile证据，以及entity-native Projectile迁移前的历史审计。Behavior Source、通用插件和当前阶段状态以`EntityBehaviorSourceArchitecture.md`与`PhasePlan.md`为准。

[COMPUTED][HIGH] 当前覆盖说明：下文“PreparedProjectiles/32槽镜像/全Agent扫描仍存在”是迁移前历史断言。当前T8已改为Mass Fragment唯一权威、动态容量、网格Broadphase、相对/环境Sweep和通用Impact/Hit宿主提交；专项13/13通过。

## 1. 文档职责与当前状态

[INFERRED][HIGH] 本文件是远程实体投射物、多业务状态、VAT 表现、击退、击飞和命中改色的当前设计事实源；`CurrentArchitecture.md` 只描述已经接入的生产链，`PhasePlan.md` 记录实施顺序，`TestScenarioMatrix.md` 定义独立验收场景。

[COMPUTED][HIGH] 截至 2026-07-16，T7 的测试 HitFact、受击运动、Death VisualState、真实 VAT 资产、同步 ServerTime ISM 播放、双端技术门和近景人工审片均已通过；T8 的静止目标选择、windup、轻量 Mass projectile、fixed-step swept hit、gameplay damage、客户端 Spawn/Impact/Expire 视觉事件和统一 HitResponse 也已通过独立 Small 验收。

[COMPUTED][HIGH] 当前代码已有 Idle/Move/Attack/HitReact/Death、VisualRevision、StateStartServerTime 与 HitFlash 数据；客户端 representation 使用真实 VAT mesh/runtime material，并向主体与红色overlay提交同一Frame/PreviousFrame。8445近景录像已证明HitFlash与五类状态动作可辨识。

## 2. 外部参考事实与复用边界

| 来源 | 固定版本 | 已确认能力 | 本 Demo 只复用的原则 | 禁止误报 |
|---|---|---|---|---|
| [COMPUTED][HIGH] `E:\Projects\_MassAI_Refs\MassAIExample` | [COMPUTED][HIGH] `53e80bc0ede189928f90acdd1542a4d6ea5ccb20` | [COMPUTED][HIGH] `BulletHellExample` 使用轻量 Mass bullet entity、Signal 初始化/延迟销毁和半径命中；`VertexAnimCharacter` 使用 AnimToTexture data、动画状态索引和 ISM custom data。 | [INFERRED][HIGH] 复用小型 projectile archetype、批量 processor、signal 生命周期和 VisualState→VAT clip 的分层思想。 | [COMPUTED][HIGH] BulletHell 当前点半径查询不是 swept collision；VertexAnim 源码只明确证明 Idle/Run 自动选择，未证明 Attack/Hit/Death VAT 的完整 ISM 业务接入。 |
| [COMPUTED][HIGH] `E:\Projects\_MassAI_Refs\MassSample` | [COMPUTED][HIGH] `ca9825861f35ab4f8e152351de2adb893b51ca70` | [COMPUTED][HIGH] `ProjectileSim` 从 previous 到 current 做 line trace，生成 hit fact，并将命中处理和 Niagara 表现拆开。 | [INFERRED][HIGH] 复用 swept collision、Simulation→HitFact→Resolve 和客户端批量视觉的边界。 | [COMPUTED][HIGH] 该工程没有射手目标选择、windup、cooldown 或攻击状态，并且工程版本为 UE 5.8，不能把源码直接复制到当前 UE 5.7 工程。 |
| [COMPUTED][HIGH] `E:\Projects\_MassAI_Refs\ue-mass-extension-plugin` | [COMPUTED][HIGH] `a2b8592ad8fb307b98b9c30f7d801d05a2009d5f` | [COMPUTED][HIGH] 提供 Mass 扩展、碰撞和复制参考，但未找到完整远程投射物业务链。 | [INFERRED][MED] 仅在后续确有接口缺口时参考通用扩展方式。 | [INFERRED][HIGH] 不得把通用插件能力写成已经提供本 Demo 的射击、VAT 或受击系统。 |
| [COMPUTED][HIGH] `E:\Projects\SuperInvincibleTank_BugFix` | [COMPUTED][HIGH] `23204fe514f0e96496a4e9d61221d787bda8aa94` | [COMPUTED][HIGH] 已有业务 Attack 状态、windup、锁定目标位置、攻击视觉 revision、Mass projectile、命中效果和销毁链。 | [INFERRED][HIGH] 作为远程虫子业务语义的主要依据，提取状态、前摇、单次发射和视觉事件合同。 | [INFERRED][HIGH] 不整体复制旧工程的历史业务、复制或视觉兼容路径。 |

[COMPUTED][HIGH] 当前 `Docs`、原工程 `Docs` 和当前 Git 历史中未找到上述 `_MassAI_Refs` 仓库的正式记录；本节补齐该缺失，不代表这些外部实现已经进入当前生产代码。

[INFERRED][HIGH] 外部参考没有任何一个项目单独提供“远程虫子业务状态 + VAT + 投射物 + 击退/击飞 + 命中改色”的完整实现；当前设计必须组合参考原则并重新满足本 Demo 的 fixed-step、hash、rollback 和 client-visual-only 边界。

## 3. 四类状态必须分离

[INFERRED][HIGH] 业务状态、攻击阶段、受击运动和视觉表现是四种不同事实，禁止用一个枚举或 `VatClipIndex` 同时表达。

```text
BusinessState
├── Idle
├── Move
├── AcquireTarget
├── Attack
├── Recover
└── Dead

AttackPhase
├── None
├── Windup
├── Fired
└── Recovery

ReactiveMotionMode
├── None
├── Knockback
├── KnockUp
└── LandingRecovery

VisualState
├── Idle
├── Move
├── Attack
├── HitReact
└── Death
```

[INFERRED][HIGH] `BusinessState` 决定实体当前业务意图；`AttackPhase` 保证一次 windup 只发射一次；`ReactiveMotionMode` 暂时覆盖普通 locomotion；`VisualState` 只负责客户端应播放的 clip。

[INFERRED][HIGH] 视觉状态由唯一 resolver 根据业务、攻击、受击和死亡事实生成；优先级固定为 `Death > HitReact > Attack > Move > Idle`。命中闪色是可以覆盖在任意非死亡 clip 上的材质反馈，不应通过切换 `VisualState` 丢失当前 Attack clip。

[INFERRED][HIGH] StateTree 若后续接入，只负责低频高层业务转换和 signal 唤醒；Shared Flow、Target Region Transport、Particle、投射物轨迹、碰撞和受击运动仍由批量 Mass processors 与纯 C++ kernels 执行。

## 4. VAT 表现合同

[INFERRED][HIGH] 每个实体 profile 共享一个 VAT descriptor，稳定映射 `VisualState → ClipIndex/Loop/Duration/PlayRate/FireNormalizedTime`；实体只保存 VisualState、VisualRevision、StateStartServerTime、PlayRate 和稳定 PhaseSeed。

[INFERRED][HIGH] Server 权威提交离散状态、revision 和开始时间；Client 根据同步 server time 推导 clip phase，不每 fixed step 复制动画 phase。Correction rollback 必须恢复 revision 和开始时间，避免攻击 clip 重启或重复触发发射。

[COMPUTED][HIGH] 用户已取消原工程资源迁移。当前资源策略改为只参考原工程的生产需求，在 Blender 与当前 UE5.7 工程内重新生成静态网格、骨骼网格、五个独立 AnimSequence 和 Bone VAT；禁止读取或复制原工程资源二进制。

[INFERRED][HIGH] 资源生产的权威入口与帧合同见 `VatAssetProductionPipeline.md`。运行时代码不得继续使用原工程帧号；必须消费当前 DataAsset 的五段实际范围。

[INFERRED][HIGH] VAT 材质 custom data 至少需要稳定分配 clip/playback 数据与 `HitFlashIntensity`；命中颜色和持续时间由共享 profile 定义。Client visual processor只消费权威/预测视觉事实并提交 ISM，不计算攻击、伤害或受击运动。

## 5. 远程攻击与投射物合同

[INFERRED][HIGH] Target Region Transport 的 Melee/MidRange/Ranged 距离带只决定实体可以在哪个终端区域自然落位；是否有目标、是否进入 Attack、是否完成 windup 和是否发射由独立业务链决定。

[INFERRED][HIGH] 第一版远程攻击 fixed-step 顺序固定为：

```text
TargetAcquire/Validate
→ AttackRangeAndFacingCheck
→ AttackPhaseAdvance
→ ProjectileSpawnRequestBuild
→ ProjectileBatchSpawn
→ ProjectileMovementPredict
→ ProjectileSweptCollision
→ ProjectileHitFactBuild
→ HitResponseResolve
→ ProjectileFinalize/Destroy
→ FacingFinalize（同一次原子写回提交Movement与Combat/Visual）
→ Authority/VisualEventCommit（无Mass查询，仅消费最终记录/事件）
```

[INFERRED][HIGH] windup 开始时锁定 `TargetAgentId + TargetLifecycleSerial` 和目标位置；发射请求按 `(FixedStepIndex, SourceAgentId, FireSequence)` 稳定排序，同一 windup 只能生成一个请求。

[INFERRED][HIGH] 最终插件中的每颗gameplay projectile必须是权威轻量Mass entity，至少保存ProjectileId、Source/Lifecycle、FireSequence、Previous/CurrentPosition、Velocity、Radius、Age/Lifetime和EffectProfileKey。当前跨Boundary持久权威已是Mass Fragment；Boundary临时数组和模块化现状见`MassProjectileHitFrameworkDesign.md`。

[INFERRED][HIGH] 第一版直射弹使用 fixed-step previous→proposed swept sphere/segment collision；命中候选按最早量化 hit time，再按 TargetAgentId 决胜。不得退化为仅查询当前点附近实体，也不得一发投射物对半径内全部实体重复造成直接命中伤害。

[INFERRED][HIGH] 服务端权威决定 projectile hit、damage 和受击事件；客户端根据紧凑 Spawn/Impact/Expire 事件重建 ISM 或 Niagara 视觉，不为每颗投射物创建持续复制 Actor，也不在客户端决定 gameplay damage。

[INFERRED][HIGH] 若后续武器明确为迫击炮，才新增“锁定落点 + 固定飞行时间 + 解析抛物线”profile；直射 swept projectile 与落点式 mortar 必须是两个显式 profile，不能在同一隐式分支中混用碰撞语义。

## 6. 统一命中事实

[INFERRED][HIGH] 所有近战、远程或测试注入命中都先生成统一、稳定排序的 `HitFact`，再由唯一 `HitResponseResolve` 写入业务和受击状态。

```text
HitEventId
SourceAgentId / SourceLifecycleSerial
TargetAgentId / TargetLifecycleSerial
FixedStepIndex
QuantizedHitPosition
QuantizedHitDirection
Damage
HorizontalImpulse
VerticalImpulse
HitFlashProfileKey
```

[INFERRED][HIGH] `HitEventId` 必须在 rollback/replay 后保持稳定；目标 LifecycleSerial 不匹配、目标已死亡或事件已消费时不得再次应用。一次事件只允许产生一次伤害、一次 impulse 和一次 hit-flash revision。

## 7. 击退、击飞与局部安全

[INFERRED][HIGH] 击退是水平受击速度/位移覆盖；击飞是在相同水平受击响应上增加量化垂直速度、重力、落地和 LandingRecovery。二者不能由 visual processor 直接修改 Transform。

[INFERRED][HIGH] 受击响应使用 fixed-step timer 和整数/量化事实，不依赖 render-frame DeltaSeconds；同一 boundary 的多个 HitFact 按稳定 EventId 顺序合成，并受 profile 的最大水平/垂直 impulse 限制。

[INFERRED][HIGH] 水平 reactive motion 必须进入 `MovementPredict → ParticleConstraintSolve`，继续满足 Hard/Swept/Obstacle/Bounds；不得在 Particle 之后直接位移穿过实体或墙体。普通 Flow/Transport guidance 在受击覆盖期间暂停或按明确权重恢复，不能同时由两个 writer 积分。

[INFERRED][HIGH] 第一版击飞采用保守 2.5D 合同：Z 用确定性 ballistic 状态产生可见离地和落地，XY 物理 footprint 仍参加统一 Particle/Environment 约束，因此本阶段不宣称实体可以从其他虫子上方穿越。真正 3D 空中穿越需要另立碰撞能力阶段。

[INFERRED][HIGH] 落地只在 fixed-step boundary 发生；落地后进入有限 LandingRecovery，再恢复原业务目标。Correction rollback 必须恢复 ReactiveMotionMode、水平/垂直速度、累计时间、落地 revision 和原业务恢复事实。

[INFERRED][HIGH] 若 Particle 无法安全实现请求 impulse，应报告 requested/realized impulse 和被约束原因；安全裁剪不是命中丢失，但不能把未实现的请求位移统计为已完成击退距离。

## 8. 命中改色合同

[INFERRED][HIGH] 命中改色使用独立 `HitFlashState`：FlashRevision、StartServerTime、Duration、Color/ProfileKey 和 PeakIntensity；它是材质叠加事实，不改变 BusinessState、AttackPhase 或 VisualState。

[INFERRED][HIGH] Client 按同步时间计算闪色强度曲线并写 ISM custom data；Server 只复制离散 revision、开始时间和 profile，不逐帧复制颜色。连续命中按稳定规则刷新 revision 和开始时间，不能依赖客户端本地碰撞触发。

[INFERRED][HIGH] 死亡状态可以保留最后一次 hit flash，也可以由 profile 明确禁止；首版固定 `Death` clip 优先、hit tint 允许在短时内叠加，以便录像能同时确认命中和死亡事实。

## 9. Processor 与写入边界

[INFERRED][HIGH] 完整业务接入后仍保持职责分离：

```text
群体运动事实：Shared Flow / Target Region Transport
普通与受击预测：MovementIntentCompose / ReactiveMotionPredict
局部安全：ParticleConstraintSolve
攻击业务：Target/AttackPhase/FireRequest processors
投射物业务：Spawn/Move/SweptHit/Resolve processors
状态提交：唯一 Business/Reactive boundary writer
视觉解析：唯一 VisualState/HitFlash resolver
客户端表现：VAT ISM + projectile ISM/Niagara submit
```

[INFERRED][HIGH] Coordinator 只复制计划、状态/event stream、checkpoint 和紧凑指标；不得承载目标选择、投射物碰撞、击退、击飞或 VAT clip 算法。

## 10. 自动化与验收场景

[INFERRED][HIGH] 先建立两个独立 20 实体 Small 场景，不把所有新能力塞进一个综合关卡。

### T7 Multi-State VAT and Hit Response Small

[INFERRED][HIGH] T7 使用真实虫子 VAT mesh/material，稳定分组执行 Idle、Move、Attack、HitReact、Death；通过直接、确定性的测试 HitFact 分别验证水平击退、垂直击飞、落地恢复和命中闪色，不生成投射物。

[INFERRED][HIGH] T7 自动化门包括：状态转换表、唯一 writer、VisualRevision、clip mapping、fixed-step phase、HitEvent 去重、多个事件稳定合成、Particle 安全裁剪、ballistic 落地、rollback/replay、输入反序和双端 hash。

[INFERRED][HIGH] T7 真实关卡门包括：全部20实体可见；每种VisualState至少有稳定样本；knockback requested/realized可核对；knock-up apex与landing只发生一次；Hard/Swept/Obstacle/Bounds=0；hit flash revision与可见改色次数一致；无Cube占位、隐藏实例、视觉owner错误或correction后动画重启。

### T8 Ranged Projectile Combat Small

[INFERRED][HIGH] T8 首轮使用10个射手与10个静止目标，并从合法、无重叠、已满足射程的稳定位置直接开始；本轮不启用 Target Region Transport。它只验证 AcquireTarget→Windup→Fire→Recovery 与投射物命中，命中后复用 T7 的统一 HitFact/HitResponse。

[INFERRED][HIGH] T8 自动化门包括：windup锁定、目标Lifecycle失效、单次发射、高速swept命中、最早hit稳定决胜、过期销毁、命中一次、事件hash和rollback不重复伤害。

[INFERRED][HIGH] T8 真实关卡门包括：`completed_windup_count == projectile_spawned_count`；`spawned = active + impacted + expired`；duplicate fire/hit/damage=0；客户端 projectile visual 数量与事件流一致；Attack VAT、发射时刻、impact、hit flash 和受击响应在录像中可辨识。

[COMPUTED][HIGH] 8451历史版本的T8生产链使用32实体Mass镜像pool、Pipeline数组权威和静止目标；该句只限定8451证据边界。当前R5实现状态以本文件顶部覆盖说明和`MassProjectileHitFrameworkDesign.md`为准。

[COMPUTED][HIGH] 最终构建 8451 单轮得到 acquired/windup/spawned/impacted/damage=`50/50/50/50/50`，active/expired/duplicate fire/duplicate hit/invalid projectile=`0/0/0/0/0`；10 个目标死亡后各产生一次 lifecycle invalidation，`invalid_target_lifecycle=10`。server/client 的 attack/projectile/event hash 分别一致为 `2730049702/4215166500/4204062592`。

[COMPUTED][HIGH] 8451 同时满足 20 agents/20 visible、Particle Hard/Swept/Obstacle/Bounds=`0/0/0/0`、invalid/fallback=`0/0`、rollback=`54/0/0`、replayed steps=`55`、checkpoint 与 correction interval position error p95=`0cm`，且未出现 Fatal、Assertion、Ensure、`LogWindows: Error` 或 VIOLATION。

[COMPUTED][HIGH] 8450 的 20 秒 1280×720 录像与密集 contact sheet 显示两排各10个实体、两排之间的10发齐射、Attack VAT、红色 impact/HitFlash 和目标 Death；未观察到隐藏实例、错误 visual owner 或明显 fixed-step 跳变。俯视镜头下 projectile 尺寸偏小，但可直接辨识。

### T9 Mixed Combat Integration Small

[INFERRED][HIGH] T9 只在 T7、T8 分别通过后设计，用于混合 Melee/MidRange/Ranged、移动目标、死亡与群体重新运输；它不是首版实现门，也不得替代 T7/T8 的归因性验收。

## 11. 明确停止与禁止外推

[INFERRED][HIGH] 正确实施顺序为：外部参考与资产审计 → VAT/状态纯合同 → T7 → 投射物纯合同 → T8 → T9。T7 通过不能证明投射物通过，T8 Small 通过不能外推为100/500战斗、复杂伤害或真实WORK预算成立。

[INFERRED][HIGH] 本阶段不实现行为树/EQS全套业务、范围伤害、穿透弹、追踪弹、反弹弹、空中穿越、ragdoll、永久尸体或攻击slot；这些能力不能借远程投射物首版暗中加入。

## 12. 资产审计与当前实施停点（2026-07-16）

[COMPUTED][HIGH] UE5.7 只读审计确认 FlyingAphid 与 MudMortarSnail 的 AnimToTexture DataAsset 均为 Bone VAT、30fps、单 AnimSequence，`animations=0–124`、`num_frames=125`；对应 AnimSequence 长度为 `4.133333s`。

[COMPUTED][HIGH] 原工程 ItemDefine 的真实 playback 配置为 Spawn `0–24`、Idle `25–49`、Move `50–74`、Attack `75–99`、HitReact `100–124`、Death `125–149`。Death 区间超出当前烘焙数据，不能通过映射 clip index 或钳制到末帧伪造 Death VAT。

[COMPUTED][HIGH] 原工程审计只保留为失败需求证据，不再作为资产来源。新的 `BuildCrowdDemoVatSource.py → BuildCrowdDemoVatAssets.py` 已从空源生成并烘焙独立五状态资源：Idle `0–24`、Move `25–49`、Attack `50–74`、HitReact `75–99`、Death `100–124`，总帧数125，Death合法。

[COMPUTED][HIGH] `ValidateCrowdDemoVatAssets.py` 已验证1个骨骼网格、1个静态网格、5个AnimSequence、3张Bone VAT纹理、1个DataAsset、7个材质实例、UV通道数2和完整帧范围。资产生产门、T7 package、8447双端技术运行和8445近景录像均通过；Attack、HitReact、击飞落地与Death细动作已获得可靠人工结论。

[COMPUTED][HIGH] T7 与 T8 独立 Small 均已通过。T8 固定为10射手+10静止目标合法起始位置，不启用 Target Region Transport；该结果不证明移动目标、T9混合战斗、100/500投射物规模或真实远程群体运输已经成立。

## 13. 大量远程敌人与模块化修正（2026-07-17）

[COMPUTED][HIGH] T8文档此前把“32个Mass projectile pool实体”描述得过于接近entity-native simulation；代码复核确认权威轨迹和命中仍由`PipelineSubsystem::PreparedProjectiles`数组计算，Mass pool只经`MirrorProjectileStates()`接收Active状态镜像。

[COMPUTED][HIGH] 当前T8对每颗projectile全量遍历存活Agent，未接入Agent空间Broadphase、环境障碍命中和移动目标相对sweep。8451/8450仍是有效的20实体静止目标基线，但不能作为大量远程敌人最终架构证据。

[INFERRED][HIGH] 最终修正为真正的Mass Projectile Entity、稳定spatial grid、相对swept narrowphase、通用ImpactFact/HitFact和宿主Combat Adapter；原工程现有`FCombatDamageSpec`、状态、击退/击飞、死亡、掉落和表现通过Adapter复用，不进入插件纯kernel。

[INFERRED][HIGH] 详细POD合同、WORK/GT边界、插件模块、迁移顺序和规模门以`MassProjectileHitFrameworkDesign.md`为准；本文件继续作为T7/T8现有业务与视觉证据事实源。

[COMPUTED][HIGH] 这是2026-07-17历史快照：当时尚未创建通用插件或切换T8。后续T8已迁移为Mass持久权威、动态容量、网格Broadphase和相对/环境Sweep；PJ0–PJ6公共模块化现亦已关闭。

## 14. 宿主Combat boundary transaction（2026-07-22）

[COMPUTED][HIGH] RangedCombat、HitResponse与ReactiveMotion已从三个独立processor收敛为唯一Demo宿主Combat事务。事务先一次采集完整、稳定排序的Agent业务事实，再严格执行Attack/Projectile→Hit resolve→Reactive advance，最后一次性写回Stats/Business/Attack/Reactive/HitFlash/Visual和ReactiveStep；任一完整性校验失败时不允许部分提交。

[COMPUTED][HIGH] 历史切片事实：原跨processor `PendingProjectileHitFacts`桥与三个旧processor实现当时已删除，T7/T8进入统一Resolve路径；但该切片时Projectile轨迹权威仍是Pipeline数组、Mass pool仍只是表现镜像。该限制随后由R5与PJ1–PJ6消除，不描述当前生产代码。

[COMPUTED][HIGH] 8755 T7技术回归为20 agents/20 visible、fixed-step p95=`2.452ms`；8756 T8得到spawn/impact/damage=`50/50/50`、duplicate fire/hit=`0/0`，server/client attack/projectile/event hash一致为`2730049702/4215166500/4204062592`，fixed-step p95=`2.247ms`。本切片没有重录T7/T8人工视频，也不外推移动目标、100/500或entity-native projectile能力。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
