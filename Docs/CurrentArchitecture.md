# MassAI Crowd Demo 当前架构

## 1. 文档职责

[COMPUTED][HIGH] 本文件只描述当前生产代码、数据所有权和已验证边界。阶段计划查阅`PhasePlan.md`，验收状态查阅`FeatureChecklist.md`与`TestScenarioMatrix.md`，退出实验与旧端口查阅`Docs/History`。

## 2. 正式场景与当前生产链

[COMPUTED][HIGH] 顶层场景只保留`SimRoundObstacle=0`与`SimRoundSoftPressure=1`；T1–T8是SoftPressure下的能力测试，不是八套独立模拟架构。

```text
RoundPlanApply
→ Business / Target Fact Prepare
→ SharedFlowFieldBuild
→ Flow Guidance Candidate
→ [可选] Target Topology / Demand / Plan / Guidance Candidate
→ [可选] Business Guidance Candidate
→ Guidance Compose
→ Local Predictive
→ MovementPredict
→ Particle Safety / SF1 Obstacle Constraint
→ Facing
→ MovementFinalize
→ Authority Commit / Client Prediction Commit
```

[COMPUTED][HIGH] Guidance provider固定为`BusinessOverride > TargetRegion > SharedFlow > Stop`；只有`Guidance Compose`写最终宏观自主速度，不再依靠processor先后覆盖同一Intent。

[COMPUTED][HIGH] Local Predictive只消费Composed Guidance并输出局部可执行速度；Particle只处理预测位置的Soft/Hard/Swept/Obstacle/Bounds约束；Facing在稳定前只消费自主速度，不读取局部避让或Particle修正向量。

[COMPUTED][HIGH] `MovementFinalize`是普通fixed-step中`FCrowdDemoRoundSimStateFragment`的唯一写入点；它先由Runtime WORK构建全局稳定Commit plan并预验证完整AgentId/Lifecycle集合，再在同一GT boundary同步发布Runtime movement/state与Demo checkpoint兼容镜像。PlanApply与correction只在boundary恢复或应用状态。

## 3. GT / WORK与数据所有权

[COMPUTED][HIGH] Coordinator只负责RoundPlan、版本化RoundResultHeader、checkpoint/correction chunks、readiness和紧凑汇总，不承载Flow、Transport、Local Predictive或Particle算法。

[COMPUTED][HIGH] Guidance Compose、Local Predictive、MovementPredict、Particle与Facing已经使用不可变POD输入在WORK线程执行；GT等待完成并验证完整AgentId/result集合后统一写回对应Mass fragments。Compose、Local Predictive、Particle与Facing只调用Core纯kernel，MovementPredict是Runtime的确定性POD积分阶段。

[COMPUTED][HIGH] `MassCrowdRuntime`现已建立`UMassCrowdMovementTrait`、Base Movement九个fragment，以及按CapabilityProfile稳定分批的Gather、全局AgentId稳定Merge、Lifecycle全量预验证和Commit映射。Demo实体模板已并行加入plugin tag及identity/state/properties/guidance candidates/composed guidance/local velocity/particle constraint/facing/movement output fragments。

[COMPUTED][HIGH] 每个fixed-step在Plan/correction边界处理完成后先构建Runtime基础snapshot；Shared Flow、Target Region与Business分别发布按AgentId排序的不可变Guidance overlay。`FCrowdMassRuntimeBridge::BuildGuidanceRecords`把基础snapshot与三类overlay稳定合并为Compose WORK记录，要求Shared Flow覆盖全部Agent，并拒绝重复provider/Agent、错误revision或错误provider。WORK完成且全量AgentId/result集合验证通过后，GT一次更新Runtime composed与同源prepared SoA；旧Demo composed与MoveIntent已删除。Runtime结果、overlay与基础snapshot都是可重建派生状态，correction rollback后从恢复的权威事实重新生成，不复制进rollback snapshot。

[COMPUTED][HIGH] Local Predictive不再重新读取Runtime identity/state/properties/composed fragments来构建WORK输入；它直接消费同一boundary基础snapshot与prepared Core composed结果。Runtime WORK调用Core kernel并输出pair、grant、result、summary和可选trace。Mass查询只保留完整结果集合验证与Runtime local-velocity发布；Demo Pipeline继续保存grant、diagnostic和rollback状态，旧Demo local-velocity fragment已删除。

