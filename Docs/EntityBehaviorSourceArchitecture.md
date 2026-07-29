# 通用 Behavior Source 架构（R0–R7基础框架 / S0–S6 Standard Sources）

## 0. 2026-07-28 架构重新基线

[INFERRED][HIGH] 本节与“通用Behavior Source、Boundary Scheduler与Mass Projectile重构计划”取代旧B0–B7作为现行实施口径；旧B0–B7正文和测试记录只保留为迁移证据，不再定义框架完成条件。

[INFERRED][HIGH] 六通道协议继续保留。当前问题是Registry封闭、Context不足、Runtime内建Demo语义、生产消费者按具体TypeId分支、公共Orchestrator硬编码领域阶段，以及Projectile存在Mass与数组双权威。

[INFERRED][HIGH] 本架构采用类似`FRootMotionSource`的可扩展方式，但不实现动画Root Motion：稳定协议管理Source身份、生命周期、优先级、混合、复制和恢复；可注册Evaluator把只读POD Context转换为Contribution。框架完成条件不包含动画Root Motion Clip。

[COMPUTED][HIGH] R0–R7基础框架和S0–S6 Standard Sources现均已关闭。插件随包的通用Source由独立`MassCrowdStandardSources`模块承载；StateTree仍是默认禁用的兄弟插件，动画Root Motion和真实StateTree业务Task不在本轮范围。

[INFERRED][HIGH] 现行实施顺序固定为：R0架构重新基线 → R1 Source扩展接口 → R2 Resolved生产接入 → R3通用Boundary Scheduler → R4 Source生产网络 → R5 Mass权威Projectile → R6 StateTree拆分 → R7同路径20/100/500。

### 0.1 Provider、Context与实例状态

[INFERRED][HIGH] `ICrowdBehaviorSourceProvider`必须只通过`FCrowdBehaviorRegistryBuilder`注册Profile、Spec、Context Schema和Evaluator；Provider按稳定`ProviderId`排序，完整验证后冻结。冻结后注册、重复ID和Schema冲突必须拒绝，Registry Hash进入Boundary和网络基线。

[INFERRED][HIGH] 标准Evaluation Context包含固定步、位置、速度、Facing、Capability和Source Instance；扩展Context最多8项，每项由`ContextTypeId + SchemaVersion`标识且不超过96字节。实例持久状态不超过96字节，只能由Evaluator通过Writer提交Next State。

[INFERRED][HIGH] Core/Runtime不得内建敌我、攻击、护送任务、Haul、Pickup或Deliver等领域语义。`MassCrowdStandardSources`可以提供不选择目标、不解释敌我且只输出通用Contribution的`MoveToLocation`、`FollowEntity`、`PursueEntity`、`FleeFromEntity`、`MaintainDistance`、Facing和Constraint实现；Demo/产品Provider拥有目标选择、攻击、物流和表现解释。

### 0.2 生产消费与临时压制

[INFERRED][HIGH] Demo控制器必须对期望Source集合做稳定Diff，只生成必要Start/Update/Stop；禁止逐步Stop-All/Start-All。临时高优先级Source结束后，原持久实例及其状态必须原样恢复。

[INFERRED][HIGH] Movement、Facing和Constraint生产消费者只读取Resolved Channels；Business和Presentation只由通用Adapter消费Resolved请求。生产代码不得扫描SourceSet或按具体SourceTypeId重建行为含义。

### 0.3 通用Boundary与Projectile

[INFERRED][HIGH] 公共Scheduler使用稳定`StageId`、`TaskTypeId`和`ScopeKey`描述DAG；领域顺序由Movement/Projectile Pipeline Builder锁定，基础Orchestrator不得认识SharedFlow、Target、Particle、Facing或Projectile。

[INFERRED][HIGH] Patch Adapter按`ApplyPhase → AdapterId → PatchKey`稳定排序。Prepare/Validate可以失败；所有验证完成后才进入返回`void`的Final Apply，Apply阶段只允许断言实现违约。

[INFERRED][HIGH] Projectile Mass Fragment是位置、速度、发射者、生命周期、Collision Profile、Effect Profile和状态的唯一权威；WORK只产生稳定POD Impact/Hit事实，GT唯一Adapter提交Projectile更新、伤害和表现。候选查询必须使用量化NavLayer/Cell Broadphase，不得执行Projectile×Agent全扫描。

### 0.4 网络与验收

