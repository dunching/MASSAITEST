# MassAI Crowd Demo 当前阶段计划

## 当前唯一阶段：架构收敛与20实体回归

[COMPUTED][HIGH] 产品收敛顺序为“插件边界→纯算法迁移→Mass Runtime迁移→完整GT/WORK→可选Networking/Presentation→Demo收缩→生产宿主烟雾测试→100/500”。插件阶段1和阶段2已完成；阶段3已完成Shared Flow、Target Region、Guidance Compose、Local Predictive、Particle Safety、Facing及最终Movement/Commit的Runtime WORK生产切换。

[COMPUTED][HIGH] 旧兼容面、Guidance覆盖写入和大数组rollback已经收敛；当前停在“首批POD WORK边界已成立，但20实体完整性能/人工门和完整GT/WORK尚未关闭”。

## 已完成

- [x] [COMPUTED][HIGH] 建立`MassCrowdSimulation`插件、Core/Runtime/Networking/Presentation/Tests五模块、公共合同和边界扫描；Development无依赖警告编译及插件边界1/1自动化通过。
- [x] [COMPUTED][HIGH] Shared Flow已提取为`MassCrowdCore`原生纯内核并接入Runtime生产WORK；权威Flow资源和动态anchor使用Runtime合同、暂由Demo Pipeline托管，Demo算法结构只保留迁移期field/sample镜像。SF1 golden hash、旧/Core/Runtime等价fixture、输入乱序和动态anchor均通过。
- [x] [COMPUTED][HIGH] Target Region Transport已提取为`MassCrowdCore`原生纯内核并接入Runtime生产WORK；Topology、Demand、Plan、validation、quota execution、Guidance、EngagedHold及claim replacement的旧/Core/Runtime结果与hash一致。
- [x] [COMPUTED][HIGH] Guidance Compose已提取为`MassCrowdCore`原生纯内核；provider优先级、稳定候选排序、量化、fallback和全部hash与Demo旧实现一致。
- [x] [COMPUTED][HIGH] Local Predictive及Velocity Half-Plane已提取为`MassCrowdCore`原生纯内核；8518六实体结果、pair、grant、component fixture hash及输入乱序合同与Demo旧实现一致。
- [x] [COMPUTED][HIGH] Particle Safety已提取为`MassCrowdCore`原生纯内核；8372完整20实体的各安全阶段、最终结果、candidate hash及applied几何hash与Demo旧实现一致，Combat RoundSim hash未迁入Core。
- [x] [COMPUTED][HIGH] Facing已提取为`MassCrowdCore`原生纯内核；稳定排序、转速限制、移动自主朝向、最终落位后朝目标、保持当前Yaw和角度跨界均与Demo旧实现一致。
- [x] [COMPUTED][HIGH] `MassCrowdRuntime`第一段已建立：Base Movement trait/fragments、Capability分批Gather、稳定Merge、全量Lifecycle预验证和Commit适配，并通过最小Mass World测试。
- [x] [COMPUTED][HIGH] Demo template已并行加入Base Movement plugin fragments；正式Guidance Compose改由Runtime WORK调用Core kernel，完整结果校验后仍经唯一Demo Intent写回，旧Demo WORK kernel退出生产调用。
- [x] [COMPUTED][HIGH] 正式Local Predictive改由Runtime WORK消费Runtime composed镜像并调用Core kernel；完整结果校验后同步更新Runtime/Demo local-velocity，旧Demo kernel退出生产调用。
- [x] [COMPUTED][HIGH] 正式MovementPredict消费统一snapshot、prepared composed/local结果；Particle继续消费prepared预测结果与snapshot属性并调用Core kernel。Solve和applied-state复验均在WORK内完成，完整结果校验后同步更新Runtime/Demo particle结果。
- [x] [COMPUTED][HIGH] 最终Movement由Runtime WORK按Capability分批生成、Bridge全局稳定Merge并在全量AgentId/Lifecycle预验证后提交；Authority/Client Commit已改为消费Runtime MovementOutput，Demo RoundSim保留checkpoint/指标兼容镜像。

