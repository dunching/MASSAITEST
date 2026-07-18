# MassAI Crowd Demo 当前架构

## 1. 文档职责

[COMPUTED][HIGH] 本文件只描述当前生产代码，不保存端口流水账或已经退出的算法设计。历史SF2–SF4、Portal、ORCA、Position/Holding、Elastic和Joint实验统一查阅`Docs/History`与Git历史。

[COMPUTED][HIGH] Demo长期目的和架构原则以`DemoPurposeAndTargetEffect.md`为准；当前任务以`PhasePlan.md`为准；验收状态以`FeatureChecklist.md`和`TestScenarioMatrix.md`为准。

## 2. 正式场景

[COMPUTED][HIGH] 外部正式场景仅有：

- `SimRoundObstacle=0`：Shared Flow基线。
- `SimRoundSoftPressure=1`：当前群体运动、目标接近和业务验证场景。

[COMPUTED][HIGH] parser、console、地图配置和`UCrowdDemoMassSubsystem::SetScenario()`均拒绝旧数值2/3及旧场景名称，并明确回退`SimRoundObstacle`。

[COMPUTED][HIGH] 旧2/3枚举、processor、Pipeline状态、Mass fragment、纯kernel、RVO2适配、CLI和地图已经物理删除；不是可回退的生产分支。

## 3. 当前处理链

### 3.1 Shared Flow基线

```text
RoundPlanApply
→ SharedFlowFieldBuild
→ FlowPreferredVelocity
→ MovementPredict
→ ObstacleConstraint
→ MovementFinalize
→ AuthorityCommit / ClientPredictionCommit
```

### 3.2 SoftPressure通用链

```text
RoundPlanApply
→ [OpenSpawnRelaxationPhasePrepare]
→ [TargetFactApply]
→ SharedFlowFieldBuild
→ FlowPreferredVelocity
→ [TargetPolarTopologyBuild]
→ [TargetRegionPopulationBuild]
→ [TargetRegionTransportSolve]
→ [TargetRegionGuidance]
→ [TargetApproach Schedule/Commit/Guidance]
→ ReactiveMotionIntentCompose
→ LocalPredictiveInteraction
→ MovementPredict
→ ParticleConstraintSolve
→ MovementFinalize
→ VisualStateResolve
→ AuthorityCommit / ClientPredictionCommit
```

[COMPUTED][HIGH] 方括号阶段由当前testcase/capability显式启用，不是所有SoftPressure行为的固定必经层。

[COMPUTED][HIGH] `MovementFinalize`是普通fixed-step内提交`FCrowdDemoRoundSimStateFragment`的位置；PlanApply和correction只在边界原子恢复状态。

## 4. 群体驱动与个体修正

[INFERRED][HIGH] 当前长期稳定分层为：

```text
宏观Guidance Provider
├── Shared Flow远程导航
├── Target Region分布（可选）
└── 未来自由游荡等群体意图
              ↓
统一Desired/Preferred合同
              ↓
Local Predictive Interaction
              ↓
Particle Constraint
              ↓
最终RoundSim状态
```

[COMPUTED][HIGH] Target Region Transport是目标周围区域分布的可选策略，不是普通移动和窄口移动的统一必经层。它维护共享Cell拓扑、Region供需和短期Edge quota，不生成永久Slot、PositionId或per-agent Region owner。

[COMPUTED][HIGH] Local Predictive使用无场景语义的邻域轨迹、half-plane、component和有限期grant求解局部可执行速度；kernel不读取T3/T4/T5/T6、地图名、Portal或目标标签。

[COMPUTED][HIGH] Particle负责Pair Soft压力、Hard/Swept pair、Obstacle、Bounds和量化后安全闭环，不负责宏观路线、目标人口或窄口调度。

## 5. 双端与复制边界

[COMPUTED][HIGH] Server/Client使用相同fixed-step driver和纯C++ kernels。correction只在fixed-step boundary应用，rollback恢复prepared SoA、累计器、样本和hash后重放。

