# 全面 Worker 权威字段 Ownership Matrix

## 2026-08-03 Dirty Proxy/Gather 删除检查点

[COMPUTED][HIGH] WA7-R Digest 已对齐为 Unreliable 自覆盖语义；Intent/Correction 继续可靠有序。Result Apply Proxy 使用稳定实体视图、Stable Slot 和可 ACK Dirty Batch，普通 Intent/Proxy refresh 不再复制或扫描完整 membership。

[COMPUTED][HIGH] Round 四阶段、公共 Runner/Orchestrator/WorkGraph、`FCrowdDemoRoundBoundaryGatherStage` 与 `RequestSubmitQuery` 已删除。完整 Snapshot Mass 读取只存在于 Input Sync bootstrap/Plan Revision；正常 Result Apply 缺 Dirty Batch 时 fail-closed。

[COMPUTED][HIGH] 最终 `ResultCommitQuery` 已改为持久 StableEntityRef→Mass Handle 索引驱动的 Dirty EntityCollection 提交；写前完整验证，普通帧不存在无界完整 Mass Query traversal。

[INFERRED][HIGH] 当前剩余 WA8 ownership/复杂度缺口是 Demo-local DAG、完整 `BoundarySnapshot` 与 Commit/PostFinalize/Checkpoint CPU 数组仍按完整成员构造；下一步收敛这些普通帧 O(N) 容器和序列化。

## 2026-08-02 T8 Clock/tail ownership 检查点

[COMPUTED][HIGH] 9687证明普通同Generation、同Plan的T8 Tick只需先提交有序Clock Intent，Worker即可在Legacy诊断事务完成前自主推进Movement、Particle、Facing、Projectile与Combat；Production tail从Worker输出直接形成原子Mass代理提交，不再二次异步模拟。

[COMPUTED][HIGH] 两轮900 Tick p95=`33.999/33.981ms`、realtime=`0.998/1.000`，业务闭合与确定性hash不变。首次Tick、Plan Revision、Target Region和非Production模式不走快路径，仍由版本资源或prepared输入建立所有权边界。

[INFERRED][HIGH] 该证据关闭Round T8性能与Clock/tail ownership缺口，但不关闭WA8；Mixed完整业务门、Friendly/Mixed Legacy Runner和Round事务外壳仍OPEN。

## 2026-08-02 跨 Round ownership 检查点

[COMPUTED][HIGH] 9685 证明同 World 新 Round 不需要更换 Worker Generation 或完整 resnapshot：PlanRevision 变化通过一次 input-owned InputSnapshot baseline 和版本化 Movement/Projectile Resource 进入同一 Runtime；Position/Velocity/Facing/Combat 继续由 Worker Store 推进。普通 Tick 的完整实体状态输入仍为零。

[COMPUTED][HIGH] 该证据关闭 Round transition ownership 缺口，但不关闭 WA8：boundary pending 导致 realtime=`0.500`，Mixed 完整业务覆盖与 Legacy shell 物理删除仍未完成。

## 使用规则

[INFERRED][HIGH] 本表同时记录“当前Production Writer”和“WA终态Writer”。迁移切换只能先通过Shadow/Canary证据，再原子关闭旧Writer并启用新Writer；任一字段出现两个Production Writer时结构门直接失败。

