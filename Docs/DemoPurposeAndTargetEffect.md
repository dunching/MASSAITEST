# MassAI Crowd Demo 目的与目标效果

## 2026-07-14 当前权威目标：统一软排斥粒子群

[COMPUTED][HIGH] 参考视频 `C:\Users\dunch\Videos\NVIDIA\Desktop\Desktop 2026.07.13 - 14.27.46.01.mp4` 的约 `208s–225s` 只证明一种可见目标：较大实体进入密集小实体群时，局部空间被逐层释放并形成新的平衡。视频不提供底层算法、碰撞状态或双端确定性证据。

[INFERRED][HIGH] Demo 当前唯一的局部群体目标模型改为：每个实体都具有真实硬半径、不可压缩安全间隙、可压缩软间隔和质量/逆质量；SoftMargin同时作用于其他粒子与静态环境。任意局部接触进入软距离时都产生有限排斥，第一层位移继续改变第二、第三层接触的误差，从而自然传播压力。

```text
HardPairDistance = RadiusA + RadiusB + HardSafetyGap
SoftPairDistance = HardPairDistance + SoftMarginA + SoftMarginB
SoftError        = SoftPairDistance - PairDistance
```

[INFERRED][HIGH] `SoftError > 0`时，确定性局部solver按显式fixed-step response施加有限软压力，再按双方Mobility/InverseMass分担修正；它不要求本步完全恢复SoftDistance。静态障碍和盒体边界视为Mobility为0的不可移动粒子：环境Soft修正全部由实体承担，但只消除指向环境内部的法向分量，合法切向运动必须保留。SoftError允许长期非零，真实半径与HardSafetyGap不得被软模型压缩。

[INFERRED][HIGH] 该模型不区分“普通实体”“穿行者”“Transit Source”“靠墙实体”或“正在恢复阵型的实体”。尺寸和质量只通过PhysicalRadius、SoftMargin与Mobility进入同一求解合同；局部solver不保存另一套priority/responsibility，也不创建额外通道状态机或预先证明某条完整穿行路径。

[INFERRED][HIGH] Flow、目标追逐和玩家输入只提供外部驱动力；局部群体层只负责软压力和硬安全。实体被移除后是否重新填满空洞，由现存密度、边界压力和外部目标共同决定；开放空间中不承诺回到原槽位或原矩阵。

[INFERRED][HIGH] Priority ORCA、Joint Velocity、Transit Influence、Position/Holding回位和多分支Shadow都不是该最小模型的组成部分。ORCA只允许在未来有证据证明“高速预测碰撞”无法由新固定步约束链处理时重新立项，不作为当前兼容层保留。

[COMPUTED][HIGH] 当前代码已实现粒子—粒子的显式Soft response、量化后的Hard/Swept/Obstacle安全闭环、candidate/applied状态拆分和invalid立即停止；8368旧fixture已经关闭。8371 Small 20在step 155因Agent 5/13最终距离`93.021cm < 94cm`而失败。当前Obstacle与Bounds仍只使用`PhysicalRadius + HardSafetyGap`，没有消费SoftMargin；Hard pair与环境也仍按顺序投影，尚未证明存在稳定二维共同解。

[COMPUTED][HIGH] 工程仍保留SF3/SF4 ORCA、SF4 Position/Holding、Elastic Transit和Joint Shadow等旧路径，因为“Small通过后才删除”的前提未成立；当前过渡态不能写成最终架构。

[INFERRED][HIGH] 历史SF1–SF4实验的数值仍可作为回归证据，但历史“下一步”“首选路线”和兼容设计不再覆盖本节。

## 1. 文档职责

[INFERRED][HIGH] 本文件是 MassAI Crowd Demo 的目的、最终目标效果和长期架构原则的稳定事实源。

[INFERRED][HIGH] `CurrentArchitecture.md` 回答“当前如何实现”，`PhasePlan.md` 回答“当前做什么”，`FeatureChecklist.md` 回答“哪些项目已经通过”；这三份阶段文档不能替代本文件，也不能把某一阶段算法写成最终产品架构。

