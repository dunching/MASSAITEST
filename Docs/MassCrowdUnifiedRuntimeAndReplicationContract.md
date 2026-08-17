# MassCrowd 通用运行与生产复制合同

## 2026-08-15 Worker Result Owner Commit 合同

[COMPUTED][HIGH] Runtime Commit Token 只冻结 Prepared Result 的 Generation、Publish/Input/Event 水位与 Stable Entity View revision。Runtime Barrier 必须先匹配 Token、执行一次 Proxy Final Validate，再调用 Host FinalValidate；任何拒绝都不得调用 Host Apply、Proxy Commit 或 Host side-effect callback。

[COMPUTED][HIGH] Host FinalValidate 成功后进入 no-fail 区：Host state apply → Proxy commit → Host state/Ordered Event/表现/网络 side effects。Dirty Batch ACK 不属于 Barrier 内写入，只能由成功提交后的消费者执行。Host-specific Mass、Target/Resource、Behavior、Round 或 Scenario 类型不得进入 Runtime 公共 API。

[COMPUTED][HIGH] Demo 通过 `FCrowdDemoPreparedRoundCommitPlan` 冻结自己的 PlanRevision、FixedStepIndex、Mass Handle/Lifecycle/Fragment collection 与 Target/Resource Owner/Revision/引用 Token。Runtime 不提供旧 Demo Barrier 的 alias、wrapper 或 fallback。

## 0. R版合同覆盖

[INFERRED][HIGH] 2026-07-28起，本合同以开放Behavior Provider、通用Boundary Scheduler和Mass权威Projectile为目标；旧B0–B7只保留历史证据，不再要求StateTree业务链、具体Demo Source或动画Root Motion Clip作为框架完成条件。

[INFERRED][HIGH] Core/Runtime只能拥有稳定ID、Schema、Source生命周期、六通道Resolver、通用Task/Patch调度和权威Mass数据合同；插件随包的`MassCrowdStandardSources`拥有通用运动/朝向/约束Evaluator；Demo Provider与宿主Adapter拥有目标选择、攻击、取货、交付等领域解释。

[INFERRED][HIGH] 网络基线必须携带Behavior Registry Hash；Predictable Source要求双端Registry一致。Source Codec v3必须包含Context/State Schema和持久实例状态，旧Behavior Codec拒绝。

[INFERRED][HIGH] Projectile的权威数据只存在于Mass Fragment；对象池仅负责实体复用，池外数组不得保存可独立推进的Projectile状态。

## 1. 文档职责

[INFERRED][HIGH] 本文件是通用Agent能力组合、持续生命周期、生产复制、Demo宿主和模块边界的长期合同。行为Source的详细数据模型、阶段状态和专项缺口以`EntityBehaviorSourceArchitecture.md`为事实源。

[INFERRED][HIGH] `CurrentArchitecture.md`只描述当前代码；`PhasePlan.md`只描述实施顺序；`FeatureChecklist.md`只记录已经满足的验收；`TestScenarioMatrix.md`只记录具体场景证据。旧设计文档中的日期快照不得覆盖这些现行文档。

## 2. 统一Agent模型

```text
FCrowdAgentFacts
├── StableEntityRef = ProviderId + StableEntityId + LifecycleSerial
├── Faction / Team
├── CapabilityProfileKey
├── bounded Capability Modifiers
├── Capability Binding
├── BusinessTaskRef
├── TargetRef
├── MovementProfile
├── PresentationProfile
└── non-authoritative DerivedBehaviorLabel

Runtime World Store[StableEntityRef]
└── FCrowdBehaviorSourceSet
    ├── Revision / StableHash
    └── sorted Source Instances
```

[INFERRED][HIGH] `ActiveBehavior`不属于权威Agent模型。多个Source可以同时贡献Movement、Facing、Constraint、Interaction、Business和Presentation。