[INFERRED][HIGH] Source Command/Set Codec升级为v3并携带Registry Hash、Context/State Schema和持久实例状态；旧Behavior Codec明确拒绝。Predictable要求客户端Registry Hash一致，late join包含持久SourceSet，缺序列或Hash不符触发resync。

[COMPUTED][HIGH] S6已让StandardSources迁移后的20、100、500依次走同一Source/Resolver/完整Movement/Particle/Facing/Prepared Apply与Networking路径；服务端p95分别为`1.593/8.772/27.587ms`，客户端p95为`4.801/4.951/4.822ms`。三种规模均在双端late join后达到active/visible全集、Entity/Membership Hash一致、零resync和零安全违规；旧Round规模结果只保留为迁移基线。

## 1. 文档职责与状态口径

[INFERRED][HIGH] 本文件是“实体行为能力架构”的现行设计事实源，同时区分锁定合同、已实现代码、已有证据和未关闭验收。`PhasePlan.md`只能引用本文件的阶段状态，旧专项设计不得覆盖本文件。

[COMPUTED][HIGH] 截至2026-07-28当前工作树，Core数据模型、命令状态机、Runtime World Store、开放Provider Registry、六通道Resolver、通用Boundary Scheduler、Commit Envelope、行为网络v3、Mass权威Projectile和兄弟StateTree插件均已实现。

[COMPUTED][HIGH] Mixed生产移动直接消费Resolver的Goal/DesiredVelocity/DesiredFacing/Constraint，领域TypeId位于Demo Provider，五Controller使用持久集合Diff；行为Codec v3已接入生产可靠状态、late join和resync。Local Predictive/Particle按InteractionLayer过滤跨层实体；目标丢失Stop、20/100/500服务端门和20双端late join均已通过。

[COMPUTED][HIGH] `CrowdDemo.Integration.R7.ThirdPartySourceMassProjectile20`已经补齐此前唯一组合性缺口：20个实体同时运行第三方Fixture、持久Movement/Cargo/Business Source、临时HitReaction压制/恢复与移动安全阶段；10发并发Projectile由生产Mass Fragment Store保存并经网格Broadphase/Sweep产生10次精确命中，恢复后持久Source保持20/20。

## 2. 锁定的权威边界

[INFERRED][HIGH] 行为权威模型是多个并存的Behavior Source，不是单一`ActiveBehavior`、中心`CanActivate(Behavior)`或互斥Provider选择。旧行为枚举只允许作为Recipe迁移输入和非权威诊断Label。

[INFERRED][HIGH] `Local Predictive`、Particle、障碍、边界约束和最终量化是所有移动结果之后的强制安全阶段，不得建模为可卸载Source。

[INFERRED][HIGH] Source Handle的正式键固定为`EntityRef + ControllerId + SourceSequence`；多个控制器不得共享只有实体与序号的缩略键。

[INFERRED][HIGH] Source权威实例由Runtime World Store按`FCrowdStableEntityRef`保存。Mass Fragment最终只保存紧凑Capability Binding、SourceSet Revision/Hash和必要的非权威诊断，不保存每实体动态Source数组。

[INFERRED][HIGH] Faction只表达关系、过滤和权限，不授予Capability，也不决定Movement、Networking、Presentation或安全算法。

## 3. Capability、Source与Registry合同

[INFERRED][HIGH] Capability Profile是不可变、稳定排序的数值ID集合；实体保存Profile Key和最多8项有界Add/Remove Modifier，Boundary Gather生成有效Capability集合。

[INFERRED][HIGH] 每实体最多16个活动Source、8个Controller；每个Source最多8个Required Capability、96字节Payload；每通道最多32个Contribution。超限、未知类型、重复Handle、Schema错误、缺失Capability或非法通道/Blend Mode必须整批拒绝。

[INFERRED][HIGH] Source Spec必须使用显式稳定数值ID、Schema版本、Required Capability、Channel Mask、默认Priority、Exclusive Group、生命周期和`ServerOnly/ResolvedOnly/Predictable`复制策略；字符串、Tag、UObject和自动编号不得进入Worker热路径。

[INFERRED][HIGH] Profile Registry与Source Registry必须在首个Boundary前冻结。重复ID、同ID不同Schema或冻结后修改均属于启动/执行失败。

[INFERRED][HIGH] Evaluator接口固定为“只读Context + 有界Contribution Writer”；Evaluator不得直接写Mass Fragment、Actor、业务账本、表现或网络状态。

