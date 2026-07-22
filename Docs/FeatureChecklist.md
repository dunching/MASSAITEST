# MassAI Crowd Demo 功能检查表

## 核心模拟与架构

- [x] [COMPUTED][HIGH] `MassCrowdSimulation`插件阶段1骨架与五模块单向依赖建立。
- [x] [COMPUTED][HIGH] Core公共API无`CrowdDemo`命名，插件边界源码扫描1/1通过。
- [x] [COMPUTED][HIGH] Shared Flow已提取到Core并接入Runtime生产WORK；Runtime定义的权威resource覆盖静态场、动态anchor和T3双cohort，当前由Demo Pipeline托管，Demo field/sample仅为迁移期镜像。SF1 golden hash=`267519150`。
- [x] [COMPUTED][HIGH] Target Region Transport已提取到Core并接入Runtime生产WORK，旧/Core/Runtime全链fixture覆盖Topology、Demand、Plan、quota execution、Guidance与claim replacement。
- [x] [COMPUTED][HIGH] Guidance Compose已提取到Core，旧/Core fixture覆盖provider优先级、乱序、stale revision、Stop fallback、量化与hash。
- [x] [COMPUTED][HIGH] Local Predictive及Velocity Half-Plane已提取到Core，旧/Core fixture覆盖真实8518六实体联合恢复、pair、grant、result与component hash。
- [x] [COMPUTED][HIGH] Particle Safety已提取到Core，8372完整20实体fixture覆盖Soft、Environment、UnifiedHard、Quantized、FinalSafety、candidate及applied几何hash。
- [x] [COMPUTED][HIGH] Facing已提取到Core，旧/Core fixture覆盖自主朝向、最终落位后朝目标、转速限制、保持Yaw、角度跨界、输入乱序及稳定hash。
- [x] [COMPUTED][HIGH] Runtime Base Movement trait及identity/state/properties/guidance/composed guidance/output fragments已建立；Gather按Capability稳定分批，Merge按AgentId唯一化，Commit先全量验证Lifecycle再允许写回。
- [x] [COMPUTED][HIGH] Demo template只保留Runtime Base Movement fragments作为中间运动权威；正式Guidance Compose由Runtime WORK执行Core kernel并写Runtime composed，旧Demo MoveIntent/GuidanceCandidates/ComposedGuidance fragments已删除。
- [x] [COMPUTED][HIGH] 正式Local Predictive由Runtime WORK消费prepared composed并执行Core kernel；Runtime local-velocity与同源prepared SoA一次发布，旧Demo local-velocity fragment及Demo kernel生产调用均已删除。
- [x] [COMPUTED][HIGH] 正式Particle Safety由Runtime WORK执行Core Solve及applied-state安全复验；Runtime particle结果与同源prepared SoA一次发布，旧Demo particle fragment已删除，MovementFinalize仍是RoundSim唯一写入点。
- [x] [COMPUTED][HIGH] 正式Facing由Runtime WORK调用Core kernel；完整AgentId/result集合校验后发布Runtime Facing及精确rollback fact，旧Demo facing fragment已删除，MovementFinalize只消费Runtime Facing。
- [x] [COMPUTED][HIGH] 最终Movement由Runtime WORK生成并经稳定Merge形成唯一Commit plan；完整AgentId/Lifecycle集合通过后才同步写Runtime/Demo状态，Authority/Client Commit只消费Runtime MovementOutput。

- [x] [COMPUTED][HIGH] 顶层parser只接受0/1及`SimRoundObstacle/SimRoundSoftPressure`。
- [x] [COMPUTED][HIGH] TargetApproach、TargetSlotLayout和旧Polar Density生产引用已删除。
- [x] [COMPUTED][HIGH] Flow、Target Region和Business输出独立candidate；唯一Guidance Compose写自主速度。
- [x] [COMPUTED][HIGH] Local Predictive与Particle不反向改写自主向量或Facing。
- [x] [COMPUTED][HIGH] Rollback使用不可变资源引用与可变执行态，correction仍只在fixed boundary应用。
- [x] [COMPUTED][HIGH] Compose、Local Predictive、MovementPredict、Particle和Facing已接入统一snapshot/prepared POD WORK输入链。
- [ ] [COMPUTED][HIGH] 整个boundary单次Mass读取、完整GT原子提交尚未完成。
- [ ] [COMPUTED][HIGH] Mass archetype尚未按Base/Target/Combat/Projectile能力拆分。

