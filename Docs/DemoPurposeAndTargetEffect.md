# MassAI Crowd Demo 目的与目标效果

## 1. 文档职责

[INFERRED][HIGH] 本文件是 Demo 目的、最终目标效果与长期架构原则的稳定事实源；`CurrentArchitecture.md` 描述当前实现，`PhasePlan.md` 描述当前任务，`FeatureChecklist.md` 描述验收状态。

## 2. Demo 来源与目的

[KNOWN][HIGH] 本 Demo 基于 `E:\Projects\SuperInvincibleTank_BugFix` 中已有 MassAI 实验、代码和设计文档形成独立验证工程。

[INFERRED][HIGH] 独立工程用于隔离群体运动、复制、视觉与业务变量，建立小而可信、可复现、可逐阶段验收的技术验证器；它不是对原工程的整体替代。

[INFERRED][HIGH] Demo 总目的，是使用 Unreal Mass 验证适用于大规模虫群的“群体驱动 + 个体修正”运动架构。

## 3. 群体驱动 + 个体修正

```text
群体调度层
├── 选择 cohort/群体目标
├── 生成 Shared Flow 与 Target-relative Navigation Cells
├── 聚合 Region 人口、容量、缺口与供给
├── 计算共享 Cell Edge transport quota
└── 输出稳定、可批量消费的宏观 guidance

个体实体层
├── 保存 AgentId、位置、速度、尺寸和业务状态
├── 按 AgentId 消费群体 quota/guidance
├── 执行 Particle Soft/Hard/Environment 局部修正
├── 保留受击、攻击、死亡等个体事件边界
└── 提交最终 Transform/Velocity
```

[INFERRED][HIGH] 每个虫子仍是完整业务实体，但不应独立重复执行完整目标生成、Nav/EQS 查询或整体路线规划。

[INFERRED][HIGH] WORK processors 应承担大规模、可批处理、稳定排序的事实生成；GT processors 只承担必须依赖世界对象、复制或视觉提交的边界工作。

## 4. 目标效果

[INFERRED][HIGH] Demo 最终应证明：自由游荡和静态/动态目标追逐都由共享宏观场驱动；虫群能绕障、过通道、围绕目标自然分布，并通过局部粒子约束维持硬安全和可压缩软间距。

[INFERRED][HIGH] Target 周围不使用永久 Slot 或 per-agent Region owner；宏观层只维护可行 Polar Navigation Cells、固定 Demand Regions、PlanEpoch 和 Cell Edge quota。

[INFERRED][HIGH] Navigation Cell 是可共享、可先后通过的空间区域，不是单个实体必须命中的站位点；Cell anchor 只能作为方向参考，不能被解释为要求多个实体同时占据的精确坐标。

[INFERRED][HIGH] 合理落位必须同时满足“覆盖与人口分布成立”和“群体在目标参考系内稳定下来”。多个实体可以读取同一 Flow 方向或先后通过同一 Cell，但不得因持续争抢同一 anchor 而形成“靠近→Particle推开→再次靠近”的闭环振荡。

[INFERRED][HIGH] Target Region Transport 负责 Region 人口需求、宏观 Cell Edge quota 与 terminal 供需；通用 Local Predictive Interaction 负责根据邻域轨迹决定当前可执行速度与必要的公平让行；Particle 只负责 Soft 压力与 Hard/Swept/Environment 最终安全。不得用加强 Particle 推力或放宽 HardDistance 掩盖上层持续发出不可执行 guidance 的问题。

[INFERRED][HIGH] 局部预测层不识别“窄口测试”“同目标测试”或具体地图。Navigation Cell 不是独占站位，局部并发容量必须由当前实体半径、速度、轨迹和环境共同可行域自然产生；只有共同前进不可行时才使用有限期、可回滚的通用让行顺序。

[INFERRED][HIGH] 不同半径、有效距离和 Mobility 的实体应在同一套通用规则下产生差异，不为具体 Agent、墙边实体或特定地图添加特殊生产分支。

[INFERRED][HIGH] 最终群体必须支持SmallLight、Standard、LargeHeavy等不同PhysicalRadius/Mobility实体与Melee、MidRange、Ranged等不同Target中心距离带联合运行；尺寸/质量决定局部Particle响应，攻击距离决定群体Transport的终端区域，两者不得被硬编码成同一职业身份。

