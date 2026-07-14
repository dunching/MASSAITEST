# MassAI Crowd Demo 阶段计划

## 当前停止点：Small step 155 的环境容量不足

[COMPUTED][HIGH] 8368完整20实体fixture、环境感知Hard/Swept闭环、独立SoftPressure rollback、candidate/applied hash v2、settling公共tracker和强化能力测试已经完成。Particle 17/17、完整`CrowdDemo.SF` 42/42、Development与DebugGame Editor均通过。

[COMPUTED][HIGH] 8371真实网络运行证明rollback已生效：10次snapshot hit、10个replayed step、miss/mismatch=`0/0`，correction与checkpoint p95=`0cm`；candidate/applied hash双端一致。

[COMPUTED][HIGH] Small能力门在fixed-step 155失败。首fixture Agent 5/13仅有`93.021cm`，而硬门为94cm；固定8轮沿当前分离法线可实现的环境容量不足，因此明确invalid。该结果不授权调参、放宽半径或增加迭代，但暴露了当前solver没有统一表达环境Soft压力和一般二维切向自由度。

[COMPUTED][HIGH] 因Small安全门失败，删除阶段取消。SF3/SF4必须保留；Small连续两轮、独立Round 1、录像、人工审片以及清理后回归均未执行。本任务准确停止在删除门之前。

[INFERRED][HIGH] 后续实施按下述统一粒子计划推进，并以Agent 5/13完整fixture为生产第一红测。不能把沿单一分离法线容量不足事后解释为Soft response、rollback或hash问题，也不能为该fixture编写AgentId、ObstacleId或场景专用分支。

## 下一代码阶段：统一粒子—环境Soft与二维Hard共同闭环

### 阶段A：先固定通用合同与失败fixture

- [INFERRED][HIGH] 增加粒子—静态环境双阈值测试：`WallHardDistance = Radius + HardGap`，`WallSoftDistance = WallHardDistance + SoftMargin`；Soft残差不得影响valid，Hard残差必须失败。
- [INFERRED][HIGH] 增加1:3质量、零Mobility墙体、单侧垂直墙、单侧斜墙、双侧墙、窄口、靠墙多粒子压力传播和8371完整20实体重放。测试名称描述几何fixture，不进入生产场景parser。
- [INFERRED][HIGH] 对每个fixture分别断言Soft压力方向、质量分配、墙面法向禁止、切向保留、Hard/Swept/Obstacle/Bounds最终安全和输入反序hash一致。

### 阶段B：补齐环境Soft压力

- [INFERRED][HIGH] 从排序ObstacleSpecs和FlowBounds生成稳定环境contact；每个contact提供稳定EnvironmentId、最近点、外法向、HardDistance、SoftDistance和当前SoftError。静态环境Mobility固定为0。
- [INFERRED][HIGH] 环境Soft只在主Soft阶段按同一fixed-step response施加有限法向修正，不进入最终Hard安全闭环，也不因未恢复WallSoftDistance使candidate invalid。
- [INFERRED][HIGH] 环境Soft必须使用经过Obstacle/Bounds约束后实际实现的位移记录指标；被墙体截断的尝试修正不能计为已传播压力。
- [INFERRED][HIGH] RoundResult新增独立的wall soft contact/error/correction指标，不能把它们混入Hard penetration或粒子pair SoftError。

### 阶段C：统一二维Hard共同可行闭环

- [INFERRED][HIGH] 将当前“pair径向投影后再Obstacle reproject”改为同一局部接触分量内的确定性约束闭环：粒子Hard/Swept约束与环境法向Hard约束共同参与，墙面切向保持自由。
- [INFERRED][HIGH] 第一版应复用稳定spatial grid、AgentId/pair排序、固定迭代和1cm量化；可采用带残差记忆的确定性投影或小分量二维可行性求解，但必须先由纯fixture证明不会因约束顺序遗漏已知可行解，再选择生产算法。
- [INFERRED][HIGH] 当某一实体的质量份额受环境截断时，未实现残差回到局部接触分量，按Mobility重新分配；不得只在原pair两端或单一normal上循环，也不得把实体推入墙体。
- [INFERRED][HIGH] 若固定约束集合不存在共同解，则返回稳定invalid和最小fixture；全体Start仍只作为紧急安全输出，不计为能力通过。

### 阶段D：生产接入与门控

- [INFERRED][HIGH] 保持现有`FlowPreferredVelocity → MovementPredict → ParticleConstraintSolve → MovementFinalize`处理器边界；粒子/环境contact与局部共同求解全部留在纯C++ kernel或prepared SoA，不把算法塞进Coordinator。
- [INFERRED][HIGH] candidate hash覆盖环境Soft设置、排序contact及共同闭环事实；rollback恢复新增累计器、样本和fixture。客户端继续只消费client sim state。
- [INFERRED][HIGH] 先运行纯fixture、Development、完整`CrowdDemo.SF`和原P0 Small单轮。Small完整30秒通过前不删除SF3/SF4、不运行100/500，也不实施真实WORK调度、自由游荡或动态目标追逐。
- [INFERRED][HIGH] Small仍失败时只固定新的最早fixture并停止；不得启用ORCA、Portal、Holding或逐场景fallback掩盖统一粒子模型的失败。

## 2026-07-14 历史任务：8368 Obstacle reproject与Hard/Swept闭环

[COMPUTED][HIGH] 第一批语义修正已完成：Soft使用显式fixed-step response且允许残余SoftError；量化后执行不含Soft的Hard/Swept/Obstacle安全闭环；invalid立即固定fixture、发布失败RoundResult并停止；candidate/applied hash和invalid/fallback计数已经拆分；旧`pressure_propagation_layer`已改名为`first_influenced_iteration_max`。

[COMPUTED][HIGH] correction rollback现已覆盖粒子prepared状态、累计指标、solver样本、settling窗口和首失败fixture。SoftPressure的MovementPredict已修正为消费Flow DesiredVelocity；此前消费未执行ORCA fragment导致静止的8365运行是无效证据。

[COMPUTED][HIGH] 14项Particle定向测试与完整`CrowdDemo.SF` 42/42通过，Development Editor通过。基础正式阵型采用128cm；95cm阵型保留为明确的Stress Gate。三张既有SoftPressure地图和用户Lighting均未重新生成或覆盖。

[COMPUTED][HIGH] 8368 Small 20在fixed-step 93立即失败并停止。candidate Hard/Swept/Obstacle/Bounds=`1/1/0/0`，实际应用fallback状态为`0/0/0/0`；invalid/fallback=`1/1`，candidate/applied/fixture hash=`3480157140/1765689391/3809298193`且双端一致。

[COMPUTED][HIGH] fixture确认Agent 4/12在Obstacle前约`95.26cm`安全，Obstacle reproject后为`93.723cm`，相对94cm硬门缺`0.277cm`。下一任务只应修正Obstacle与pair约束的共同闭环，不能调大Soft response、增加Soft push或恢复ORCA。

[INFERRED][HIGH] 修复必须先由该两实体fixture证明：Obstacle reproject后的Hard/Swept修正不会再次越过Obstacle/Bounds，且最终量化后四类安全门同时成立。若共同可行域为空，应明确invalid并保留失败；不得用全局位移、放宽94cm或忽略0.277cm掩盖。

[COMPUTED][HIGH] Small仍未通过，correction-interval p95=`30cm`也未达到`<1cm`。因此SF3/SF4删除、Medium 100、Cohort 500、DebugGame、正式两轮、录像和contact sheet均未执行；当前准确停止点是Small首个硬安全fixture。

[COMPUTED][HIGH] 下方Elastic、Joint、Priority ORCA和SF4章节全部降级为历史实验记录；其旧“当前任务”与“下一步”不再生效。

## 历史设计任务：群体穿行能力与局部联合速度 fixture（未授权生产接入）

[COMPUTED][HIGH] `Desktop 2026.07.13 - 14.27.46.01.mp4` 的约 `208s–225s` 已登记为目标效果证据：大尺寸单位进入密集小型单位阵型，邻近单位释放空间并在之后重组。视频不提供算法、服务器状态或精确碰撞证据。

[INFERRED][HIGH] 根本需求已从“修复 SF4 最后一个 Holding Agent”重新分类为“高优先级、不同尺寸或玩家控制实体穿行密集集群”。SF4 是第一批真实 fixture 来源，不应吸收玩家业务、退圈状态机或全局路径所有权。

[COMPUTED][HIGH] 当前 Generic Priority ORCA 已真实产生25%/75%责任分配，但 Static Small 仍为物理 `19/20`，Agent 6 的 preferred/ORCA/final=`(79,796)/(0,78)/(0,0)cm/s`，并有 `stop_violates=427`；因此仅调责任比例不能写成穿行能力完成。