[COMPUTED][HIGH] MovementPredict直接消费boundary snapshot、prepared composed与prepared local-velocity结果；仅通过Mass查询叠加T1 boundary freeze和业务垂直运动事实，随后由`FCrowdMassMovementPredictWork`稳定排序、选择自主/局部速度、限制局部速度并积分预测位置。其prepared预测结果是Particle的正式输入，不再由Particle重新读取Runtime identity/properties镜像拼装基础事实。

[COMPUTED][HIGH] Particle Safety从同一boundary snapshot与prepared MovementPredict结果构造Core agent；`FCrowdMassParticleWork`执行Core Solve和applied-state安全复验，输出candidate/applied summary、hash、pair、result及可选trace。GT保留诊断、路线/稳定指标和T1状态采集，只发布Runtime particle与同源prepared结果；旧Demo particle fragment已删除。

[COMPUTED][HIGH] Facing现由`FCrowdMassFacingWork`在WORK线程调用Core kernel；它消费boundary snapshot、prepared composed与prepared particle结果。GT只从Facing fragment读取上一boundary settled计数，完整校验结果集合后同步发布Runtime Facing与Demo Facing兼容镜像；Movement Finalize只消费Runtime Facing结果。

[COMPUTED][HIGH] Shared Flow生产构建和Preferred生成现由`FCrowdMassSharedFlowWork`在WORK线程调用Core kernel。权威对象是Runtime定义的不可变Flow resource、动态anchor及T3双cohort resource，当前实例仍由Demo Pipeline迁移期托管；Demo `FCrowdDemoSharedFlowField`只作为Transport、末态指标和rollback迁移期结构镜像，不再执行生产Build或运动Guidance采样。GT准备Config、Target位置和每Agent停止/Goal事实，WORK稳定排序后输出Flow sample与SharedFlow candidate，GT在完整AgentId集合校验后发布Runtime candidate和Demo FlowSample阶段事实，不再发布Demo GuidanceCandidates。

[COMPUTED][HIGH] Target Region生产Topology、Demand、短期Plan、quota/claim execution、validation与Guidance现由`FCrowdMassTargetRegionWork`在线程池调用Core kernel。Target Demand已从统一基础snapshot和prepared Flow输出构建Agent输入，不再重复读取Mass；其余Demo processor仍负责场景规则准备、等待WORK、验证结果并发布兼容镜像。Demo Target Region kernel已退出这些生产调用。Plan与quota execution作为同一份状态进出WORK，转换后的Demo镜像继续服务Round指标、生命周期诊断、资源引用rollback及客户端调试绘制，当前尚未把这些宿主职责迁入Runtime subsystem。

[COMPUTED][HIGH] Runtime movement output已经接入正式Finalize与Commit：Particle或SF1 Obstacle阶段发布prepared final kinematics，Facing发布prepared yaw；`FCrowdMassMovementFinalizeWork::BuildInputFromPrepared`将这些结果与boundary snapshot稳定组装，再按CapabilityProfileKey分批生成Movement并全局按AgentId稳定Merge。GT不再为Finalize输入执行第一遍全实体Mass Gather，但仍在任何写入前对完整AgentId/Lifecycle及Runtime Facing/Particle或SF1 Obstacle结果执行原子预验证，随后同步更新Runtime simulation/movement与Demo RoundSim最终checkpoint状态。Authority/Client Commit均从Runtime MovementOutput写Transform、Velocity和Demo Movement，并校验其与RoundSim状态一致。

[COMPUTED][HIGH] Demo仍持有`MovementFinalize` processor外壳和Round指标/rollback采集，因此这不是整个Pipeline移出Demo；但最终运动事实已经只由Runtime Commit plan产生，Demo不再独立重算另一份最终Movement。

[COMPUTED][HIGH] 第十二切片将Compose、Local Predictive与MovementPredict合并为`FCrowdMassMovementPipelineWork`。GT从同一boundary snapshot、三类Guidance overlay、Local公平历史、T1 boundary fact和Reactive垂直运动事实构建一份按AgentId排序的不可变输入；一个ThreadPool任务内部顺序调用原有三个Runtime纯阶段；完整结果集验证通过后，GT在一次Mass查询中同步发布Runtime identity/state/properties/candidates/composed/local与Demo ProposedMovement。三个旧processor实现已物理删除，不存在逐阶段`Async→Get→GT发布→再Gather`链。