[INFERRED][HIGH] 远程实体应沿共享Flow到达目标外围并在外层可行Regions自然展开；近战实体继续跨环进入内层；大型/重型实体通过更大HardDistance和更低Mobility自然影响局部空间，而不是获得另一套碰撞优先级。

[INFERRED][HIGH] Server/Client 必须使用相同 processors 与纯 kernels；correction 只在 fixed-step boundary 应用；客户端完整显示全部实体，不通过隐藏实例或视觉偏移伪造效果。

## 5. 典型场景事实源

[INFERRED][HIGH] T1开放生成、T2开放群体移动、T3双向交换、T4有效通道、T5静态/移动Target和T6异构共享区域必须各有独立真实尝试关卡；场景package、输入矩阵和三级验收以`TestScenarioMatrix.md`为准。

[INFERRED][HIGH] 典型场景的终态合同必须匹配测试目的：T2在开放移动后使用Target Region Transport极坐标运输和Distance Band自然落位；T5/T6S/T6M使用目标相对Region站位；T3使用两侧宽交换区，T4/T6A使用通道出口平面。除非场景专门验证点目标，不得把“全体进入140cm圆”作为通用群体完成门。

[INFERRED][HIGH] 每个场景必须依次通过纯kernel、20实体真实关卡、稳定窗口验收、FFmpeg和人工审片，再升级100/500；不能用一个综合地图替代所有归因场景。

[INFERRED][HIGH] 目标类场景不能只用单帧`inside-band`、Region coverage或某一帧`TerminalSettle`数量判定完成。Static使用世界位置，Moving使用Target-relative位置/速度；两者都必须证明连续窗口内没有持续merge阻塞、终态状态抖动、位置往返和高频Particle反向修正。

## 6. 当前实现与目标差距

[COMPUTED][HIGH] 当前已实现 Shared Flow V2、Target Region Transport、SoftPressure Particle、双端 hash、correction rollback 和 client-only visual。

[COMPUTED][HIGH] 8417 Static Small 20 已证明 20/20 进入有效距离带并覆盖 16/16 可行 Demand Regions；8418 Moving Small 已证明 20/20 进入有效距离带、覆盖当轮 12/12 可行 Demand Regions，且 Plan/Guidance unrouted、validation failure 与 Particle 安全违规均为 0。这些运行没有记录本文新增的连续稳定窗口、merge等待与目标相对抖动指标，因此只保留为旧口径到达/安全证据。

[COMPUTED][HIGH] T1/T2/T3/T4/T6 对应的真实 Small package 与异构 Particle/CapabilityProfile 基础已建立；T1已完成开放压力传播验收，T2已在8426完成Shared Flow→Target Region Transport→Distance Band旧口径单轮验收，T3已在8455完成双cohort相反Shared Flow→中心交换→对侧完成平面的20/20验收，T4已在8460/8461完成有效通道20/20技术、能力与人工审片。T6A 8464通过，T6S 8479仅保留旧口径到达/安全证据，T6M 8492仍为19/20失败。

[COMPUTED][HIGH] Shared Guidance 与 Particle 之间的通用 Local Predictive Interaction 已经接入，并以同一规则通过T3双向交换、T4通道和T5 Static稳定性V1能力门。8521 T5 Static达到inside-band=`20/20`、Region coverage=`16/16`且最终速度/位置抖动为0；当前下一差距是人工审片与Moving Target复验，不是直接进入100/500。

## 2026-07-15 与长期目标的当前差距

[COMPUTED][HIGH] Demo 已具备逐实体异构 Particle 事实与按 capability cohort 共享 Transport 的代码边界；这符合“群体驱动 + 个体修正”的职责分离，但尚未由 T6 真实运行证明最终视觉和能力效果。

[COMPUTED][HIGH] T1 已证明 20 个稳定 Mass 实体在不 spawn/despawn 的前提下，通过测试专用参与状态完成 `0→5→10→15→19→20→19`；真实 Soft correction 图的稳定 BFS 达到第 3 层，插入与移除后两个 settling 窗口均成立，且移除后 12 个保留实体相对 pre-insert 布局仍有超过 1cm 的量化位移。