## 自动化与构建

- [x] [COMPUTED][HIGH] Development Editor使用`-DisableUnity`通过。
- [x] [COMPUTED][HIGH] DebugGame Editor使用`-DisableUnity`通过。
- [ ] [COMPUTED][HIGH] 默认Unity Development仍因`MassCrowdSimulation`插件旧`.cpp`匿名命名空间辅助函数重名失败；第十切片未修改这些文件。
- [x] [COMPUTED][HIGH] 当前105/105项`CrowdDemo`自动化通过；插件Boundary、Core纯算法、Runtime Gather/Merge/Commit、Target Region WORK、合并Movement Pipeline WORK与最小Mass World测试共13/13通过。
- [x] [COMPUTED][HIGH] 8663 T2生产回归通过Runtime Finalize/Commit链，fixed-step/Commit p95=`3.529/0.021ms`；8664 SF1 Single authority短运行无VIOLATION。
- [x] [COMPUTED][HIGH] Facing迁移后8665 T2维持20/20 terminal、16/16 coverage和双端Yaw误差0，fixed-step/Commit p95=`3.638/0.020ms`；8666 SF1无Particle路径无VIOLATION。
- [x] [COMPUTED][HIGH] Shared Flow迁移后8667 T2维持20/20 terminal、16/16 coverage、安全和双端hash通过，fixed-step/Flow p95=`3.166/0.264ms`；8668 SF1确认hash=`267519150`、rebuild=1且无硬错误。
- [x] [COMPUTED][HIGH] Target Region迁移后8669 T2维持20/20 terminal、16/16 coverage、五类hash与性能门；8671异构T6 Static维持inside-band/coverage=`20/20`、unrouted/invalid/validation failure=0，安全、同步及性能门通过。
- [x] [COMPUTED][HIGH] Runtime单boundary基础snapshot通过乱序、重复Agent拒绝和完整字段hash测试；Flow与Target Demand已复用该snapshot和prepared Flow SoA。8672 T2为20/20 terminal、16/16 coverage、fixed-step p95=`3.599ms`；8673异构T6为20/20 inside/coverage、fixed-step p95=`4.844ms`，两者安全与双端hash通过。
- [x] [COMPUTED][HIGH] Flow/Target/Business Guidance overlay已与boundary snapshot稳定合并；Compose和Local Predictive不再为WORK输入重复读取基础Mass fragments。8677 T2、8678异构T6保持能力、安全、双端hash及性能门。
- [x] [COMPUTED][HIGH] MovementPredict、Particle与Facing的基础WORK输入已从统一snapshot/prepared链消费；8681 T2、8682异构T6及8683 SF1 smoke无行为、安全或hash回退。
- [x] [COMPUTED][HIGH] MovementFinalize从snapshot与prepared kinematics/facing组装Commit输入；旧第一遍全实体Gather已删除，完整镜像原子预验证保留。8684 T2、8685异构T6及8686 SF1 smoke通过。
- [x] [COMPUTED][HIGH] MovementFinalize的原子镜像验证与提交后业务/指标采集已拆为最小查询；ApplyMetrics不再读取MoveIntent、Runtime properties、Runtime Particle/Facing。8687 T2、8688异构T6及8689 SF1 smoke通过。
- [x] [COMPUTED][HIGH] Finalize状态写入与post-finalize业务/诊断采集已成为独立processor；每boundary Finalize成功标记同时保护post-finalize及Authority/Client Commit。8693 T2、8694异构T6和8695 SF1 smoke通过。
- [x] [COMPUTED][HIGH] Post-finalize不再读取Formation、Composed Guidance、Particle Properties或未使用Particle Constraint fragment；精确Formation Radius由boundary fact保存。8698异构T6 rollback=`80/0/0`，8699 T2 rollback=`54/0/0`，8702 SF1 hash=`267519150`。
- [x] [COMPUTED][HIGH] Post-finalize不再读取FlowSample与ObstacleConstraint fragment；prepared Flow恢复rollback事实，snapshot+final state复验penetration。8703异构T6 rollback=`80/0/0`、fixed-step p95=`4.595ms`，8704 SF1 hash=`267519150`。
- [x] [COMPUTED][HIGH] Post-finalize不再读取GuidanceCandidates与Facing fragment；Guidance由snapshot+prepared overlays重建，Facing rollback fact由Facing阶段精确发布。8705异构T6 rollback=`80/0/0`、inside/coverage=`20/20`、fixed-step p95=`4.551ms`，8706 SF1 hash=`267519150`。
- [x] [COMPUTED][HIGH] T1 OpenSpawn唯一runtime生成稳定prepared boundary facts；per-agent OpenSpawn fragment已物理删除，pending reset完整验证后原子消费。
- [x] [COMPUTED][HIGH] Combat/Visual rollback由VisualStateResolve完成最终事实；movement/combat双完成门阻止不完整snapshot replay或checkpoint。PostFinalize结构测试确认只读取Identity与最终RoundSim。
- [x] [COMPUTED][HIGH] 8707 T1、8708 T7、8709异构T6及8710 T8双端回归通过安全、同步、snapshot完整性及性能门；8714 SF1 smoke保持hash=`267519150`、rebuild=1。
- [x] [COMPUTED][HIGH] 六个Demo迁移运动镜像及其模板/processor/rollback/适配入口已物理删除；结构自动化阻止类型和Mass模板注册回流。
- [x] [COMPUTED][HIGH] 第十二切片Development、DebugGame、`CrowdDemo` 105/105、`MassCrowd` 13/13通过；8723/8724/8725/8726分别覆盖T2、异构T6、T1和T8，四次双端运行无安全、同步、性能或业务hash回退。
- [x] [COMPUTED][HIGH] Compose→Local Predictive→MovementPredict已合并为一次GT准备、一次ThreadPool dispatch和一次原子发布；旧三个processor实现引用为0，阶段结果/hash等价测试通过。
- [ ] [COMPUTED][HIGH] Particle、Facing、Finalize等剩余WORK/GT接缝及按能力archetype尚未完成；完整boundary单次Mass读取未关闭。
- [x] [COMPUTED][HIGH] RoundResultHeader contract v2的高熵自动化为1566字节，8790真实异构T6M为1970字节；均低于2048字节且无Native NetSerialize Warning。