[COMPUTED][HIGH] 合并WORK仍保留三个独立阶段hash与诊断语义；性能归类把任务内部Compose耗时记入GuidanceCompose，其余GT准备、Local Predictive与MovementPredict记入LocalPredictive。计时值不进入确定性hash。

[COMPUTED][HIGH] 当前尚未达到“整个boundary只读取Mass一次”：基础运动事实及Compose→Local Predictive→MovementPredict→Particle/Obstacle→Facing→MovementFinalize的WORK输入链已收敛到同一snapshot/prepared链，但Business状态准备、T1/诊断/累计器读取、原子镜像预验证、阶段兼容写回及最终业务采集仍由分阶段GT processors处理；最终Mass archetype也尚未按能力拆分。因此不能宣称完整GT/WORK迁移完成。

[COMPUTED][HIGH] Rollback snapshot已停止复制Target topology、demand、guidance和短期plan大数组；短期plan以资源key引用不可变资源，snapshot只保存quota/claim等可变执行态、累计器和游标。恢复后按Target Fact重建派生结果。

[COMPUTED][HIGH] Server与Client运行同一fixed-step driver和纯C++ kernels；客户端visual只读取client RoundSim/visual state并提交ISM，不计算gameplay movement。

## 4. 已删除的当前兼容面

[COMPUTED][HIGH] TargetApproach、TargetSlotLayout、旧Polar Density及其execution diagnostic已从settings、fragment、kernel、processor、rollback、metrics、CLI与自动化中删除；Target Fact已提取为独立纯kernel。

[COMPUTED][HIGH] RoundResultHeader使用contract v2版本化NetSerialize；仅Server本地消费的Performance汇总不再进入复制payload，AgentState仍只经100-agent checkpoint chunks传输。高熵异构自动化为1566字节，8790真实T6M运行时为1970字节，均低于2048字节硬门且无Native NetSerialize Warning。

## 5. 当前验证事实

[COMPUTED][HIGH] 第十二切片通过Development、DebugGame Editor（均`-DisableUnity`）、`CrowdDemo` 105/105与`MassCrowd` 13/13。新增`MassCrowd.Runtime.MovementPipelineWork`证明合并前后三个阶段hash相同、输入反序不变且重复overlay被拒绝。8723 T2、8724异构T6、8725 T1和8726 T8均通过双端安全、同步与性能门；fixed-step p95分别为`3.849/5.337/1.649/2.131ms`，agents/visible均为`20/20`，correction位置/速度/Yaw误差均为0。

[COMPUTED][HIGH] 2026-07-22：Development、DebugGame Editor（均使用`-DisableUnity`）、`git diff --check`、当前105/105项`CrowdDemo`自动化及13/13项`MassCrowd`插件自动化通过。Runtime测试覆盖Target Region四阶段WORK、统一boundary输入、合并Movement Pipeline、MovementPredict语义、输入反序、重复Agent/overlay拒绝及旧/Core/Runtime等价。

[COMPUTED][HIGH] 8663 T2生产回归通过Runtime Finalize/Commit链：handoff/inside/terminal=`20/20/20`、Region coverage=`16/16`、安全与invalid/fallback为0、双端candidate/applied hash匹配、correction位置/速度/Yaw误差均为0；fixed-step p95=`3.529ms`、Commit p95=`0.021ms`。8664 SF1 Single authority路径运行未出现VIOLATION；该短运行未覆盖完整RoundResult。

[COMPUTED][HIGH] Facing生产迁移后的8665 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`、双端Yaw误差为0；fixed-step/Commit p95=`3.638/0.020ms`。8666 SF1 Single无Particle路径也完成Facing→Finalize→Authority Commit且无VIOLATION。

[COMPUTED][HIGH] Shared Flow生产迁移后的8667 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`、Flow/Transport双端hash一致、Hard/Swept/Obstacle/Bounds与invalid/fallback为0；fixed-step/Flow p95=`3.166/0.264ms`。8668 SF1 Single authority烟雾确认build hash=`267519150`、rebuild=`1`且无Fatal/Assertion/Ensure/`LogWindows: Error`/VIOLATION；该短运行没有完成整轮路线验收。