[INFERRED][HIGH] T1 的“移除”只表示实体退出 Particle active 集合并回到可见 staging 状态，不是业务 despawn、死亡或 Mass 实体生命周期销毁；该边界不能被写成已实现真实生成/销毁系统。

[COMPUTED][HIGH] T1、T3与T4已通过各自现行20实体能力合同；T2的8426结论现降级为旧口径到达/安全证据并等待稳定性V1复验。T6仍未通过，因此当前状态不能描述为T1–T6已完成。

[COMPUTED][HIGH] 8426 T2证明20实体能够从开放Shared Flow接近切换到目标相对Transport与Distance Band，并在覆盖16/16可行Region、当帧`TerminalSettle=20/20`时保持Particle硬安全、双端确定性、correction replay和20/20完整显示；当前`terminal_settled_count`没有连续低速或位置稳定窗口语义，因此8426不能单独证明最终无抖动自然落位。

## 2026-07-17 合理站位与稳定落位合同

[INFERRED][HIGH] 最终目标效果不是“每个实体一个永久格子”，也不是“所有实体到同一个点”，而是共享宏观流下的区域分散、容量受控通过和Target-relative稳定终态。

```text
Shared Flow / Transport决定去哪个可行区域
→ Local Predictive Interaction根据邻域短期轨迹求解可执行速度
→ 几何允许时多人并行；不可同时前进时确定性、有限期让行
→ 进入terminal band后以迟滞保持完成状态
→ Particle只修正剩余局部Soft/Hard安全
```

[INFERRED][HIGH] 上述让行不是 Flow Cell“进入锁”：它不创建永久 Cell owner，不把每个 Cell 容量固定为1，也不要求算法知道当前是窄口。完整定义以`LocalPredictiveInteractionDesign.md`为准。

[INFERRED][HIGH] “稳定”必须由连续fixed-step窗口证明：有效带和Region覆盖持续成立、相对目标速度收敛、位置峰峰抖动受限、Terminal/Transport无无因往返、Particle修正趋于稳定。单帧截图、最终一帧计数或Hard violation为0均不足以单独证明稳定落位。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 2026-07-17 T6M 修复后的目标效果边界

[COMPUTED][HIGH] Demo现已证明连续Round可以从相同稳定输入重新开始，动态目标Shared Flow可以在固定世界障碍Topology上按量化目标anchor重建Integration，并保持server/client、rollback与Particle安全合同一致。

[COMPUTED][HIGH] Demo尚未证明20个异构实体都能围绕移动目标完成各自距离带落位：8487两轮均只有`17/20`进入有效距离带。因此“群体驱动 + 个体修正”的移动异构目标效果仍是不完整能力，不能由技术hash通过替代。

[INFERRED][HIGH] 当前应先关闭剩余三实体的终态能力缺口，再讨论GT/WORK并行边界和旧实验路径删除；否则清理会失去可复现的失败参照，并把未通过能力误写成最终架构。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 2026-07-17 T6 异构目标效果当前证据

[COMPUTED][HIGH] T6A 8464 与 T6S 8479 已证明20实体异构尺寸、Mobility及三档 Target distance band 在通道与静态目标条件下可以保持统一 Particle 安全、双端确定性、correction rollback 和完整显示。

[COMPUTED][HIGH] T6M 8481 未证明移动目标异构终态：虽然技术门通过，但仅10/20实体进入自己的有效距离带，LargeMelee 与 StandardMelee 均为0/3。

[INFERRED][HIGH] 该失败说明“群体驱动”中的共享导航事实必须与动态群体目标处于同一空间参考系；只让个体速度跟随目标平移、但让共享势场继续指向旧世界坐标，不构成完整的移动群体调度。

[COMPUTED][HIGH] 因此当前最终效果仍不能描述为 T1–T8 全部无硬失败，Mass Projectile 插件迁移前置门继续保持关闭。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 2026-07-15 个体业务、VAT 与受击效果目标补充

[INFERRED][HIGH] “每个虫子是完整业务实体”不仅表示保存身份和位置，还要求最终能表达 AcquireTarget、Attack、Recover、Death 等业务状态，以及 HitReact、Knockback、KnockUp 等受击响应事实；但这些个体事件不得反向承担群体目标生成、共享路线规划或区域人口调度。