[INFERRED][HIGH] 下一步若获代码授权，只做纯 fixture：先捕获 Agent 6 的最小真实约束和 ORCA 后各阶段速度，再实现与生产并列的确定性局部联合速度 solver；必须证明它在相同安全约束下找到当前 Priority ORCA 未找到的正向速度，才允许另行设计生产接入。

[INFERRED][HIGH] 第一批 fixture 固定覆盖后快前慢、进入五实体环、从环内离开、固定速度玩家穿群、多尺寸单位进入并重组、交叉穿行者和 Agent 6 真实输入。输入反序、量化结果、pair安全与hash必须一致；超出已验证分量规模只记录并回退，不宣称100/500容量。

[INFERRED][HIGH] 同一任务必须先补齐自适应间距纯合同：`PhysicalRadius`与`HardSafetyGap`是不可压缩安全距离，`PreferredSpacingGap`是可压缩软目标，`AssignedSpacing`由Position/Holding恢复。开放区、窄口压缩、出口恢复和穿行局部净空必须分别fixture验证，禁止继续复用一个`SafetyGap`同时表达全部语义。

[INFERRED][HIGH] 实施顺序固定为：距离语义与pair owner纯测试→普通群体spacing fixture→2–8实体joint+spacing fixture→Agent 6真实重放→不消费结果的shadow模式→另行授权Static Small生产接入。任何阶段无法证明硬安全、双端hash或相对Priority ORCA的正向改善时停止。

[INFERRED][HIGH] 完整需求和实现复审以 [CrowdTransitCapabilityDesign.md](CrowdTransitCapabilityDesign.md) 为事实源。本次只修改文档，没有修改生产 ORCA、processor、地图、Lighting、参数或网络。

## 当前停点：SF4 Generic Priority ORCA 单轮失败（2026-07-13）

[COMPUTED][HIGH] 已完成 Generic PriorityKey、SF4 Steering 映射、25%/75% reciprocal responsibility、Yieldable Commit blocker、Priority round hash/rollback、物理满足指标及对应自动化；Development 与完整 `CrowdDemo.SF` 33/33 通过。

[COMPUTED][HIGH] Static Small 单轮 `Saved/CrowdDemo/CrowdDemo_8324_20260713_134501` 的双端 hash、penetration、Target crossing、corridor、deadlock、visible instances 和 correction 门通过；Priority asymmetric pair=`19247` 且责任和违规=`0`。

[COMPUTED][HIGH] 本轮仍失败：物理满足=`19/20`，唯一 Agent 6 在 Holding 状态中 preferred/ORCA/final=`(79,796)/(0,78)/(0,0)cm/s`；同时 ORCA `stop_violates=427`，不满足 safety hard gate。

[INFERRED][HIGH] 下一任务若获授权，应先对 Agent 6 的 ORCA 后速度链做只读 final-boundary 归因，区分 ObstacleConstraint、PBD/Reproject 与 MovementFinalize；不得从当前证据直接修改 handoff、ORCA 参数或容差。ORCA stop violation 也必须独立归因，不能因最终 penetration=0 而忽略。

[COMPUTED][HIGH] 因安全门和物理 20/20 均未通过，本轮不运行 DebugGame、正式两轮、录像或 contact sheet，并停止在 Static Small；不进入 Moving Target、100/500、攻击或死亡。

## 当前停点：8个未完成实体已完成逐Agent归因

[COMPUTED][HIGH] 最终boundary只读fixture、每Agent Commit拒绝位掩码、六状态ORCA约束来源、稳定hash、rollback和双端比较已实现；Development、Positioning 3/3、完整SF 33/33通过。

[COMPUTED][HIGH] `CrowdDemo_8322_20260713_124520`得到8实体、hash=`1989554829`且双端一致：4个近Holding实体被ActiveCommitConflict/ReserveBlocker拒绝，3个Holding/Commit实体的强preferred被ORCA压至约零，1个Pursuit实体在1226cm外preferred为0。

[INFERRED][HIGH] 下一任务只复核4个CommitGate blocker：输出冲突pair、双方route、Reserve占用和单fixed-step安全反事实，决定是否存在安全并发Admission。没有该证据前不放宽Gate；本轮不修改Admission、ORCA或handoff。

## 当前停点：联合 assignment 20/20，动态收敛仅9/20

[COMPUTED][HIGH] 原子 Position+Holding 发布、correction rollback 和 Commit residual capacity gate 已接入正式 SF4 processor。Development、联合规划1/1、Positioning 3/3与完整SF 33/33通过。

[COMPUTED][HIGH] Static Small Round 1证据为`Saved/CrowdDemo/CrowdDemo_8317_20260713_120042`：联合assignment=20、unmatched=0、Reacquire=0、Commit residual=7/7；corridor=20、deadlock=0、双端penetration=0、hash match=1、checkpoint/interval p95=0.047/0.064cm、agents=visible=20。补充只读ingress诊断`CrowdDemo_8319_20260713_120430`连续两轮Target exclusion crossing=0，最大interval p95=0.066cm。

[COMPUTED][HIGH] 能力末态为`1/6/1/3/9/0`，StableOccupied+ReserveHold=9/20，未达到20/20。因此没有运行DebugGame、正式两轮或录像，也没有修改ORCA、Commit Gate之外的状态行为、handoff或容差。

[INFERRED][HIGH] 下一步只做末态运动归因：分别量化6个Holding的距离/速度进展、1个Commit的route-forward进展、1个Pursuit的preferred/ORCA/final速度及3个未稳定Commit的arrival条件；得到唯一证据后再单独设计一个生产分支。

## 当前停点：分支B纯联合规划通过，未生产接入

[COMPUTED][HIGH] Hall几何fixture证明Position 2834的150条Holding路径均被真实Stable安全圆封闭；最佳margin=-35.922cm，几何/生命周期审计错误计数为0。因此没有选择compatibility几何修复，也没有删除Stable约束。

[COMPUTED][HIGH] 只读Joint Planner真实容量为20/20，Agent 5被改配到`H2843/P2886`；5个Commit候选的剩余容量前瞻均为4/4。Development、Geometry、HoldingHall、Positioning和完整SF 33/33通过，正式证据目录为`Saved/CrowdDemo/CrowdDemo_8311_20260713_021028`。

[COMPUTED][HIGH] 本轮按分支B边界停止：未替换`AssignHoldingPositions()`，未接入Commit residual gate，未运行生产修复后Static、DebugGame、正式两轮或录像。下一任务必须独立设计原子Position+Holding发布、rollback及CommitGate接入，不能顺手修改ORCA。

## 当前停点：Holding Hall 诊断完成，不修改生产

[COMPUTED][HIGH] 最终boundary精确Hall fixture、三类硬owner释放反事实、逐边compatibility拒绝分类、completed-round fixture快照与JSON落盘已经完成；Development、`CrowdDemo.SF4.HoldingHall` 1/1、`CrowdDemo.SF4.Positioning` 3/3和完整`CrowdDemo.SF` 31/31通过。

[COMPUTED][HIGH] Static证据目录为`Saved/CrowdDemo/CrowdDemo_8308_20260713_013102`。最小Hall集合`{5}`只有0个可用Holding，deficiency=1；current/no-Stable/no-Reserve/no-Commit=`18/15/18/18`。因此不进入owner生命周期修复，也不替换生产matcher。

[INFERRED][HIGH] 下一独立设计应在固定Position 2834上复核Target gate与Stable/Reserve blocker的必要性；若改变Position才出现合法边，则必须设计Position+Holding联合分配。没有新证据前不得修改ORCA、Commit或候选布局。

## 当前完成门：Steering-first Holding/Commit纯kernel已通过，尚未Mass集成

[COMPUTED][HIGH] Holding candidate、Holding→Position compatibility、20实体唯一assignment、Commit Gate、membership release/Reacquire和六状态guidance纯C++接口已实现，未修改生产fragment或processor。

[COMPUTED][HIGH] 真实Small Flow/Obstacle fixture产生不少于20个合法Holding candidates；20个Position均有compatibility边并完成20/20稳定分配。输入倒序、重复执行、existing owner复用、commit优先级和owner removal hash/ownership测试通过。

[COMPUTED][HIGH] Development Editor、全部3项`CrowdDemo.SF4.Positioning`、独立`CrowdDemo.SF4.Positioning.SteeringFirstHoldingCommit`与`git diff --check`通过。

[COMPUTED][HIGH] 本阶段按边界停止：旧Phase Reservation仍是正式runtime，Holding/Commit尚无prepared SoA、Mass fragment、rollback、双端hash或processor stage；没有运行Static、DebugGame、完整SF、两轮或录像。

[INFERRED][HIGH] 下一阶段具备进入独立Mass集成设计的纯kernel前提，但集成必须替换而非叠加旧Polar/Phase生产owner，并重新建立correction rollback和双端hash；本轮不实施。