[COMPUTED][HIGH] Target Region Runtime接管后的8669 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`、plan/guidance unrouted及validation failure为0，五类Target Region hash双端一致；fixed-step/Topology/Demand/Plan/Guidance p95=`4.061/0.012/0.198/1.252/0.221ms`。8671异构T6 Static覆盖7个Capability cohort，inside-band=`20`、feasible coverage=`20`、unrouted/invalid/validation failure=`0`，五类hash双端一致，fixed-step p95=`6.552ms`。两次运行均为20/20可见、安全违规0且性能门通过；correction使用零误差快速路径，未实际触发历史重放。

[COMPUTED][HIGH] Guidance overlay与Local Predictive统一输入第二切片通过Development、DebugGame、`MassCrowd` 12/12及`CrowdDemo` 102/102。8677 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`，Compose/Local Predictive双端hash一致，fixed-step p95=`3.854ms`；8678异构T6保持inside/coverage=`20/20`、unrouted/invalid/validation failure=0、五类Transport及Compose/Local Predictive hash一致，fixed-step p95=`5.265ms`。两次运行安全、同步、可见实例与性能门通过。

[COMPUTED][HIGH] MovementPredict、Particle与Facing统一输入第三切片通过Development、DebugGame、`MassCrowd` 12/12及`CrowdDemo` 102/102。8681 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`，安全与双端hash通过，fixed-step p95=`5.379ms`；8682异构T6保持inside/coverage=`20/20`、安全、同步和五类Transport hash通过，fixed-step p95=`6.598ms`。8683 SF1 smoke保持Flow hash=`267519150`、rebuild=`1`且无硬错误；该短运行未完成整轮。

[COMPUTED][HIGH] MovementFinalize统一输入第四切片通过Development、DebugGame、`MassCrowd` 12/12及`CrowdDemo` 102/102。8684 T2保持handoff/inside/terminal=`20/20/20`、coverage=`16/16`，fixed-step p95=`4.393ms`；8685异构T6保持inside/coverage=`20/20`，fixed-step p95=`5.438ms`。两者安全、同步、prepared/兼容镜像原子门与性能门通过。8686 SF1 smoke保持Flow hash=`267519150`、rebuild=`1`且prepared Obstacle→Finalize路径无硬错误；短运行未完成整轮。

[COMPUTED][HIGH] MovementFinalize查询职责第五切片已将写前原子一致性检查与提交后业务/指标采集拆为`ValidationQuery`和`ApplyMetricsQuery`。前者只读取身份及Demo/Runtime的Particle、Facing、Obstacle镜像；后者不再读取已由prepared链替代的MoveIntent、Runtime properties、Runtime Particle和Runtime Facing。两段仍在同一processor和同一fixed-step boundary内执行，写前完整集合验证与失败时零部分写入语义保持不变。Development、DebugGame、`MassCrowd` 12/12及`CrowdDemo` 102/102通过；8687 T2保持terminal=`20/20`、coverage=`16/16`，fixed-step p95=`4.149ms`；8688异构T6保持inside/coverage=`20/20`，fixed-step p95=`5.683ms`。8689 SF1 smoke保持Flow hash=`267519150`、rebuild=`1`且无硬错误。

[COMPUTED][HIGH] 第六切片已把第五切片的`ApplyMetricsQuery`彻底拆成`MovementFinalize::ApplyQuery`与独立`PostFinalizeMetricsProcessor`。Finalize现在只执行prepared Commit plan的Demo/Runtime状态原子写入；post-finalize只读最终状态并采集Flow路线、T1/T3/T4进度、SoftPressure rollback和Combat snapshot，且仍位于VisualResolve之前，因此采样语义未改变。Pipeline为每个boundary记录Finalize成功step；post-finalize和Authority/Client Commit均以该标记为门，Finalize失败时不得对旧状态生成新snapshot或提交旧Movement。该职责拆分增加一次20实体只读查询，尚不是最终单次Mass读取实现。

[COMPUTED][HIGH] 第六切片通过Development、DebugGame、`MassCrowd` 12/12和`CrowdDemo` 102/102。8693 T2保持terminal=`20/20`、coverage=`16/16`，fixed-step p95=`4.074ms`，client Game/Render/GPU p95=`2.790/4.963/4.593ms`；8694异构T6保持completed/settled/inside-band/coverage=`20/20`，fixed-step p95=`5.073ms`，client Game/Render/GPU p95=`3.451/5.068/4.712ms`。8695 SF1 smoke保持Flow hash=`267519150`、rebuild=`1`且无硬错误。

[COMPUTED][HIGH] 第七切片继续收缩`PostFinalizeMetricsProcessor`查询：FormationIndex与checkpoint `RadiusCm`由boundary formation facts提供，Composed Guidance由prepared Runtime结果转换；post-finalize不再读取Formation、Composed Guidance、Particle Properties或未使用的Particle Constraint fragment。`RadiusCm`与Particle `PhysicalRadiusCm`保持不同语义，禁止互相替代。

[COMPUTED][HIGH] 首次8697异构T6回归因误把Particle `PhysicalRadiusCm`写入rollback checkpoint的`RadiusCm`，从第一帧correction起出现agent mismatch；该实现已修正为保存精确Formation radius。8698随后达到rollback hit/miss/mismatch=`80/0/0`、inside/coverage=`20/20`、fixed-step p95=`6.540ms`且双端无硬错误。8699 T2保持terminal/inside=`20/20`、coverage=`16/16`、rollback=`54/0/0`、fixed-step p95=`4.322ms`；8702 SF1保持Flow hash=`267519150`、rebuild=`1`。

[COMPUTED][HIGH] 第八切片又删除post-finalize对FlowSample与ObstacleConstraint fragment的读取。rollback FlowSample从同一prepared Runtime Shared Flow输出重建；SF1 penetration按原定义从boundary起点与Finalize终点复验膨胀障碍。8703异构T6 rollback=`80/0/0`、inside/coverage=`20/20`、fixed-step p95=`4.595ms`；8704 SF1保持Flow hash=`267519150`、rebuild=`1`且无硬错误。

[COMPUTED][HIGH] 第九切片删除post-finalize对GuidanceCandidates与Facing fragment的读取。完整rollback GuidanceCandidates由boundary snapshot和Flow/Target/Business prepared overlay通过Runtime Bridge重新构建；Facing processor在发布Runtime结果时同步保存包含连续settle计数与最终资格的精确rollback fact，post-finalize只按AgentId消费该prepared事实。Development、DebugGame、`MassCrowd` 12/12与`CrowdDemo` 102/102通过；8705异构T6 rollback hit/miss/mismatch=`80/0/0`、inside/coverage=`20/20`、fixed-step p95=`4.551ms`且双端无硬错误；8706 SF1保持Flow hash=`267519150`、rebuild=`1`且无硬错误。

[COMPUTED][HIGH] 第十切片把T1 OpenSpawn状态收敛为PipelineSubsystem中的唯一运行时权威状态，并为每个boundary生成按AgentId稳定排序的prepared事实；MovementPredict、Particle和客户端视觉只消费该事实或唯一runtime，已物理删除`FCrowdDemoOpenSpawnRelaxationFragment`。pending reset在完整集合验证后原子消费，重复、缺失、错位或过期事实均拒绝。

[COMPUTED][HIGH] Combat/Visual rollback现在采用两阶段完成门：PostFinalize只记录Identity与最终RoundSim movement facts；VisualStateResolve完成最终Health、BusinessState、AttackPhase、Reactive、HitFlash和VisualState事实。snapshot只有在`MovementFactsComplete && CombatFactsComplete`后才进入`SnapshotReadyForReplay`，不完整snapshot不得用于correction replay或checkpoint发布。

[COMPUTED][HIGH] `PostFinalizeMetricsProcessor`当前Mass requirements仅为`FCrowdDemoMassIdentityFragment`与只读`FCrowdDemoRoundSimStateFragment`；结构自动化同时禁止OpenSpawn及六个Combat/Visual fragment回流。Development与DebugGame Editor（`-DisableUnity`）、T1 4/4、Combat 15/15、结构1/1、`MassCrowd` 12/12及完整`CrowdDemo` 105/105通过。

[COMPUTED][HIGH] 默认Unity Development仍在未由本切片修改的`MassCrowdSimulation`插件旧`.cpp`中因匿名命名空间辅助函数重名失败；当前验证使用`-DisableUnity`。该构建兼容性债务未被运行回归掩盖，也不应写成第十切片行为回退。

[COMPUTED][HIGH] 第十切片双端运行：8707 T1保持六阶段、传播与settling合同，Particle安全违规和invalid/fallback均为0，rollback hit/miss/mismatch=`53/0/0`，agents/visible=`20/20`，fixed-step p95=`1.851ms`；8709异构T6保持inside/coverage=`20/20`、rollback=`53/0/0`及五类Transport hash一致，fixed-step p95=`6.334ms`；8708 T7与8710 T8均无安全、同步或snapshot完整性错误，T8 attack/projectile/event hash双端一致，fixed-step p95分别为`2.559/2.215ms`。8714 SF1短时smoke保持Flow hash=`267519150`、rebuild=`1`；该smoke未完成整轮。

[COMPUTED][HIGH] 第十一切片物理删除六个已被Runtime权威状态取代的Demo迁移镜像：`RoundMoveIntent`、`RoundGuidanceCandidates`、`RoundComposedGuidance`、`RoundLocalVelocity`、`RoundParticleConstraint`与`RoundFacing` fragments。Mass template、spawn初始化、processor requirements/写入、派生rollback副本及旧`BuildGatherRecord()`适配入口均同步删除；Facing连续settle状态改由`FCrowdMassFacingFragment`持有并由rollback显式恢复。MovementFinalize直接验证Runtime Facing/Particle，不再做Runtime↔Demo派生镜像一致性检查。

[COMPUTED][HIGH] 本切片没有删除`RoundProposedMovement`、`RoundFlowSample`或`RoundObstacleConstraint`：三者分别承载MovementPredict阶段传输/诊断、Transport与路线/rollback事实、SF1正式障碍安全结果，不是Runtime结构的逐字段重复。`RoundSimState`仍是checkpoint、网络结果和Demo指标所需的最终提交状态，也不是待删迁移镜像。

[COMPUTED][HIGH] 第十一切片通过Development与DebugGame Editor（`-DisableUnity`）、结构删除与Runtime适配器等价测试、`CrowdDemo` 105/105及`MassCrowd` 12/12。8722 T2保持terminal/inside=`20/20`、coverage=`16/16`、安全与双端hash通过，fixed-step/client frame p95=`4.028/5.248ms`；8717异构T6保持completed/settled=`20/20`、七类Capability及Transport/T6 hash一致，fixed-step/client frame p95=`4.556/5.481ms`；8718 T1与8719 T8分别为`1.641/2.025ms`，T8三类业务hash双端一致。8721 SF1 smoke保持Flow hash=`267519150`、rebuild=`1`，该短运行未完成整轮。

[COMPUTED][HIGH] 收敛后20实体fixed-step p95：T1=`1.131ms`、T2=`3.073ms`、T3=`2.938ms`、T4=`3.376ms`、T5S=`5.362ms`、T6A=`3.114ms`、T6S=`4.261ms`、T7热复跑=`1.739ms`、T8=`1.598ms`；这些通过运行的realtime均不低于1.000，step-limit hit为0。

[COMPUTED][HIGH] T1的staging/active测试布局切换现已显式归类：普通`non_correction_discontinuity=0`，测试boundary reset jump=21，Round reset jump=20；不得把测试夹具换布局混入普通移动连续性失败。

[COMPUTED][HIGH] 客户端性能窗口现从Round 1实际激活开始，并分别记录Game、Render、GPU、shader compile、async loading、visual asset compiling与PSO precache；启动热身单独统计，不再与Round 1混算。

[COMPUTED][HIGH] T7首次冷运行8777出现client frame p95=`112.235ms`、collapsed steps p95=`4`；新增证据后的两次普通运行8781/8783连续通过，frame p95=`6.016/5.820ms`，Round内shader/loading/PSO帧均为0。`-noshaderddc`控制运行8782因shader job超过60秒而未进入场景，只证明冷资源门可以阻塞ready，不能事后归因为8777的唯一根因。

[COMPUTED][HIGH] T5M 8785通过安全、同步、Transport与性能门：fixed-step p95=`6.196ms`，稳定诊断`valid=1`且无merge block/chatter；移动目标窗口`settled_windows=0`、相对位置peak-to-peak p95=`125.431cm`，所以它是稳定追随技术通过，不是静止落位结论。

[COMPUTED][HIGH] 8788把T6M诊断总窗口增加到60秒后，最终inside/coverage仍为`16/20`；因此“只因45秒不足”已被反驳。8789生命周期诊断记录1591次重建，所有634个具备新Plan继承资格的claim均完成迁移，`dropped_still_feasible=0`；主要变化来自真实环境图变化与path/execution invalid，不是claim迁移丢失。

[COMPUTED][HIGH] 8788/8789进一步确认`guidance_mode=3`为显式`EngagedHold`而非Terminal guidance意外清零。旧实现把Hold固定为世界坐标零速，强于“目标靠近时不主动后退”的设计。8790改为通用单向Hold：抑制目标向实体靠近的径向分量，保留切向运动及目标远离时的跟随分量。

[COMPUTED][HIGH] 8790原P0 T6M的Round末inside/coverage恢复为`20/20`，Hard/Swept/Obstacle/Bounds、invalid/fallback、双端hash、correction均通过；fixed-step p95=`12.137ms`，client Game/Render/GPU p95=`10.332/6.852/5.802ms`。最后90步诊断仍记录terminal population最低`18/20`、Region coverage最低`17/20`、particle settled window=0；这些值保留为移动目标过程诊断，不再作为AcquireThenHold实体的持续重排硬门。

[COMPUTED][HIGH] 用户确认的T6M终态合同为：实体取得正确Terminal并进入AcquireThenHold后，只要交互资格仍有效就不主动换Region；目标靠近时不主动后退，切向运动与目标远离跟随仍可执行。当前Demo的资格失效条件为Region变成Supply、策略不再是AcquireThenHold，或距离超过`Maximum + 100cm`释放滞回；目标Actor销毁/业务Target丢失尚未形成Demo运行场景，不能写成已验收。

## 6. 准确停止点

[COMPUTED][HIGH] 可复用产品边界现由`Docs/MassCrowdSimulationPluginArchitecture.md`定义。阶段1插件骨架与阶段2纯Core迁移已完成；阶段3已把Shared Flow、Target Region、Guidance Compose、Local Predictive、Particle Safety、Facing以及最终Movement组装/稳定Merge/Commit切到Runtime WORK。各阶段Demo processor外壳、Round指标、诊断及rollback协调仍属于Demo宿主。

[COMPUTED][HIGH] 单boundary Gather前十一切片已接入：`TryBeginFixedStep()`成功后由专用GT processor一次读取身份、FormationIndex、Formation Radius、RoundSim位置/速度/Yaw、Movement速度上限和Particle半径/间隔/Mobility，构建Runtime `FCrowdMassBoundarySnapshot`及最小Demo formation facts。Flow、Target与Business发布Guidance overlay；Runtime稳定合并后，Compose、Local Predictive、MovementPredict、Particle/Obstacle、Facing与MovementFinalize从snapshot/prepared链构建WORK输入，不再为这些基础事实重复读取Mass fragments。Finalize写入与Demo业务/诊断采集现已成为两个明确processor；六个迁移期Demo运动镜像已删除，后者仍需一次只读Mass遍历实际提交的Identity/RoundSim状态。

[COMPUTED][HIGH] 该snapshot只保存boundary开始时的基础运动事实；Target Fact、Combat、Business override、Transport plan/guidance、Local Predictive与Particle等依赖前序阶段的派生事实仍按原processor顺序产生。correction恢复后重新执行Gather，因此snapshot不是rollback权威状态，也不进入rollback副本。

[COMPUTED][HIGH] RoundResultHeader运行时门已关闭；T6M按AcquireThenHold资格保持合同技术放行。最后90步严格Region窗口继续保留为观察指标，不再要求已接战实体追随移动目标持续重排。单进程DebugGame PIE、当前版人工审片、所有业务事实单次Mass读取/统一原子提交和按能力archetype拆分仍未执行。

[INFERRED][HIGH] 下一步只继续收敛Particle、Facing与Finalize周围剩余GT发布接缝；不得同时进入Mass archetype拆分。Identity与最终RoundSim state仍是post-finalize对实际提交状态采样的必要事实，不应为追求形式上的零查询复制第二份权威状态。当前整个boundary仍不是单次Mass读取，20实体证据不得外推为100/500能力。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
