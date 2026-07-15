# 历史记录索引

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