[COMPUTED][HIGH] 客户端visual只读取client RoundSim、业务visual state和correction offset，不计算gameplay movement。

[COMPUTED][HIGH] Coordinator负责Plan、Result、checkpoint chunks、correction、readiness和紧凑汇总，不应承载Flow、Transport、Local Predictive或Particle算法。

## 6. 性能与视觉连续性合同

[COMPUTED][HIGH] fixed-step driver现在分别记录整步与Shared Flow、Target Topology、Demand、Plan、Guidance、Local Predictive、Particle、Finalize/Commit阶段的p95和max，并记录每个Game Frame执行步数、追帧次数、最大积压、步数上限命中和simulation realtime factor。旧`server_frame_ms_p95`不能再冒充算法CPU成本。

[COMPUTED][HIGH] 静态Target的Polar Topology只在Round内构建一次；后续fixed step复用缓存。静态Demand将不变的Region容量/Desired人口与每步人口、attachment和Supply更新分开；短Plan有效时复用外部Agent的稳定source attachment，只在初始、Plan到期或Plan失效时刷新。

[COMPUTED][HIGH] Shared Flow V2在Topology构建时保存稳定`CellKey→NavigationNodeIndex[]`索引。运行时attachment采样只扫描当前位置周围的cell ring，不再对每个实体、每次采样执行`ring×全NavigationNodes`扫描；候选仍按Node index和StableNodeKey稳定排序。

[COMPUTED][HIGH] 静态Demand轻量更新同时覆盖单cohort与异构Capability cohort；外部cohort人口、半径/HardGap/SoftMargin拥挤事实仍按每步位置重算，不会因缓存而冻结。T6S 8689实测Topology build/hit=`7/6300`、Demand full/update=`7/6300`，Transport五类hash与8688完整BuildDemand路径一致。

[COMPUTED][HIGH] correction先比较snapshot中的完整Agent/业务/TargetApproach状态。完全一致时仍记录合法snapshot hit和correction比较，但不恢复runtime、不改写Mass状态、不回退模拟时钟且不重放fixed step；存在任一差异时继续走原子rollback/apply/replay路径。

[COMPUTED][HIGH] 客户端visual在相邻client RoundSim快照之间做位置/Yaw插值，插值时长同时受模拟时间与空间距离约束；积压目标不得再被强制压缩到最多3个fixed steps内追完。correction offset继续叠加衰减，visual不写gameplay movement。Round切换的稳定初态重置单独计入`round_reset_jump_count`。

[COMPUTED][HIGH] 当前20实体性能硬门为fixed-step p95不超过33.333ms、simulation realtime factor至少0.95、fixed-step上限命中0、client frame p95不超过33.333ms、visual processor p95不超过16.667ms、collapsed steps p95不超过1且非correction视觉跳变为0。max尖峰、catch-up和Round reset跳变必须单独报告，不能被p95掩盖。

## 7. 当前明确技术债务

[COMPUTED][HIGH] 第二版清理已物理删除旧processor、Pipeline prepared/rollback/fixture状态、Mass fragment、Separation/PBD/Traffic/Portal/ORCA/Position/Holding/Elastic/Joint kernel及RVO2适配；加入现行Round、Transport、时间合同与反射运动测试后，当前`CrowdDemo.SF`为34项。

[COMPUTED][HIGH] `FCrowdDemoRoundRules`已移除旧算法设置，旧`TrafficMetrics`已收缩并改名为`SharedFlowMetrics`，RoundResult已移除旧Separation/PBD字段。

[COMPUTED][HIGH] RoundResultHeader已使用版本1 NetSerialize：公共头之后按payload kind传输SoftPressure或Combat聚合结果，二进制payload使用无属性标签布局并压缩；AgentState仍只经checkpoint chunks传输。代表性T6/Combat测试为1193/598字节，真实T1/T5M分别为739/861字节，均低于2048字节硬门。

