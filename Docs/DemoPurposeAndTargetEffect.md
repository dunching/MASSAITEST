# MassAI Crowd Demo 目的与目标效果

## 1. 文档职责

[COMPUTED][HIGH] Demo的最终产品角色已明确为`MassCrowdSimulation`插件的测试宿主；插件模块边界、公共API与迁移顺序以`MassCrowdSimulationPluginArchitecture.md`为准。Demo保留T1–T8、地图、Round、指标、Lighting和人工审片，不作为可复用插件本体。

[INFERRED][HIGH] 本文件是 Demo 目的、最终目标效果与长期架构原则的稳定事实源；`CurrentArchitecture.md` 描述当前实现，`PhasePlan.md` 描述当前任务，`FeatureChecklist.md` 描述验收状态。

[INFERRED][HIGH] 生产运行、行为组合、持续生命周期与复制的长期合同以`MassCrowdUnifiedRuntimeAndReplicationContract.md`为唯一事实源。Demo 的最终角色是生产插件的验收宿主：使用同一 Runtime、Networking 和 Presentation，只增加 Scenario 输入、固定 Round 窗口、readiness、hash、fixture、故障注入、VIOLATION 与人工审片设施。

[INFERRED][HIGH] Demo 不得维护一套“当前先测、以后再换掉”的运行或复制协议。当前 `RoundBootstrapPacket`、`RoundPlan`、`RoundResult` 和固定 Agent 集合只属于实现现状；它们必须适配未来通用 Relevant Snapshot/Delta 合同，不能成为插件最终产品 API。

[INFERRED][HIGH] 最终产品目标是通用持续 Agent population，不是持续敌群专用系统。敌方追逐、友方搬运和中立游荡只是 Capability、Behavior、Objective 与关系策略的不同配置，所有实体复用同一 Movement、Avoidance、Particle、Replication 与 Presentation 路径。

## 1.1 当前能力与未来目标

[COMPUTED][HIGH] 当前能力包括固定 Agent 集合的 Round 验收、Core/Runtime 通用运动链、Demo correction/checkpoint chunks、Target Region、Combat HitFact 与 Demo ISM/VAT 表现。

[COMPUTED][HIGH] 当前已具备生产Snapshot、bounded lifecycle batches、持续spawn/despawn、死亡移除、Lifecycle槽位复用、membership增量、统一Behavior、owner-only late join、空间RelevantSet、公共Presentation、NavFlowProductSmall、FriendlyLogisticsSmall和20/100/500同路径Mixed Sandbox。真实移动视区enter/exit专项仍未单独保存证据。

[INFERRED][HIGH] 未来能力必须以 Faction、Capability、Active Behavior 与 Cohort 分离为前提：Faction 只表达关系/权限/目标过滤；Capability 表达能做什么；Active Behavior 表达当前做什么；Cohort 由共享 Objective、NavigationLayer、MovementProfile、CapabilityProfile 与宏观策略形成，不等同于阵营。

## 2. Demo 来源与目的

[KNOWN][HIGH] 本 Demo 基于 `E:\Projects\SuperInvincibleTank_BugFix` 中已有 MassAI 实验、代码和设计文档形成独立验证工程。

[INFERRED][HIGH] 独立工程用于隔离群体运动、复制、视觉与业务变量，建立小而可信、可复现、可逐阶段验收的技术验证器；它不是对原工程的整体替代。

[INFERRED][HIGH] Demo 总目的，是使用 Unreal Mass 验证适用于大规模虫群的“群体驱动 + 个体修正”运动架构。

## 3. 群体驱动 + 个体修正

```text
群体Guidance层
├── 选择 cohort/群体目标
├── 生成 Shared Flow 等共享导航事实
├── 按行为选择可选的宏观策略
│   ├── Target Region空间分布
│   └── 未来自由游荡等群体意图
└── 输出统一、可批量消费的Desired/Preferred guidance

个体实体层
├── 保存 AgentId、位置、速度、尺寸和业务状态
├── 按 AgentId 消费群体 guidance
├── 执行 Local Predictive短程轨迹避让与公平让行
├── 执行 Particle Soft/Hard/Environment最终安全修正
├── 保留受击、攻击、死亡等个体事件边界
└── 提交最终 Transform/Velocity
```

