# Local Predictive Interaction 通用局部预测交互设计

## 1. 文档职责与状态

[INFERRED][HIGH] 本文件是 Shared Guidance 与 Particle Safety 之间“通用局部预测交互”层的权威设计事实源；它定义目标职责、纯数据接口、确定性规则、代码结构和验收顺序。

[COMPUTED][HIGH] 本文件所述生产层已经实现并接入全部 `SimRoundSoftPressure` 运动边界。当前正式链为 `Guidance → LocalPredictiveInteraction → MovementPredict → ParticleConstraint`；新层不依赖旧 `FCrowdDemoOrcaAgent`、PortalAdmission、PassingBand 或 SF4 route 数据结构。

[INFERRED][HIGH] 本设计不是 T4 窄口专用调度，也不是 T5/T6 同目标专用逻辑。T3、T4、T5、T6 只是向同一个通用 kernel 提供不同空间事实的专项测试输入；生产代码不得读取 TestCase、地图名、“窄口”“同目标”或“Target场景”等语义来选择局部避让规则。

## 2. 要解决的问题

[COMPUTED][HIGH] 8496 的 T6M 最后 90 个 fixed steps 中存在持续同 next-cell 请求、2 个实体连续低进展和 Particle 反向修正；Terminal chatter 为 0，Particle 安全违规为 0。

[INFERRED][HIGH] 该证据说明当前宏观 guidance 可以持续给出几何上互相冲突的瞬时速度，而 Particle 只能在预测位置之后把实体推回安全范围，于是可能形成“再次靠近 → 被推开 → 再次靠近”的控制闭环。

[INFERRED][HIGH] 解决方法不是给 Navigation Cell 增加永久 owner 或固定容量 1，而是在积分前让实体基于邻域位置、速度和短期轨迹共同选择局部可执行速度。若几何上允许多人并行，则全部通过；只有共同可行速度不存在时才使用通用、公平、有限期的让行规则。

## 3. 三层职责

```text
Shared Guidance
├── Shared Flow：世界空间宏观路线
├── Target Region Transport：Region人口需求与宏观Cell Edge quota
└── 输出每实体PreferredVelocity

Local Predictive Interaction
├── 预测邻域轨迹和最近接近
├── 构建稳定pair约束与冲突component
├── 求解满足局部碰撞约束且尽量接近Preferred的速度
├── 仅在共同前进不可行时确定性让行
└── 输出LocallyFeasibleVelocity

Particle Safety Closure
├── 处理Pair Soft压力
├── 保证Hard/Swept/Obstacle/Bounds安全
└── 作为最终位置级安全闭环
```

[INFERRED][HIGH] Target Region Transport 只决定宏观人口应向哪些可行区域运输以及聚合 quota，不拥有每个实体的瞬时通行权，也不把 Cell anchor 当作唯一站位点。

[INFERRED][HIGH] Local Predictive Interaction 只消费通用运动事实，不知道 Preferred 来自 Shared Flow、Transport、自由游荡还是动态目标追逐。

[INFERRED][HIGH] Particle 继续是不可删除的最终安全层；Local Predictive Interaction 减少未来冲突，但不得替代 Hard/Swept/Obstacle/Bounds 复验。

## 4. 通用性与禁止边界

[INFERRED][HIGH] 生产 kernel 的输入不得包含 `TestCase`、MapName、PortalId、SlotId、PositionId、AttackType、Melee/Ranged 身份或“当前是否窄口”等业务标签。

[INFERRED][HIGH] Navigation Cell、Target Region 和 Flow Cell 都是宏观空间事实，不是局部避让所有权；不得新增永久 `AgentId → Cell` 映射。

[INFERRED][HIGH] 首版局部速度责任保持同级对称，不使用 PhysicalRadius、Mobility、职业或 CapabilityProfile 生成不可让行优先级。`Mobility` 继续只作为 Particle 位置修正的逆质量权重。