[INFERRED][HIGH] 当阶段实现、实验参数或验收结果变化时，本文件只在 Demo 总目的、最终效果或长期职责边界发生变化时更新。

## 2. Demo 来源

[KNOWN][HIGH] 本 Demo 基于 `E:\Projects\SuperInvincibleTank_BugFix` 中已有的 MassAI 实验、代码和设计文档形成独立验证工程。

[COMPUTED][HIGH] 原工程设计文档覆盖 Planner、GroundMovement、MassReplication、Client Visual、WorkRuntime/Prepared 数据、局部避让、PBD/HardSeparation、StateTree 和战斗业务行为。

[INFERRED][HIGH] 随着实验分支、性能路径、复制路径、视觉修复和业务逻辑叠加，在原工程中同时定位群体路线、局部拥塞、复制误差和视觉表现的归因成本越来越高。

[INFERRED][HIGH] 独立 Demo 的目的不是抛弃原工程，而是隔离群体运动变量，建立小而可信、可复现、可逐阶段验收的技术验证器。

[INFERRED][HIGH] Demo 中验证通过的职责边界、数据契约和纯算法内核可作为原工程未来收敛的依据；它们不构成“将 Demo 整体复制回原工程”的默认决定。

## 3. Demo 总目的

[INFERRED][HIGH] Demo 的总目的是：使用 Unreal Mass 验证一套适用于大规模虫群的“群体驱动 + 个体修正”运动架构。

[INFERRED][HIGH] 群体目标、共享导航事实、密度趋势、通道调度和主要运动方向应按 cohort、区域或目标批量生成和消费。

[INFERRED][HIGH] 每个虫子仍是完整业务实体，保存独立身份、位置、速度、生命周期和业务状态，并承担局部安全修正；它不应为相同群体目标重复执行完整目标生成、Nav/EQS 查询和整体路线规划。

[INFERRED][HIGH] Demo 最终必须同时证明：

1. [INFERRED][HIGH] 群体运动在开放区域、障碍路线、窄通道和目标附近具有可读且连续的整体效果。
2. [INFERRED][HIGH] 大规模 processor 的数据准备、WORK 计算和 GT 提交边界成立。
3. [INFERRED][HIGH] Server/Client 使用同一模拟事实，correction 不产生跨轮扩散或客户端伪运动。
4. [INFERRED][HIGH] 20、100、500 实体可以按正确性、容量和规模逐级验收。
5. [INFERRED][HIGH] 客户端完整显示全部实体，不通过隐藏实例、客户端偏移、传送或视觉假间距制造通过结果。

## 4. 核心架构原则

### 4.1 群体驱动 + 个体修正

[INFERRED][HIGH] 长期职责结构为：

```text
群体调度层
├── 选择 cohort / 群体目标
├── 生成共享 Navigation / Crowd / Flow Field
├── 聚合密度和局部交通状态
├── 决定主要通行趋势和共享运动事实
└── 输出稳定、可批量消费的群体结果

个体实体层
├── 保存 AgentId、位置、速度和业务状态
├── 消费群体结果
├── 处理局部避让和安全修正
├── 处理受击、攻击、死亡等个体事件
└── 提交最终 Transform / Velocity
```

[INFERRED][HIGH] 群体层决定“大多数实体总体往哪里、通过哪片区域、何时进入或等待通道”；个体层决定“这个实体如何在局部安全地完成该趋势，以及是否因业务事件暂时退出群体行为”。

[INFERRED][HIGH] Flow、Density、Traffic/Portal 或未来 Crowd Navigation Field 属于共享运动事实；ORCA、PBD、ObstacleConstraint 和个体 locomotion 属于局部运动或安全修正。攻击 slot、受击、死亡和能力状态不是 Crowd Field 的职责。

[INFERRED][HIGH] 群体结果不能抹掉 AgentId、生命周期或业务状态；个体修正也不能反过来为每个实体重新生成一套完整群体路线。