- [x] [COMPUTED][HIGH] 删除TargetApproach、TargetSlotLayout、旧Polar Density及其生产/测试兼容面。
- [x] [COMPUTED][HIGH] 建立唯一Guidance Compose writer及稳定provider优先级。
- [x] [COMPUTED][HIGH] Rollback改为不可变plan资源引用加小型可变执行态。
- [x] [COMPUTED][HIGH] 异构Target调试标记按Capability Profile绘制；性能阶段拆为11个准确阶段。
- [x] [COMPUTED][HIGH] Guidance Compose、Local Predictive、Particle进入不可变POD WORK线程。
- [x] [COMPUTED][HIGH] 修正T1测试boundary reset与普通视觉不连续混算；普通不连续现为0。
- [x] [COMPUTED][HIGH] Development、DebugGame、当前102/102项`CrowdDemo`自动化及12/12项`MassCrowd`插件自动化通过。

## 当前失败与下一步

1. [x] [COMPUTED][HIGH] 已增加Round 1对齐的Game/Render/GPU/资源热身证据；T7普通运行8781/8783连续通过。历史8777首轮失败未被删除，也未被事后归因为单一资源原因。
2. [x] [COMPUTED][HIGH] T5M 8785安全、同步、Transport、性能和稳定诊断通过；移动追随不等同于静态settled。
3. [x] [COMPUTED][HIGH] RoundResultHeader contract v2已排除Server本地Performance payload；高熵自动化1566字节，8790真实异构payload 1970字节，无Native NetSerialize Warning。
4. [x] [COMPUTED][HIGH] 8788证明延长到60秒不能恢复能力；8789证明具备继承资格的634个claim全部迁移且无仍可行claim丢失。
5. [x] [COMPUTED][HIGH] `EngagedHold`由全量世界坐标零速收敛为单向目标跟随；8790 Round末inside/coverage=`20/20`且全部技术与性能门通过。
6. [x] [COMPUTED][HIGH] 用户确认AcquireThenHold实体在交互资格有效期间不需要持续重排Region；8790最后90步的`18/20`、`17/20`与settled window=0降为过程诊断，T6M按Round末20/20、安全、同步和性能门技术放行。
7. [x] [COMPUTED][HIGH] Guidance Compose纯内核与生产段迁移完成；Runtime WORK的provider选择、结果/hash及旧/Core等价fixture通过，Demo MoveIntent唯一写入语义保持不变。
8. [x] [COMPUTED][HIGH] Local Predictive纯内核与生产段迁移完成；Runtime WORK的Half-Plane/8518 fixture及旧/Core等价fixture通过，Demo grant/diagnostic/rollback存储语义保持不变。
9. [x] [COMPUTED][HIGH] Particle Safety纯内核与生产段迁移完成；Runtime WORK的pair/result/candidate/applied hash、输入乱序和8372旧/Core等价fixture通过，Demo指标/fixture/rollback兼容消费保持不变。
10. [x] [COMPUTED][HIGH] Facing纯内核与生产段迁移完成；Runtime WORK消费Runtime state/composed/particle并调用Core，完整结果校验后同步发布Runtime/Demo facing，最终Movement只消费Runtime结果。
11. [x] [COMPUTED][HIGH] `MassCrowdRuntime`最小fragment/trait与Gather/Merge/Commit合同完成；Demo适配器等价测试通过。
12. [x] [COMPUTED][HIGH] Demo Base Movement plugin fragments单向镜像与Guidance Compose单段生产切换完成；Runtime镜像由当前Demo事实重建，不加入rollback副本，未增加第二个Movement writer。
13. [x] [COMPUTED][HIGH] Local Predictive生产段已迁移到Runtime WORK；Runtime local-velocity镜像与Demo兼容fragment同时发布，该切片未改变当时的Particle、MovementFinalize及Authority/Client Commit。
14. [x] [COMPUTED][HIGH] Particle生产段已迁移到Runtime WORK；Runtime particle镜像与Demo兼容fragment在完整结果校验后同步发布。
15. [x] [COMPUTED][HIGH] Runtime最终Movement/Commit接管完成：乱序稳定Merge、重复Agent拒绝、全量Lifecycle预校验、Runtime/Demo镜像一致性门及Authority/Client提交均已接入；8663 T2与8664 SF1 authority短运行无回退。
16. [x] [COMPUTED][HIGH] Facing生产WORK接入完成；Runtime乱序/非法输入与旧/Core/Runtime等价测试通过，8665 T2双端Yaw误差为0，8666 SF1无Particle路径无VIOLATION。
17. [x] [COMPUTED][HIGH] Shared Flow生产Build、动态anchor、T3双cohort资源和Preferred candidate已接入Runtime WORK；8667 T2保持20/20 terminal、16/16 coverage、fixed-step p95=`3.166ms`，8668 SF1 golden hash=`267519150`。
18. [x] [COMPUTED][HIGH] Target Region四阶段生产入口已接入Runtime WORK；8669 T2与8671异构T6 Static保持能力、安全、双端hash和性能门，Demo指标/诊断/rollback仍消费兼容镜像。
19. [x] [COMPUTED][HIGH] 单boundary基础运动Gather第一切片完成：Runtime snapshot覆盖身份、状态和运动/Particle属性；Shared Flow与Target Demand复用同一快照及prepared Flow输出。8672 T2与8673异构T6保持能力、安全、hash和性能门。
20. [x] [COMPUTED][HIGH] Guidance overlay与Local Predictive统一输入第二切片完成：Flow/Target/Business candidate作为prepared POD发布，Runtime Bridge与boundary snapshot稳定合并Compose记录；Local Predictive直接消费snapshot和prepared composed。8677 T2与8678异构T6保持能力、安全、hash和性能门。
21. [x] [COMPUTED][HIGH] MovementPredict、Particle与Facing统一输入第三切片完成：三段从boundary snapshot及prepared composed/local/predict/particle链构造WORK输入；Mass查询只保留T1/业务垂直运动、settled历史、诊断、累计器及兼容发布。8681 T2与8682异构T6保持能力、安全、同步、hash和性能门，8683 SF1保持golden Flow hash。
22. [x] [COMPUTED][HIGH] MovementFinalize统一输入第四切片完成：Particle/SF1 Obstacle和Facing发布prepared最终事实，Runtime helper与boundary snapshot组装完整Commit输入；删除旧Finalize第一遍全实体Gather，保留写入前完整身份及镜像原子门。8684/8685保持T2/T6能力、安全、同步和性能门，8686 SF1保持golden Flow hash。
23. [x] [COMPUTED][HIGH] MovementFinalize查询职责第五切片完成：写前一致性检查与提交后业务/指标采集拆为`ValidationQuery`和`ApplyMetricsQuery`；后者删除MoveIntent、Runtime properties、Runtime Particle/Facing冗余读取，原子预验证保持。8687/8688保持T2/T6能力、安全、同步和性能门，8689保持SF1 golden Flow hash。
24. [x] [COMPUTED][HIGH] post-finalize业务/诊断职责已从MovementFinalize提取为独立processor；Finalize只保留原子状态写入，成功step标记阻止失败后采集旧状态或提交旧Movement。8693/8694保持T2/T6能力、安全、同步与性能门，8695保持SF1 golden Flow hash。
25. [x] [COMPUTED][HIGH] post-finalize已删除Formation、Composed Guidance、Particle Properties和未使用Particle Constraint读取；FormationIndex/checkpoint Radius来自boundary formation facts，Composed Guidance来自prepared Runtime结果。8697暴露的异构Radius语义替代错误已由8698 correction replay闭合。
26. [x] [COMPUTED][HIGH] post-finalize的FlowSample改由prepared Runtime Shared Flow输出重建，Obstacle penetration改由boundary起点与Finalize终点直接复验；移除两个fragment读取。8703异构T6 correction与8704 SF1 golden hash通过。
27. [x] [COMPUTED][HIGH] post-finalize的GuidanceCandidates由snapshot与三类prepared overlay稳定重建，Facing连续settle与最终资格由Facing阶段发布精确rollback fact；两个fragment读取均已删除。8705异构T6 correction与8706 SF1 golden hash通过。
28. [ ] [INFERRED][HIGH] 下一步分类T1 OpenSpawn及Combat/Visual事实：rollback必需状态进入可恢复业务快照，纯指标进入独立诊断快照；Identity与最终RoundSim state保留为实际提交状态采样输入，当前额外只读查询仍不能描述为完整单次Mass读取。

## 保护与停止门

[INFERRED][HIGH] 每阶段均执行`git diff --check → Development → 定向自动化 → 全部当前自动化`；行为、确定性、安全或稳定性能回退时停止。

[COMPUTED][HIGH] 不stage、commit、push，不修改地图、Lighting、30Hz、Particle硬门、网络频率、chunk size或复制预算；不进入100/500。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
