# MassAI Crowd Demo 目的与目标效果

## 1. 文档职责

[INFERRED][HIGH] 本文件是 Demo 目的、最终目标效果与长期架构原则的稳定事实源；`CurrentArchitecture.md` 描述当前实现，`PhasePlan.md` 描述当前任务，`FeatureChecklist.md` 描述验收状态。

## 2. Demo 来源与目的

[KNOWN][HIGH] 本 Demo 基于 `E:\Projects\SuperInvincibleTank_BugFix` 中已有 MassAI 实验、代码和设计文档形成独立验证工程。

[INFERRED][HIGH] 独立工程用于隔离群体运动、复制、视觉与业务变量，建立小而可信、可复现、可逐阶段验收的技术验证器；它不是对原工程的整体替代。

[INFERRED][HIGH] Demo 总目的，是使用 Unreal Mass 验证适用于大规模虫群的“群体驱动 + 个体修正”运动架构。

## 3. 群体驱动 + 个体修正

```text
群体调度层
├── 选择 cohort/群体目标
├── 生成 Shared Flow 与 Target-relative Navigation Cells
├── 聚合 Region 人口、容量、缺口与供给
├── 计算共享 Cell Edge transport quota
└── 输出稳定、可批量消费的宏观 guidance

个体实体层
├── 保存 AgentId、位置、速度、尺寸和业务状态
├── 按 AgentId 消费群体 quota/guidance
├── 执行 Particle Soft/Hard/Environment 局部修正
├── 保留受击、攻击、死亡等个体事件边界
└── 提交最终 Transform/Velocity
```

[INFERRED][HIGH] 每个虫子仍是完整业务实体，但不应独立重复执行完整目标生成、Nav/EQS 查询或整体路线规划。

[INFERRED][HIGH] WORK processors 应承担大规模、可批处理、稳定排序的事实生成；GT processors 只承担必须依赖世界对象、复制或视觉提交的边界工作。

## 4. 目标效果

[INFERRED][HIGH] Demo 最终应证明：自由游荡和静态/动态目标追逐都由共享宏观场驱动；虫群能绕障、过通道、围绕目标自然分布，并通过局部粒子约束维持硬安全和可压缩软间距。

[INFERRED][HIGH] Target 周围不使用永久 Slot 或 per-agent Region owner；宏观层只维护可行 Polar Navigation Cells、固定 Demand Regions、PlanEpoch 和 Cell Edge quota。

[INFERRED][HIGH] 不同半径、有效距离和 Mobility 的实体应在同一套通用规则下产生差异，不为具体 Agent、墙边实体或特定地图添加特殊生产分支。

[INFERRED][HIGH] Server/Client 必须使用相同 processors 与纯 kernels；correction 只在 fixed-step boundary 应用；客户端完整显示全部实体，不通过隐藏实例或视觉偏移伪造效果。

## 5. 当前实现与目标差距

[COMPUTED][HIGH] 当前已实现 Shared Flow V2、Target Region Transport、SoftPressure Particle、双端 hash、correction rollback 和 client-only visual。

[COMPUTED][HIGH] 8417 Static Small 20 已证明 20/20 进入有效距离带并覆盖 16/16 可行 Demand Regions；8418 Moving Small 已证明 20/20 进入有效距离带、覆盖当轮 12/12 可行 Demand Regions，且 Plan/Guidance unrouted、validation failure 与 Particle 安全违规均为 0。

[COMPUTED][HIGH] 自由游荡、100/500、真实 WORK 调度边界、攻击、死亡和回原工程收敛尚未完成。

[INFERRED][HIGH] 当前下一差距不是继续修改 Small 的 Transport 参数，而是独立验证 100/500 规模、DebugGame、录像与真实 WORK 调度边界；Small 通过不能外推这些能力已经成立。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
