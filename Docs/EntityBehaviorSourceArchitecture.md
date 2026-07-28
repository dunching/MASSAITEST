# 实体行为能力 Source 架构（B0–B7）

## 1. 权威边界

[COMPUTED][HIGH] 行为权威已经从单一 `ActiveBehavior` 与 Behavior Provider 选择迁移到 `FCrowdBehaviorSourceRuntime`。Runtime World Subsystem 按 `FCrowdStableEntityRef` 保存 SourceSet；Mass Fragment 只保留紧凑的非权威 `DerivedBehaviorLabel`。

[COMPUTED][HIGH] `Local Predictive`、Particle、障碍、边界和最终量化仍位于行为解析后的强制移动安全阶段，不是可卸载 Source。

[COMPUTED][HIGH] Source Handle 的正式键为 `EntityRef + ControllerId + SourceSequence`；命令排序键为 `EffectiveFixedStep + EntityRef + ControllerId + CommandSequence`。

## 2. Core POD 与容量

[COMPUTED][HIGH] `MassCrowdBehaviorSource.h`定义稳定数值 ID、Capability Profile/Binding、最多 8 项 Modifier、Source Spec/Command/Instance/Set/Event、固定 Payload 和六个 Contribution 通道。

[COMPUTED][HIGH] 每实体活动 Source 上限为 16、Controller 上限为 8、每通道 Contribution 上限为 32。未知类型、Schema 不符、缺失 Capability、重复 Handle、非法混合模式或超限均使整个 staged Boundary 失败。

[COMPUTED][HIGH] Capability Profile Registry 与 Source Spec Registry 在注册后冻结；重复 ID、非法 Profile、未排序 Capability 或不一致 Spec 无法进入冻结后的执行路径。

## 3. 生命周期与 Resolver

[COMPUTED][HIGH] Source 状态机支持 Start/Update/Stop、严格命令序号、最近命令幂等重放、序号缺口拒绝、到期停止与 Capability 撤销。一次成功 Boundary 至多增加一次 SourceSet Revision；失败不暴露 staged Source 或事件。

[COMPUTED][HIGH] Resolver 对所有输入先按 `Priority 降序 → SourceTypeId → ControllerId → SourceSequence` 排序。Movement 支持 Override、Q15 WeightedAdd、Additive；Facing 独立解析；Constraint 合并速度上下限、移动锁和 NavLayer 交集；Interaction 选独占胜者；Business 冲突拒绝；Presentation 按属性解析。

[COMPUTED][HIGH] HitReaction、Stun 与 Death 只通过 Constraint 压制移动。Cargo、任务、Facing、Presentation 和低优先级持久 Source 不因临时反应 Source 被删除。

## 4. Runtime Boundary

[COMPUTED][HIGH] Runtime 的固定数据流为：读取 SourceSet/Capability 快照、在副本应用到期命令、Evaluate、Resolve、生成 Prepared Entity/Boundary Hash、宿主预验证、最终 Commit。

[COMPUTED][HIGH] Prepared Boundary 校验会重新计算每实体 Hash、SourceSet/Command/Resolved 聚合 Hash、实体排序和最终 Stable Hash；任何不一致都在写入前拒绝。

[COMPUTED][HIGH] Commit Envelope 已升级为 v3，包含 SourceSet Revision/Hash、Command Batch Hash、Resolved Channel Hash、Movement/Facing 结果与有序 Patch Descriptor。

## 5. 迁移与 StateTree

[COMPUTED][HIGH] `FCrowdLegacyBehaviorRecipe`只作为迁移输入，把旧 Wander、MoveTo、Pursue、Guard、Flee、Pickup、Deliver、Attack、Dead 标签展开为多个 Source Command。旧枚举不存入权威 Fragment、网络 Agent 字节或业务账本。

[COMPUTED][HIGH] Mixed Sandbox 的移动、Facing、物流、攻击和受击约束已由 Source Runtime 组合；Source 权威实例来自 `UMassCrowdRuntimeSubsystem` 的 World Store，Demo 只提交命令并消费 Resolved/Business 输出。

[COMPUTED][HIGH] 可选 `MassCrowdStateTreeAdapter` 单向依赖 Runtime、StateTree 与 GameplayStateTree。Task 只能提交 Source Command 或等待已提交 Event，不能直接写 Mass Fragment、移动结果或业务账本。

## 6. 网络协议

[COMPUTED][HIGH] Relevant Snapshot、Lifecycle Batch、Apply Frame 与通用 Codec 已升级到 v2；v1 输入明确拒绝。

[COMPUTED][HIGH] Agent 复制记录不再携带单字节 Behavior，改为 Capability Profile/Modifier Revision、SourceSet Revision/Hash、Resolved Hash 和非权威诊断 Label。可靠类型包含 Source Command、SourceSet 与 ResolvedBehaviorState。

[COMPUTED][HIGH] Source Command/SourceSet Codec 校验版本、稳定 EntityRef、容量、Payload Schema、SourceSet Hash 和 Resolved Hash；乱序、重复、缺口及通用 reliable resync 继续由公共 Channel 合同处理。

## 7. 验收证据

[COMPUTED][HIGH] 自动化覆盖 Profile/Modifier、Source 上限、命令冲突/缺口/幂等、到期、Capability 撤销、Resolver 顺序无关、Q15、临时 HitReaction 恢复、Prepared Hash 篡改、跨通道原子提交、v1 拒绝/v2 Codec 与 StateTree 物流中断恢复。

[COMPUTED][HIGH] 2026-07-28 最终重跑已通过 DebugGame `-DisableUnity`、Development `-ForceUnity`、`MassCrowd 50/50` 和 `CrowdDemo 115/115`。

[COMPUTED][HIGH] 真实双端规模门已按仓库既有顺序完成：8202的20实体Mixed Sandbox、8210的100实体SoftPressure、8215的500实体Obstacle。500实体连续5轮服务端障碍穿透=0、客户端revision gap=0、最终位置误差p95=`0.014cm`，且没有bunch过大或硬错误。

[COMPUTED][HIGH] 8216的20实体T8在自定义紧凑Agent序列化后保持攻击/投射/伤害=`50/50/50`、duplicate fire/hit=`0/0`和三类业务Hash双端一致。

[INFERRED][HIGH] Demo Round 的 Shared Flow、Target Region、Local Predictive 和 Particle 仍是 Resolver 之后的生产移动/安全内核；它们不重新获得行为 Source 的生命周期权威。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
