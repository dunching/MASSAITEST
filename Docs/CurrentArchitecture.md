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

[COMPUTED][HIGH] `MovementFinalize`是普通fixed-step中`FCrowdDemoRoundSimStateFragment`的唯一写入点；PlanApply与correction只在boundary恢复或应用状态。

## 3. GT / WORK与数据所有权

[COMPUTED][HIGH] Coordinator只负责RoundPlan、版本化RoundResultHeader、checkpoint/correction chunks、readiness和紧凑汇总，不承载Flow、Transport、Local Predictive或Particle算法。

[COMPUTED][HIGH] Guidance Compose、Local Predictive与Particle已经使用不可变POD输入在WORK线程执行纯kernel，GT等待完成后统一写回对应Mass fragments。

[COMPUTED][HIGH] 当前尚未达到“整个boundary只读取Mass一次”：Business、Flow与Target Topology/Demand/Plan/Guidance仍由分阶段GT processors准备；最终Mass archetype也尚未按能力拆分。因此当前结论是首批真实WORK边界成立，不是完整GT/WORK迁移完成。

[COMPUTED][HIGH] Rollback snapshot已停止复制Target topology、demand、guidance和短期plan大数组；短期plan以资源key引用不可变资源，snapshot只保存quota/claim等可变执行态、累计器和游标。恢复后按Target Fact重建派生结果。

[COMPUTED][HIGH] Server与Client运行同一fixed-step driver和纯C++ kernels；客户端visual只读取client RoundSim/visual state并提交ISM，不计算gameplay movement。

## 4. 已删除的当前兼容面

[COMPUTED][HIGH] TargetApproach、TargetSlotLayout、旧Polar Density及其execution diagnostic已从settings、fragment、kernel、processor、rollback、metrics、CLI与自动化中删除；Target Fact已提取为独立纯kernel。

[COMPUTED][HIGH] RoundResultHeader继续使用版本化NetSerialize并通过2048字节上限测试；AgentState只经100-agent checkpoint chunks传输。

## 5. 当前验证事实

[COMPUTED][HIGH] 2026-07-19：Development、DebugGame Editor、`git diff --check`及删除后当前95/95项`CrowdDemo`自动化通过。

[COMPUTED][HIGH] 收敛后20实体fixed-step p95：T1=`1.131ms`、T2=`3.073ms`、T3=`2.938ms`、T4=`3.376ms`、T5S=`5.362ms`、T6A=`3.114ms`、T6S=`4.261ms`、T7热复跑=`1.739ms`、T8=`1.598ms`；这些通过运行的realtime均不低于1.000，step-limit hit为0。

[COMPUTED][HIGH] T1的staging/active测试布局切换现已显式归类：普通`non_correction_discontinuity=0`，测试boundary reset jump=21，Round reset jump=20；不得把测试夹具换布局混入普通移动连续性失败。

[COMPUTED][HIGH] T7首次冷运行出现client frame p95=`112.235ms`、collapsed steps p95=`4`；同代码同参数热复跑为client frame p95=`5.055ms`。该差异尚未唯一归因，当前只能记录为冷启动/运行环境性能不稳定，不能删除失败证据。

## 6. 准确停止点

[COMPUTED][HIGH] 尚未完成T5 Moving、T6 Moving、单进程DebugGame PIE、当前版人工审片、完整单次Mass读取/原子提交和按能力archetype拆分。

[INFERRED][HIGH] 下一步必须先关闭T7冷启动性能不稳定，并完成上述20实体门；此前不进入100/500、自由游荡或新战斗能力。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