## 当前任务：Steering-first Holding/Commit文档收敛，代码迁移尚未开始

[COMPUTED][HIGH] 当前工作只完成顶层架构复核和Markdown修订，没有修改Source、Scripts、Config、地图、测试参数或Git历史。当前正式代码仍是两阶段Phase Reservation与Route-Aware ORCA失败基线，Static能力保持`1/20`。

[INFERRED][HIGH] 下一代码阶段应先创建最小纯kernel合同，不直接改生产行为：从现有Flow raster和Candidate几何生成稳定Holding Positions；为每个未获Commit实体确定性分配Holding；按`WaitEpoch → Candidate可达代价 → PositionId → AgentId`选择少量简单Commit集合；释放条件覆盖到达、assignment失效和未来membership移除。第一版只处理Static Small 20。

[INFERRED][HIGH] 预期状态机收缩为：

```text
Pursuit
→ HoldingAssigned / Holding
→ CommitGranted / Commit
→ StableOccupied 或 ReserveHold
→ Reacquire
```

[INFERRED][HIGH] Movement Intent由明确owner合成：Portal未Exited时Portal优先；否则Holding/Commit/Occupied correction覆盖普通Flow；ORCA只消费最终preferred velocity并处理连续局部安全，不再承担Phase队列或路径所有权。Waiting没有Holding target时必须保持Pursuit并停留在目标外围，不得静默继续向中心挤压。

[INFERRED][HIGH] 第一代码门只允许纯fixture验证：Holding不重叠、位于walkable/reachable cell、避开Target exclusion和Commit corridors；Assignment输入乱序确定；Commit集合路线互不冲突；释放后Reserve promotion稳定；相同输入hash一致。fixture失败即停止，不接入Mass。

[INFERRED][HIGH] 纯fixture通过后才允许接入prepared SoA、最小fragment、boundary apply和rollback，并运行Static单轮。硬门仍为Target crossing=0、双端penetration=0、hash match、checkpoint/interval p95<1cm、agents=visible=20；能力门为Assigned=20、StableOccupied+ReserveHold=20、corridor=20、deadlock=0。任一失败即停止，不恢复Polar Phase Reservation、不调ORCA/PBD参数、不进入Moving Target。

[INFERRED][HIGH] Portal/通路不是下一阶段默认依赖。只有另行场景证明随机地形中的实际瓶颈需要方向、公平或饥饿调度，才设计自动Portal gate；不人工标记所有障碍缝隙，不把ZoneGraph lane模型强加给开放虫群地形。

[COMPUTED][HIGH] 下方所有Phase Reservation、Wait-For和Reservation-Aware ORCA章节保留为历史实现与准确停止点；它们不再是当前推荐的下一生产方向。

## 当前停止点：RoundResult最终边界无合法Reservation-ORCA fixture

[COMPUTED][HIGH] 纯fixture、Active三类分类、不可行核心、单步containment、稳定hash、RoundResult单次捕获和Saved JSON出口已实现；Development、SF4定向自动化和diff门通过。

[COMPUTED][HIGH] 唯一诊断运行`Saved/CrowdDemo/CrowdDemo_8391_20260712_184754`在最终边界返回`valid=0, too_large=0, primary=-1, agents=0, core=0`并触发预声明VIOLATION，未写出伪fixture。

[INFERRED][HIGH] 最终边界没有唯一证据选择ActiveRouteDisjoint、真实Route冲突或Route containment分支；按计划停止，禁止改生产ORCA或改用更早step挑选有利样本。

[COMPUTED][HIGH] 分支A/B/C均未实施；没有运行分支后Static、DebugGame、完整SF、正式两轮或录像。

## 当前停止点：Wait-For Graph选择分支C，生产采样撤销

[COMPUTED][HIGH] wait-for graph纯内核、prepared SoA、rollback、紧凑指标、双端hash及最小fixture已完成；Development、两项`CrowdDemo.SF4.Positioning`和`git diff --check`通过。

[COMPUTED][HIGH] Static Small单轮得到3个cycle、最大cycle=9、11个stalled blocker、0个stale owner和0个atomic-safe cycle。由此排除A（无安全原子handoff集合）、B（无过期owner）和D（存在真实cycle/stall），只选择C进行纯fixture归因。

[COMPUTED][HIGH] C fixture确认Active来源是route-forward不可行的主导来源，Waiting次要，Stable/Other近零；生产态采样会造成correction interval缺失或revision gap，已完整撤销。当前生产路径不执行额外half-plane重求解，也不把诊断hash加入RoundResult。

[COMPUTED][HIGH] 最终安全证据`Saved/CrowdDemo/CrowdDemo_8391_20260712_182256`通过双端hash、Target crossing、penetration、checkpoint/interval、revision gap和20/20可见门；能力仍为StableOccupied=`1/20`，故按硬门停止。

[COMPUTED][HIGH] 不运行DebugGame、完整SF、正式两轮、录像、Moving Target或更大规模；不修改ORCA/PBD、Target exclusion、地图、时长和网络参数。

[INFERRED][HIGH] 下一任务必须是独立设计并纯fixture证明Active reservation的安全迁移/释放语义，而不是直接删约束。membership变更的未来顺序必须为：membership boundary、Portal token release、Phase reservation release、assignment invalidation、survivor preservation、deterministic promotion、ghost cleanup、双端hash；本轮不实施。

## 当前停止点：两阶段生产接入安全通过、Static能力1/20失败

[COMPUTED][HIGH] prepared SoA、最小fragment状态、schedule processor、boundary apply、committed-phase Guidance/ORCA、rollback和phase round hash均已接入。Development、两项定向自动化和diff门通过。

[COMPUTED][HIGH] Static Small单轮安全门全部通过；能力结果为StableOccupied+ReserveHold=`1/20`，低于当前保留版0/20对比目标的完整20/20，也显著低于历史最好16/20。第一失败能力项为完成状态不足，主要末态为10个RadialStage、3个Waiting、2个AngularAlign、1个RadialCommit。

[COMPUTED][HIGH] phase route conflict累计4027、held steps p95=170；Radial forward p50/min仅`3.160/-9.832cm/s`。按预声明门控停止，不继续调参或扩展验证。

[INFERRED][HIGH] 若另开后续设计，必须先用纯fixture研究“current reservation非抢占条件下的多boundary公平迁移/可重试集合”，并证明不会重新产生Target crossing；同时需要固定当前half-plane集合解释active/waiting/stable来源为何把route-forward速度压低。该工作不属于本轮。

[COMPUTED][HIGH] DebugGame、完整SF、正式两轮、录像、Moving Target、攻击、多profile、P1与100/500均未执行。

## 当前任务：两阶段Phase Reservation接入前门控

[COMPUTED][HIGH] `CurrentPhaseReservation + RequestedNextPhase → Granted/Held/Invalid`纯C++ scheduler及稳定哈希已完成，Development、两项`CrowdDemo.SF4.Positioning`与diff门通过。

[COMPUTED][HIGH] 该段是接入前计划记录；生产接入现已完成，但能力门失败，当前停止点以上一节为准。先前违反Target安全门的phase-local/held实验仍保持撤销。

[INFERRED][HIGH] 下一步应新增prepared request/result SoA和显式boundary apply：先从当前phase构建不可抢占reservation，再生成next-phase请求，调用scheduler，只有Granted才写ApproachPhase；Held保持旧phase/旧安全终点，Invalid按明确原因留在当前phase或回队。接入前必须增加correction rollback快照与紧凑grant/held/invalid/hash指标。

[INFERRED][HIGH] 生产接入后先运行单轮Static Small硬安全门；若Target crossing、双端penetration、hash mismatch或VIOLATION任一非零，立即撤销接入。只有安全门成立才比较完成数、grant吞吐和arrival p95，不直接进入两轮、DebugGame或录像。

## 当前停止点：Phase-Local实验违反Target安全门并撤销

[COMPUTED][HIGH] 简单phase-local reservation提高grant但造成requeue抖动；held改版产生Target crossing=1并放大局部约束，两版均已撤销。

[COMPUTED][HIGH] 显式`CurrentPhaseReservation → RequestedNextPhase → Granted/Held/Invalid`纯scheduler已经实现并通过定向自动化；生产状态机和Mass数据交换尚未接入。Target exclusion继续作为grant前硬门，而不是事后修正。

[COMPUTED][HIGH] 当前继续停在Static单轮失败，不运行正式两轮、DebugGame、完整SF或录像。

## 当前停止点：动态ORCA预测已撤销，转向Phase-Local Admission设计

[COMPUTED][HIGH] constraint来源诊断证明保留版Radial仅受1个active和1个ReserveCommit约束。动态预测/active-active省略实验恶化infeasible与grant数量，已撤销。