## 20实体能力与性能

- [x] [COMPUTED][HIGH] T1六阶段/传播/settling通过；普通视觉不连续=0，测试reset单列。
- [x] [COMPUTED][HIGH] T2 handoff/band/settled=`20/20/20`，coverage=`16/16`。
- [x] [COMPUTED][HIGH] T3双cohort=`10/10`、deadlock=0。
- [x] [COMPUTED][HIGH] T4 wall/corridor/completed/settled=`20/20/20/20`。
- [x] [COMPUTED][HIGH] T5S inside=`20/20`、coverage=`16/16`；当前版性能门通过。
- [x] [COMPUTED][HIGH] T6A corridor/completed/inside/coverage=`20/20/20/20`；T6S七类profile技术门通过。
- [x] [COMPUTED][HIGH] T7新增阶段证据后的普通运行8781/8783连续通过；Round内shader/loading/PSO帧为0。历史8777冷运行112.235ms仍保留为未唯一归因证据。
- [x] [COMPUTED][HIGH] T5M 8785技术、性能与稳定诊断通过；无merge block/chatter。
- [x] [COMPUTED][HIGH] T6M 8790的Round末inside/coverage=`20/20`且安全、同步、性能通过；AcquireThenHold资格有效期间不要求持续重排Region，最后90步18/17保留为过程诊断。
- [ ] [COMPUTED][HIGH] 单进程DebugGame PIE和当前版人工审片未完成。
- [ ] [COMPUTED][HIGH] 100/500当前组合未验收。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
