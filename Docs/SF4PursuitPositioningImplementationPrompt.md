# SF4 确定性追逐与稳定站位实施提示词

> [COMPUTED][HIGH] **已废止，不得直接执行。** 本提示词对应原始Candidate/Assignment与后续Polar Approach/Phase Reservation演进前的实施路线。当前权威设计已在`Docs/SF4PursuitPositioningDesign.md`顶部修订为Steering-first Holding/Commit；当前任务以`Docs/PhasePlan.md`顶部为准。
>
> [INFERRED][HIGH] 后续实现必须保留Shared Flow、Candidate/Assignment、Deterministic ORCA、Obstacle/PBD、双端fixed-step和rollback，停止扩展Radial/Angular/Phase polyline reservation与Route-Aware ORCA特例；第一阶段只允许Holding Position与Commit Gate纯kernel fixture。本文以下内容只作为历史实施输入保留。

以下内容是已废止的历史提示词，不得作为新Codex任务直接执行。

---

工程：

`E:\Projects\SuperInvincibleTank_MASSAITEST`

任务：

按照 `Docs/SF4PursuitPositioningDesign.md`，逐阶段实施SF4确定性追逐与稳定站位。必须严格执行阶段门控；任何硬门失败立即停止，不自动进入下一阶段。

## 一、任务目标

[INFERRED][HIGH] 新增隔离场景`ECrowdDemoScenario::SimRoundPursuitPositioning=3`，验证20个实体对静态与动态目标的：

```text
共享追逐
→ 近目标Candidate生成
→ 确定性容量分配
→ StableOccupied / ReserveHold
→ 目标移动后的reservation保持、失效和重分配
```

[COMPUTED][HIGH] 本任务不实现攻击、受击、死亡、技能、业务StateTree、NavMesh Bake、P1、100/500规模或通用A/B框架。

## 二、开始前必须读取

### 当前Demo事实源

1. `Docs/DemoPurposeAndTargetEffect.md`
2. `Docs/CurrentArchitecture.md`
3. `Docs/PhasePlan.md`
4. `Docs/FeatureChecklist.md`
5. `Docs/SF4PursuitPositioningDesign.md`

### 当前Demo代码

1. `Source/MassAICrowdDemo/CrowdDemoTypes.h`
2. `Source/MassAICrowdDemo/CrowdDemoRoundSimCoordinator.*`
3. `Source/MassAICrowdDemo/CrowdDemoScenarioRegistry.*`
4. `Source/MassAICrowdDemo/Arena/CrowdDemoTargetActor.*`
5. `Source/MassAICrowdDemo/Mass/CrowdDemoMassFragments.h`
6. `Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimProcessors.*`
7. `Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.*`
8. `Source/MassAICrowdDemo/Mass/CrowdDemoSharedFlowFieldKernel.*`
9. `Source/MassAICrowdDemo/Mass/CrowdDemoTrafficSchedulingKernel.*`
10. `Source/MassAICrowdDemo/Mass/CrowdDemoDeterministicOrcaKernel.*`
11. `Source/MassAICrowdDemo/CrowdDemoDeterminismTests.cpp`
12. SF3地图创建与运行脚本。

### 原工程参考，只读

1. `E:\Projects\SuperInvincibleTank_BugFix\Docs\AI_STATE\PROJECT_DESIGN\25_AttackPointServicePlan.md`
2. `E:\Projects\SuperInvincibleTank_BugFix\Docs\AI_STATE\PROJECT_DESIGN\26_MassEnemyAttackZoneReservationPlan.md`
3. `E:\Projects\SuperInvincibleTank_BugFix\Docs\AI_STATE\PROJECT_DESIGN\34_MassAISwarmBusinessLogicContract.md`
4. `E:\Projects\SuperInvincibleTank_BugFix\Docs\AI_STATE\PROJECT_DESIGN\35_MassAIArchitectureBoundaries.md`
5. `E:\Projects\SuperInvincibleTank_BugFix\Docs\AI_STATE\PROJECT_DESIGN\38_MassAIProcessorPipelineContract.md`
6. `E:\Projects\SuperInvincibleTank_BugFix\Source\Gameplay\Systems\Enemy\ProjectAttackPointTypes.*`
7. `E:\Projects\SuperInvincibleTank_BugFix\Source\Gameplay\Systems\Enemy\ProjectAttackPointSubsystem.*`
8. `E:\Projects\SuperInvincibleTank_BugFix\Source\MassAIFramework\Public\Mass\ProjectMassNPCClusterPlanner.h`
9. `E:\Projects\SuperInvincibleTank_BugFix\Source\MassAIFramework\Private\Mass\ProjectMassNPCClusterPlanner.cpp`