[INFERRED][HIGH] 每个虫子仍是完整业务实体，但不应独立重复执行完整目标生成、Nav/EQS 查询或整体路线规划。

[INFERRED][HIGH] WORK processors 应承担大规模、可批处理、稳定排序的事实生成；GT processors 只承担必须依赖世界对象、复制或视觉提交的边界工作。

## 4. 目标效果

[INFERRED][HIGH] Demo 最终应证明：自由游荡和静态/动态目标追逐都由共享宏观场驱动；虫群能绕障、过通道、围绕目标自然分布，并通过局部粒子约束维持硬安全和可压缩软间距。

[INFERRED][HIGH] Target Region Transport是“需要围绕目标进行区域分布”时使用的可选宏观策略，不是普通移动、自由游荡或窄口通行的固定必经层。

[INFERRED][HIGH] 启用Target Region策略时，目标周围不使用永久 Slot 或 per-agent Region owner；该策略只维护可行 Polar Navigation Cells、固定 Demand Regions、PlanEpoch 和短期 Cell Edge quota。

[INFERRED][HIGH] Navigation Cell 是可共享、可先后通过的空间区域，不是单个实体必须命中的站位点；Cell anchor 只能作为方向参考，不能被解释为要求多个实体同时占据的精确坐标。

[INFERRED][HIGH] 合理落位必须同时满足“覆盖与人口分布成立”和“群体在目标参考系内稳定下来”。多个实体可以读取同一 Flow 方向或先后通过同一 Cell，但不得因持续争抢同一 anchor 而形成“靠近→Particle推开→再次靠近”的闭环振荡。

[INFERRED][HIGH] 当业务启用目标区域分布时，Target Region Transport负责Region人口需求、宏观Cell Edge quota与terminal供需；普通开放移动和窄口移动不应为了兼容该模块而建立目标Region。所有宏观策略之后都统一由Local Predictive Interaction根据邻域轨迹决定当前可执行速度与必要的公平让行，再由Particle负责Soft压力与Hard/Swept/Environment最终安全。

[INFERRED][HIGH] 局部预测层不识别“窄口测试”“同目标测试”或具体地图。Navigation Cell 不是独占站位，局部并发容量必须由当前实体半径、速度、轨迹和环境共同可行域自然产生；只有共同前进不可行时才使用有限期、可回滚的通用让行顺序。

[INFERRED][HIGH] 不同半径、有效距离和 Mobility 的实体应在同一套通用规则下产生差异，不为具体 Agent、墙边实体或特定地图添加特殊生产分支。

[INFERRED][HIGH] 最终群体必须支持SmallLight、Standard、LargeHeavy等不同PhysicalRadius/Mobility实体与Melee、MidRange、Ranged等不同Target中心距离带联合运行；尺寸/质量决定局部Particle响应，攻击距离决定群体Transport的终端区域，两者不得被硬编码成同一职业身份。

[INFERRED][HIGH] 远程实体应沿共享Flow到达目标外围并在外层可行Regions自然展开；近战实体继续跨环进入内层；大型/重型实体通过更大HardDistance和更低Mobility自然影响局部空间，而不是获得另一套碰撞优先级。

[INFERRED][HIGH] Server/Client 必须使用相同 processors 与纯 kernels；correction 只在 fixed-step boundary 应用；客户端完整显示全部实体，不通过隐藏实例或视觉偏移伪造效果。

## 5. 典型场景事实源

[INFERRED][HIGH] T1测试参与集切换与压力传播、T2开放群体移动、T3双向交换、T4有效通道、T5静态/移动Target和T6异构共享区域必须各有独立真实尝试关卡；T1不验收真实spawn/despawn。场景package、输入矩阵和三级验收以`TestScenarioMatrix.md`为准。