### 4.2 WORK processor 与 GT processor

[KNOWN][HIGH] 原工程 `38_MassAIProcessorPipelineContract.md` 规定：WORK processor 只消费 POD snapshot 并产生 POD result，不访问 `UWorld`、`UObject`、`AActor`、Mass fragment、Subsystem、NavigationSystem 或可变业务状态。

[KNOWN][HIGH] 同一契约规定：GT processor 拥有 Mass fragment 读写、Transform/Velocity 提交、`UWorld/UObject` 访问、Nav 投影/射线、复制和业务 adapter。

[INFERRED][HIGH] Demo 的长期流水线应遵守以下方向：

```text
GT Prepare
→ 稳定排序的 POD / SoA snapshot
→ WORK 纯计算
→ revision / compatibility 检查
→ GT Apply / Commit
→ Replication / Client Visual
```

[INFERRED][HIGH] WORK 结果未就绪时，只能在明确 TTL 和 compatibility hash 内消费 last-good result，或者保持已有输出；不能静默调用旧算法重算来掩盖 WORK 缺失。

[COMPUTED][HIGH] 当前 Demo 已把主要算法放入纯 C++ kernel，并通过 prepared SoA 在 processor 之间交换；这证明了纯数据接口的可行性，但尚未证明真实 off-thread WORK 调度、预算 continuation 和大规模异步消费已经完成。

### 4.3 统一群体调度与个体业务实体

[INFERRED][HIGH] Planner/群体调度拥有 cohort 目标、共享路径、区域压力、Portal 方向和群体级 approach；它不拥有单个实体的攻击、受击、死亡或能力执行。

[KNOWN][HIGH] 原工程架构边界要求 Planner 选择目标/角色/阶段/slot/移动目标，Movement 消费 intent 并产生权威 locomotion，Replication 发布服务器事实，Client Visual 只负责表现消费。

[INFERRED][HIGH] Demo 的群体调度结果未来接入原工程时，应成为 Planner/Movement 的共享输入，而不是取代 StateTree、BattleObjective、Enemy Profile 或能力 processor。

[INFERRED][HIGH] 个体实体可以因攻击、死亡、受击、保护状态或特殊能力退出普通 crowd guidance；该例外必须由显式业务状态表达，不能通过视觉偏移或局部 solver 的隐式副作用表达。

### 4.4 Locomotion、安全修正与视觉修正

[KNOWN][HIGH] 原工程契约将 locomotion displacement、Nav/Obstacle adjustment、PBD/HardSeparation 和 Client Visual correction 定义为不同概念。

[INFERRED][HIGH] 只有 locomotion 表示正常业务运动；ORCA/PBD/Obstacle correction 只能改变局部安全结果，不能自动成为目标选择、攻击朝向或普通移动动画的依据。

[INFERRED][HIGH] Client Visual 可以平滑和有限预测显示位置，但不得写回 gameplay movement，也不得用额外 pairwise push、隐藏实例或扩大显示步长掩盖服务器拥塞。

### 4.5 随机地形、Steering与离散调度边界

[INFERRED][HIGH] 随机地形中的石块、墙体、U形结构和不规则缝隙首先是walkability与共享导航事实，不应默认转换为人工lane、永久Portal或逐实体路径锁。静态或一轮内稳定的障碍进入Flow/Crowd Navigation Field；短期小型动态障碍由Obstacle Steering与局部避让处理；持续存在并改变可达拓扑的大型动态障碍才进入确定性overlay并触发有界重建。

[INFERRED][HIGH] 连续运动的目标组合为：

```text
Shared Flow / Seek / Arrive / Player Input
→ Preferred Velocity
→ MovementPredict
→ Deterministic Particle Constraint Solve
   ├─ Soft Pair Pressure
   ├─ Hard Pair Separation
   └─ Obstacle / Bounds
→ MovementFinalize
```

