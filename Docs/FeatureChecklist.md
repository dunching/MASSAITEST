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
- [x] [COMPUTED][HIGH] Runtime Base Movement trait及identity/state/properties/facing/output持久 fragments 已建立；Guidance、local velocity 与 Particle 中间结果使用 prepared POD。Gather按Capability稳定分批，Merge按AgentId唯一化，Commit先全量验证Lifecycle再允许写回。
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
- [x] [COMPUTED][HIGH] P1生产代码已收敛为一次gather/dispatch/wait/writer，Business/Combat、按Cohort Target、SharedFlow、Obstacle、Movement、Particle与Facing均由typed Worker DAG输出；8132/8137/8138/8139当前版本T2/T6/T7/T8双端复测通过。
- [ ] [COMPUTED][HIGH] Mass archetype尚未按Base/Target/Combat/Projectile能力拆分。

## 生产运行与复制（与 Demo 验收分开）

- [x] [COMPUTED][HIGH] 通用运行、行为组合、持续生命周期和生产复制合同已冻结在`MassCrowdUnifiedRuntimeAndReplicationContract.md`。
- [x] [COMPUTED][HIGH] `FCrowdStableEntityRef`、通用 CapabilitySet、ActiveBehavior 与组合式 AgentFacts POD 已在`MassCrowdCore`实现；Runtime identity/behavior fragments 提供基础映射，Faction 不决定 Capability。
- [x] [COMPUTED][HIGH] `MassCrowdNetworking`已实现通用 Relevant Snapshot、lifecycle batches、owner-only replication actor、late-join baseline、可靠状态序列、latest-wins correction、空间RelevantSet与resync重建。
- [x] [COMPUTED][HIGH] `MassCrowdPresentation`已实现按StableEntityRef管理的slot table、swap-remove反向表、幂等spawn/update/despawn、stale tombstone、Profile与可选Cargo引用；J和ContinuousLifecycle已通过Demo ISM sink消费该公共层。
- [x] [COMPUTED][HIGH] 阶段F已在插件最小World实现真实Mass entity创建/销毁、boundary apply、LifecycleSerial槽位复用、membership迁移与stale correction/despawn拒绝。
- [x] [COMPUTED][HIGH] Demo continuous lifecycle 与J均使用公共owner-only replication channel和Presentation subsystem；Continuous 7975延迟加入追平可靠生命周期序列，J 7977 step600双端hash与active/visible通过。
- [x] [COMPUTED][HIGH] Runtime统一Behavior provider/transition合同显式输出Target、Objective、MovementProfile、InteractionIntent与BusinessCommitRequest；Demo Logistics/Combat adapters、Cargo pickup/deliver、真实HitFact映射及rollback commit幂等测试通过，Faction不授予Capability。
- [x] [COMPUTED][HIGH] Core稳定分层Surface Graph与Runtime静态Recast提取器已实现；Shared Flow按NavLayer构建integration/direction，支持动态目标重新attachment而不改变拓扑，真实高低差地图已验证坡道、桥上桥下、高台、多路线、窄桥与不可通行落差。
- [x] [COMPUTED][HIGH] 20实体混合Sandbox已在独立非Round入口组合continuous lifecycle、统一Behavior、Cargo、Combat、Recast Shared Flow和增量ISM；行为切换、幂等commit、死亡/重生、membership迁移、安全间距及双端hash均通过真实运行。
- [x] [COMPUTED][HIGH] Demo固定Round初态已通过显式版本化adapter进入通用Relevant Snapshot；完整`RoundBootstrapPacket.Agents`不再作为复制属性，本地packet只作为既有Pipeline消费对象。
- [ ] [COMPUTED][HIGH] T1 OpenSpawn 只切换既存实体的 Particle 参与状态和 staging 布局，不创建或销毁 Mass Agent，不能计为生产 spawn/despawn 通过。
- [x] [COMPUTED][HIGH] 插件 Source 当前未检出 Enemy/Friendly/Faction 运动或复制特判；provider-neutral 边界保持。

## P0–P5 产品化闭环