[INFERRED][HIGH] 典型场景的终态合同必须匹配测试目的：T2在开放移动后使用Target Region Transport极坐标运输和Distance Band自然落位；T5/T6S/T6M使用目标相对Region站位；T3使用两侧宽交换区，T4/T6A使用通道出口平面。除非场景专门验证点目标，不得把“全体进入140cm圆”作为通用群体完成门。

[INFERRED][HIGH] 每个场景必须依次通过纯kernel、20实体真实关卡、稳定窗口验收、FFmpeg和人工审片，再升级100/500；不能用一个综合地图替代所有归因场景。

[INFERRED][HIGH] 目标类场景不能只用单帧`inside-band`、Region coverage或某一帧`TerminalSettle`数量判定完成。Static使用世界位置，Moving使用Target-relative位置/速度；两者都必须证明连续窗口内没有持续merge阻塞、终态状态抖动、位置往返和高频Particle反向修正。

## 5.1 可视化、FFmpeg 与人工验收流程（2026-07-29）

[INFERRED][HIGH] 每个专项关卡的当前版验收分为两次独立运行，不能用开启录屏和调试文字后的帧率替代性能基线。第一轮使用独立Server/Client、关闭录屏与状态标签，保存fixed-step、client frame、visual、Game/Render/GPU、realtime、step-limit和启动/稳定窗口分类；第二轮开启可视化标签与FFmpeg，保存连续录像、contact sheet、专项事件sidecar和事件短片。

[INFERRED][HIGH] FFmpeg只判定视频是否可读、是否近黑/近白、是否发生长时间冻结，并按权威事件时间切片；它不能从像素反推出BusinessState、ReactiveMode、HitEvent、目标资格或Particle事实。业务正确性必须由服务端/复制权威日志与sidecar判定，人工审片负责核对“权威状态变化是否被连续、可辨识地表现出来”。

[INFERRED][HIGH] 人工验收顺序固定为：先核对无录屏性能门；再看完整contact sheet排除黑屏、错误镜头和长期冻结；再按专项事件短片逐段看进入、保持、退出是否连续；最后对照sidecar中的expected/actual、authority sample step、client observation step和事件计数。复制延迟导致的短暂expected/actual差异必须保留为诊断，不得自动改写成业务失败或静默抹掉。

[INFERRED][HIGH] T1只验收参与集切换、staging reset、压力传播与新平衡，允许被明确标记的测试边界reset，不允许普通帧瞬移；T3/T4/T6A只验收安全穿越与离开出口，不要求出口形成Target Region站位；T2/T5/T6S/T6M才验收目标相对Region覆盖和连续稳定落位。穿过窄口与目标站位是两项不同能力，不得互相替代。

[INFERRED][HIGH] 推荐命令分为两轮：

```powershell
.\Scripts\RunCrowdDemo.ps1 `
  -Map /Game/Maps/CrowdDemo_MultiStateVatHitResponseSmall `
  -EntityCount 20 `
  -Scenario SimRoundSoftPressure `
  -RequirePerformanceGate

.\Scripts\CaptureCrowdDemo.ps1 -T7StateAcceptance
```

[COMPUTED][HIGH] `CaptureCrowdDemo.ps1 -T7StateAcceptance`固定选择T7 Small和20实体，给客户端开启预期/实际状态标签并写入`scenario_state_events.jsonl`；录制结束后以实际Knockback、KnockUp和Death事件时间生成`step_030_knockback`、`step_060_knockup`、`step_090_death`短片及contact sheet，并写出`acceptance_manifest.json`。

## 6. 当前实现与目标差距

[COMPUTED][HIGH] 当前已实现 Shared Flow V2、Target Region Transport、SoftPressure Particle、双端 hash、correction rollback 和 client-only visual。

[COMPUTED][HIGH] 8417 Static Small 20 已证明 20/20 进入有效距离带并覆盖 16/16 可行 Demand Regions；8418 Moving Small 已证明 20/20 进入有效距离带、覆盖当轮 12/12 可行 Demand Regions，且 Plan/Guidance unrouted、validation failure 与 Particle 安全违规均为 0。这些运行没有记录本文新增的连续稳定窗口、merge等待与目标相对抖动指标，因此只保留为旧口径到达/安全证据。