| 字段/事实域 | 当前Production Writer | WA终态Writer | Legacy Adapter迁移点 | GT/Mass允许角色 |
|---|---|---|---|---|
| StableEntityRef、LifecycleSerial、Spawn/Despawn | [COMPUTED][HIGH] Worker Lifecycle Store；GT只产生外部Lifecycle输入 | [COMPUTED][HIGH] Worker Lifecycle Store | [COMPUTED][HIGH] WA6已关闭Legacy Lifecycle状态Writer；同批复用稳定执行Despawn→Spawn，旧Serial Work/Wakeup/Result拒绝 | [COMPUTED][HIGH] 只提交Spawn/Despawn并应用代理 |
| Simulation Tick/Epoch | [COMPUTED][HIGH] Worker Runtime与Legacy Fixed Step并存于不同域 | [INFERRED][HIGH] Worker Clock | [INFERRED][HIGH] WA2–WA8逐域消除Legacy时间线 | [INFERRED][HIGH] 只提供外部目标时间 |
| Position/Velocity | [COMPUTED][HIGH] Runtime v2 Worker Movement Store | [COMPUTED][HIGH] Worker Movement | [COMPUTED][HIGH] WA2已完成Time Wheel自主调度、Local Predictive、环境约束及Canary→Production切换；Legacy只保留非生产等价基准 | [COMPUTED][HIGH] Mass仅代理写回；Production规划不读取GT回送运动状态 |
| Facing | [COMPUTED][HIGH] Worker Particle kinematics后运行Facing/Finalize并形成Production Commit | [COMPUTED][HIGH] Worker Facing | [COMPUTED][HIGH] WA3已关闭Legacy Particle→Facing提交Writer；Legacy输出只作诊断/Canary对照 | [COMPUTED][HIGH] Mass仅代理写回和插值 |
| Particle/Interaction状态与Pair | [COMPUTED][HIGH] Worker Particle Executor | [COMPUTED][HIGH] Worker Particle Executor | [COMPUTED][HIGH] WA3已完成闭合集合、唯一Pair、字段依赖传播与Canary→Production切换 | [COMPUTED][HIGH] 仅代理Patch/诊断 |
| Target/Cohort/Membership/Plan/Quota | [COMPUTED][HIGH] Worker Target Executor/Resource Store | [COMPUTED][HIGH] Worker Target Store | [COMPUTED][HIGH] WA4已通过Shadow/Canary/Production关闭GT Target资源与Guidance生产Writer；Legacy仅作诊断Oracle | [COMPUTED][HIGH] 仅提交外部Objective/Resource Revision并应用代理Patch |
| Attack/Cooldown | [COMPUTED][HIGH] Worker Combat Extension；Round T8与Mixed T9均为Production Writer | [COMPUTED][HIGH] Worker Combat/Event/Time Loop | [COMPUTED][HIGH] Production Commit取Worker Combat Patch，Legacy只读对照 | [INFERRED][HIGH] GT只提交外部Command |
| Projectile/Impact/Hit Event | [COMPUTED][HIGH] Worker Projectile Domain；9453生产提交Writer | [COMPUTED][HIGH] Worker Projectile/CombatReactive | [COMPUTED][HIGH] Legacy结果仅保留逐步对照，Prepared Projectile提交取Worker patch | [INFERRED][HIGH] 后续删除Legacy模拟计算 |
| Damage/Death/Hit React | [COMPUTED][HIGH] Worker Combat Extension；Round T8与Mixed T9均为Production Writer | [COMPUTED][HIGH] Worker Combat/Event/Time Loop | [COMPUTED][HIGH] Agent Combat、ReactiveSteps、Hit摘要、Projectile状态和StableHash由Worker结果重建 | [INFERRED][HIGH] Mass只接收代理Patch |
| Capability Binding/Behavior State/Source Set/Command Journal/Business Ledger | [COMPUTED][HIGH] Worker Behavior Store与Ordered Event Ring | [COMPUTED][HIGH] Worker Behavior Store | [COMPUTED][HIGH] WA6已关闭Legacy Behavior提交Writer；Production只消费Worker prepared records并ACK输入序列 | [COMPUTED][HIGH] 只提交Command/配置Revision并应用代理 |
| Spatial Index/邻居依赖 | [COMPUTED][HIGH] Worker Spatial/Dependency Store | [COMPUTED][HIGH] Worker Dependency/Spatial Store | [COMPUTED][HIGH] WA3以闭合Particle Resource work和Movement字段scope传播完成切换 | [COMPUTED][HIGH] 不保存独立权威时间线 |
| Nav/Flow/Environment/Rules/Objective | [COMPUTED][HIGH] GT快照与Worker镜像 | [INFERRED][HIGH] Worker Resource Store Current Revision | [INFERRED][HIGH] WA2切为Building验证/Epoch交换 | [INFERRED][HIGH] GT只提交版本化Resource |
| Dirty State Patch | [COMPUTED][HIGH] Worker Dirty Store已覆盖WA2–WA6生产字段 | [COMPUTED][HIGH] Worker Dirty Store | [COMPUTED][HIGH] WA2–WA6字段Owner Mask已切换；WA7继续增加网络Delta投影 | [COMPUTED][HIGH] Result Apply只消费 |
| Ordered Gameplay Event | [COMPUTED][HIGH] Worker Ordered Event Ring持有Lifecycle、Hit、Behavior与Business事件 | [COMPUTED][HIGH] Worker Ordered Event Ring | [COMPUTED][HIGH] WA5/WA6已迁移不可latest-wins的Gameplay事件 | [COMPUTED][HIGH] GT不得覆盖或重排 |
| Checkpoint/Intent/Digest/Correction/Event Baseline | [COMPUTED][HIGH] Worker Checkpoint Store与有序Intent/Digest/稀疏Correction合同；Demo网络Adapter仍保留迁移期发布外壳 | [COMPUTED][HIGH] Worker Checkpoint Store | [COMPUTED][HIGH] WA7-R已关闭普通完整State Correction语义；WA8删除剩余PostCommit/Legacy发布外壳 | [COMPUTED][HIGH] 网络Adapter只序列化冻结数据 |
| Presentation/ISM/VAT/Actor | [COMPUTED][HIGH] GT Presentation | [INFERRED][HIGH] GT Presentation Proxy | [INFERRED][HIGH] 不迁入Worker | [INFERRED][HIGH] 只表现，不参与下一模拟步 |
| Legacy Frame Transaction/Mailbox | [COMPUTED][HIGH] 公共类型与四节点已删除；仅余 Demo-local Work Batch/DAG | [INFERRED][HIGH] 不存在 | [COMPUTED][HIGH] 公共 Legacy 已物理删除；剩余本地完整成员 DAG 继续收敛 | [INFERRED][HIGH] 无 |