[INFERRED][HIGH] Melee/MidRange/Ranged 的 Target 距离带只定义群体站位前提，不等于攻击已发生。真正远程攻击必须另有目标有效性、windup、单次发射、projectile、hit fact 和 recovery 合同。

[INFERRED][HIGH] 最终视觉目标不是“一个StaticMesh换颜色”：真实虫子 VAT 必须由业务/受击事实驱动 Idle、Move、Attack、HitReact 和 Death clip；命中改色是可叠加材质反馈，不能替代动画状态或由客户端本地碰撞自行触发。

[INFERRED][HIGH] 击退和击飞属于确定性受击运动。水平位移必须继续经过统一 Particle/Environment 安全链；首版击飞以保守2.5D ballistic Z表现离地和落地，XY footprint继续参与Hard/Swept安全，不外推为空中穿越能力。

[COMPUTED][HIGH] T7 已接入五状态业务/视觉事实、确定性测试 HitFact、击退/击飞/落地、真实 VAT ISM 与同步 ServerTime 播放，并已通过自动化、单轮双端技术门和近景人工审片。

[COMPUTED][HIGH] 命中闪色事实、HitFlashIntensity和红色同帧VAT overlay已接入；T7近景录像已证明完整受击动作可辨识。T8进一步实现并验证了10射手+10静止目标的目标选择、windup、Mass projectile、swept hit、damage、客户端事件视觉与统一HitResponse。

[INFERRED][HIGH] T8 Small通过只证明静止目标远程投射物链成立，不能把Ranged距离带本身误报为攻击能力，也不能外推为移动目标、远程Transport handoff、T9混合战斗或100/500规模已经成立。当前边界以`RangedCombatVatAndHitResponseDesign.md`为准。

## 2026-07-16 类游戏业务Sandbox与插件化迁移目标

[INFERRED][HIGH] Demo长期目标增加一个独立的类游戏虫群Sandbox：玩家控制Pawn移动，地图持续生成受上限约束的敌群，群体按玩家和世界事实共享目标与导航结果，玩家通过fixed-step命令释放线形或圆形伤害，并由统一HitFact驱动伤害、HitFlash、击退、击飞、落地、死亡和群体membership变化。

[INFERRED][HIGH] 该Sandbox必须补充基于静态烘焙NavMesh的稳定分层Surface Graph与共享Flow，验证坡道、桥上桥下、高台、多路线和不可通行落差；当前XY Shared Flow/Particle结果不能外推为高低差能力已经成立。

[INFERRED][HIGH] 新目标具有强制前置顺序：先使T1-T8独立Small测试大致全部通过并关闭已知硬失败，再提取不依赖Demo地图、Round脚本和固定资产路径的插件，完成独立宿主与Demo双重回归，最后才实施T9/T10综合业务场景。

[INFERRED][HIGH] 插件化用于降低未来接入`E:\Projects\SuperInvincibleTank_BugFix`的迁移风险；未来回原工程采用公开接口与Adapter，不默认复制整个Demo Source、Coordinator、地图或历史诊断代码。详细事实源为`GameplaySwarmSandboxAndPluginMigrationPlan.md`。

[COMPUTED][HIGH] 2026-07-17 的Mass Projectile插件前置核对最初因T3生产合同缺失而停止；T3已由8455/8456完成独立验收，T4已由8460/8461关闭，但T6仍未关闭，因此仍没有创建插件、切换T8或删除旧Projectile路径。

## 2026-07-17 大量远程敌人与通用命中目标

[INFERRED][HIGH] Demo长期目标明确包含大量远程类敌人和大量同时在场Projectile；最终实现必须使用真正的Mass Projectile Entity、稳定空间Broadphase与fixed-step相对sweep，不能长期维持Pipeline数组权威和`Projectile×Agent`全量扫描。

[INFERRED][HIGH] “命中”和“被命中”必须通过通用、无Actor依赖的HitFact连接：Projectile、线形、圆形和近战只生产事实；原工程Adapter负责伤害、防御、状态、击退/击飞、死亡、掉落和视觉结果。

[INFERRED][HIGH] 插件化不是复制当前Demo代码，而是先纠正Projectile权威状态和接口边界，再在最小宿主、Demo和原工程三处验证。完整事实源为`MassProjectileHitFrameworkDesign.md`。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