[COMPUTED][HIGH] T1/T2/T3/T4/T6 对应的真实 Small package 与异构 Particle/CapabilityProfile 基础已建立；T1已完成开放压力传播验收，T2已在8426完成Shared Flow→Target Region Transport→Distance Band旧口径单轮验收，T3已在8455完成双cohort相反Shared Flow→中心交换→对侧完成平面的20/20验收，T4已在8460/8461完成有效通道20/20技术、能力与人工审片。T6A 8464通过，T6S 8479仅保留旧口径到达/安全证据，T6M 8492仍为19/20失败。

[COMPUTED][HIGH] Shared Guidance 与 Particle 之间的通用 Local Predictive Interaction 已经接入，并以同一规则通过T3双向交换、T4通道和T5 Static稳定性V1能力门。8521 T5 Static达到inside-band=`20/20`、Region coverage=`16/16`且最终速度/位置抖动为0；当前下一差距是人工审片与Moving Target复验，不是直接进入100/500。

## 2026-07-15 与长期目标的当前差距

[COMPUTED][HIGH] Demo 已具备逐实体异构 Particle 事实与按 capability cohort 共享 Transport 的代码边界；这符合“群体驱动 + 个体修正”的职责分离，但尚未由 T6 真实运行证明最终视觉和能力效果。

[COMPUTED][HIGH] T1 已证明 20 个稳定 Mass 实体在不 spawn/despawn 的前提下，通过测试专用参与状态完成 `0→5→10→15→19→20→19`；真实 Soft correction 图的稳定 BFS 达到第 3 层，插入与移除后两个 settling 窗口均成立，且移除后 12 个保留实体相对 pre-insert 布局仍有超过 1cm 的量化位移。

[INFERRED][HIGH] T1 的“移除”只表示实体退出 Particle active 集合并回到可见 staging 状态，不是业务 despawn、死亡或 Mass 实体生命周期销毁；该边界不能被写成已实现真实生成/销毁系统。

[COMPUTED][HIGH] 历史稳定性审计快照：当时T1、T3、T4已通过，T2/T5仍等待稳定性V1，T6M刚由8670关闭能力门。当前技术回归状态以`TestScenarioMatrix.md`为准，当前版本人工审片仍未全部完成。

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

## 2026-07-18 自主移动、局部修正与最终朝向

[INFERRED][HIGH] 实体的“想往哪里走”和“为安全实际怎样微调”必须保持两个语义层。Flow、Target Region Transport和业务guidance产生自主Preferred；Local Predictive、Particle和Obstacle只修正可执行位置/速度，不得反向改写实体认为自己的前进方向。

[INFERRED][HIGH] 视觉朝向因此采用两阶段合同：最终站位成立前朝自主Preferred，并用确定性角速度限制平滑转向；只有最终落位已经连续稳定后才朝向目标。瞬时避让侧移、推开和墙体滑动不应造成虫子身体高频左右甩头。

[INFERRED][HIGH] Ranged实体取得合法外圈站位后，目标向它靠近不应触发主动后退。保持资格仍需满足群体分布合同：若该Region后来成为超额人口，应稳定释放多余实体做区域重分流；这不是“因为目标靠近而退远”，而是避免多个实体永久占用同一Region造成空缺。

[COMPUTED][HIGH] T6A现已把通道与目标落位串成同一端到端能力，但两阶段指标继续分开：corridor完成证明安全穿越，inside/coverage和终态稳定证明目标周围自然落位；任何一项都不能替代另一项。

## 2026-07-18 当前目标效果验收修正

[COMPUTED][HIGH] T1与T2此前的双端hash/旧Flow owner误判已经修复；T5 Static/Moving也已达到20实体全部进入有效带并覆盖12/12可行Region。