[INFERRED][HIGH] 下一任务应构建phase-local admission fixture：RadialStage只预留当前径向段、AngularAlign只预留当前arc、RadialCommit只预留最终径向段；phase变化时重新参与稳定冲突选择。必须先证明同phase安全、跨phase不会交换顺序且hash不受输入乱序影响。

[COMPUTED][HIGH] 当前仍未进入正式两轮、DebugGame、完整SF或录像。

## 当前停止点：Route-Aware ORCA已接入但Static仍失败

[COMPUTED][HIGH] Active route与Waiting/ReserveCommit安全圆真实相交时采用单向yielding责任；非冲突pair及其他场景不变。phase变化重置progress bucket，Radial进入阈值使用Tolerance+SafetyGap。

[COMPUTED][HIGH] Development、定向自动化与diff门通过；最新单轮双端hash、路线、安全和误差门通过，但Stable+ReserveHold仍为0/20，arrival p95=`1531.321cm`。

[COMPUTED][HIGH] Radial速度幅值约800cm/s而forward仅83.6cm/s，说明剩余half-plane集合持续把速度转向。当前结果不能称为Static站位能力通过。

[INFERRED][HIGH] 下一任务只应建立多active-route half-plane feasibility fixture、约束来源聚合和几何route/速度碰撞预测一致性；不得继续调整阈值、速度、ORCA horizon、round duration或地图。正式两轮、DebugGame、录像和提交继续停止。

## 当前停止点：Approach ownership已稳定，ORCA route契约缺失

[COMPUTED][HIGH] 1200cm entry gate、grant后phase-lock、Portal优先和1cm progress语义已实现并通过Development与定向自动化。保留版本恢复corridor/turn=20、deadlock=0、双端penetration=0。

[COMPUTED][HIGH] Static Small仍为0/20完成；Radial preferred=`800cm/s`但ORCA/final=`65.765cm/s`，radial error p95=`927cm`，说明速度压缩发生在ORCA阶段。

[COMPUTED][HIGH] 将SF4状态直接映射为Portal ORCA priority的反证运行产生ORCA/final=0和target crossing=1，已经撤销。不得把该失败实验写成已实现能力。

[INFERRED][HIGH] 下一任务应先设计并测试SF4 route-aware ORCA输入/约束：只有与active swept route真实冲突的实体才让行，并验证约束仍保持Target exclusion和Obstacle安全。没有该设计前不继续正式两轮、DebugGame、录像或更大规模。

## 当前停止点：Admission并发已修正，Compose边界振荡暴露

[COMPUTED][HIGH] Admission已改为active routes占用路径、waiting routes按稳定顺序扫描，并允许与全部active/已选route均无冲突的候选并发grant。Development和两项SF4 Positioning自动化通过。

[COMPUTED][HIGH] Static Small单轮没有恢复原16/20：完成状态仍为0/20，waiting/RadialStage=`14/2`，grant/requeue=`7/5`。Radial preferred p95=`70cm/s`，ORCA/final p95=`799.966cm/s`，radial error p95=`1009cm`，compose boundary switches=`295`。

[INFERRED][HIGH] 下一实现任务应把compose-range选择改为有明确进入/退出滞回或由Approach phase锁定的单调状态，随后验证Radial guidance是否持续有效；在此之前不得继续扩大Admission并发、放宽ORCA、提高MaxSpeed或延长round。

[COMPUTED][HIGH] 当前门控已停止：未运行正式两轮、完整SF、DebugGame Editor或录像，代码保持未提交/未暂存。

## 当前停止点：Target-Aware Approach单轮能力门回退

[COMPUTED][HIGH] Approach Gate/Polar Route kernel与直接相关自动化通过，Development编译通过；单轮Target crossing、Stable radial blocker、penetration和坏日志均为0。

[COMPUTED][HIGH] 单轮站位完成状态为0/20，明显低于原16/20；末态13 Waiting、2 RadialStage、1 RadialCommit，Admission仅grant 7且requeue 4。按阶段顺序立即停止，正式两轮、完整SF、DebugGame与录像均未执行。

[INFERRED][HIGH] 后续若另立任务，应先解释route grant利用率、RadialStage耗时和no-progress requeue为何在30秒内只推进到一个RadialCommit；当前不得通过延长轮次、提高速度或放宽ORCA解决。

## 当前停止点：Front Ingress分支A正式硬门失败

[COMPUTED][HIGH] 无行为诊断选择分支A的依据为Stable阻断4/4、Stable blocker pairs=14、ingress order inversion=3/4与Stable来源ORCA约束48/69；Target crossing仅1/4，Reserve阻断与PBD/Obstacle推离均为0。

[COMPUTED][HIGH] 已实现固定wave=2的远侧优先Front admission、交叉路径拆wave和180-step timeout/requeue；纯Positioning自动化与Development编译通过。

[COMPUTED][HIGH] 正式两轮第一硬失败为`StableOccupied+ReserveHold=7/20`，并从修改前16/20回退；arrival p95=`1196.852cm`。按停止条件不再尝试Approach Gate、ORCA责任、速度、容差或轮次修改。

[COMPUTED][HIGH] 完整`CrowdDemo.SF`与DebugGame Editor没有在失败分支后继续运行；原因是正式Static硬门已经触发立即停止。Moving、多Profile、攻击、NavMesh Bake、P1、100、500均未开始。

## 当前停止点：SF4 单Front带已验证，Static仍为16/20

[COMPUTED][HIGH] 单Front带/外层Reserve失败优先fixture与纯kernel修改已通过Development、DebugGame Editor和完整25项`CrowdDemo.SF`。Static Small两轮均为Front/Reserve capacity=`16/134`、StableOccupied/ReserveHold=`12/4`，仍有4个SlotCommit，arrival error p95=`488.945cm`。

[COMPUTED][HIGH] 路线、安全、双端hash和误差门通过，但`StableOccupied+ReserveHold=16/20`未通过Static能力硬门；阶段B保持失败，阶段C Moving Target禁止开始，也不运行P1、100或500。

[INFERRED][HIGH] 下一步必须另行设计4个Front尾部实体的占用/通行证据门，不能把本轮结果解释成调大ArrivalTolerance、提高速度或弱化ORCA的许可。

## 当前停止点：SF4 Static双端rollback与settle硬门失败

[COMPUTED][HIGH] SF4连续reproject已关闭低速位移被整厘米舍入为0的问题，但原参数两轮仍为StableOccupied/ReserveHold=13/0。7个未settle实体均为活跃SlotCommit，ORCA adjusted=7、constraint p95=19、最终速度p95=2.828cm/s，说明当前停止点已从Obstacle精度下移到多Front带/目标容量与ORCA联合占用。

[INFERRED][HIGH] 下一允许变更应先在纯kernel fixture把Front限制为最内侧合法preferred radial band，其余合法外层为Reserve，并验证20实体能形成Front+Reserve而不是全部Front；不得修改ORCA、MaxSpeed、ArrivalTolerance或round duration。

[COMPUTED][HIGH] rollback与连续reproject结构门现已通过：两轮Traffic/Portal/ORCA/AgentState双端hash match=1，checkpoint/interval/cross-round error p95=`0.049/0.063/0cm`，无VIOLATION。当前Static硬失败为StableOccupied+ReserveHold=`13/20`与arrival error p95=`199.057cm`。

[INFERRED][HIGH] 下一次只允许对7个未settle实体做紧凑聚合诊断，先确定SlotCommit/arrival-speed/ORCA-PBD/candidate几何的第一失败层；未有证据前不改容差、速度、ORCA、PBD或地图。

[COMPUTED][HIGH] Static两轮路线安全通过但能力/确定性失败：corridor=20、deadlock=0、assigned=20、candidate overlap/unreachable=0、penetration=0、agents=visible=20；StableOccupied+ReserveHold仅13，未达到20。

[COMPUTED][HIGH] 第一结构停点已按原参数关闭；当前只保留7个实体未settle的能力停点。

[COMPUTED][HIGH] 按预声明门控不进入Moving Target、不录像、不运行P1/100/500，也不实现攻击或NavMesh Bake。阶段B集成尚未提交为通过。

## 当前：SF4阶段A通过，等待Static Target Small集成

[COMPUTED][HIGH] 阶段A纯Candidate/Assignment kernel与failure-first fixture已通过并提交为`bac361f7`。完整25项`CrowdDemo.SF`、Development、DebugGame Editor与diff门均通过；SF1/SF2/SF3未修改。

[INFERRED][HIGH] 下一门仅为Static Target Small 20：新增隔离scenario=3、最小assignment fragment、prepared SoA、显式Movement Intent Compose、Static真实地图与双端metrics。只有assigned=20、StableOccupied+ReserveHold=20、corridor=20、churn/penetration/hash/error硬门全部通过才允许进入Moving Target。