[INFERRED][HIGH] 通用让行公平性只能来自可回滚的等待时间、当前量化进展和 AgentId 稳定决胜；它不表示赢家无碰撞权，也不能绕过环境或 Particle 安全。

## 5. 目标数据接口

[COMPUTED][HIGH] 当前已经落地以下纯 POD；所有数组在进入 kernel 前按稳定键排序，kernel 不依赖 `TMap/TSet` 迭代顺序。

```cpp
struct FCrowdDemoLocalPredictiveSettings
{
    float FixedStepSeconds;
    float TimeHorizonSeconds;
    float NeighborDistanceCm;
    int32 MaxNeighbors;
    float VelocityQuantizationCmps;
    int32 GrantDurationSteps;
};

struct FCrowdDemoLocalPredictiveAgent
{
    int32 AgentId;
    FVector2f Position;
    FVector2f Velocity;
    FVector2f PreferredVelocity;
    float PhysicalRadius;
    float HardSafetyGap;
    float MaxSpeedCmps;
    int32 BlockedAgeSteps;
};

struct FCrowdDemoLocalPredictiveEnvironment
{
    FCrowdDemoSharedFlowFieldConfig FlowConfig;
    bool bConstrainToFlowBounds;
};

struct FCrowdDemoLocalPredictivePair
{
    int32 MinAgentId;
    int32 MaxAgentId;
    int32 DistanceBucket;
    float ClosestTimeSeconds;
    float PredictedSeparationCm;
    float ResponsibilityA;
    float ResponsibilityB;
};

struct FCrowdDemoLocalVelocityConstraint
{
    int32 AgentId;
    int32 OtherAgentId;
    int32 StableConstraintOrder;
    FVector2f Point;
    FVector2f Normal;
};

struct FCrowdDemoLocalConflictComponent
{
    uint32 ComponentKey;
    TArray<int32> AgentIds;
    int32 GrantEpoch;
};

struct FCrowdDemoLocalPredictiveResult
{
    int32 AgentId;
    FVector2f Velocity;
    int32 NeighborCount;
    int32 ConstraintCount;
    bool bYielding;
    bool bValid;
};
```

[INFERRED][HIGH] Mass processor 输入继续读取现有 `FCrowdDemoRoundMoveIntentFragment::DesiredVelocity`，但不得覆写它；新增 `FCrowdDemoRoundLocalVelocityFragment` 保存局部速度结果，使宏观请求、局部选择和 Particle 最终结果可以分别归因。

[INFERRED][HIGH] 跨 step 最小状态只保存在 PipelineSubsystem 的 prepared SoA/rollback 中，包括每实体 `BlockedAgeSteps`、最近有效 grant epoch 和必要的短期 component key；不得制造永久业务 owner fragment。

## 6. 确定性求解规则

### 6.1 邻域与轨迹冲突

[INFERRED][HIGH] 使用稳定 swept spatial grid 生成候选邻居；cell 内按 AgentId 排序，pair 最终按 `(MinAgentId, MaxAgentId)` 排序，并以 brute-force fixture 证明不漏掉高速交换 pair。

[INFERRED][HIGH] 每个 pair 使用当前位置、当前速度、PreferredVelocity、双方真实半径与 HardGap，在固定 time horizon 内计算量化最近接近；只有存在预测冲突的 pair 才进入约束图。

[INFERRED][HIGH] 同一阶段还必须从排序后的ObstacleSpecs与FlowBounds构建环境速度约束，保证预测endpoint与swept segment可行；否则局部层可能选择指向墙体的侧移速度，再被Particle推回而形成新的振荡。

[INFERRED][HIGH] 冲突 component 由当前有效 pair 图按 AgentId 稳定 BFS 构建；它是瞬时求解范围，不是场景、cohort、Cell 或 Portal。

### 6.2 连续速度可行域

[INFERRED][HIGH] 每个实体的约束统一表达为 `dot(v - Point, Normal) >= 0`，并与 `|v| <= MaxSpeed` 共同形成二维凸可行域。