[COMPUTED][HIGH] 上述结果仍不能证明自然稳定落位：两次T5运行最后90步的连续Particle settled window均为0，目标相对速度p95约134cm/s。现行验收继续要求连续窗口，而不是只看最终一帧coverage。

[COMPUTED][HIGH] T6 Moving的短期群体路线计划现已拥有可回滚Quota消费进度且不使用永久Region owner。严格30秒旧证据为inside20/20、coverage19/20；显式30秒名义窗口加15秒完成宽限后，8670连续两轮达到inside/coverage20/20。

[INFERRED][HIGH] 测试宽限用于区分“合法路线仍在途”和“永久缺口”，不能用于掩盖速度骤降、安全失败或无路可达。稳定验收仍须同时检查实际覆盖、在途Supply、目标相对速度、位置峰峰抖动和Particle settling。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 2026-07-17 T6M 修复后的目标效果边界

[COMPUTED][HIGH] Demo现已证明连续Round可以从相同稳定输入重新开始，动态目标Shared Flow可以在固定世界障碍Topology上按量化目标anchor重建Integration，并保持server/client、rollback与Particle安全合同一致。

[COMPUTED][HIGH] Demo尚未证明20个异构实体都能围绕移动目标完成各自距离带落位：8487两轮均只有`17/20`进入有效距离带。因此“群体驱动 + 个体修正”的移动异构目标效果仍是不完整能力，不能由技术hash通过替代。

[COMPUTED][HIGH] 该2026-07-17停止条件已由8670的30+15秒连续两轮结果解除；当前不再需要为T6M修改Transport/Particle。下一能力缺口是T5/T2连续稳定窗口和人工复验。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 2026-07-17 T6 异构目标效果当前证据

[COMPUTED][HIGH] T6A 8464 与 T6S 8479 已证明20实体异构尺寸、Mobility及三档 Target distance band 在通道与静态目标条件下可以保持统一 Particle 安全、双端确定性、correction rollback 和完整显示。

[COMPUTED][HIGH] T6M 8481 未证明移动目标异构终态：虽然技术门通过，但仅10/20实体进入自己的有效距离带，LargeMelee 与 StandardMelee 均为0/3。

[INFERRED][HIGH] 该失败说明“群体驱动”中的共享导航事实必须与动态群体目标处于同一空间参考系；只让个体速度跟随目标平移、但让共享势场继续指向旧世界坐标，不构成完整的移动群体调度。

[COMPUTED][HIGH] 历史2026-07-17结论：当时T6M尚未关闭，因此不能描述为T1–T8全部无硬失败。该前置停止条件后来已经解除，Mass Projectile功能迁移、PJ0–PJ6三模块重构与专项均已完成。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 2026-07-15 个体业务、VAT 与受击效果目标补充

[INFERRED][HIGH] “每个虫子是完整业务实体”不仅表示保存身份和位置，还要求最终能表达 AcquireTarget、Attack、Recover、Death 等业务状态，以及 HitReact、Knockback、KnockUp 等受击响应事实；但这些个体事件不得反向承担群体目标生成、共享路线规划或区域人口调度。

[INFERRED][HIGH] Melee/MidRange/Ranged 的 Target 距离带只定义群体站位前提，不等于攻击已发生。真正远程攻击必须另有目标有效性、windup、单次发射、projectile、hit fact 和 recovery 合同。

[INFERRED][HIGH] 最终视觉目标不是“一个StaticMesh换颜色”：真实虫子 VAT 必须由业务/受击事实驱动 Idle、Move、Attack、HitReact 和 Death clip；命中改色是可叠加材质反馈，不能替代动画状态或由客户端本地碰撞自行触发。

[INFERRED][HIGH] 击退和击飞属于确定性受击运动。水平位移必须继续经过统一 Particle/Environment 安全链；首版击飞以保守2.5D ballistic Z表现离地和落地，XY footprint继续参与Hard/Swept安全，不外推为空中穿越能力。

