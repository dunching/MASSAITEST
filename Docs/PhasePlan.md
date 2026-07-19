# MassAI Crowd Demo 当前阶段计划

## 当前唯一阶段：架构收敛与20实体回归

[COMPUTED][HIGH] 旧兼容面、Guidance覆盖写入和大数组rollback已经收敛；当前停在“首批POD WORK边界已成立，但20实体完整性能/人工门和完整GT/WORK尚未关闭”。

## 已完成

- [x] [COMPUTED][HIGH] 删除TargetApproach、TargetSlotLayout、旧Polar Density及其生产/测试兼容面。
- [x] [COMPUTED][HIGH] 建立唯一Guidance Compose writer及稳定provider优先级。
- [x] [COMPUTED][HIGH] Rollback改为不可变plan资源引用加小型可变执行态。
- [x] [COMPUTED][HIGH] 异构Target调试标记按Capability Profile绘制；性能阶段拆为11个准确阶段。
- [x] [COMPUTED][HIGH] Guidance Compose、Local Predictive、Particle进入不可变POD WORK线程。
- [x] [COMPUTED][HIGH] 修正T1测试boundary reset与普通视觉不连续混算；普通不连续现为0。
- [x] [COMPUTED][HIGH] Development、DebugGame与当前95/95自动化通过。

## 当前失败与下一步

1. [COMPUTED][HIGH] T7首次独立运行client frame p95为112.235ms，热复跑为5.055ms；冷启动性能不稳定尚未归因。
2. [INFERRED][HIGH] 先增加可区分Game/Render/GPU/资源热身的客户端性能证据，并复跑T7；不得用单次热复跑覆盖首轮失败。
3. [INFERRED][HIGH] 随后补跑T5M、T6M和单进程DebugGame PIE，并完成人工审片。
4. [INFERRED][HIGH] 只有20实体全部门关闭后，才继续把Target/Business准备合并为整boundary单次Mass读取，并按能力拆分archetype。

## 保护与停止门

[INFERRED][HIGH] 每阶段均执行`git diff --check → Development → 定向自动化 → 全部当前自动化`；行为、确定性、安全或稳定性能回退时停止。

[COMPUTED][HIGH] 不stage、commit、push，不修改地图、Lighting、30Hz、Particle硬门、网络频率、chunk size或复制预算；不进入100/500。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