- [x] [COMPUTED][HIGH] P0已关闭“A–J历史完成”与“J尚未组合”的文档冲突，并冻结GT/WORK、静态Recast Nav V1、物流混合边界、late join/relevancy和模块依赖方向；P0未修改源码、配置、地图或资产。
- [x] [COMPUTED][HIGH] P1公共Orchestrator与完整Worker DAG已接入Round；T6首次门发现并修复旧同步验证在Wait前读取空集合的问题，随后T2/T6/T7/T8全部通过。
- [x] [COMPUTED][HIGH] P2 Runtime Nav provider、Graph resource、revision/hash、Flow handle/refcount/有界LRU及`NavFlowProductSmall`已通过真实20实体垂直Recast双端门。
- [x] [COMPUTED][HIGH] P3 owner-only channel、late-join baseline、可靠state、latest-wins correction、空间相关集、resync与Presentation实例生命周期已实现并通过低层及真实双端验证。
- [x] [COMPUTED][HIGH] P4公共事务Store与专用`CrowdDemo_FriendlyLogisticsSmall`地图真实通过20实体、late join、竞争、守恒、幂等、死亡恢复、fallback、退避和取消；8154客户端Cargo attach/detach=`2/2`、实例=`20`，携货/交付近景证据已保存。
- [x] [COMPUTED][HIGH] P5已移除J私有Graph/Flow cache、完整MixedState multicast、直接ISM表与O(N)安全检查；旧Round bootstrap/correction/ResultHeader/projectile及实体视觉已迁移到公共Networking/Presentation。8151/8153/8157分别关闭Round、J和双客户端late join门。

## 自动化与构建

- [x] [COMPUTED][HIGH] Development Editor使用`-DisableUnity`通过。
- [x] [COMPUTED][HIGH] DebugGame Editor使用`-DisableUnity`通过。
- [ ] [COMPUTED][HIGH] 默认Unity Development仍因`MassCrowdSimulation`插件旧`.cpp`匿名命名空间辅助函数重名失败；第十切片未修改这些文件。
- [x] [COMPUTED][HIGH] 阶段H收口时`CrowdDemo` 111/111与`MassCrowd` 25/25自动化通过；RuntimeBehavior与BehaviorAdapters定向2/2通过。
- [x] [COMPUTED][HIGH] 阶段I收口时Development/DebugGame `-DisableUnity`、NavSurfaceGraph定向3/3、`MassCrowd` 27/27与`CrowdDemo` 112/112通过；8800真实Recast probe的8/8可达marker与不可达drop门通过，并保留视觉截图。
- [x] [COMPUTED][HIGH] 阶段J收口时Development/DebugGame `-DisableUnity`、MixedSandbox定向2/2、`MassCrowd` 27/27与`CrowdDemo` 114/114通过；8804双端20实体业务、安全、同步和视觉门通过。
- [x] [COMPUTED][HIGH] P0–P5最终Development/DebugGame `-DisableUnity`通过；累计`MassCrowd` 40/40与`CrowdDemo` 115/115通过。8156 NavFlow、8154 Friendly Logistics、8153 J、8151 Round及8157双客户端late join均无Fatal、Assertion、Ensure、`LogWindows: Error`或VIOLATION。

## 2026-07-28 回归增量

- [x] [COMPUTED][HIGH] P3可靠记录支持有界batch和ACK后分批追赶；空ApplyFrame队列不再误报stale，未放宽缓存或网络预算。
- [x] [COMPUTED][HIGH] P4 7953通过20实体真实运输、守恒、竞争、死亡恢复、fallback、不可达退避、late join及Cargo attach/detach；EmptyHand/Pickup/Carrying/Delivered四类近景文件已生成。
- [x] [COMPUTED][HIGH] P5 7939 J、7946 Continuous和7948–7951 Round T2/T6/T7/T8通过，硬错误扫描为0。
- [x] [COMPUTED][HIGH] 最新Development/DebugGame `-DisableUnity`、MassCrowd 43/43、CrowdDemo 115/115、反向依赖扫描与`git diff --check`通过。
- [ ] [COMPUTED][HIGH] J隐藏客户端Actor Tick p95=`400ms`不是有效渲染帧性能证据；正式客户端帧性能仍须由前台/非节流采样门证明，不能用该数字标记通过。
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
- [x] [COMPUTED][HIGH] 第十切片当时由VisualStateResolve完成Combat/Visual rollback事实；第二十一切片已把同一职责并入FacingFinalize原子写回。movement/combat双完成门继续阻止不完整snapshot replay或checkpoint。
- [x] [COMPUTED][HIGH] 8707 T1、8708 T7、8709异构T6及8710 T8双端回归通过安全、同步、snapshot完整性及性能门；8714 SF1 smoke保持hash=`267519150`、rebuild=1。
- [x] [COMPUTED][HIGH] 六个Demo迁移运动镜像及其模板/processor/rollback/适配入口已物理删除；结构自动化阻止类型和Mass模板注册回流。
- [x] [COMPUTED][HIGH] 第十二切片Development、DebugGame、`CrowdDemo` 105/105、`MassCrowd` 13/13通过；8723/8724/8725/8726分别覆盖T2、异构T6、T1和T8，四次双端运行无安全、同步、性能或业务hash回退。
- [x] [COMPUTED][HIGH] Compose→Local Predictive→MovementPredict已合并为一次GT准备、一次ThreadPool dispatch和一次原子发布；旧三个processor实现引用为0，阶段结果/hash等价测试通过。
- [x] [COMPUTED][HIGH] Facing与MovementFinalize已合并为一次Runtime WORK和一次GT原子提交；组合/旧链stage hash等价、反序稳定及revision mismatch整批拒绝测试通过，8727/8728无能力、安全、同步或性能回退。
- [x] [COMPUTED][HIGH] Particle通用结果发布已由Runtime publish plan统一：active/inactive/fallback/external结果、最终kinematics与稳定hash一次生成；Demo不再从ProposedMovement/FlowSample Mass fragments重建诊断输入。8729/8730保持T2/T6能力、安全、同步和性能门。
- [ ] [COMPUTED][HIGH] Particle后的Demo专用诊断/累计器组装与按能力archetype尚未完成；完整boundary单次Mass读取仍未关闭。
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