[COMPUTED][HIGH] T7 已接入五状态业务/视觉事实、确定性测试 HitFact、击退/击飞/落地、真实 VAT ISM 与同步 ServerTime 播放，并已通过自动化、单轮双端技术门和近景人工审片。

[COMPUTED][HIGH] 命中闪色事实与HitFlashIntensity已接入；当前由主体VAT材质读取PICD slot 2独立闪白单个实例，不再使用红色同帧overlay ISM。9208 T7逐帧录像证明Knockback/KnockUp/Death分别只改变目标实例且无副本或重影。T8进一步实现并验证了10射手+10静止目标的目标选择、windup、Mass projectile、swept hit、damage、客户端事件视觉与统一HitResponse。

[INFERRED][HIGH] T8 Small本身只证明静止目标远程投射物链；PJ6另以20/100/500实体和4/20/100并发Projectile关闭模块化规模门。两者仍不能外推为远程Transport handoff、T9混合战斗或原工程迁移已经成立。

## 2026-07-16 类游戏业务Sandbox与插件化迁移目标

[INFERRED][HIGH] Demo长期目标增加一个独立的类游戏虫群Sandbox：玩家控制Pawn移动，地图持续生成受上限约束的敌群，群体按玩家和世界事实共享目标与导航结果，玩家通过fixed-step命令释放线形或圆形伤害，并由统一HitFact驱动伤害、HitFlash、击退、击飞、落地、死亡和群体membership变化。

[COMPUTED][HIGH] 阶段I已补充基于静态烘焙NavMesh的稳定分层Surface Graph与共享Flow，并在真实地图验证坡道、桥上桥下、高台、多路线、窄桥和不可通行落差。[INFERRED][HIGH] 该独立导航probe不能外推为continuous lifecycle、Behavior、Combat、Logistics与Presentation已经完成混合运行。

[COMPUTED][HIGH] 该前置顺序已执行到T9：T1–T8独立Small、插件提取/双重回归和T9固定20实体混合战斗均已完成；T10持续生成、玩家命令与完整游戏循环仍是后续阶段。

[INFERRED][HIGH] 插件化用于降低未来接入`E:\Projects\SuperInvincibleTank_BugFix`的迁移风险；未来回原工程采用公开接口与Adapter，不默认复制整个Demo Source、Coordinator、地图或历史诊断代码。详细事实源为`GameplaySwarmSandboxAndPluginMigrationPlan.md`。

[COMPUTED][HIGH] 2026-07-17 的Mass Projectile插件前置核对最初因T3生产合同缺失而停止；随后T3、T4、T6M、T8和PJ0–PJ6均已关闭。本句仅保留历史停止原因，不再构成当前前置门。

## 2026-07-17 大量远程敌人与通用命中目标

[COMPUTED][HIGH] Demo长期目标中的大量远程类敌人与大量同时在场Projectile已由PJ6公共Mass Projectile Entity、稳定空间Broadphase和fixed-step相对sweep实现到本仓库规模门；结构测试持续禁止跨Boundary持久Pipeline数组权威或`Projectile×Agent`全量扫描。

[INFERRED][HIGH] “命中”和“被命中”必须通过通用、无Actor依赖的HitFact连接：Projectile、线形、圆形和近战只生产事实；原工程Adapter负责伤害、防御、状态、击退/击飞、死亡、掉落和视觉结果。

[COMPUTED][HIGH] 插件公共模块与Demo验证已经完成；原工程Adapter验证尚未执行。完整事实源为`MassProjectileHitFrameworkDesign.md`。

## 2026-07-18 最终站位标记语义

[INFERRED][HIGH] 客户端验收画面必须忠实显示算法真正拥有的空间事实。若系统存在稳定Agent→Slot分配，则可以显示精确最终位置；若系统只拥有Region人口与可行terminal cell，则只能显示范围、分区和候选cell，不能为了“看起来明确”伪造逐实体Slot。

[INFERRED][HIGH] 精确Slot标记用于检查分配是否被正确执行；Region标记用于检查实体是否进入正确距离带、覆盖可行区域并自然稳定。两者都只服务验收可解释性，不参与server gameplay movement、局部避让或Particle求解。