[INFERRED][HIGH] 求解目标是从速度圆内选择满足全部约束且最接近 PreferredVelocity 的速度；连续解完成后在 1cm/s 量化邻域中稳定选择仍满足全部约束的候选。

[INFERRED][HIGH] component 内可按 AgentId 稳定顺序调用单实体二维half-plane LP，但必须在全部实体得到量化速度后联合复验每个pair的相对轨迹与每个实体的环境轨迹。任一局部结果只在整个component联合复验通过后发布。

[INFERRED][HIGH] 无冲突实体必须精确保留其限速后的 PreferredVelocity；局部层不得在没有约束时主动恢复历史队形、Cell中心或旧位置。

### 6.3 几何容量与通用让行

[INFERRED][HIGH] “容量”由当前 component 中可同时成立的速度集合自然产生：若多个实体均有可行正向速度，则无需 admission，全部可以并行移动。

[INFERRED][HIGH] 若所有实体同时保持期望进展不可行，则按 `BlockedAgeSteps降序 → 量化前向进展缺口降序 → AgentId升序` 选择有限期 grant；非 grant 实体仍求解安全让行速度，不默认归零。

[INFERRED][HIGH] grant 通过稳定调整冲突pair的回避责任，使获准实体承担较少速度变化、让行实体承担较多速度变化；双方责任之和保持1，pair约束不会被删除。责任上下限必须由纯fixture在生产接入前冻结，不能根据场景调参。

[INFERRED][HIGH] 冲突消失、参与者改变、固定租期到期或目标/计划 revision 失效时，在 fixed-step boundary 重新选择。winner仍必须满足全部pair/environment约束，不获得穿透、穿墙或无敌权。

[INFERRED][HIGH] `BlockedAgeSteps` 只在实体有正向请求但局部实际进展不足时增长；恢复有效进展后确定性衰减或清零。该状态必须进入 correction rollback，防止 replay 改变公平顺序。

### 6.4 失败语义

[INFERRED][HIGH] 连续可行域、量化修复或数值验证失败必须输出明确 invalid、固定最小 fixture 并使能力运行失败；不得静默恢复旧 ORCA fallback、直接使用 Preferred 或把失败统计成正常 Particle 刹车。

[INFERRED][HIGH] 紧急 applied 输出可以保持安全静止，但 candidate hash 与 applied hash 必须分开，且 validation run 必须把该 fixed step 计为失败。

## 7. 目标 Processor 顺序

```text
RoundPlanApply
→ TargetFactApply（需要时）
→ SharedFlowFieldBuild / DynamicIntegrationUpdate
→ FlowPreferredVelocity
→ TargetRegionTransport（需要时，只生成宏观Preferred）
→ LocalPredictiveInteraction
→ MovementPredict
→ ParticleConstraintSolve
→ MovementFinalize
→ AuthorityCommit / ClientPredictionCommit
```

[COMPUTED][HIGH] `LocalPredictiveInteraction` 对所有 `SimRoundSoftPressure` 运动场景使用同一 kernel 和规则；processor 不按 T3/T4/T5/T6 或地图分支启停算法。

[INFERRED][HIGH] `MovementPredict` 在局部结果有效时只积分 `LocalVelocity`；`MovementFinalize` 继续是 `FCrowdDemoRoundSimStateFragment` 唯一写入点。

## 8. 代码复用与替换边界

[COMPUTED][HIGH] `CrowdDemoVelocityHalfPlaneKernel.*` 已从旧 ORCA 数值实现中提取二维半平面、速度圆裁剪、连续精确求解、验证与 1cm/s 量化修复；`HalfPlaneParity` 自动化证明代表性 fixture 与旧数值结果等价。

[COMPUTED][HIGH] 新生产 kernel 只依赖通用 half-plane POD，不依赖旧 `FCrowdDemoOrcaAgent`、旧 fallback 或旧 processor。