[INFERRED][HIGH] 只复用原工程的业务概念，不复制其UWorld subsystem、逐消费者请求、TMap扫描、legacy fallback或大规模历史调参路径。

## 三、工作区保护

1. [COMPUTED][HIGH] 开始前运行`git status --short`和`git diff --check`。
2. [INFERRED][HIGH] 识别所有用户未提交文件；不还原、不覆盖、不暂存、不提交无关变更。
3. [INFERRED][HIGH] 四张SF3 Lighting地图及用户历史文档改动默认视为受保护文件。
4. [INFERRED][HIGH] 新地图必须新建真实package，不能重建或覆盖已有SF1/SF2/SF3地图。
5. [INFERRED][HIGH] 每次提交只显式暂存本阶段文件。

## 四、基线门

实施前必须完成：

1. Development Editor编译。
2. DebugGame Editor编译。
3. 完整`CrowdDemo.SF`自动化。
4. `git diff --check`。

[INFERRED][HIGH] 任一步失败立即停止，报告失败，不进入SF4实现。

## 五、阶段A：纯C++ Candidate与Assignment kernels

### 5.1 数据边界

新增或等价实现：

```cpp
FCrowdDemoPursuitTargetFact
FCrowdDemoPursuitPositioningSettings
ECrowdDemoPositionRole
ECrowdDemoPursuitPositionState
FCrowdDemoPositionCandidate
FCrowdDemoPositioningAgent
FCrowdDemoPositionAssignment
FCrowdDemoPositioningSummary
```

[INFERRED][HIGH] Candidate catalog、proposal lists和全局assignment保存在PipelineSubsystem prepared SoA；每实体fragment只保存最小assignment消费状态。

### 5.2 Candidate kernel

新增纯C++ `FCrowdDemoPursuitPositioningKernel`或职责等价的独立kernel。

必须满足：

1. 根据Target radius、Agent radius、Allowed距离和SafetyGap生成annulus。
2. 第一版只使用现有FlowField raster判断bounds、blocked、reachable和clearance。
3. 一个candidate capacity=1。
4. Candidate中心距离不得小于实体直径加SafetyGap。
5. Front与Reserve分开生成。
6. 排序固定为`Role → RadialBand → AngularSector → StableCellKey`。
7. `PositionId`由稳定物理输入hash生成，不依赖插入顺序。
8. 不访问UWorld、UObject、Actor、Mass fragments或导航系统。

### 5.3 Assignment kernel

必须使用批量确定性分配，禁止逐Agent即时占位。

评分使用量化整数：

```text
TravelDistance
+ PreferredRangePenalty
+ ApproachSectorChangePenalty
+ CandidateConflictPenalty
+ ReassignmentPenalty
- ExistingAssignmentReuseBonus
```

决胜顺序固定为：

```text
Cost → ExistingOwner → AgentId → PositionId
```

使用固定最大轮数的deterministic deferred-acceptance；最终结果按AgentId排序。

### 5.4 阶段A自动化

至少覆盖：

- 单目标20容量fixture。
- blocked、unreachable、bounds和clearance过滤。
- Candidate spacing与无重叠。
- Candidate输入乱序。
- Agent输入乱序。
- PositionId稳定。
- 20个唯一assignment。
- Existing assignment复用。
- Front不足时Reserve分配。
- Vacancy promotion。
- Candidate失效与release。
- 同分AgentId/PositionId决胜。
- 两轮Candidate/Assignment hash一致。

[INFERRED][HIGH] 阶段A测试、Development、DebugGame和完整`CrowdDemo.SF`任一失败立即停止。

[INFERRED][HIGH] 阶段A通过后建议提交：

```text
Add deterministic SF4 pursuit positioning kernels
```

## 六、阶段B：Static Target Small 20集成

