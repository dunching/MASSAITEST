# 历史记录索引

## 2026-07-17 SF2–SF4第一版退役清理

[COMPUTED][HIGH] 正式场景入口收缩为0/1，fixed-step driver停止构造和调用SF2旧Separation/PBD、SF3 Traffic/Portal/ORCA与SF4 Position/Holding/Elastic/Joint生产processor。

[COMPUTED][HIGH] 已删除8张旧地图、SF3/SF4地图生成脚本和37项SF3/SF4自动化；旧混合测试文件中的4项当前基础测试迁入`CrowdDemoCoreDeterminismTests.cpp`，旧PBD测试退出当前测试集。

[COMPUTED][HIGH] 第一版清理后Development Editor、`git diff --check`和当前`CrowdDemo.SF` 26/26通过。

[COMPUTED][HIGH] 旧UCLASS实现、Pipeline state、Mass fragment、metrics和纯kernel仍作为不可达编译期代码存在，留待第二版物理删除；不得将第一版描述成Source已经完全清零。

[INFERRED][HIGH] Demo 的目的、最终目标效果和长期架构原则以 `DemoPurposeAndTargetEffect.md` 为稳定事实源。

[COMPUTED][HIGH] 旧业务、R4–R6、PA1、SF2–SF4、Portal、ORCA、Holding、Reservation、Elastic、Joint及历史 RoundSim 只属于历史实验，不属于现行路线。

[COMPUTED][HIGH] 现有简要历史归档位于 `Docs/History/LegacyBusinessAndRoundSim.md`；更完整的旧设计、实施提示词和逐端口归因可从 Git 历史查阅。

[INFERRED][HIGH] 当前实现、下一步、通过项、Target Region Transport、Particle规则和正式场景分别只以 `CurrentArchitecture.md`、`PhasePlan.md`、`FeatureChecklist.md`、`TargetRegionTransportFieldDesign.md`、`CrowdTransitCapabilityDesign.md` 和 `TestScenarioMatrix.md` 为准。

## 2026-07-15 T5 Target Region Transport checkpoint

[COMPUTED][HIGH] 本 checkpoint 已完成动态 `FeasibleGraphHash`、实际 soft-clearance cost、Plan validator、Guidance quota consumption、round-sticky 指标、双端 hash、失败 fixture 与 correction rollback 接入。

[COMPUTED][HIGH] 8416 step 331 fixture 将旧 Moving 失败唯一归因为 terminal-anchor Supply 被 terminal sink 直接吸收；通用修复要求该 Supply 先消费一条真实 Topology Edge，不包含 AgentId、CellId 或场景特判。

[COMPUTED][HIGH] 8417 Static Small 通过 `inside=20/20、coverage=16/16、Plan/Guidance unrouted=0、invalid/validation failure=0`；8418 Moving Small 通过 `inside=20/20、coverage=12/12、Plan/Guidance unrouted=0、invalid/validation failure=0`，两者 Particle 四类安全违规均为 0，五类 Transport hash 双端一致。

[COMPUTED][HIGH] Development Editor、Target Region Transport 5/5、Particle 23/23 与完整 `CrowdDemo.SF` 43/43 已通过；100/500、DebugGame 正式门、录像和真实 WORK 调度仍未执行，不能由 Small 结果外推。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