[INFERRED][HIGH] Recipe只把高层意图展开为Source Command；Recipe可读取宿主数据资产，但提交前必须解析成稳定数值POD。

[INFERRED][HIGH] 基础Source的详细目录、Context/State合同、通用/产品所有权和S0–S6实施门以`MassCrowdStandardSourcesDesign.md`为事实源。高层Escort、Pursue+Attack、Logistics等应由多个Source组合，不得重新退化为单一互斥Behavior对象。

## 4. Command与Source生命周期合同

[INFERRED][HIGH] Command排序键固定为`EffectiveFixedStep → StableEntityRef → ControllerId → CommandSequence`。

[INFERRED][HIGH] 完全相同的Command Key与内容Hash按幂等成功；同Key不同内容、序号倒退、序号缺口、Update/Stop不存在实例或Start已存在实例，均拒绝整批命令。

[INFERRED][HIGH] Capability撤销时，依赖它的Source在下一Boundary staged copy中确定性停止并产生Event；Boundary失败时Source变化与Event均不可见。

[INFERRED][HIGH] 每次成功Boundary最多递增一次SourceSet Revision，不随命令数量递增；Stable Hash覆盖排序后的实例、Capability Binding、Modifier和Payload。

[INFERRED][HIGH] 临时HitReaction、Stun和Death通过高优先级Movement/Constraint贡献压制移动，不删除Cargo、任务、Facing、Presentation或低优先级持久Source；临时Source结束后原任务必须精确恢复。

## 5. Resolver合同

[INFERRED][HIGH] 每实体每通道的统一排序键为`Priority降序 → SourceTypeId → ControllerId → SourceSequence`；物理输入顺序不得改变结果或Hash。

[INFERRED][HIGH] Movement先选最高优先级Override；没有Override时归一化Q15 WeightedAdd；随后应用Additive和Constraint，最终量化后进入强制安全流水线。

[INFERRED][HIGH] Facing独立于Movement，支持Override和Q15 WeightedAdd；`FaceTarget`、`FaceMovement`、Formation和移动Source可以并存。

[INFERRED][HIGH] Constraint合并最小限制、最大限制、布尔移动锁和NavLayer允许集合交集。

[INFERRED][HIGH] Interaction采用Exclusive Winner。Business输出按稳定键排序的宿主请求；同互斥组冲突必须拒绝，不能进行数值混合。

[INFERRED][HIGH] Presentation按Property独立Override或Additive，只消费已经解析的事实，不得反向驱动业务权威状态。

## 6. Boundary与原子提交合同

[INFERRED][HIGH] 固定数据流为：Gather不可变Snapshot → staged SourceSet应用到期命令 → Evaluate → Resolve → Movement安全阶段与宿主业务预验证 → Prepared Patches → 完整集合/Hash校验 → 一次Final Apply。

[INFERRED][HIGH] 所有可能失败的业务检查必须在Final Apply前完成。Final Apply采用“已验证后不可失败”合同；断言只捕获实现违约，不以写入后回滚提供业务恢复。

[INFERRED][HIGH] Commit Envelope v3必须包含SourceSet Revision/Hash、Command Batch Hash、各通道Hash、Movement/Facing结果、稳定排序的Patch Descriptor和最终Stable Hash。

## 7. 网络与StateTree合同

[INFERRED][HIGH] Snapshot、Lifecycle、Apply Frame和Demo Payload统一使用协议v2并明确拒绝v1；不维护长期双协议路径。

[INFERRED][HIGH] 可靠行为状态包括Capability Binding、SourceSet Revision、持久Source集/命令增量和Resolved Hash；晚加入Baseline包含当前持久Source、业务事实和表现事实。

[INFERRED][HIGH] Source复制策略只允许`ServerOnly`、`ResolvedOnly`和`Predictable`。StateTree本身不复制，客户端只接收Source命令或解析结果。

[INFERRED][HIGH] 命令缺口、Schema不符或Hash不一致触发SourceSet resync；客户端相关性退出只清理副本，不停止服务端Source。

[INFERRED][HIGH] 可选`MassCrowdStateTreeAdapter`只允许Task提交Start/Update/Stop Command并等待已提交Runtime Event；Runtime不得反向依赖StateTree，Task不得直接写移动、业务或表现状态。

## 8. 当前实现矩阵