[INFERRED][HIGH] `LifecycleSerial`拒绝槽位复用后的过期Spawn、Correction、Hit、Cargo、Source Command和Despawn事实；只比较短AgentId不足以形成生产身份。

[INFERRED][HIGH] Faction只用于关系、权限、目标过滤、伤害和交互规则；Faction不得授予Capability，也不得选择Movement、Networking、Presentation或安全实现。

## 3. Capability与Behavior Source

[INFERRED][HIGH] Capability Profile是不可变排序ID集合；实体使用Profile Key和最多8项Add/Remove Modifier。Boundary从Profile与Modifier生成有效Capability Binding。

[INFERRED][HIGH] Source Handle固定为`StableEntityRef + ControllerId + SourceSequence`。Source Spec使用稳定数值TypeId、Schema、Required Capability、Channel Mask、Priority、Exclusive Group、Lifetime和Replication Policy。

[INFERRED][HIGH] 每实体最多16个活动Source；Registry在首个Boundary前冻结；未知类型、Schema冲突、缺失Capability、重复Handle和超限必须整批拒绝。

[INFERRED][HIGH] Evaluator只读取不可变Context并写入有界Contribution Writer；不得直接写Mass Fragment、Actor、业务账本、网络或表现状态。

[INFERRED][HIGH] Recipe和StateTree只负责生成Start/Update/Stop Command，不直接拥有移动或业务权威。

[INFERRED][HIGH] 通用`MoveToLocation`、`FollowEntity`、`PursueEntity`、`FleeFromEntity`、`MaintainDistance`、Facing和Constraint Source应由插件的`MassCrowdStandardSources`提供；它们不得判断敌我、选择攻击目标或提交伤害/物流业务。

[INFERRED][HIGH] Escort、Combat和Logistics属于宿主Recipe：它们组合Standard Source与产品Business/Interaction Source，而不是在Runtime建立新的互斥Behavior中心。详细合同查阅`MassCrowdStandardSourcesDesign.md`。

## 4. Resolver与通用运动链

```text
Capability / SourceSet Snapshot
→ Apply due Source Commands to staged copy
→ Evaluate Sources
→ Resolve Movement / Facing / Constraint / Interaction / Business / Presentation
→ Shared Flow / Target Region / Guidance
→ Local Predictive
→ Movement Predict
→ Particle / Obstacle / Bounds Safety
→ Quantize
→ Prepared Patches
→ Validate complete set and hashes
→ Final Apply
```

[INFERRED][HIGH] Resolver排序固定为`Priority降序 → SourceTypeId → ControllerId → SourceSequence`。

[INFERRED][HIGH] Movement支持Override、Q15 WeightedAdd和Additive；Facing独立支持Override和Q15 WeightedAdd；Constraint合并min/max/lock/NavLayer交集；Interaction使用Exclusive Winner；Business冲突拒绝；Presentation按Property执行Override或Additive。

[INFERRED][HIGH] Local Predictive、Particle、障碍、边界和最终量化是不可卸载安全阶段。它们消费Resolver结果，但不拥有Source生命周期权威。

[INFERRED][HIGH] 所有阵营和行为复用同一安全链；Business决定“做什么和目标是什么”，Movement/Safety决定“如何安全执行”。

## 5. Boundary原子性

[INFERRED][HIGH] 跨 Worker Result Proxy、Mass 代理状态和宿主 side effect 的最终提交由 `MassCrowdRuntime` 通用 Owner Commit Barrier 协调：Prepare 产生不可变候选与 Commit Token，Final Validate 在首次写入前复核 Generation/Sequence/Stable View/Lifecycle 及宿主 Token，随后只调用预验证完成的 no-fail Host Apply；对外事件与 ACK 最后发布。Runtime 不解释 Demo Target、Combat、Round 或 Scenario 语义，宿主通过 Prepared Plan adapter 接入。