[COMPUTED][HIGH] Target Region Transport现已拆成不可变短期`FlowPlan`与独立`QuotaExecutionState`。执行态按稳定edge key保存initial/consumed quota，并用短期Agent edge claim保证实体停留在source cell时不会重复领取；真正进入ToCell后才消费一次，随后可继续同一Plan中的下一段。该claim只服务当前短计划，不是永久Slot或Region owner。

[COMPUTED][HIGH] Plan生命周期合同现为：Edge成本刷新只影响下一次求解，不使当前15步Plan失效；结构Cell/Edge失效、Target/Membership变化、寿命到期或执行态非法才触发替换。替换时先迁移仍满足source-cell、Supply和Topology条件的claim，把冻结claim变换为剩余流的起点，再求剩余quota并原子发布Plan与执行态。

[COMPUTED][HIGH] `QuotaExecutionState`已进入Pipeline prepared状态、双端guidance/execution hash和SoftPressure rollback snapshot。旧`ValidatePlanForDemand()`只用于新Plan的初始结构验证；运行中改用execution validation，在Guidance写Preferred前检查claim、剩余quota和当前Supply的下一段可用性。

[COMPUTED][HIGH] 当前fixed-step算法仍在Game Thread串行执行，prepared SoA存在但真实GT/WORK拆分尚未实施。

## 8. 当前验证事实

[COMPUTED][HIGH] 2026-07-18：`git diff --check`通过，Development与DebugGame Editor通过；加入Round时间合同与反射目标运动自动化后的完整`CrowdDemo.SF`为34/34成功，`CrowdDemo.SF.TargetRegionPlanLifecycle`为6/6、Target Region Transport为6/6。多edge执行态、输入乱序、claim迁移、最终Region候选选择、反事实输入反序、rollback replay hash及反射运动边界测试均通过。

[COMPUTED][HIGH] 当前二进制20实体回归新增：T1端口8645双端candidate/applied hash一致并完成全部6阶段；T2端口8636达到handoff/band/settled=`20/20`与coverage=`16/16`；T5 Static端口8638为inside=`20/20`、coverage=`12/12`；T5 Moving端口8646为inside=`20/20`、coverage=`12/12`；T6 Static端口8639 aggregate inside/coverage=`20/20`。这些运行安全与同步门均通过。

[COMPUTED][HIGH] 执行态接入后的T5 Static 8653为inside=`20/20`、coverage=`16/16`、安全/同步错误0，但`settling_steps=-1`，仍未证明稳定静止。T6 Moving 8652完整单轮同样无Fatal/Assertion/Ensure/`LogWindows: Error`/VIOLATION，Particle安全违规0、inside=`20/20`，但coverage仍为`19/20`。

[COMPUTED][HIGH] T6 Moving 8654的聚合Plan rebuild仍为2010，最终缺口仍是Profile `2729413899`的Region 3。移动目标每步改变位置，但本轮`TargetRevision=1`保持不变，TargetRevision重建计数为0；此前把2010次重建归因于TargetRevision是错误结论。

[COMPUTED][HIGH] 生命周期诊断按原因闭合为：Lifetime 229、TargetRevision 0、FeasibleGraph 1559、Membership 0、DemandSatisfied 31、ExecutionInvalid 184、InitialInvalid 7，总计2010。FeasibleGraph替换中1208次只有edge成本变化，351次Cell可行性变化，351次Edge集合变化；Plan年龄p50/p95/max为1/15/15，提前重建1773次。

[COMPUTED][HIGH] 8654累计观察到1080个active claim，其中1033个在新几何中仍有效、851个在新Plan中仍有对应edge，但1033个仍因Plan替换被清空。Execution invalid子类为state mismatch 7、claim off edge 41、quota exceeded 0、Supply无可用出口392、other 3。最终fixture位于step 323，明确关联Profile `2729413899`与最终缺失Region 3；旧edge和claim仍可继承，但该boundary同时出现Supply无出口。