[COMPUTED][HIGH] 当前仍禁止Moving Target、攻击、业务slot、NavMesh Bake、P1及100/500规模；阶段A通过不等于Static或最终SF4效果通过。

## 当前：SF3 ORCA/LP 收尾与 Small 确定性验收

[COMPUTED][HIGH] 已固定并核验官方上游 `snape/RVO2@b577921d2bc1281a6b721c2d4778f397d37da97d` 的Apache-2.0来源，只引入测试专用LP1/LP2适配器、许可证、来源与修改说明。reference不进入Shipping行为或正式Mass pipeline。

[COMPUTED][HIGH] failure-first fixtures与Small差分均得到current miss/reference hit=0，因此采用分支B：保留当前continuous solver；正式路径已移除Oracle调用和witness写回。Exact、3×3 quantized recovery与best-effort/StopViolation已分开计数。

[COMPUTED][HIGH] Development、DebugGame Editor和完整24项`CrowdDemo.SF`通过。reference关闭后的Small P0三轮保持flow unreachable=0、corridor=20、deadlock=0、penetration=0、误差=0、双端/跨轮hash一致、Oracle invocation/witness=0；goal=10/20按未通过效果记录，不伪装为20。

[COMPUTED][HIGH] LP剩余事件为每轮742次exact无严格可行输出、19次continuous可行但1cm/s与3×3量化均失败、19次可行best-effort（Portal=2、StopFeasible=17）及742次StopViolation。目标附近10个未到达实体与大量reached/non-reached约束表明还存在目标容量竞争；当前证据不能把goal失败逐实体归因。

## 下一阶段：稳定站位/目标容量设计（本轮不实施）

[INFERRED][HIGH] 先建立20实体纯fixture，再决定processor名称和顺序。cohort围绕目标按攻击距离、实体半径/安全间距、可通行域以及必要时的视线/攻击可达性生成多圈或可用区域候选；按稳定AgentId和量化代价确定性分配，而不是所有实体争抢同一goal cell或写死单圆。

[INFERRED][HIGH] reached实体应转入StableOccupied；后续实体进入外围等待/补位。仅在目标移动、候选失效、实体死亡或攻击距离变化时确定性重分配。站位阶段只输出movement intent/target position，攻击、受击、死亡仍属于个体业务层。

## 后续基础设施：离线导航数据生成 Crowd Navigation Field（本轮不实施）

[INFERRED][HIGH] Editor/Commandlet从NavMesh/Recast或其他明确导航数据按固定origin、cell size、高度/层与邻接顺序Bake确定性资产，保存walkable、height、clearance、area flags、connectivity、源版本与内容hash。Portal由baked connectivity/clearance派生，避免最终方案继续只依赖手写ObstacleSpecs。

[INFERRED][HIGH] 运行时加载同一资产，结合cohort goal和受控动态障碍overlay重建integer Dijkstra/integration/shared flow；goal变化不重新扫描整张Mesh。本轮不实现NavMesh查询、Bake asset、站位、攻击、P1或更大规模运行。

## 当前停点：Flow bounds恢复通过，剩余尾部集中目标附近

[COMPUTED][HIGH] `093b5c44 Diagnose SF3 flow reachability loss`完成四类Flow状态、六阶段诊断、failure witness、continuous/raster冲突测试和ORCA recovery拆分。正式诊断证明7个deadlock=7个final invalid=7个OutOfBounds，首次失效全部位于MovementPredict；BlockedRaster和UnreachableFree均为0。

[COMPUTED][HIGH] `796d1d45 Keep SF3 movement on reachable flow domain`只实施证据支持的分支B：SF3 continuous movement constraint裁掉越界分量并保留切向分量，PBD后再次执行。没有叠加raster recovery或free-island safety reproject。

[COMPUTED][HIGH] 修复后正式P0 Small两轮Flow unreachable=0、corridor=20、deadlock=0、wall/turn=20、final invalid=0、双端penetration=0，满足情况A。Flow build hash保持267519150；navigation-domain reproject max=23.267cm。

[COMPUTED][HIGH] goal仍为13/20，但7个尾部全部集中在目标141.662–286.575cm范围；stopped far=0。true-no-witness全部属于reachable Flow，goal-near/Corridor agent-step=`222/117`；ORCA仍有445次infeasible/stop violation和607次Oracle quantized witness使用，reservation timeout=1。

[INFERRED][HIGH] 下一阶段若另行授权，应先形成“稳定攻击位置/目标容量”独立设计：为目标周围生成确定性共享可用位置或cohort级arrival分配，定义reached实体退出通行约束的规则，并保持业务攻击事件仍属个体层。该阶段必须先有failure-first容量测试和20实体目标分布验收；当前任务不实施。

[COMPUTED][HIGH] 本轮按情况A停止，不进入LP3、站位实现、P1、Medium、Cohort或Crossing；不修改Flow build、ORCA pair/LP、Portal/Holding/Band、goal 140cm、参数、网络、NavMesh、地图或processor顺序。

## 当前停点：parallel 数值漏解关闭，真实空可行域与 Corridor 能力门失败

[COMPUTED][HIGH] 已完成 `36d7dbbc Add parallel half-plane numerical regressions` 与 `9f01b31c Fix near-parallel interval clipping stability`。BehaviorEpsilon 保持0.1cm/s，Angular/Residual/Parameter 三种数值容差独立，Formal LP 关键中间运算使用 double；生产 fixture、残差边界、近平行和 1–24 constraint 固定矩阵通过。

[COMPUTED][HIGH] Development、DebugGame Editor、ORCA 9/9 与完整 `CrowdDemo.SF` 22/22 通过；git diff check通过。P0 Small 两轮的 Formal missed→Zero 与 missed→Oracle 均从旧基线的4087/3791降为0，说明本轮目标的 parallel 漏解已经关闭。

[COMPUTED][HIGH] Formal 整体硬门仍未通过：true-no-witness=380、stop-violation=380、ORCA infeasible=380，目标均为0。能力门也失败：goal=12、corridor=12、deadlock=7、flow unreachable=7；8个实体停在Corridor，未恢复20/20通行。

[COMPUTED][HIGH] Oracle invocation仍为915；现有指标未拆分固定3×3量化恢复与Oracle quantized witness，且正式代码仍可能采用后者。本轮只能确认parallel连续漏解分类清零，不能确认正式速度对Oracle完全无依赖。

[COMPUTED][HIGH] 两轮 Traffic/Portal/ORCA/AgentState/diagnostic hash 完全一致，双端 hash一致，11-stage mismatch=0，checkpoint/interval/cross-round error=0，penetration=0，agents=visible=20。诊断录像和contact sheet位于 `Saved/CrowdDemoCapture/CrowdDemoCapture_8291_20260712_002822`，仅作失败证据。

[INFERRED][HIGH] 精确停止原因是：数值漏解已清零，但部分正式约束集合确实无可行 witness，并造成 Corridor 停滞。下一任务若获授权，应审计产生这380次空交集的 constraint compatibility 与 traffic/preferred 状态，不能继续放宽数值容差，也不能先实现goal spreading、arrival band、slot或攻击位置。

[COMPUTED][HIGH] 按预声明门控立即停止：不启用P1，不运行Medium/Cohort/Crossing，不修改Portal/Holding/Band、goal/reached、ORCA参数、PBD、地图、网络、NavMesh或processor顺序，不进入稳定站位阶段。

## 当前停点：Half-plane interval首轮修复未关闭Formal LP硬门

[COMPUTED][HIGH] 已完成失败优先fixture提交`feee063d`和interval/recovery实现提交`434a8571`。红测、interval helper、parallel/contradictory、量化恢复和既有ORCA回归通过。

[COMPUTED][HIGH] 正式Small触发预声明情况D：每轮仍有`formal_lp_missed_zero_recovered=4087`与`formal_lp_missed_oracle_recovered=3791`，硬门要求均为0；另有`true_no_feasible_witness=380`与`stop_violation=380`。因此本轮立即停止，不继续修parallel tolerance，不进入P1或站位阶段。

[COMPUTED][HIGH] 能力门同时失败：goal=12、corridor=12、deadlock=7、flow unreachable=7。server/client hash一致和0cm correction只证明确定性，不证明算法正确。

[INFERRED][HIGH] 下一轮若单独授权，应从已记录的同向平行等价fixture开始，为parallel redundancy建立独立于行为epsilon的有限浮点容差，并扩展Formal-vs-Oracle固定枚举测试。完成前不得再次运行能力验收或引入goal/attack positions。

[COMPUTED][HIGH] 本轮没有修改pair constraint公式、责任比例、ORCA参数、goal、Portal/Holding/Band、地图、网络、PBD或processor顺序；没有运行Medium/Cohort/Crossing。

## 当前停点：Goal/ORCA只读根因诊断完成