### 6.1 场景与地图

新增：

```cpp
SimRoundPursuitPositioning = 3
```

新增真实地图package：

```text
/Game/Maps/CrowdDemo_SimRoundPursuitPositioningStaticSmall
```

[INFERRED][HIGH] 地图必须通过Unreal Python `delete_asset → new_level → save_current_level`创建，不复制umap。

[INFERRED][HIGH] 地图必须包含用户指定Lighting集合：DirectionalLight、ExponentialHeightFog、SkyAtmosphere、SkyLight、SM_SkySphere、VolumetricCloud，并保持现有可读曝光与俯视相机风格。

### 6.2 Processor集成

严格顺序：

```text
RoundPlanApply
→ TargetFactApply
→ SharedFlowFieldBuild
→ CrowdTrafficFieldBuild
→ PortalSchedule
→ PositionCandidateBuild
→ PositionAssignment
→ FlowPreferredVelocity
→ PassingBandGuidance
→ PursuitPositionGuidance
→ MovementIntentCompose
→ DeterministicORCA
→ MovementPredict
→ ObstacleConstraint
→ HardSeparationPBD
→ ObstacleReproject
→ MovementFinalize
→ AuthorityCommit / ClientPredictionCommit
```

要求：

1. PositionCandidateBuild仅在Target/candidate generation dirty时重建。
2. PositionAssignment仅在fixed-step boundary且assignment dirty时执行。
3. Portal未Exited时positioning不能绕过Portal guidance。
4. MovementIntentCompose显式实现优先级，不允许processor静默覆盖速度。
5. StableOccupied/ReserveHold在arrival范围内preferred=0，只允许低增益位置回正。
6. StableOccupied实体仍作为ORCA邻居。
7. MovementFinalize仍是唯一写RoundSimState的位置。
8. correction仍只在fixed-step boundary应用。
9. client visual只显示client sim state，不计算gameplay movement。

### 6.3 状态机

实现：

```text
Pursuit
→ SlotCommit / ReserveCommit
→ StableOccupied / ReserveHold
→ Reacquire
→ Pursuit
```

要求：

- Arrival需要连续固定步成立。
- ExitTolerance大于ArrivalTolerance。
- PBD/Obstacle短暂推出arrival范围不能立即释放。
- Front vacancy允许稳定Reserve promotion。
- Assignment变化必须增加revision并进入hash。

### 6.4 Static Small硬门

使用20实体，至少连续两轮：

```text
corridor_exit = 20
corridor_deadlock = 0
position_assigned_count = 20
position_stable_occupied_count + position_reserve_hold_count = 20
position_candidate_overlap_count = 0
position_candidate_unreachable_count = 0
settle窗口后position_assignment_churn_count = 0
server/client obstacle penetration = 0
agents = visible_instances = 20
Candidate/Assignment/AgentState双端hash一致
两轮AgentState hash一致
checkpoint/interval/cross-round error不扩散
无Fatal/Assertion/Ensure/LogWindows: Error/VIOLATION
```

[INFERRED][HIGH] 任一硬门失败立即停止；不实现Moving Target，不调P1，不运行100/500。

[INFERRED][HIGH] Static通过后建议提交：

```text
Integrate SF4 static pursuit positioning
```

## 七、阶段C：Moving Target Small 20

### 7.1 Target fixed-step kernel

新增纯C++ deterministic target motion kernel或等价POD轨迹求值器。

目标路径必须覆盖：

```text
静止
→ 低速直线
→ 连续转角
→ 较高速移动
→ 减速
→ 停止
```

禁止：

- Actor render Tick驱动玩法位置。
- Server/Client各自积累frame DeltaSeconds。
- 目标位置视觉插值反写gameplay事实。

### 7.2 双Anchor

实现：

```text
RouteAnchor      = 有界预测后的共享追逐目标
EngagementAnchor = 当前权威Target位置
```

要求：

- Shared Flow追RouteAnchor。
- Candidate围绕EngagementAnchor。
- 小幅Target移动保留PositionId/LocalOffset并重新验证。
- 大幅移动或Candidate失效只重分配受影响assignment。
- 禁止全群同帧清空重抢。
- 高速目标阶段不强制StableOccupied。
- 目标比实体快时允许物理落后，禁止速度膨胀、传送和扩大visual correction。
- Target连续低速/停止达到固定步门槛后才开放稳定占位。