[INFERRED][HIGH] 离散群体调度只拥有不能靠连续局部压力表达的真实有限资源，例如业务slot或经过证据门控的极端Portal容量；这些资源不进入通用Soft-Pair数据合同。当前群体运动验证不要求Position归属、Holding归属、Commit许可或逐实体完整polyline。

[INFERRED][HIGH] UE官方Mass NavMesh/ZoneGraph/MassCrowd可作为MoveTarget、ShortPath、Waiting Slot和Lane Gate的参考实现；当前Demo不直接采用其逐实体同步FindPath、StateTree、World DeltaTime或ZoneGraph运行时作为双端确定性主路径。

### 4.6 统一硬核与软壳

[INFERRED][HIGH] Demo必须把“不能物理重叠”和“群体自然保留空间”定义为同一pair的两个阈值：HardPairDistance是硬核，SoftPairDistance是可压缩软壳。不得再由ORCA、Separation、Elastic和Position/Holding分别拥有不同间距语义。

[INFERRED][HIGH] 开放区域中软壳使实体自然分散；窄口、高密度和墙体压力允许软壳被压缩，但不得压缩真实半径与HardSafetyGap。压力解除后solver只消除仍存在的SoftError，不恢复原Agent顺序、PositionId或矩阵。

[INFERRED][HIGH] 大尺寸或低Mobility实体无需成为特殊Transit Source：更大的PhysicalRadius/SoftMargin或更低Mobility会自然使邻居承担更多位移。完整数据语义、实现路径和指标记录在 [CrowdTransitCapabilityDesign.md](CrowdTransitCapabilityDesign.md)。

[INFERRED][HIGH] 统一距离合同同时适用于粒子和环境：

```text
PairHardDistance = RadiusA + RadiusB + max(HardGapA, HardGapB)
PairSoftDistance = PairHardDistance + SoftMarginA + SoftMarginB

WallHardDistance = RadiusA + HardGapA
WallSoftDistance = WallHardDistance + SoftMarginA
```

[INFERRED][HIGH] 当粒子接近静态墙体、障碍或FlowBounds时，环境在WallSoftDistance内产生可压缩法向压力，在WallHardDistance内形成不可违反的硬约束；墙体不承担位移，粒子的合法切向自由度必须保留。粒子—粒子与粒子—环境不得再由两套互不知情的间距语义处理。

[INFERRED][HIGH] 该模型采用类似Boids的“少量局部规则产生群体现象”原则，但不照搬经典Boids的凝聚、对齐和分离三项行为。共享Flow/Seek/Player Input已经负责宏观方向；局部粒子solver只负责质量/可动性分配、Soft压力与Hard安全，不额外生成凝聚目标。第一版的Mobility是约束修正权重，不自动宣称已经实现牛顿力学中的惯性、动量或冲量。

[INFERRED][HIGH] 垂直靠墙、斜墙滑动、双侧夹持、窄口压缩、粒子插入/移除和多粒子交叉只作为通用规则的fixture，不得成为生产代码中的场景枚举或特殊身份分支。规则应保持简单；多粒子与环境同时接触时的共同求解可以技术上严谨，但不能引入Portal、Holding、Transit Source或逐场景脚本来代替局部物理合同。

[INFERRED][HIGH] 每个fixed-step的目标顺序为：外部guidance生成DesiredVelocity → 预测位置 → 粒子Soft压力 → 环境Soft压力 → 粒子Hard/Swept约束与环境Hard约束的统一局部闭环 → 从最终位移重建Velocity → 独立Hard安全复验 → 原子提交。复验只判断结果是否安全，不能代替前面的相互影响求解。

[COMPUTED][HIGH] 当前工程已实现单一`PhysicalRadius/HardSafetyGap/SoftMargin/Mobility`数据合同和粒子—粒子Soft/Hard/Swept链，但所有正式实体Mobility仍为1，环境只消费`PhysicalRadius + HardSafetyGap`，尚未实现环境Soft压力、异构质量能力门或粒子/环境统一二维共同求解。

## 5. 最终目标行为

### 5.1 自由游荡