[INFERRED][HIGH] 测试宿主不保留旧 Barrier、Transaction 或 rollback 数据结构的兼容路径；替代实现获得业务与故障门证据后，应在同一切片物理删除旧生产者、消费者、类型和绑定旧结构的测试断言。

[INFERRED][HIGH] Boundary固定为一次不可变Gather、显式POD Overlay、依赖图WORK、稳定Merge、完整集合预验证和一次GT Final Apply。

[INFERRED][HIGH] 所有可能失败的业务检查必须发生在Final Apply之前；Final Apply采用已经验证后不可失败的合同。不得以部分写入后的补偿回滚冒充失败零写入。

[INFERRED][HIGH] WORK不得访问UObject、World或EntityManager；任何缺失、重复、stale lifecycle、错误revision、错误Hash或任务失败都必须整批零写入。

[INFERRED][HIGH] Commit Envelope v3覆盖SourceSet Revision/Hash、Command Batch Hash、六通道Hash、Movement/Facing结果、排序后的Patch Descriptor和最终Stable Hash。

## 6. Cohort与持续生命周期

[INFERRED][HIGH] Cohort由共享运动事实形成，包括ObjectiveKey、NavigationLayer、MovementProfile、CapabilityProfile、MacroStrategy和EnvironmentRevision；Cohort不等同于Faction。

[INFERRED][HIGH] Cohort membership支持fixed-step boundary增量加入、退出和迁移，不假设Round内完整Agent集合永久固定。

```text
Initial Relevant Snapshot
→ Spawn / Despawn / Membership batches
→ Capability / SourceSet baseline
→ Source Command deltas
→ Resolved state
→ Movement correction
→ Reliable gameplay facts
→ Presentation facts
```

[INFERRED][HIGH] Spawn/Despawn在fixed-step boundary原子应用。Despawn区分死亡、相关性退出、业务回收和宿主销毁；客户端表现回收不得反向决定服务端Source或实体生命周期。

[INFERRED][HIGH] 生产世界不得依赖Round reset，必须支持持续spawn/despawn、Lifecycle槽位复用、membership变化、late join和动态Relevant Set。

## 7. 生产复制合同

| 事实类别 | 生产传输合同 |
|---|---|
| 共享不可变资源 | [INFERRED][HIGH] Revision + Hash或资产引用。 |
| 初始相关集 | [INFERRED][HIGH] v2 Snapshot Header + bounded Chunks。 |
| 动态生命周期 | [INFERRED][HIGH] v2 Spawn/Despawn/Membership bounded batches。 |
| Capability | [INFERRED][HIGH] Profile Key、Modifier Revision和有效Binding Hash。 |
| 行为Source | [INFERRED][HIGH] SourceSet Revision/Hash、持久Source baseline和可靠Command delta。 |
| 行为结果 | [INFERRED][HIGH] Resolved Hash及`ResolvedOnly`策略要求的结果。 |
| 运动纠错 | [INFERRED][HIGH] 当前Relevant实体的latest-wins correction chunks。 |
| 权威业务事实 | [INFERRED][HIGH] Stable Event/Commit Id + StableEntityRef/Lifecycle校验。 |
| 表现事实 | [INFERRED][HIGH] 已解析的动画、Cargo、Hit、Projectile视觉状态。 |

[INFERRED][HIGH] Source复制策略只允许`ServerOnly`、`ResolvedOnly`和`Predictable`；StateTree不复制。

[INFERRED][HIGH] 命令缺口、Schema错误或SourceSet/Resolved Hash不一致触发SourceSet resync。客户端相关性退出只清理本地副本，不向服务端发送Stop。

[INFERRED][HIGH] 所有O(N)数组必须有有界chunk/batch；Navigation/Flow资源只复制Revision/Hash或资产引用，不复制整图。

[INFERRED][HIGH] 复制频率和精度按Relevancy、Ownership、事实可靠性、变化率和预算决定，不按Faction硬编码。