[COMPUTED][HIGH] 上述组合合同已在8658生产运行：重建从2010降到883，原因闭合为Lifetime 238、Target 0、FeasibleGraph 351、Membership 0、DemandSatisfied 51、ExecutionInvalid 236、InitialInvalid 7；active/geometry/supply/new-plan/migrated/completed/dropped claim=`520/480/346/346/346/115/0`。

[COMPUTED][HIGH] 8658仍为inside=`20/20`、coverage=`19/20`，最终缺Profile `2217961739`的Region 13。双端Transport与生命周期hash一致，rollback=`53/0/0/100`，checkpoint/interval error p95=`0cm`，Particle Hard/Swept/Obstacle/Bounds与invalid/fallback均为0。

[COMPUTED][HIGH] 修正诊断口径后，所有346个仍为Supply且edge有效的claim均被迁移，实际有效claim丢失为0；115个旧claim是在替换边界到达ToCell后正常完成。当前最早fixture为step 1、reason=ExecutionInvalid，记录同一缺失Profile的3个Supply无可用出口。

[COMPUTED][HIGH] 8659用只读fixture选择器连续完成两轮，两轮均保持inside=`20/20`、coverage=`19/20`，缺Profile `2217961739`的Region 13；Transport五类hash、生命周期hash=`2238624409`和fixture hash=`4114446767`双端一致，Round 1/2的输入与初态hash一致，rollback分别为`53/0/0/76`与`52/0/0/52`。

[COMPUTED][HIGH] 新fixture位于step 883，selection=`FinalRegionSupplyWithoutOutgoing`，直接观察Region 13 deficit。旧、新Plan都存在通向Region 13的正quota路线；Agent 5的`168→225` claim被迁移，有效claim丢失为0。当前图相对旧Plan发生Cell/Edge/成本变化，旧执行态在当前Supply上出现1个无出口；新Plan同一boundary补入`134→135→167`并保留`168→225→226→227→228`。

[COMPUTED][HIGH] 最后90步中该Region的Demand gap为0、Plan gap为4、Guidance gap为81、retention gap为51，并发生40次进入和38次离开。该证据排除“Region无Demand”“求解器没有路线”和“合法claim再次丢失”作为当前第一原因。

[COMPUTED][HIGH] 8659把候选归因缩到动态Topology切换后的执行连续性与终端保留/实际行程时间；随后已用同一事实窗口分别验证跨图attachment连续性和冻结terminal判定，结果见下。

[COMPUTED][HIGH] 8661完成上述两个只读反事实。Region 13在最后90步中缺失73步，但73步全部存在合法多段在途实体；跨图attachment恢复Guidance次数为0，terminal可冻结转换和恢复次数均为0，人口守恒违规0，反事实outcome=`Neither`。因此两个候选生产分支均被真实运行否定。

[COMPUTED][HIGH] 最终在途实体为Agent 5，剩余edge为2；窗口内剩余edge最小/最大=`2/9`，变化为下降7、增加1、保持64，最终相对目标速度=`301.496cm/s`。该实体没有Local Predictive阻塞或低速，Particle安全与Transport validation仍为0错误。

[COMPUTED][HIGH] Round Plan现显式复制`NominalDurationSeconds`与`CompletionGraceSeconds`。普通SoftPressure保持30秒且无宽限；T6M固定采用30秒名义窗口加15秒完成宽限。宽限只改变验收截止，不改变30Hz、实体/目标名义速度、Transport、Guidance、Particle或网络参数。

[COMPUTED][HIGH] 8662/8665证明把旧单向目标直接延长到45秒会在约31.1秒把半径100cm、HardGap 10cm且Mobility 0的目标代理推到FlowBounds之外，统一Hard closure因此正确判为不可行。T6M现使用通用有界反射直线运动：前30秒轨迹与旧规则相同，到安全运动边界后确定性反向；没有AgentId、ObstacleId或墙边特判。

[COMPUTED][HIGH] 8670连续两轮均达到inside-band=`20/20`、aggregate coverage=`20/20`；7个Capability Profile逐项满足`Coverage=min(AgentCount,FeasibleRegionCount)`，Plan/Guidance unrouted、Transport invalid/validation、final deadlock和Particle Hard/Swept/Obstacle/Bounds/invalid/fallback均为0。