[INFERRED][HIGH] 无战斗目标或尚未发现目标时，虫群应在允许区域内形成松散、持续、非完全同步的游荡，而不是全部永久 Idle、全部追逐同一点或每个实体独立运行昂贵目标查询。

[INFERRED][HIGH] 群体层可周期性生成区域目标、共享方向或低频 wander field；个体层保留短暂停顿、局部偏移、转向差异和障碍修正，使群体可读但不过度整齐。

[KNOWN][HIGH] 原工程游戏设计要求 ClearBugs 中噪音范围外的小青虫和泥蜗牛在 Idle/Wander 间循环，不能永久停在 AcquireTarget，也不能在无噪音时主动追逐平台。

[COMPUTED][HIGH] 当前 Demo 没有自由游荡场景，因此该效果是最终目标和当前缺口，不是已实现能力。

### 5.2 静态目标追逐

[INFERRED][HIGH] 群体发现静态目标后，应共享主要接近路线，绕过障碍、通过连续转角和窄通道，并在目标附近由Soft-Pair Pressure自然消化密度，不能全部挤向同一个goal cell。

[INFERRED][HIGH] 第一版不再要求Candidate/Assignment、Holding、Commit、RadialStage、Arc/Final route或逐实体槽位。目标吸引与pair排斥共同形成局部平衡；若未来战斗业务需要严格攻击slot，应作为独立业务资源层另行设计，不能反向污染通用群体solver。

[COMPUTED][HIGH] 当前 SF1 已证明共享静态 FlowField 路线、障碍约束和双端确定性；SF2/SF3 尚未证明100/500容量和最终目标附近群体效果成立。

### 5.3 动态目标追逐

[INFERRED][HIGH] 动态目标移动时，群体层应以有界 cadence 更新共享目标事实和主要追逐趋势，避免500个实体独立对目标做完整路径/EQS计算，也避免整个群体同一帧机械急转。

[INFERRED][HIGH] 个体可以因速度、位置、局部拥塞和业务状态产生自然落后；当目标速度高于虫子基础速度时，虫子允许落后，不得通过速度膨胀、传送或视觉 correction 假装追上。

[INFERRED][HIGH] 目标停止或低速后，群体应逐步收敛为可读的 approach/包围/预留结构，而不是维持大面积远端尾巴、厘米级追随抖动或同步 stop-go。

[COMPUTED][HIGH] 当前 Demo 没有动态目标追逐场景，该效果尚未实现或验收。

## 6. 参考媒体中的可见目标效果

[COMPUTED][HIGH] `Desktop 2026.07.08 - 19.22.20.02.mp4` 为约61.84秒、2560×1440、约30fps的录屏。均匀帧可见开放区域内大量单位围绕多个大型战斗主体持续聚散，屏幕保持高实体密度，并出现局部包围、流动空隙和战斗效果遮挡。

[COMPUTED][HIGH] `Desktop 2026.07.08 - 19.28.20.03.mp4` 为约38.13秒、2560×1440、约30fps的录屏。均匀帧可见群体进入蛇形窄通道、连续转弯并从出口继续展开，局部拥塞在画面上可读。

[COMPUTED][HIGH] `Desktop 2026.07.12 - 22.15.23.01.mp4` 为约32.20秒、2560×1440、约30fps的录屏。均匀帧可见游戏单位在开放区域、岩石和不规则墙体之间聚散、追随并参与战斗；画面没有持续稳定的人工lane或固定单一入口。

[INFERRED][HIGH] 三段视频提供的目标效果参考是：大量单位同时可见、整体趋势明确、局部运动不完全同步；明确窄通道中保持推进并在出口重新展开，开放随机地形中则能围绕障碍压缩与展开。它们不要求实体保持固定编队，也不要求所有地形先转换为道路网络。

[COMPUTED][HIGH] 视频没有提供内部算法、服务器权威状态或处理器数据，因此不能据此声称画面使用了 NavMesh、FlowField、ORCA、RVO、PBD 或任何指定算法。