- [x] [COMPUTED][HIGH] 临时`FCrowdMassParticleConstraintFragment`已物理删除；Particle processor无Mass query，FacingFinalize从prepared final kinematics验证并提交最终运动事实。
- [x] [COMPUTED][HIGH] 第十五切片Development、DebugGame、MassCrowd 15/15、CrowdDemo 105/105通过；8731 T2为20/20 settled、16/16 coverage，8732异构T6为20/20 completed/settled/inside/coverage，双端安全、同步与性能门通过。
- [x] [COMPUTED][HIGH] Particle持久诊断已改为Finalize成功后的单次延迟提交；失败的FacingFinalize boundary不会累计候选Summary、route/stability、OpenSpawn或fixture事实。8733/8734保持原能力、candidate hash、安全、同步和性能门。
- [x] [COMPUTED][HIGH] PostFinalize已删除最后的Identity/RoundSim Mass查询；FacingFinalize写回时捕获稳定最终记录，PostFinalize仅消费prepared事实。结构测试同时要求这些记录在每个BoundarySnapshot发布时清空。
- [x] [COMPUTED][HIGH] 第十七切片Development、DebugGame、MassCrowd 15/15、CrowdDemo 105/105通过；8737 T2为20/20 terminal、16/16 coverage、fixed-step p95=`4.067ms`，8738异构T6为20/20 completed/settled/inside/coverage、fixed-step p95=`4.716ms`，双端同步与性能门通过。
- [x] [COMPUTED][HIGH] Transform、Velocity与Demo Movement已并入FacingFinalize的全量预验证后单次原子写回；Authority/Client Commit无Mass query，旧`ConfigureCommitQuery/CommitRoundState`重复两遍遍历已物理删除。
- [x] [COMPUTED][HIGH] 第十八切片Development、DebugGame、MassCrowd 15/15、CrowdDemo 105/105通过；8739 T2 fixed-step p95=`3.638ms`，8740异构T6=`5.003ms`，能力、安全、同步、可见实例与性能门均通过。
- [x] [COMPUTED][HIGH] 第十九切片删除CheckpointPublisher九类fragment查询，prepared checkpoint states在Visual/Combat决议后整批发布并按最终记录校验；Development、DebugGame、MassCrowd 15/15、CrowdDemo 105/105通过，8741 T2与8742异构T6的能力、安全、同步、correction和性能门通过。
- [x] [COMPUTED][HIGH] 第二十切片删除VisualStateResolve的Formation/RoundSim query requirements，并用最终记录与boundary formation facts重建相同状态；8743 T2 fixed-step p95=`3.137ms`，8744异构T6=`4.023ms`，能力、安全、同步、可见实例与性能门通过。
- [x] [COMPUTED][HIGH] 第二十一切片删除独立VisualStateResolve query；FacingFinalize单次原子写回同时提交运动与Demo Combat/Visual，PostFinalize保持rollback双完成门。Development、DebugGame、MassCrowd 15/15、CrowdDemo 105/105通过；8745–8748覆盖T2/T6/T7/T8且无能力、安全、同步或业务hash回退。
- [x] [COMPUTED][HIGH] 第二十二切片删除三个无fragment消费者的Runtime Movement中间fragments，MovementWork不再逐步回写持久Runtime identity/state/properties；双端Plan边界初始化合同已补齐。Development、DebugGame、MassCrowd 15/15、CrowdDemo 105/105通过；8753 T2 terminal/inside=`20/20`、coverage=`16/16`、fixed-step p95=`4.021ms`，8754异构T6 completed/settled/inside/coverage=`20/20`、fixed-step p95=`4.920ms`，两次运行硬错误与correction误差均为0。
- [x] [COMPUTED][HIGH] 第二十三切片已将RangedCombat、HitResponse、ReactiveMotion收敛为单一宿主Combat boundary transaction；旧三个processor与Pending HitFact桥已删除，T7/T8分别只保留一次gather和一次原子apply。Development、DebugGame、MassCrowd 15/15、CrowdDemo 105/105通过；8755 T7 fixed-step p95=`2.452ms`，8756 T8 spawn/impact/damage=`50/50/50`、duplicate fire/hit=`0/0`且三类业务hash双端一致；8757 T2 terminal/coverage=`20/20、16/16`，8758异构T6 completed/settled/inside/coverage=`20/20`，fixed-step p95=`4.263/5.230ms`。
- [x] [COMPUTED][HIGH] 第二十四切片完成剩余查询/写入接缝矩阵：删除ReactiveMotionStep与TargetCapability两个无持久所有权fragment；TargetRegionGuidance改读boundary snapshot且零Mass遍历；FlowPreferred只保留一次FlowSample持久写回；SoftPressure MovementWork不再写SF1 ProposedMovement桥。Development、DebugGame、MassCrowd 15/15、CrowdDemo 105/105通过；8759–8762覆盖T7/T8/T2/T6，能力、安全、同步、可见实例及性能门均通过。
- [x] [COMPUTED][HIGH] 第二十五切片已删除SF1 ProposedMovement/ObstacleConstraint两个中间fragment；MovementWork与ObstacleConstraint均为零Mass遍历并通过prepared POD交换，8763 SF1 Single保持golden Flow与障碍安全，8765/8766 T2/T6无回退。
- [x] [COMPUTED][HIGH] 8763曾出现correction interval位置误差p95=`26.745cm`；第二十七切片已由8770修复到`0.064cm`，该项不再是开放失败。
- [ ] [COMPUTED][HIGH] SF1 Cohort 500回归8764在Round前触发`Ensure !IsBunchTooLarge`并未完成client readiness；500网络启动门未通过，不能列为能力或架构验收通过。
- [x] [COMPUTED][HIGH] 第二十六切片把Facing历史settle事实并入BoundaryGather，FacingFinalize由3次Mass遍历降为2次；全量预验证与唯一原子提交仍独立。Development、DebugGame、MassCrowd 15/15、CrowdDemo 105/105及8767–8769 SF1/T2/T6回归通过。
- [x] [COMPUTED][HIGH] 第二十七切片已把fixed-step correction history/replay扩展到SF1。8770 snapshot hit/miss/mismatch=`36/0/0`，correction interval位置误差p95=`0.064cm`、checkpoint p95=`0.008cm`，Flow golden hash与路线/障碍结果不变；Development、DebugGame、MassCrowd 15/15、CrowdDemo 106/106通过。
- [x] [COMPUTED][HIGH] 阶段 B 通过 Development Editor `-DisableUnity`、`MassCrowd` 17/17与完整`CrowdDemo` 106/106自动化；新增 Core POD/capability/lifecycle fixture 与 Runtime AgentFacts 映射测试。
- [x] [COMPUTED][HIGH] 阶段 C 通过 Development Editor `-DisableUnity`、Relevant Snapshot定向3/3、`MassCrowd` 20/20与`CrowdDemo` 106/106；插件Networking Source未检出Demo/Round/Scenario/Combat依赖。
- [x] [COMPUTED][HIGH] 阶段 D 已删除完整`RoundBootstrapPacket.Agents`复制路径；版本化字段往返、合成500实体transport、逆序/重复/缺块/超时及源码架构定向3/3通过。Development/DebugGame、MassCrowd 20/20、CrowdDemo 109/109与8773真实20实体双端bootstrap通过；正式500产品运行仍待K。
- [x] [COMPUTED][HIGH] 阶段 E 的trivially-copyable batch header/entries、四类Despawn原因、严格排序、bounds、Snapshot→Delta连续性、重复/乱序/缺序列、原子拒绝、槽位复用及路径无关membership hash已通过定向3/3；Development/DebugGame、MassCrowd 23/23、CrowdDemo 109/109通过。
- [x] [COMPUTED][HIGH] 阶段 F 的最小Mass World定向1/1验证snapshot初始化、不同fixed-step spawn、真实destroy、Mass handle serial变化、StableEntityRef高serial复用、membership原子迁移、stale correction/despawn拒绝及完整entity-set hash；Development/DebugGame、MassCrowd 24/24与CrowdDemo 109/109通过。
- [x] [COMPUTED][HIGH] 8771 T2 rollback hit/miss/mismatch=`54/0/0`、terminal/inside/coverage=`20/20、20/20、16/16`；8772异构T6 rollback=`80/0/0`、completed/settled/inside=`20/20`、coverage=`20`，通用历史没有造成SoftPressure能力、安全、同步或性能回退。
- [ ] [INFERRED][HIGH] 整个fixed-step尚未达到一次Mass读取；现行实施必须按 A–L 完成通用 POD、Relevant Snapshot、Demo 适配和真实生命周期后，才进入混合 Sandbox 与20/100/500能力外推。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