[COMPUTED][HIGH] `CrowdDemoLocalPredictiveInteractionKernel.*` 负责稳定 swept pair、component、有限期 grant、环境可行性、联合结果复验和结果 hash；旧 Portal/Admission/SF4 fallback 未迁移。

[COMPUTED][HIGH] 现有 `CrowdDemoParticleConstraintKernel` 保持独立，不合并到速度求解器；它继续消费预测位置并执行位置级安全闭环。

## 9. Pipeline、Hash、Rollback 与指标

[COMPUTED][HIGH] PipelineSubsystem 保存排序后的 results、grant state、round hash、样本与 invalid 计数；processor 只准备 Mass 数据、调用纯 kernel，并一次发布完整 `FCrowdDemoRoundLocalVelocityFragment` 结果。

[COMPUTED][HIGH] SoftPressure rollback snapshot 已原子保存局部结果、grant state、summary、round hash、样本与 invalid 计数，并按既有 correction boundary 语义恢复后重放。

[INFERRED][HIGH] step hash 应折叠 settings、排序输入、pair/constraint稳定键、component决策、连续/量化结果和公平状态；round hash 按 `(FixedStepIndex, StepHash)` 折叠并由 Server/Client 比较。

[COMPUTED][HIGH] RoundResult 已增加 LocalPredictive valid/hash/sample、pair/component、adjusted/granted/yielding、infeasible、quantization、joint validation、joint resolution、environment constraint、grant switch、blocked age 与 invalid step 紧凑字段；不复制逐实体轨迹。

## 10. 失败优先自动化

[INFERRED][HIGH] 纯测试至少覆盖：无冲突精确保留Preferred、两实体迎面、交叉、追赶、两实体同点接近、三/四向交叉、靠墙让行、墙角共同可行域、无标签窄通道、异构半径、高速交换、移动参考系、输入全反序、grid/brute-force一致及两轮hash一致。

[INFERRED][HIGH] 公平测试至少覆盖：几何允许多人并行时不错误串行、不可同时前进时稳定grant、非grant仍有安全可行速度、BlockedAge防饥饿、component成员变化、租期释放及rollback replay不改变决策。

[INFERRED][HIGH] Processor测试必须证明没有 TestCase/地图/Portal分支、诊断关闭不改变结果、宏观Desired不被覆写、MovementPredict只消费有效LocalVelocity、invalid不会伪装为正常Applied。

[INFERRED][HIGH] 生产接入后必须用同一规则依次复验 T3 双向交换、T4 通道、T5 静态目标和 T6M 异构移动目标；专项地图用于暴露不同几何，不授权不同算法。

## 11. 当前实现与验证停止点

[COMPUTED][HIGH] 已新增 `CrowdDemoVelocityHalfPlaneKernel.*`、`CrowdDemoLocalPredictiveInteractionKernel.*`、`FCrowdDemoRoundLocalVelocityFragment`、Mass processor、prepared SoA、RoundResult hash/metrics 和 correction rollback。

[COMPUTED][HIGH] 联合求解使用固定 64 次稳定 component sweep，并对独立候选与共同可行基线做确定性最大安全 Q15 插值；新增跨 component 冲突会单调加入 pair 集并触发重新合并，直到固定点或明确 invalid。1cm/s 量化安全裕量进入 pair 几何与最终联合复验。

[COMPUTED][HIGH] Development Editor与完整 `CrowdDemo.SF` 63/63通过；LocalPredictive 定向自动化6/6通过。

[COMPUTED][HIGH] T3运行8507达到两侧center/completed=`10/10,10/10`、deadlock=0、LocalPredictive samples=`901/901`、invalid=`0/0`、hash=`1161166200`双端一致，Particle四类安全违规为0。

[COMPUTED][HIGH] T4运行8509达到wall/corridor/completed=`20/20/20`、deadlock=0、LocalPredictive samples=`901/901`、invalid=`0/0`、hash=`3029136817`双端一致，Particle四类安全违规为0。