| 范围 | 当前状态 | 仍需关闭 |
|---|---|---|
| R0基线 | [COMPUTED][HIGH] 旧B0–B7已降级为历史证据，R0–R7成为现行计划。 | [COMPUTED][HIGH] 无。 |
| R1 Provider/Context | [COMPUTED][HIGH] 稳定Provider排序、Registry冻结/Hash、8×96 Context、96字节实例状态和第三方Fixture已实现。 | [COMPUTED][HIGH] 无。 |
| R2生产消费 | [COMPUTED][HIGH] 领域Evaluator在Demo Provider；持久Source Diff和Resolved Movement生产消费已实现。 | [COMPUTED][HIGH] 无。 |
| R3 Scheduler/原子Apply | [COMPUTED][HIGH] 通用Task Key/资源描述、Patch排序和验证后`void` Apply已实现。 | [COMPUTED][HIGH] 无。 |
| R4网络v3 | [COMPUTED][HIGH] Registry/Context/State Schema、持久状态、late join、可靠分批和Hash resync已接入。 | [COMPUTED][HIGH] 无。 |
| R5 Mass Projectile | [COMPUTED][HIGH] Mass唯一权威、网格Broadphase、相对/环境Sweep、通用Impact/Hit与宿主唯一提交已实现；旧数组/镜像/32槽已删除。 | [COMPUTED][HIGH] 无。 |
| R6 StateTree拆分 | [COMPUTED][HIGH] Adapter为默认禁用兄弟插件，主插件无反向依赖。 | [COMPUTED][HIGH] 真实StateTree业务链不属于当前框架门。 |
| R7规模与最终门 | [COMPUTED][HIGH] 同一路径20/100/500、20实体第三方Fixture/持久业务/临时压制/移动安全 + 10发并发Mass Projectile组合门、四种构建与完整自动化已通过。 | [COMPUTED][HIGH] 无。 |

[COMPUTED][HIGH] 上表的R0–R7基础框架门与`MassCrowdStandardSourcesDesign.md`定义的S0–S6现均已关闭；完整Mixed Movement Pipeline、Presentation Additive和Pursue/Wander/Escort组合均有代码与专项证据。

## 9. 测试证据与证据边界

[COMPUTED][HIGH] PJ6最终完整自动化日志为`MassCrowd 65/65`与`CrowdDemo 125/125`，失败数为0；构建记录覆盖Development/DebugGame与`-ForceUnity -DisableAdaptiveUnity`/`-DisableUnity`。

[COMPUTED][HIGH] 当前同路径证据分别为`R7-Mixed20-Gate`、`R7-Mixed100-Gate`和`R7-Mixed500-Gate3`；20与500在fixed step 600达到双端Entity/Membership Hash一致，100门的每个接收包按对应固定步验证期望Hash，三者均无resync或安全违规。

[COMPUTED][HIGH] 8210证明100实体旧Round SoftPressure的启动、correction和性能；8215证明500实体旧Round Obstacle的分块、correction、障碍安全和五轮Checkpoint。

[INFERRED][HIGH] 8210/8215不得表述为100/500 Behavior Source组合验收，也不得替代同一路径的Source Gather/Evaluate/Resolve、行为复制和临时Source恢复规模门。

[COMPUTED][HIGH] 8215日志在第五轮后继续进入第六轮，没有明确最终PASS/正常退出记录；可表述为“完成五轮Checkpoint”，不能表述为测试脚本完整成功退出。

[COMPUTED][HIGH] R7组合证据由`CrowdDemo.Integration.R7.ThirdPartySourceMassProjectile20`提供：20个未知于Core的Fixture Source与持久Movement/Cargo/Business Source、两步HitReaction压制及恢复、移动安全阶段和10发并发Mass Projectile同场运行；Projectile往返生产Mass Fragment Store，命中守恒为10/10，Broadphase未退化为Projectile×Agent全扫描，恢复后持久Source保持20/20。真实StateTree业务Task已从框架门移除，不得继续列为R0–R7阻塞项。

## 10. 当前关闭结论

[COMPUTED][HIGH] R0–R7基础框架门与S0–S6通用Source库/严格生产消费闭环均已关闭。

[COMPUTED][HIGH] S0–S6关闭建立在自主Evaluator/Context/State专项、第三方三复制策略Fixture、Demo组合/恢复专项、完整Movement安全链、Codec v3网络回放、20/100/500真实门和Mass Projectile回归七类证据上。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
