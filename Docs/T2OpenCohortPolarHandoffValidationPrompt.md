# T2 Open Cohort Movement Polar Handoff 实施与验收提示词

工程：`E:\Projects\SuperInvincibleTank_MASSAITEST`

## 任务目标

[COMPUTED][HIGH] 8425已证明T2的开放Shared Flow、Particle安全、双端同步、correction rollback和20/20完整显示成立，但其 `goal=16/20` 使用了已废弃的140cm点目标终态合同。

[INFERRED][HIGH] 本任务只为 `OpenCohortMovement` 接入现有目标终态架构，使T2按以下链路完成端到端验证：

```text
Shared Flow远程接近
→ Target Region Transport极坐标运输
→ Distance Band自然落位
→ Particle Soft/Hard/Environment局部安全
```

## 先读取

1. `Docs/DemoPurposeAndTargetEffect.md`
2. `Docs/TestScenarioMatrix.md`
3. `Docs/CurrentArchitecture.md`
4. `Docs/PhasePlan.md`
5. `Docs/FeatureChecklist.md`
6. `Docs/TargetRegionTransportFieldDesign.md`
7. `Docs/CrowdTransitCapabilityDesign.md`
8. `Source/MassAICrowdDemo/CrowdDemoRoundSimCoordinator.*`
9. `Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimProcessors.*`
10. `Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.*`
11. `Source/MassAICrowdDemo/Mass/CrowdDemoTargetRegionTransportKernel.*`
12. `Source/MassAICrowdDemo/Mass/CrowdDemoOpenCohortMovementKernel.*`

## 实施边界

- [INFERRED][HIGH] 保留T2现有无障碍Flow配置、`10×2`、128cm间距布局、20实体和现有Particle P0参数。
- [INFERRED][HIGH] 只在 `OpenCohortMovement` 启用时复用T5已验证的Target Influence、Target Region Transport和Distance Band纯kernel/processors。
- [INFERRED][HIGH] Polar domain外只消费Shared Flow；进入domain后按现有owner顺序使用Transport guidance；满足Demand的终端实体使用Distance Band settle。
- [INFERRED][HIGH] `flow_goal_reached_count`只保留为宏观接近/handoff诊断，不得作为最终能力门。
- [INFERRED][HIGH] 不增加永久Slot、PositionId、per-agent Region owner、ORCA、Portal、passing band或场景专用避让。
- [COMPUTED][HIGH] 不放宽94cm Hard门，不修改30Hz、Particle迭代、网络频率、chunk size、复制预算、地图或Lighting。
- [INFERRED][HIGH] 不进入T3、T4、T6、100/500、DebugGame正式门、真实WORK、投射物、攻击、伤害或死亡。

## Processor合同

```text
RoundPlanApply
→ SharedFlowFieldBuild
→ FlowPreferredVelocity
→ TargetPolarTopologyBuild
→ TargetRegionPopulationBuild
→ TargetRegionTransportSolve
→ TargetRegionGuidance
→ MovementPredict
→ ParticleConstraintSolve
→ MovementFinalize
→ AuthorityCommit / ClientPredictionCommit
```

[INFERRED][HIGH] `MovementFinalize` 仍是 `FCrowdDemoRoundSimStateFragment` 唯一写入点；Coordinator只处理Plan/Result/correction和紧凑指标，不承载Transport或Particle算法。

## 必要自动化

- [INFERRED][HIGH] T2配置只在本testcase启用Target Region Transport，T1/T3/T4不被误启用。
- [INFERRED][HIGH] Shared Flow→Transport→Terminal settle的guidance owner顺序稳定，不会同一boundary同时写入两份Preferred。
- [INFERRED][HIGH] 进入Transport后离开140cm圆不计为能力失败；point-goal sticky不得将实体强制归零或拉回中心。
- [INFERRED][HIGH] 输入乱序不改变Topology/Demand/Transport/Guidance/candidate/applied hash。
- [INFERRED][HIGH] correction rollback恢复Transport prepared SoA、PlanEpoch、quota、settling状态、指标样本和hash，重放不重复累计。
- [INFERRED][HIGH] 增加T2紧凑指标：`flow_approach_entered_count`、`transport_handoff_count`、`inside_effective_band_count`、`feasible_region_count/coverage_count`、`plan/guidance_unrouted_count`、`transport_validation_failure_count`、`terminal_settled_count/step`。

## 验证顺序

1. `git diff --check`
2. Development Editor
3. T2定向自动化
4. Target Region Transport自动化
5. Particle 23项
6. 完整 `CrowdDemo.SF`
7. 端口8426，原P0，T2 Small 20，server/client，correction replay，30秒单轮

## T2新验收门

- [INFERRED][HIGH] `inside_effective_band_count=20`。
- [INFERRED][HIGH] `feasible_region_coverage_count=feasible_region_count`。
- [INFERRED][HIGH] `max_region_population <= ceil(20/feasible_region_count)+1`。
- [INFERRED][HIGH] Plan/Guidance unrouted=0，Transport invalid/validation failure=0。
- [INFERRED][HIGH] terminal settled=20/20，final deadlock=0。
- [INFERRED][HIGH] Hard/Swept/Obstacle/Bounds=0，Particle invalid/fallback=0。
- [INFERRED][HIGH] candidate/applied与Transport五类hash双端一致。
- [INFERRED][HIGH] rollback snapshot miss/mismatch=0，checkpoint/interval position error p95<1cm。
- [INFERRED][HIGH] agents=visible=20/20，无Fatal、Assertion、Ensure、`LogWindows: Error`或VIOLATION。

[INFERRED][HIGH] `flow_goal_reached_count`、never-reached、reached-then-left和mask-8只作为旧合同对照诊断，不得覆盖上述新能力门。

## 停止条件

[INFERRED][HIGH] 若新能力门失败，记录第一失败阶段是Flow approach、handoff、Topology/Demand、Transport、Guidance、Particle还是terminal settle，然后停止；不恢复140cm点目标，不进入T3，不调Particle/Network参数。

[INFERRED][HIGH] 若Small单轮通过，更新 `CurrentArchitecture.md`、`PhasePlan.md`、`FeatureChecklist.md`、`TestScenarioMatrix.md`和`DemoPurposeAndTargetEffect.md`，分别记录自动化、双端技术门、新能力门和准确停止点；本任务随后停止。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