[INFERRED][HIGH] Transit与Target落位也必须保持不同完成含义：Transit回答“是否全体安全通过并稳定离开出口”，Target落位回答“是否在目标相对可行区域内覆盖并稳定”。不得再用同一point-goal或同一`goal_reached`数字替代两者。

## 2026-07-22 阶段 G 持续生命周期基线

[COMPUTED][HIGH] Demo现有地图已提供独立`-CrowdDemoContinuousLifecycle`入口：固定Round agent数为0，10个真实Mass entity逐步增长到20硬上限，再持续执行membership迁移、Death/BusinessRecycle despawn和同槽位高LifecycleSerial respawn；T1 active/inactive没有被复用为生命周期。

[COMPUTED][HIGH] 8777双端运行中，序列12把slot 2 serial 1按Death销毁，序列13以serial 2重生；该历史运行使用普通/HitFlash双ISM。当前客户端只按StableEntityRef增量Add/Remove/Update主体ISM，`HitFlashIntensity`位于同一实例slot 2；9203序列44持续生命周期门通过且entity-set hash=`12305161180829922642`。[INFERRED][HIGH] 这只建立持续生命周期与单主体表现基线，不等同于H的统一Behavior或J的混合Sandbox已完成。

## 2026-07-23 阶段 H 统一 Behavior 基线

[COMPUTED][HIGH] Runtime已用同一provider/transition合同表达Wander、MoveTo、Pursue、HaulPickup、HaulDeliver、Attack、Guard和Flee，并统一输出Target、Objective、MovementProfile、InteractionIntent与BusinessCommitRequest。Demo Logistics与Combat只作为宿主adapter；Faction值变化不会授予缺失Capability。

[COMPUTED][HIGH] Cargo pickup/deliver、真实Demo HitFact伤害、HitEventId幂等提交及rollback replay不重复伤害/业务提交均通过自动化。[INFERRED][HIGH] H没有把这些行为接入G持续场景；该组合只在I导航完成后的J混合Sandbox中验收。

## 2026-07-23 阶段 I 分层导航基线

[COMPUTED][HIGH] Core稳定Surface Graph与Shared Flow、Runtime静态Recast提取器及`CrowdDemo_NavSurfaceGraphVerticalSmall`已形成同一生产导航路径；8800运行提取98 nodes、234 directed edges、4 layers和13个桥上桥下XY overlap，76条可达坡边与8/8目标marker可达，不可通行drop保持隔离。

[COMPUTED][HIGH] Development/DebugGame `-DisableUnity`、定向3/3、MassCrowd 27/27与CrowdDemo 112/112通过，视觉证据保存于`Saved/StageI_NavSurfaceGraph_Visual.png`。[COMPUTED][HIGH] 后续J已在20实体持续场景中证明导航层、业务切换、生命周期、同步与视觉共同成立。

## 2026-07-23 阶段 J 混合行为 Sandbox

[COMPUTED][HIGH] 独立`-CrowdDemoMixedSandbox`入口不创建固定Round agents；20个真实Mass实体同时消费LifecycleWorld、统一Behavior provider、Cargo/Combat commit ledger、Recast Surface Graph/Shared Flow和客户端增量ISM。行为转换来自距离、Cargo carrier、Health与当前目标事实，不使用固定事件表驱动业务结果。

[COMPUTED][HIGH] 8804 step600达到active/visible=`20/20`、行为切换29、pickup/delivery=`4/1`、Combat quantity=`500`、commit/duplicate=`25/25`、spawn/despawn=`3/3`、membership=7、同层最小间距=`71.51cm`；Server fixed-step p95=`0.863ms`、Client frame p95=`4.851ms`，双端entity/membership hash一致且无VIOLATION。视觉证据为`Saved/StageJ_MixedSandbox_Visual.png`。

[INFERRED][HIGH] 该结果只关闭J的20实体混合路径，不能外推为K的100/500规模或L原工程迁移已通过。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