[COMPUTED][HIGH] 参考图片表达了“群体驱动 + 个体修正”和“谁是主路径”的架构观点；它是设计输入，不是当前 Demo 已完成该架构的运行证据。

## 7. 验收尺度

| 规模 | 验收职责 |
|---:|---|
| 20 | [INFERRED][HIGH] 验证算法正确性、连续路线、局部避让、无穿墙、无死锁和人工可读效果。 |
| 100 | [INFERRED][HIGH] 验证窄口容量、局部密度、队列/通行、公平性和 correction 稳定性。 |
| 500 | [INFERRED][HIGH] 验证群体调度收益、processor/WORK边界、复制预算、完整显示、性能和长时间稳定性。 |

[INFERRED][HIGH] 小规模未通过时，不能用500规模“看起来热闹”替代正确性；20通过而100/500失败时，应判定容量或架构边界不足，而不是隐藏实例、降低目标或扩大视觉修正。

[INFERRED][HIGH] 每档都必须分别验证服务器运动、客户端模拟、Client Visual、实体/实例数量和错误日志；自动化、双端技术结果和人工审片结论必须分开记录。

## 8. 当前进度与最终目标差距

| 能力 | 当前证据 | 状态 |
|---|---|---|
| SF1 Shared FlowField | [COMPUTED][HIGH] 静态路线、绕墙、转角、双端 hash、penetration 已通过既有验收。 | [COMPUTED][HIGH] 阶段通过。 |
| SF2 Soft Separation + PBD | [COMPUTED][HIGH] 20规模 severe overlap 受控；100/500 出现容量和目标到达回退。 | [COMPUTED][HIGH] 小规模部分通过，容量未通过。 |
| SF3 Portal/Traffic/Holding/ORCA | [COMPUTED][HIGH] 纯内核、双端确定性、Portal/状态转换和障碍安全通过；ORCA/LP closeout正式结果为goal=10/20，成熟RVO2 reference没有发现continuous LP漏解。 | [COMPUTED][HIGH] 实验代码存在，最终目标区效果未通过。 |
| Holding | [COMPUTED][HIGH] 已有实验性 target 生成和纯测试，但 Small 能力门失败且没有通过后的人工录像。 | [COMPUTED][HIGH] 未完成最终效果验收。 |
| SF4 Positioning | [COMPUTED][HIGH] Candidate/Assignment纯内核与双端设施存在；Polar Approach、Phase Reservation、Wait-For Graph和Route-Aware ORCA的Static最终能力为1/20。 | [COMPUTED][HIGH] 复杂路径预约方向停止；Steering-first Holding/Commit修订仅完成文档决定，尚未实现。 |
| 自由游荡 | [COMPUTED][HIGH] 当前三个场景均为 RoundSim 目标路线验证。 | [COMPUTED][HIGH] 未实现。 |
| 动态目标追逐 | [COMPUTED][HIGH] 当前目标和 FlowFieldConfig 在每轮内为静态测试事实。 | [COMPUTED][HIGH] 未实现。 |
| WORK/GT 正式执行边界 | [COMPUTED][HIGH] 已有纯kernel和prepared SoA，但没有真实off-thread WORK与continuation验收。 | [COMPUTED][HIGH] 仅完成接口方向验证。 |
| 500完整群体效果 | [COMPUTED][HIGH] SF1显示/路线基线存在；SF2容量失败，SF3因Small失败未进入500。 | [COMPUTED][HIGH] 未完成。 |

## 9. 长期推进原则

[INFERRED][HIGH] 后续阶段应继续按“共享群体事实 → 局部个体修正 → GT权威提交 → 双端/视觉验收”逐层推进，不能重新回到每实体独立Nav/slot/workitem作为整体主路径。

[INFERRED][HIGH] 未来 Crowd Navigation Field 应由 Editor/Commandlet 从 NavMesh 离线烘焙为确定性资产，由Server/Client加载同一BuildHash；这只是已记录的后续架构方向，当前没有实现。

