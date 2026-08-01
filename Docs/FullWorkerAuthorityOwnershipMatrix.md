# 全面 Worker 权威字段 Ownership Matrix

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
| Checkpoint/Delta/Event Baseline | [COMPUTED][HIGH] 网络状态仍由PostCommit/Mass事实发布 | [INFERRED][HIGH] Worker Checkpoint Store | [INFERRED][HIGH] WA7关闭Mass网络权威来源 | [INFERRED][HIGH] 网络Adapter只序列化冻结数据 |
| Presentation/ISM/VAT/Actor | [COMPUTED][HIGH] GT Presentation | [INFERRED][HIGH] GT Presentation Proxy | [INFERRED][HIGH] 不迁入Worker | [INFERRED][HIGH] 只表现，不参与下一模拟步 |
| Legacy Frame Transaction/Mailbox | [COMPUTED][HIGH] 四节点Boundary | [INFERRED][HIGH] 不存在 | [INFERRED][HIGH] WA8物理删除 | [INFERRED][HIGH] 无 |

## 当前结构结论

[COMPUTED][HIGH] 当前仍未达到WA8最终结构，但WA2–WA6模拟字段已经由Worker单权威：Movement、Particle/Interaction、Target/Cohort、Combat/Projectile、Lifecycle与Behavior Production Writer均已切换。四节点只保留Boundary事务与网络发布外壳，不再拥有上述模拟字段写权。

[COMPUTED][HIGH] 当前固定顺序进入WA7网络、Correction与Late Join；WA7关闭Mass/PostCommit作为网络权威来源后，WA8物理删除四节点、Frame Transaction、完整Gather和Mailbox。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
