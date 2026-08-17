# 2026-07-18 架构收敛前状态索引

[COMPUTED][HIGH] 本文件保存`5bca3ea`之后启动架构收敛前的历史索引。完整旧文档和逐端口正文可从Git提交`5bca3ea`读取；当前文档不再复制这些流水账。

[COMPUTED][HIGH] 历史实验包括SF2 Soft Separation/PBD、SF3 Traffic/Portal/ORCA、SF4 Position/Holding/Commit/Elastic/Joint、旧Polar Density、TargetApproach/TargetSlot和多轮fixture诊断。

[COMPUTED][HIGH] 旧能力记录曾同时使用26、31、34、35、43、46、63等不同`CrowdDemo.SF`数量；这些数字属于对应历史提交，不能作为当前注册测试数量。架构收敛基线实际注册107项`CrowdDemo`自动化。

[COMPUTED][HIGH] 旧T5S结果曾为`settling_steps=-1`；2026-07-18端口8732已得到settling=595和完整90步稳定窗口，因此旧失败只保留为历史证据。

[COMPUTED][HIGH] T6M严格30秒历史结果为19/20；30+15秒宽限曾达到20/20。该时间合同仍需在架构收敛后重新回归，不能由T6A结果替代。

[COMPUTED][HIGH] 端口8645–8719及更早运行的详细输出保留在旧文档Git版本与`Saved/CrowdDemo`，不再进入当前架构、阶段计划和检查表。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