[INFERRED][HIGH] 自由游荡、动态目标、near-target positioning和战斗业务应分别立项；不能在解决群体运动问题时暗中加入攻击，也不能用局部避让承担Position容量分配。near-target第一版应优先验证Holding/Commit/Occupied最小状态，而不是继续扩张Phase Reservation或Wait-For生产控制。

[INFERRED][HIGH] 任何失败必须先按 `Symptom → Evidence → Layer Attribution → Allowed Change → Forbidden Change → Test Plan` 归因；未知层只能增加诊断，不能同时修改Planner、Movement、Replication和Client Visual。

## 10. 非目标

[INFERRED][HIGH] Demo 不负责完整复刻原工程的BattleObjective、StateTree、攻击、受击、死亡、掉落或Boss业务。

[INFERRED][HIGH] Demo 不以固定军阵、所有实体严格等距、所有虫子同时转向或永不接触为最终效果。

[INFERRED][HIGH] Demo 不接受用客户端专属运动、隐藏实例、传送、速度膨胀、扩大correction或放宽指标掩盖服务器群体运动失败。

[INFERRED][HIGH] Demo 不把某个具体算法名称当作最终目的；FlowField、Portal、ORCA或未来方案只有在满足群体效果、执行边界和双端验收时才算有效。

## 11. 依据与证据边界

[COMPUTED][HIGH] 本文件的原工程架构依据包括：

- [KNOWN][HIGH] `34_MassAISwarmBusinessLogicContract.md`：群体接近、near-target slot、locomotion/correction和可见虫群业务边界。
- [KNOWN][HIGH] `35_MassAIArchitectureBoundaries.md`：Planner、Movement、Replication、ClientSim、Client Visual和Submit/deferred的分层归因。
- [KNOWN][HIGH] `38_MassAIProcessorPipelineContract.md`：WORK POD-only、GT Mass/UObject/Transform ownership、last-good与compatibility规则。
- [KNOWN][HIGH] `10_MassAINPCExtensionDesign.md`：BattleObjective、Profile、StateTree、capability processor和高规模NPC扩展职责。
- [KNOWN][HIGH] `03_GameDesign.md`：ClearBugs idle/wander、DefendGarden objective target和SwarmControl密度/路线业务目标。
- [KNOWN][HIGH] `MassAI_CrowdAcceptance.md`：服务器、客户端、视觉、avoidance、slot/refill和性能的持续验收分类。

[COMPUTED][HIGH] 三段参考视频只作为可见群体效果参考；参考图片只作为架构意图输入。它们不提供源码、服务器事实、processor trace或算法配置，因此不承担算法实现证据职责。

[INFERRED][HIGH] 当前实现与数值结果必须回到 `CurrentArchitecture.md`、`PhasePlan.md`、`FeatureChecklist.md`及对应自动化/运行日志核验，不能从本目标文件反向推定为已完成。

## 12. Soft-Pair Pressure的长期容量原则

[INFERRED][HIGH] “群体驱动 + 个体修正”不能依赖预留固定Transit Aperture或逐穿行者component规划。容量来自当前空间、边界、密度和所有pair软壳的共同状态；局部压力能够传播时自然让行，硬墙与高密度确实无解时允许减速或停止。

[INFERRED][HIGH] 长期距离合同只保留PhysicalRadius、HardSafetyGap和SoftMargin；Mobility/InverseMass决定修正分担。RequiredTransitAperture、YieldBudget、AssignedSpacing和Transit intent不属于当前核心模型。

[INFERRED][HIGH] 同一pair在同一fixed-step只有一个constraint owner。SoftPair、HardPair与Obstacle/Bounds在统一预测位置solver中固定迭代，禁止先后叠加多个互不知情的push、ORCA修正和Shadow polish。

[COMPUTED][HIGH] 当前Demo曾实现容量公式、Transit intent、Joint component和Elastic Shadow，但真实运行没有通过能力或安全门；这些结果现在只证明旧路线复杂且未完成，不再构成兼容保留理由。