[COMPUTED][HIGH] 默认关闭的Goal congestion诊断、独立feasibility oracle、距离/Flow区域聚合、correction rollback和确定性测试已经完成。Development、DebugGame Editor和完整17项`CrowdDemo.SF`通过；正式P0 Small两轮保持原hash与AgentState结果。

[COMPUTED][HIGH] 根因规则A已满足：7206次正式LP失败时zero可行，另有1068次oracle找到非零witness；只有205次没有枚举到witness。当前任务没有修复LP，也没有用oracle速度驱动实体。

[COMPUTED][HIGH] 空间诊断显示15个non-reached全部已越过corridor并位于goal 800cm内，但仅7个位于400cm内；8个仍在PostCorridor。目标附近占位有贡献，但不满足预声明的严格规则C，不能直接进入goal slots/arrival band实现。

[COMPUTED][HIGH] 本轮在两轮诊断与diagnostic-only录像后停止。没有修改ORCA公式/LP/量化/fallback、goal/reached行为、Portal/Holding/Band、P0参数、地图、网络或实体数量；没有运行P1、Medium、Cohort或Crossing。

[INFERRED][HIGH] 下一阶段若获单独授权，应先修复/替换正式二维half-plane LP并建立oracle交叉验证：凡zero或oracle存在witness时不得返回empty。LP修复通过相同Small诊断后，再重新评估剩余goal容量；当前证据不足以优先实现goal spreading、arrival band或slots。

## 当前停点：标准 ORCA 几何通过，Small 联合可行域失败

[COMPUTED][HIGH] 已完成旧径向 pair constraint 的失败优先测试、标准 reciprocal VO/ORCA 构造、LP/量化/fallback 可行性分类和每轮紧凑诊断。提交为 `28344b0b Add deterministic ORCA constraint correctness tests` 与 `595a001b Fix reciprocal ORCA constraint construction`。

[COMPUTED][HIGH] Development、DebugGame Editor 和完整 16 项 `CrowdDemo.SF` 自动化通过；既有 Portal/Holding/Band 测试没有回退。正式证据目录为 `Saved/CrowdDemo/CrowdDemo_8291_20260711_223000`。

[COMPUTED][HIGH] P0 Small 两轮稳定复现 `goal=5/20`、`corridor=20/20`、`deadlock=0`、`multi-constraint empty=8479`、`quantization destroyed=80`、`ORCA infeasible=8559`、`stop violation=1273`。第一硬失败为 goal，且 ORCA infeasible/stop violation 同时失败。

[COMPUTED][HIGH] 因 Small 能力门失败，本轮立即停止：不启用 P1，不运行 Medium/Cohort/Crossing，不录像，不进行人工审片，不继续调 neighbor distance、time horizon、max neighbors、MaxSpeed 或 Portal/Holding/Band。

[INFERRED][HIGH] 若另行立项，下一问题应针对多邻居 ORCA 联合可行域的责任/约束协调进行设计与证明；不得回退到固定遍数投影，也不得用 PBD、stop 或参数放宽掩盖 `MultiConstraintEmptyIntersection`。

[INFERRED][HIGH] Demo 的长期目的和目标效果见 `DemoPurposeAndTargetEffect.md`；本文只记录当前停点和下一项可执行工作。

## 当前阶段：Portal/Holding 实验路径已接入，Small ORCA/goal 门失败并停止

[COMPUTED][HIGH] Portal extraction、stable binding、实验性 holding guidance、PassingBand error feedback、density clearing priority 与 deterministic half-plane LP 已接入且纯测试通过；这不等于最终群体效果完成。Development、DebugGame Editor、14项 `CrowdDemo.SF` 前缀自动化和9项 `CrowdDemo.SF3` 通过。

[COMPUTED][HIGH] 正式 P0 Small 两轮 corridor=20、deadlock=0、timeout=0、rebind=0、severe p95=0、双端 hash/checkpoint/penetration 通过，但 goal=18、ORCA infeasible=6017、fallback stop=5904、stop violation=5666。

[COMPUTED][HIGH] 第一硬失败指标为 goal=18/20；按预声明停止条件，不启用 P1，不运行 Medium/Cohort/Crossing，不录像，不继续参数或算法修改。

[INFERRED][HIGH] 若另行恢复，下一问题是 ORCA constraint construction 的联合可行性，不是 Portal 数量、双端同步、checkpoint 或障碍安全。

## 历史阶段：确定性修复通过时的 Small 基线

[COMPUTED][HIGH] 已定位两级首次分歧：原实现首先在 step 16 的 PortalSchedule 分歧，原因是 correction 未回滚 Portal/admission/band；首次修复后在 step 32 的 PlanApplyInput 分歧，原因是 correction 未回滚 TrafficField 使用的上一 step FlowSample。

[COMPUTED][HIGH] 补齐 raw state、Portal、FlowSample 与 round-hash accumulator rollback 后，正式 P0 两轮 132 个 correction boundaries 的 11-stage hashes 全部一致，Round 1/2 AgentState hash 一致。

[COMPUTED][HIGH] 构建、11 项 `CrowdDemo.SF`、6 项 `CrowdDemo.SF3`、git diff 检查均通过。

[COMPUTED][HIGH] P0 Small 通行能力未通过：goal=9、corridor=11、deadlock=13、ORCA infeasible=5344。根据停止条件，不启用 P1，不运行 Medium/Cohort/Crossing，不录制视频。

[INFERRED][HIGH] 后续若继续，应另行设计 Server 交通调度能力，而不是修改已经通过的确定性/rollback设施。

## 历史阶段：RoundResult 与验证设施修复

[COMPUTED][HIGH] 已完成小型 RoundResultHeader + correction chunks checkpoint、乱序组装、5 秒 timeout、stationary boundary reopen 和 result→next plan 顺序。

[COMPUTED][HIGH] 已完成严格客户端 readiness、pre-plan 静止实例、顶层 Mass processor 显式注册/禁止 pruning、future plan 到期后才 claim boundary。

[COMPUTED][HIGH] 已完成 Development、DebugGame Editor、5 项自动化、SF2 20/100/500 连续两轮和四段人工录像。

[COMPUTED][HIGH] 传输与验证设施验收通过；SF2 算法容量验收未通过：100 出现 severe overlap，500 出现严重拥塞、deadlock 与 goal 回退。

## 本阶段剩余项

[COMPUTED][HIGH] 无代码实施项。SF2 在此停止，不暗中加入新算法。

## 后续（需另行立项）

[INFERRED][HIGH] 若继续，应把 100/500 容量失败作为独立 SF3 输入，设计 ORCA 或 crowd passing band，并重新定义 capacity 验收；不得把它混入本次传输修复。

[COMPUTED][HIGH] 当前停止后不继续调节 SF3 density、approach/holding、passing band 或 ORCA 参数，也不实现 slot、攻击、timeline trace 或 A/B 框架。

[INFERRED][HIGH] 自由游荡、动态目标追逐、真实 WORK/GT 调度边界和500完整群体效果均属于 `DemoPurposeAndTargetEffect.md` 定义的长期缺口，必须分别立项，不能从当前SF3停点推定为已实现。

## 当前停点：SF4 Steering-first 已接入，Static 技术门失败

[COMPUTED][HIGH] 纯 kernel、Development、SF4 Mass integration contract 与完整 `CrowdDemo.SF` 28/28 已通过；SF4 正式链已旁路旧 Approach/Admission/PhaseReservation/WaitGraph/route-aware ownership。

[COMPUTED][HIGH] 原参数 Static Small 单轮完成并通过 RoundResult/header/chunk/boundary、双端四类新 hash、checkpoint、penetration、readiness 与可见实例检查，但 corridor=10/20，未达到技术门要求20/20。

[COMPUTED][HIGH] 能力门同时为 StableOccupied+ReserveHold=0/20，末态 Holding=10、Reacquire=10，commit_invalid=9010。按停止规则不运行 DebugGame、正式两轮、录像、Moving Target 或100/500，也不调 ORCA/PBD/速度/容差。

[INFERRED][HIGH] 下一任务若获授权，应先归因10个持续 Reacquire 的 compatibility/ownership 失效来源及 corridor 未完成原因；不得恢复长期未来路径 Reservation 掩盖失败。

### Compatibility 归因完成后的停止点

[COMPUTED][HIGH] Reacquire 已归因为 compatibility 二次按键查询；selected-edge proof 修复后 Reacquire/Invalid 均清零，20个实体全部保持 Holding。

[COMPUTED][HIGH] 新的第一硬失败是 corridor=0/20：Pursuit在出生 boundary立即转为Holding，Holding guidance覆盖Flow并直接指向目标附近候选，全部请求在整轮保持Held，StableOccupied+ReserveHold仍为0/20。