[COMPUTED][HIGH] 8671两轮input hash=`2573893896`、initial-state hash=`3920329045`、Mass AgentState hash=`1956985324`、dynamic-flow round hash=`2219477909`、Particle candidate hash=`2904708453`、Transport五类hash及生命周期hash=`2949210181`均相同，且每轮Server/Client匹配。`particle_applied_state_hash`还覆盖Particle求解参与者中的非Mass目标代理，不能替代只包含实际RoundSim实体的跨轮AgentState hash。

[COMPUTED][HIGH] 8671客户端两轮checkpoint与correction interval位置误差p95均为`0cm`，cross-round error growth=`0cm`；Round 2 rollback summary=`79/0/0/79`且pass=1。两端日志无Fatal、Assertion、Ensure、`LogWindows: Error`或VIOLATION。

[COMPUTED][HIGH] 用户在DebugGame PIE复测中观察到约10多FPS。8696逐阶段证据确认旧实现单个Server fixed-step约`19–31ms`，其中Shared Flow和Finalize各约`6–7ms`；根因是V2 attachment在FlowPreferred与Finalize中各执行一次全图节点搜索。Transport全量Bellman-Ford和周期性source attachment刷新分别形成约`28–47ms`与`7–8ms`尖峰。

[COMPUTED][HIGH] 修复后的DebugGame双端8701通过性能门：fixed-step p50/p95/max=`3.412/9.841/241.154ms`，Shared Flow p95=`0.139ms`，Plan p95=`3.988ms`，realtime factor=`1.000`，步数上限命中0；客户端frame p95=`6.748ms`、visual processor p95=`0.126ms`、collapsed-step p95=`1`、非correction跳变0。T2同时保持handoff/band/settled=`20/20`、coverage=`16/16`、deadlock=0、双端checkpoint匹配。

[COMPUTED][HIGH] 8700首异常witness证明旧视觉插值可在`sim_delta=0`时单帧追赶`52.147cm`，原因是较远插值终点被限制在最多0.1秒内完成。距离感知插值修复后8701该计数为0。约`237ms`客户端启动max和`699.516cm`Round初态reset仍单列，不能由steady p95通过改写成“加载和Round切换完全无跳变”。

[COMPUTED][HIGH] 2026-07-18晚间用户在单进程DebugGame PIE连续复测T2、T6 Transit和T6 Static，客户端frame p95分别出现约`25.1/46.7/55.8ms`；这推翻了“独立Server/Client进程通过即可代表编辑器PIE流畅”的旧外推。PIE在一个Game Thread上串行执行Server与Client两套确定性fixed-step，视觉processor本身仅约`0.12ms`，不是主要瓶颈。

[COMPUTED][HIGH] Shared Flow V2构图已移除两类等价重复扫描：连接区间不再逐厘米重复遍历全部AABB；阻塞cell连通统计不再对每个阻塞cell扫描完整Navigation edge表。T6 DebugGame首步Shared Flow max由`1113.375ms`降至`57.473ms`，Flow hash保持`2421036763`。

[COMPUTED][HIGH] Particle安全链加入两条结果等价快路径：最近量化格点整体已满足Hard/Swept/Environment时不进入组合格点搜索；无正残差且无历史dual时不构建空active-set系统。T6 DebugGame Particle p95由`8.148ms`降至`6.239ms`，整步p95由`16.617ms`降至`13.187ms`，realtime=`1.000`。Development T6整步/Local/Particle p95=`4.132/1.706/1.812ms`，client frame p95=`5.726ms`。

[INFERRED][HIGH] 当前性能结论必须分为两层：独立Development进程技术门已经通过；单进程DebugGame PIE按双端fixed-step约`26.4ms`再叠加编辑器渲染，虽较旧版显著改善但余量不足，必须由用户复测和新的PIE整帧门关闭，不能提前写成完全放行。