## 当前结构结论

[COMPUTED][HIGH] 当前仍未达到 WA8 最终结构，但 WA2–WA7 模拟字段已经由 Worker 单权威；四节点、公共事务外壳、完整 RequestSubmit Gather 与最终完整 Mass Commit 均已删除。剩余问题是 Demo-local 完整成员 DAG 和普通帧完整 CPU 输出容器，不是字段双 Writer。

[COMPUTED][HIGH] WA7-R Intent、Correction、Checkpoint、Late Join 与 Digest 传输合同已关闭。WA8 当前已把 Round、Friendly 和 Mixed 普通输入切到 Intent，删除完整 Gather，并把最终 Mass 写入收敛到 Dirty EntityCollection；下一固定切片是移除普通帧完整成员 DAG/CPU 输出数组。

## 2026-08-02 Projectile ownership checkpoint

[COMPUTED][HIGH] 9680全Worker T8已证明ProjectileControl在900 Tick中仅发布1次并复用899次，Combat/Projectile业务50/50/50且duplicate=0；Behavior容量、Combat时钟和Round尾部重复Tick均已关闭。[COMPUTED][HIGH] 9685又关闭Round 2 continuation，但realtime仍为`0.500`，所以“仅外部Command/Revision”仍未达到可删除Legacy兼容路径的完整验收。

[COMPUTED][HIGH] Projectile/CombatReactive 已有自主 CombatClock 和配置 revision 复用能力；完整 Worker Runtime Production 才启用语义复用。Shadow/Canary 仍逐 Tick刷新完整 HostInput，故当前尚未达到“GT仅提交外部Command/Revision”的最终列。
[COMPUTED][HIGH] 全Worker Mixed/T8真实业务门尚未通过；在门关闭前，Legacy parity、完整HostInput兼容发布和旧事务外壳均不得删除。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