[INFERRED][HIGH] 下一独立步骤必须先修复HoldingId唯一性，再设计确定性的Flow→Holding handoff gate；不能把Holding assignment存在本身等同于应立即接管远距离导航。

### Identity与handoff复测后的当前停点

[COMPUTED][HIGH] HoldingId/PositionId已改为StableCellKey，duplicate compatibility keys=0；handoff gate恢复corridor=20/20且保持deadlock/penetration=0。

[COMPUTED][HIGH] Static单轮能力仍为11/20：Pursuit=5、Holding=4、Stable=9、Reserve=2、Commit=0。按能力门停止，未运行DebugGame、正式两轮或录像。

[INFERRED][HIGH] 下一步只允许增加分状态route-forward speed、holding distance/readiness、commit conflict和ORCA来源聚合，分别归因5个Pursuit与4个Holding；在证据前不得扩大handoff范围或改变ORCA。

### 终态速度与readiness归因

[COMPUTED][HIGH] Pursuit 5个：Flow valid=5、outside handoff=5、distance p95=1332.901cm、preferred forward p50约800cm/s、ORCA/final forward p50/p95=0。

[COMPUTED][HIGH] Holding 4个：distance p95=28.994cm，distance/speed not-ready=0/0，ready conflict=4，ready granted=0。

[INFERRED][HIGH] 下一步应只增加ORCA constraint source与Commit conflict source细分；禁止先扩大1200cm handoff envelope或放宽readiness。

### Stable blocker compatibility 分支完成后的停止点

[COMPUTED][HIGH] 只读来源拆分已完成：上一轮 ready Holding 的首要拒绝源是 Stable blocker；Pursuit 约束主要来自 StableOccupied。生产修复仅让 compatibility graph 随 Stable/Reserve blocker 输入变化确定性失效重建，并让已完成实体保留自身 Holding ownership；没有删除 ORCA pair 约束或修改参数。

[COMPUTED][HIGH] Development、Positioning 3/3、完整SF 28/28通过。Static Small两轮安全门通过且完全确定：corridor=20、deadlock=0、双端penetration=0、四类Steering hash与AgentState hash跨端跨轮一致、checkpoint/interval p95=0.054/0.064cm、revision gap=0、agents=visible=20、坏日志=0。

[COMPUTED][HIGH] 能力门仍失败且两轮一致：`StableOccupied+ReserveHold=14/20`，状态为Pursuit=1、Holding=2、Commit=1、Stable=12、Reserve=2、Reacquire=2。按门控停止，不运行DebugGame或录像，不继续另一修复分支。

[INFERRED][HIGH] 后续必须另行立项并先对剩余6个状态做新的唯一归因；当前证据不授权删除Stable约束、放宽handoff/readiness、改变ORCA/PBD参数或重新启用旧Reservation链。

### Residual Capacity诊断后的停止决定

[COMPUTED][HIGH] Current/NoStable/NoReserve maximum matching均为6，单blocker最佳移除增益为0；分支A条件`matching<6且NoStable=6`不成立。

[COMPUTED][HIGH] Pursuit、Holding、Commit和Reacquire分别呈现ORCA压零、远Holding目标、低速Commit推进和HoldingInvalid；不能用一条现有证据唯一选择B/C/D/E。当前greedy Holding assignment为18/20，而Residual matching证明6个未完成实体都有Position route，这是新的独立归因缺口。

[COMPUTED][HIGH] 本任务停止在只读诊断层：不实施Commit look-ahead、transit strip、ORCA priority、owner/cache或assignment行为修复；不运行DebugGame、修复后Static、正式两轮或录像。

[INFERRED][HIGH] 若继续，应另行设计“Residual matching proof如何驱动Holding assignment而不预约未来路径”的纯fixture；必须先证明它能消除两个HoldingInvalid且不破坏另外四个状态，再授权生产接入。

### Holding Matching门控失败后的停止点

[COMPUTED][HIGH] 纯min-cost max-flow matcher、owner规则与失败fixture已实现，Development及完整SF 30/30通过；真实solver仅在RoundResult最终boundary运行，不影响fixed-step生产路径。

[COMPUTED][HIGH] 真实门控得到greedy=18、Holding matching=18、joint=18，而不是计划要求的18→20。因此未替换`AssignHoldingPositions()`，未执行修复后Static、DebugGame、正式两轮或录像。

[INFERRED][HIGH] 下一任务若继续，应归因两个固定Position为何没有任何可用唯一Holding：检查对应Position的compatibility邻接集、硬锁Holding占用与Hall witness。不得把Position重新分配混入本轮，也不得顺手修改ORCA或Commit。

### Crowd Transit + Adaptive Spacing停止决定

[COMPUTED][HIGH] Stage 0与最终回归通过：Development Editor成功，Priority ORCA基线、SF4 Positioning 3/3、AdaptiveSpacing 1/1、JointVelocity 1/1和完整`CrowdDemo.SF`35/35通过；`git diff --check`无错误。

[COMPUTED][HIGH] 纯kernel证明2–8实体fixture可由Joint solver协调多个低优先级实体，在HardPairDistance与canonical约束成立时获得高于逐实体Priority ORCA的正向速度；oversize、量化失败和非法double owner均原子回退。

[COMPUTED][HIGH] 8328最终boundary诊断得到Agent 6闭包11实体、27 pairs、4个直接Priority constraints，server/client fixture hash=`3427263131`且match=1。速度链为Priority ORCA 78→Predict 78→Obstacle 0→PBD 0→Reproject 0→Finalize 0cm/s。

[COMPUTED][HIGH] 8331在oversize状态语义修正后再次得到相同fixture hash与速度链，`joint_status=OversizeFallback(5)`；checkpoint/interval p95=`0.059/0.064cm`、agents=visible=20、双端penetration=0，但`revision_gap_total=1`且存在计划内oversize VIOLATION，因此不计作完整安全验收通过。

[COMPUTED][HIGH] 两个前置门同时失败：component超过8，不得截断；现有非零ORCA速度被ObstacleConstraint归零，禁止直接接入Joint生产路径。因此没有运行shadow或JointApply，也没有DebugGame、正式两轮、录像、100/500、Moving Target、玩家、攻击或死亡。

[INFERRED][HIGH] 唯一合理的下一任务是只读归因ObstacleConstraint：区分inflated obstacle/flow bounds/swept constraint/Target exclusion中的具体拒绝来源，并评估78cm/s方向是否几何安全。在得到该证据前，不应扩大Joint component上限或修改ORCA、PBD、Commit Gate和Position/Holding。

## 2026-07-13 Obstacle 109 handoff归因与停止点

[COMPUTED][HIGH] 8332只读诊断确认Agent 6的当前段及完整`Current→Holding`段均命中Obstacle 109，FlowBounds delta=0，server/client fixture hash=`3799203711`一致；因此唯一选择为修正handoff，未放宽ObstacleConstraint。

[COMPUTED][HIGH] 已接入`swept-clear + endpoint-clear + reachable + range`的Pursuit→Holding门，并在Holding路径失效时退回Pursuit。Development、Flow定向测试及`CrowdDemo.SF4.Positioning` 3/3通过。

[COMPUTED][HIGH] 8334安全门通过：target crossing=0、双端penetration=0、severe p95=0、corridor=20、deadlock=0、revision gap=0、checkpoint/interval p95=`0.064/0.064cm`、agents=visible=20、双端主hash一致，坏日志模式=0。

[COMPUTED][HIGH] 能力门失败：physically satisfied=`16/20`，状态顺序`Pursuit/Holding/Commit/Stable/Reserve/Reacquire=0/2/1/6/11/0`。旧列表遗漏displaced Reserve Agent 14；完整未满足集合为Agent 5/8/14/15。按停止规则没有运行DebugGame、完整正式两轮、录像或100/500。

[INFERRED][HIGH] 该旧停点已由后续完整物理闭合fixture和Capacity Shadow取代；不能再把Agent 8误写成Reacquire，也不能继续以三实体集合归因。

## 当前停止点：Capacity构造通过，真实Joint Shadow失败

[COMPUTED][HIGH] Stage 0基线完成：`git diff --check`通过；Development Editor成功；`CrowdDemo.SF4.Transit` 2/2、`CrowdDemo.SF4.Positioning` 3/3、完整`CrowdDemo.SF` 35/35通过。

[COMPUTED][HIGH] 最终boundary物理闭合fixture得到total=`20`、satisfied=`16`、unsatisfied=`4`、count_closed=`1`、hash=`1861497071`；未满足Agent为5 Holding、8 Commit、14 ReserveHold、15 Holding。该fixture修复了旧指标只遍历未完成状态、漏掉displaced Reserve的问题。

[COMPUTED][HIGH] P0 Capacity-by-Construction的Hard/Required/Baseline/PreferredGap=`94/188/128/34cm`；当前Static候选的稳定构造容量为Position=`97`、Holding=`61`，capacity hash=`4238641562`，纯容量门通过。