### 7.3 Moving地图

新增真实地图package：

```text
/Game/Maps/CrowdDemo_SimRoundPursuitPositioningMovingSmall
```

[INFERRED][HIGH] 使用与Static相同障碍、Flow配置、entity count、Lighting和相机，只增加确定性Target motion plan。

### 7.4 Moving Small硬门

至少连续两轮：

```text
corridor_exit = 20
corridor_deadlock = 0
无全群同步reservation release
无同步stop-go
无速度膨胀或传送
目标停止后在声明时间内settle
Candidate/Assignment/TargetFact/AgentState双端hash一致
两轮最终AgentState hash一致
server/client obstacle penetration = 0
agents = visible_instances = 20
checkpoint/interval/cross-round error不扩散
无Fatal/Assertion/Ensure/LogWindows: Error/VIOLATION
```

[INFERRED][HIGH] 任一硬门失败立即停止，不运行100/500，不实现攻击。

[INFERRED][HIGH] Moving通过后建议提交：

```text
Add deterministic SF4 moving target pursuit
```

## 八、指标

增加紧凑聚合指标：

```text
position_candidate_count
position_front_capacity
position_reserve_capacity
position_assigned_count
position_unassigned_count
position_stable_occupied_count
position_reserve_hold_count
position_assignment_reused_count
position_assignment_changed_count
position_assignment_churn_count
position_invalidated_count
position_promotion_count
position_promotion_wait_steps_p95
position_arrival_error_cm_p95
position_exit_after_occupied_count
position_candidate_overlap_count
position_candidate_unreachable_count
target_revision_count
target_reanchor_count
target_follow_error_cm_p95
target_stop_to_settle_seconds
position_candidate_hash
position_assignment_hash
target_fact_hash
```

禁止per-agent日志、timeline trace和通用A/B框架。

## 九、人工审片

[INFERRED][HIGH] 只有对应Small硬门全部通过后才录像。

Static审片检查：

- 20实例全部可见。
- 不再挤向同一goal cell。
- Front与Reserve分布可读。
- 到位后不持续漂移或旋转。
- 无穿墙、隐藏实例、视觉假偏移和fixed-step跳变。

Moving审片检查：

- 群体先追逐，目标减速后逐步形成站位。
- Target移动时不发生全群同步换位。
- 高速阶段允许自然落后。
- Target停止后有界收敛。
- 无传送、速度膨胀、错误visual owner或correction驱动运动。

## 十、文档

每个完成阶段更新：

- `Docs/CurrentArchitecture.md`
- `Docs/PhasePlan.md`
- `Docs/FeatureChecklist.md`
- `Docs/SF4PursuitPositioningDesign.md`

[INFERRED][HIGH] 自动化、双端技术验证和人工审片必须分别记录。未通过项必须记录准确数值和停止点。

## 十一、停止边界

[COMPUTED][HIGH] 本任务禁止自动实施：

- 攻击、受击、死亡、技能或业务StateTree。
- 原工程AttackPointSubsystem回迁。
- NavMesh/Recast查询或Bake asset。
- Crowd Navigation Field。
- SF1/SF2/SF3参数调整。
- correction频率、chunk size、复制预算或NetUpdateFrequency调整。
- P1、100、500、Medium、Cohort或Crossing。
- timeline trace或通用A/B框架。

[INFERRED][HIGH] Static失败时停止在Static；Moving失败时停止在Moving。不得通过扩大容差、隐藏实例、提高速度、放宽ORCA或视觉偏移伪造通过。

## 十二、最终输出

完成或触发停止点后输出：

1. 复用与替换边界。
2. 修改文件。
3. Candidate生成与Assignment确定性规则。
4. Processor顺序和Movement Intent优先级。
5. Static与Moving分别完成到哪一阶段。
6. 自动化结果。
7. 双端技术指标。
8. 人工审片结论或未录像原因。
9. 未通过项及准确原因。
10. 提交记录。
11. 保留未提交用户文件。
12. 明确确认没有进入攻击、NavMesh Bake或100/500阶段。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

---