[INFERRED][HIGH] T6M的20实体双端技术门与45秒能力门已经关闭；严格30秒快照仍保留为历史19/20证据，不能改写成30秒已通过。当前未完成项是人工审片，以及T5 Static/Moving的连续稳定窗口，不是继续调T6M Transport或Particle。

[COMPUTED][HIGH] 本轮未做人工审片，未运行100/500；不能把编译和纯自动化通过外推为规模能力通过。

## 2026-07-18 T6 出口与客户端验收标记

[COMPUTED][HIGH] 客户端验收标记已接入只读Visual processor。只有启用了TargetApproach且存在真实TargetSlotLayout时，才绘制可解释为“明确最终分配站位”的逐Slot标记：已分配为绿色、未分配功能Slot为黄色、未分配填充Slot为青色。

[COMPUTED][HIGH] Target Region Transport不拥有永久Agent→Position分配，因此不得把Region中心伪装成逐实体最终位置。该模式只绘制目标最小/最大有效距离环、Region扇区边界和环境可行terminal cell；它们表示合法落位范围与可行入口，不表示某一实体必须占据某一点。

[COMPUTED][HIGH] T4/T6 Transit的出口合同已改为群体出口稳定：实体先通过各自完成平面；全部实体完成后，下一fixed-step统一停止宏观Flow推进；连续15步速度不高于10cm/s才记为final settled。该合同不把通道出口误写成Target Region站位，也避免早到实体在出口立即停车阻塞后到实体。

[COMPUTED][HIGH] 8707 T6 Transit原P0单轮达到wall/corridor/completed/final-settled=`20/20/20/20`、group completion/settled step=`298/329`、final deadlock=0，server/client progress hash=`3541423868`一致；Particle与Obstacle安全错误、correction miss/mismatch及位置误差均为0，fixed-step p95=`2.277ms`、realtime=`1.000`。

[COMPUTED][HIGH] 8708 T6 Static在既有90-step Target Stability窗口中达到inside minimum=`20`、coverage=`20/20`、contended/merge-blocked/terminal-chatter=`0/0/0`、position peak-to-peak p95/max=`0/0cm`，server/client stability hash=`2774153237`一致。该结果证明技术稳定窗口通过；客户端标记的实际画面仍需用户在可见PIE中人工确认。

## 9. T6A 组合能力、远程保持与朝向合同

[COMPUTED][HIGH] `HeterogeneousTransit`不再在走廊出口永久停车。其生产顺序为`Corridor Transit → Target Region Transport → Distance Band/Engaged Hold → Particle safety`；Target Region stages只在20个实体全部完成走廊后启用，T4纯Transit行为保持不变。

[COMPUTED][HIGH] Ranged Capability使用`AcquireThenHold`：实体只有在上一boundary已是满足Region需求的`TerminalStay`且不是超额`Supply`时取得保持；目标靠近使其低于Minimum时不主动后退；超过Maximum+100cm或所在Region变为超额Supply时释放并重新分流。该状态位于per-cohort runtime，随SoftPressure snapshot原子回滚。

[COMPUTED][HIGH] 朝向由独立纯`CrowdDemoFacingKernel`和`RoundFacingResolve` processor处理。最终稳定前只消费Flow/Transport/业务层自主Preferred，不读取Local Predictive、Particle或Obstacle修正向量；连续15步满足终态低速与低修正后才以360度/秒确定性限速朝向目标。`MovementFinalize`仍是RoundSim Yaw唯一提交点，客户端只显示client sim state。

[COMPUTED][HIGH] 8716诊断证明旧取得条件会把Agent 4/5冻结在Profile `2372223050`的超额Region 9，形成`3人/需求1人`并留下Region 7/8空缺。加入Supply资格失效后，8719双端达到corridor/inside/coverage=`20/20/20`、max Region population=`1`、五类Transport hash匹配、Particle四类安全违规与invalid/fallback=`0`、checkpoint/interval/yaw/velocity error p95=`0`、agents=visible=`20/20`。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