[COMPUTED][HIGH] Transit intent、swept capsule、完整component与二维Joint clearance纯内核已覆盖2/5/8/12/20规模及Obstacle/FlowBounds/Target复验；纯测试通过，但不构成生产接入授权。

[COMPUTED][HIGH] 8338 Shadow真实运行失败：752个component-step仅49 solved、703 infeasible；hard/obstacle/target violation=`1317/1/33`，forward ratio Q15=`0`。客户端同时出现revision gap=`15`且未完成Shadow hash比较。

[COMPUTED][HIGH] 按硬门停止：不实现Production Apply，不运行DebugGame、正式两轮、录像、100/500、Moving Target、玩家、攻击或死亡。下一任务必须先修正Joint solver/constraint构造使Shadow hard、Obstacle、Target violation全部为0，并独立恢复RoundResult传输到revision gap=0；不能用调低HardGap、半径、Target exclusion或PBD兜底。

## 已完成：单seed ring-entry与3-agent canonical残差修复（2026-07-13）

[COMPUTED][HIGH] 上一停止点中的指标混合、完整600cm邻居图闭包、非一致时间窗口和RoundResult revision gap已经修复；8338不再代表当前实现。

[COMPUTED][HIGH] Capacity-by-Construction已进入正式候选链。Static Small原始Position/Holding候选=`150/150`，稳定选择后=`71/75`，deficit=`0/0`、selection hash=`1316163284`，正式assignment=`20/20`。

[COMPUTED][HIGH] 8344使用time-aligned transit relevance和hard-safety closure得到1780个component-step，最大component=`13`；状态为solved=`529`、IterationLimit=`720`、ClearanceNotAchieved=`228`、NoForwardGain=`303`、HardInfeasible=`0`。

[COMPUTED][HIGH] 双端总hash已一致、revision gap=`0`；正式SF4仍为`StableOccupied+ReserveHold=17/20`，证明Shadow没有改变生产运动，也证明正式Static能力没有因基础设施修正而自动通过。

[COMPUTED][HIGH] ring-entry的20cm固定残差已确认来自“验收为硬约束、求解却乘以Relaxation×Weight作为软修正”的语义冲突。完整相对速度投影接入后，ring-entry/ring-exit/external均为`Solved`且clearance=`0cm`。

[COMPUTED][HIGH] 8346仍复现3-agent/3-pair fixture的2cm/s canonical残差；新增32次无Desired回拉、无soft spacing的feasibility polish后，紧凑回归通过，8347最小失败不再是该fixture。

## 历史停点：8347七实体clearance可行性归因

[COMPUTED][HIGH] 8347 Shadow为solved/infeasible=`986/794`，其中IterationLimit=`169`、ClearanceNotAchieved=`301`、NoForwardGain=`324`；candidate clearance max=`22.099cm`，solver p95=`0.729ms`。

[COMPUTED][HIGH] 当前最小失败fixture为7 agents/21 pairs、单seed Agent 17、direct relevant=`3`、hard closure=`4`；clearance deficit=`8cm`，hard pair、canonical和environment residual均为0，fixture hash=`1810564063`。

[INFERRED][HIGH] 下一步必须对该固定fixture做纯可行性判断：若速度圆与全部硬约束存在交集，则改进确定性联合求解；若不存在交集，则明确报告HardInfeasible或调整transit admission/时序，而不能继续增加迭代或降低clearance门槛。此前不进入多seed、Production JointApply、DebugGame、正式两轮、录像、100/500或Moving Target。
## 当前唯一停止点：SF4 Elastic Shadow 首失败在ORCA（2026-07-13）

[COMPUTED][HIGH] 8351因不同snapshot和不同pipeline stage比较而失效；它只保留为历史无效实验，不能再支持“Elastic有效”或“Elastic无效”。

[COMPUTED][HIGH] 已完成Twin-Step公平对照、八阶段同链指标、同一次PBD Solve三轮诊断、Obstacle完整诊断、首失败稳定闭包fixture、rollback和180步独立Parallel Rollout。正式SF4行为、Elastic Production开关、ORCA/PBD参数、地图和网络均未改变。

[COMPUTED][HIGH] Development Editor、Shadow定向自动化4/4和完整`CrowdDemo.SF` 40/40通过。8352单轮满足agents=visible=`20`、双端obstacle penetration=`0`、revision gap=`0`、correction p95约`0.06cm`，且双端stage/rollout/fixture比较均match。

[COMPUTED][HIGH] 第一可归因失败为step=`29`、stage=`ORCA`、kind=`OrcaStopViolation`、attribution=`ElasticWorsened`。Agent 1的Baseline ORCA输出`(643,462)cm/s`可行；Elastic仅将Preferred从`(595,535)`改为`(593,517)cm/s`，ORCA输出即变为`(0,0)`且infeasible。20实体完整fixture hash=`4215325188`。

[COMPUTED][HIGH] Parallel recovery completed为Baseline/Elastic=`5/6`，end-error p95=`116.013/29.706cm`；但HardPair=`2/4`、Target=`358/365`、ORCA stop violation=`147/109`。Elastic的恢复指标较好不能抵消其安全失败。

[INFERRED][HIGH] 下一任务只能先复现并修正fixture中的ORCA可行性/回退不连续，再以相同Twin-Step和Parallel链复验；不得先调Elastic参数或接Production。按计划本轮到此停止，不运行DebugGame正式两轮、录像、Moving Target、100/500、攻击或死亡。

## 已完成：hash 4215325188 step 29量化漏解修复

[COMPUTED][HIGH] 约束集合完全一致；Baseline速度满足Elastic约束。Elastic连续LP成功、速度圆合法，第一失败明确为1cm/s局部3×3量化为空；fallback候选集不包含可行多边形的其他量化区域，因此错误落到stop violation。

[COMPUTED][HIGH] 已加入确定性half-plane/line-circle几何量化恢复和独立`GeometryRecovered`指标，不放宽任何安全约束。固定fixture、输入反序和Preferred微扰自动化通过。

[COMPUTED][HIGH] Development Editor、ORCA 10/10和完整`CrowdDemo.SF` 41/41通过；`git diff --check`通过。

[INFERRED][HIGH] 下一步若继续，只能重新运行原P0 Static Small单轮，确认step29不再出现并固定新的最早失败。该运行尚未执行，因此Production Elastic、Parallel安全门和SF4能力状态仍未通过。

## 当前停止点：step 40 Reproject HardPair

[COMPUTED][HIGH] 相同P0 Static Small单轮已完成。旧step29 stop violation不再成为首失败；新首失败为step40、Reproject、HardPair、ElasticWorsened，Agent 8–18，fixture hash=`410502020`。

[COMPUTED][HIGH] Twin ORCA infeasible/stop violation由旧`419`降为`183`，最终Source Q15由`12515/12538`升为`18108/18131`；但最终HardPair仍为`189/191`，Elastic比Baseline多2。

[COMPUTED][HIGH] Parallel ORCA stop下降为`126/93`，但HardPair=`3/3`、Target=`541/431`，Elastic Source Q15=`13205`低于Baseline `17857`。正式goal=`5`低于旧`6`，不能宣称整体能力改善。

[INFERRED][HIGH] 下一任务若继续，只能重放hash 410502020，依次检查PBD3输出、ObstacleReproject位移和pair 8–18的最终margin；不得返回调ORCA或接Production Elastic。
[COMPUTED][HIGH] Step40 Reproject联合安全归因已完成：新增Shadow-only、障碍可实现的HardPair polish与固定fixture；Development Editor、`CrowdDemo.SF4.Elastic` 7/7、完整`CrowdDemo.SF` 42/42通过，正式SF4 processors、PBD参数、地图和网络未修改。

[COMPUTED][HIGH] 相同P0完整单轮`CrowdDemo_8352_20260713_223511`中，旧step40不再出现；新首witness为step81 Obstacle HardPair，Agent 1–3，fixture hash=`3917118627`。该步PBD1以后两支HardPair均为0，因此它是暂态stage witness，不是最终Reproject失败。

[COMPUTED][HIGH] Twin Reproject HardPair为Baseline/Elastic=`7/6`，Source forward Q15仍为`18108/18131`；Parallel HardPair=`0/3`、Target=`536/431`、ORCA stop violation=`141/93`、Source forward=`17857/13205`。Elastic长期安全和相对Source前进均未通过，禁止接Production。

[INFERRED][HIGH] 下一步若继续，应先修正首失败选择语义：单独保留各stage witness，但HardPair安全失败只以PBD3/Reproject终态或可证明传播到终态的lineage钉住；随后从仍存在的Reproject累计6次或Parallel Elastic 3次中捕获最早终态fixture。禁止为消除step81日志而放宽Obstacle或提前绕过PBD。