[COMPUTED][HIGH] T5 Static运行8515保持LocalPredictive samples=`901/901`、invalid=`0/0`和Particle四类安全违规为0；20/20实体进入有效距离带、Transport Demand与完整Edge quota路径有效，但可行Region覆盖仍为`14/16`。

[COMPUTED][HIGH] 最后90步的Region诊断确认Region 4/5持续为空：Demand gap=`0`、Plan gap=`0`、Guidance执行gap=`90`、Terminal retention gap=`0`。供给Agent 8/16均获得约300cm/s宏观Guidance及首段quota，LocalPredictive分别输出约13.45/9.06cm/s，MovementPredict保持该速度，Particle应用速度为0。

[COMPUTED][HIGH] 在30Hz与1cm位置量化下，非零位移的最小速度阈值为`0.5cm / (1/30s) = 15cm/s`；8515双端均报告`sub_quantum_supply=2`、首个Agent=`8`、阈值=`15cm/s`，诊断hash=`2690604116`一致。

[COMPUTED][HIGH] 8517把最终冲突图完整闭包为8实体、8条pair；fixture hash=`2500233546`且双端一致。诊断证明8515的低速没有经过`CommonVelocity` fallback：Agent 8在独立half-plane阶段已经为`(-10,9)cm/s`，Agent 16从初始独立解`(-44,45)`在补全跨component pair后变为`(-9,1)`，随后因为结果已安全而停止联合目标优化。

[COMPUTED][HIGH] 同一8517 fixture存在保持全部pair相对速度不变的共同平移可行方向；对完整component增加约`(-61,13)cm/s`的相同速度偏移，可保持全部pair/environment安全，并把两个供给实体的flow-forward速度提高到60cm/s以上，同时降低整体相对Preferred的平方误差。

[COMPUTED][HIGH] 生产kernel现增加无场景语义的`CoherentTranslation`步骤：它只在独立结果已经安全时，对最终pair连通分量求平均残差方向；所有成员增加完全相同且量化一致的速度偏移，因此不改变相对速度。速度圆、Bounds、Obstacle和完整candidate pair复验失败时不应用；它不同于把所有成员收缩成同一速度。

[COMPUTED][HIGH] 8518原P0复测保持Particle Hard/Swept/Obstacle/Bounds、invalid/fallback均为0，LocalPredictive hash=`620827148`双端一致，20/20实体显示和checkpoint/interval误差p95=0。可行Region覆盖由8515的14/16提高到15/16，sub-quantum supply由2降到1，但仍有Agent 5得到约9.22cm/s Local速度并在1cm位置量化后Applied=0。

[COMPUTED][HIGH] 8518六实体fixture证明存在明显优于共同平移的联合安全解：在94cm硬门、1cm/s量化和同一环境合同下，Agent 5/19的前向速度均可超过200cm/s。原9.22cm/s不是几何容量下界，而是独立half-plane针对冻结邻居求解后形成的局部最优。

[COMPUTED][HIGH] 生产kernel已增加无场景语义的`JointPreferredRecovery`：以完整component的Preferred速度为目标，按稳定candidate pair顺序对时间域最近距离约束做固定64轮联合投影，每轮同时执行速度圆和环境投影；最终量化结果只有在完整Pair/Obstacle/Bounds复验安全、component总平方目标误差下降且grant实体进展不回退时才原子替换旧安全解。它不设置最低速度、不绕过位置量化，也不读取T5、Region或Agent身份。

[COMPUTED][HIGH] 8521原P0 Static单轮达到inside-band=`20/20`、Region coverage=`16/16`、sub-quantum supply=`0`；最终90步Target-relative speed与position peak-to-peak p95/max均为0，terminal chatter、merge blocked和Particle安全违规均为0。LocalPredictive samples=`901/901`、invalid=`0/0`且双端hash一致。

[INFERRED][HIGH] T5 Static的自动化、双端技术门和稳定性V1能力门已通过；人工审片仍未执行。本轮停止，不自动进入T5 Moving或T6M。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