## 8. 模块与宿主边界

| 所有者 | 长期职责 |
|---|---|
| `MassCrowdCore` | [INFERRED][HIGH] 稳定POD、Source状态机、Resolver、Movement kernels、排序、量化、Hash。 |
| `MassCrowdRuntime` | [INFERRED][HIGH] World Store、Registry、Mass生命周期、Gather/WORK/Merge/Prepared/Commit和Nav资源。 |
| `MassCrowdStandardSources` | [INFERRED][HIGH] 随插件交付的通用Movement/Facing/Constraint Context、Spec与Evaluator；只单向依赖Core/Runtime。 |
| `MassCrowdNetworking` | [INFERRED][HIGH] Snapshot、Lifecycle、Source状态/命令、Correction、assembly、resync。 |
| `MassCrowdPresentation` | [INFERRED][HIGH] StableEntityRef实例生命周期、ISM/VAT、插值、Cargo和已解析视觉事实。 |
| `MassCrowdStateTreeAdapter` | [INFERRED][HIGH] Source Command Task和Runtime Event等待；单向依赖Runtime。 |
| 宿主Business | [INFERRED][HIGH] Combat、Logistics、Inventory、Warehouse、Damage、Loot及最终业务验证。 |

[INFERRED][HIGH] Demo使用同一Runtime、Networking和Presentation，只增加Scenario输入、故障注入、Hash、指标和人工审片；Round/Testcase/端口不得进入插件公共产品API。

## 9. 测试合同

[INFERRED][HIGH] 测试层次固定为：纯POD fixture → 最小Mass World → Production Networking loopback → Demo真实地图 → continuous Sandbox → 同一路径20/100/500 → 原工程最小宿主。

[INFERRED][HIGH] Core专项至少覆盖所有Blend Mode、Q15、输入反序、16 Source与32 Contribution上限、命令幂等/冲突/缺口、过期、Capability撤销和Stable Hash。

[INFERRED][HIGH] Runtime专项至少覆盖staged不可见、Source/Movement/Business/Presentation跨通道原子性、失败零写入、Revision规则及HitReaction结束后任务精确恢复。

[INFERRED][HIGH] 网络专项至少覆盖v1拒绝、v2编解码、乱序/重复/缺口、late join、相关性进出、Predictable/ResolvedOnly、correction replay和Hash resync。

[INFERRED][HIGH] StateTree专项必须执行真实Task，覆盖物流完整链、Task中断/重入、重复Event和Command幂等；直接调用CommandBuilder不能替代该门。

[INFERRED][HIGH] 规模验收必须让同一Behavior Source生产路径依次通过20、100和500；旧Round 100/500结果只能作为基础运动、网络和安全基线。

## 10. 当前实现状态（2026-07-28）

[COMPUTED][HIGH] Relevant Snapshot、lifecycle、public channel、late join、correction、Presentation、World Store、Core Source状态机、Registry/Resolver数据结构、Envelope v3、v2 Codec和StateTree Adapter代码已经存在。

[COMPUTED][HIGH] 生产Mixed Movement Goal/Facing/Constraint已消费Resolved Channels并接入`FCrowdMassMovementPipelineWork → Particle Constraint → Facing Finalize`；边界采用完整预验证后的不可失败Apply；Behavior Source Codec v3已接入可靠状态、late join与resync。S6已在该路径依次通过20/100/500服务端门及20实体双端late join。

[COMPUTED][HIGH] 20实体第三方Fixture与代表性并发Mass Projectile组合门、StandardSources自主Evaluator、完整Mixed Movement Pipeline、Business/Movement通道独立性、Presentation Additive以及Pursue/Wander/Escort组合验收均已通过。

[COMPUTED][HIGH] 公共生命周期、基础网络、R0–R7、P0–P5及S0–S6均可保持关闭结论；动画Root Motion、真实StateTree业务Task和原工程迁移仍不在本轮范围。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
