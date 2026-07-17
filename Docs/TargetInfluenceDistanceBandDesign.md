# MassAI Crowd Target Influence Distance Band与Polar Region Transport设计

## 1. 目标

[INFERRED][HIGH] 使用连续、无owner的Target局部距离规则，让不同攻击距离和尺寸的普通虫子自然包围静态或移动Target。

[COMPUTED][HIGH] 当前生产版本包含解析Distance Band、Target Region Transport和Particle，不包含Transition Ring状态、Slot、owner或业务攻击。

[COMPUTED][HIGH] `Polar Region Transport Field`已经替换局部Polar Density生产processor，使群体沿Target-relative Cell图从过密区域流向欠占用可行区域。

## 2. 距离合同

```text
MinimumCombatCenterDistance
MaximumCombatCenterDistance
InfluenceBlendWidth
TargetHardDistance =
    TargetRadius + AgentRadius + max(TargetHardGap, AgentHardGap)
```

[INFERRED][HIGH] 业务攻击距离必须在adapter层转换成Target中心到Agent中心距离。kernel只消费统一中心距离。

[INFERRED][HIGH] 最小有效距离不得低于TargetHardDistance；若配置低于Hard门，规范化到Hard门并计入summary。

## 3. 解析guidance

[INFERRED][HIGH] 定义Target到Agent的外向单位向量`N`和中心距离`D`：

```text
D > Maximum
→ RadialCorrection = -N * Clamp((D-Maximum)*Gain)

Minimum <= D <= Maximum
→ RadialCorrection = 0

D < Minimum
→ RadialCorrection = +N * Clamp((Minimum-D)*Gain)
```

[INFERRED][HIGH] 有效带内部是平底区域，不把所有实体拉回Maximum或某条精确环线。第一批实体会停在接近外缘的位置；后续实体通过Particle压力沿切向和向内层展开。

## 4. Far Flow混合

[INFERRED][HIGH] Target影响从`Maximum + InfluenceBlendWidth`开始平滑增加：

```text
InfluenceWeight = SmoothStep(
    Maximum + InfluenceBlendWidth,
    Maximum,
    D)

TargetPreferred =
    TargetVelocity + RadialCorrection

DesiredVelocity =
    ClampMagnitude(
        Lerp(FarFlowPreferred, TargetPreferred, InfluenceWeight),
        AgentMaxSpeed)
```

[INFERRED][HIGH] 混合必须位置连续、量化确定且不依赖render DeltaSeconds。Target快于Agent时Clamp导致Agent掉队。

## 5. 历史Polar Density基线

[INFERRED][HIGH] 每次Solve以量化Target位置为中心构建固定连续数组。AngularSectorCount=`16`，RadialBandWidth=`100cm`；RadialBand使用Target中心绝对距离，不减去每个实体自己的Minimum距离。

[INFERRED][HIGH] 只统计`D <= Maximum + InfluenceBlendWidth`的实体。每个实体贡献整数1；各radial band独立执行一次环形平滑：

```text
Smoothed[i] = Count[left] + 2*Count[current] + Count[right]
```

[INFERRED][HIGH] 只有位于自身有效距离带内的实体使用切向density guidance。较空的左邻sector产生逆时针速度，较空的右邻sector产生顺时针速度；两侧相同且更空时按AgentId稳定拆分。Density difference每级提供20cm/s，最大120cm/s。

[INFERRED][HIGH] Density速度在Far Flow/Target径向混合之后按InfluenceWeight加入，再执行Agent MaxSpeed限制、1cm/s量化及量化后速度圆复验。

[INFERRED][HIGH] Polar field是本次Solve内临时SoA，不跨fixed-step保存，不生成Slot、owner、reservation、admission或Hard结论。Particle仍是唯一Hard安全层。

[COMPUTED][HIGH] 8412证明该基线能稳定生成切向requested velocity，但它只看同一Radial Band的相邻Sector，不能表达跨Band的全局Angular供需或到达远端空区的cost-to-go。

## 6. Polar Region Transport当前模型

### 6.1 Navigation Cells与Demand Regions

[INFERRED][HIGH] `Polar Navigation Cells`用于路径；随半径使用稳定`8/16/32`角分辨率，使外环Cell的切向尺寸不会随半径无界增长。SectorCount只由量化Radial Band和settings决定，不由当前人口决定。

[INFERRED][HIGH] `Demand Regions`用于容量与验收；第一版保持固定16个宏观Angular Regions。一个Region可包含多个不同分辨率、不同Radial Band的Navigation Cells。

[INFERRED][HIGH] 导航Cell的稳定key使用Band前缀和加SectorIndex；RegionKey独立稳定。验收Sector、Demand Region和Navigation Cell不得通过相同index偶然绑定。

### 6.2 图连接与环境可行性

[INFERRED][HIGH] 每个Navigation Cell连接同环CW/CCW邻居，以及角区间重叠的内环/外环Cell。`8→16`时内环Sector `i`稳定连接外环Sector `2i`/`2i+1`。

[INFERRED][HIGH] Cell anchor必须在按`PhysicalRadius + HardSafetyGap`内缩的FlowBounds中且不在同距离膨胀的Obstacle内；Cell Edge只在anchor间swept segment可行时建立。不可行Cell不进入Demand容量。

### 6.3 区域供需

[INFERRED][HIGH] 每个可行Demand Region输出`Capacity、DesiredPopulation、CurrentPopulation、Deficit、Surplus`。P0同质实体先按可行容量公平分配总人口，整数余数按StableRegionKey决胜。

[INFERRED][HIGH] 未来异构实体按量化`PhysicalRadius/HardGap/Minimum/MaximumDistance`稳定分cohort，每个cohort共享一份区域场；不为每个实体独立构建路径。

### 6.4 Aggregate Transport

[INFERRED][HIGH] 仅把所有空Region作为多源Dijkstra终点不能控制同Cell实体的分流和终点容量。第一版应使用整数、稳定决胜的aggregate min-cost transport：过密Cells提供supply，欠占用Regions提供有限demand，Polar edges提供量化路径cost。

[INFERRED][HIGH] solver输出Cell Edge的整数AgentQuota。同Cell实体按AgentId稳定排序消费当前fixed-step boundary的出口配额；配额是群体运输事实，不是永久Slot或Region owner。

[INFERRED][HIGH] 群体计划用`PlanEpoch、TargetRevision、EnvironmentHash、MembershipHash、EdgeFlowQuotas`表达，只在fixed-step boundary原子替换。该有限群体记忆用来防止每步CW/CCW翻转，不生成per-agent所有权。

### 6.5 Guidance合成

[INFERRED][HIGH] 远处继续消费Shared Flow；进入Influence Blend后平滑转向Polar Transport。正在运输的实体使用`NextCellAnchor - Location`作为完整Target-relative guidance，并加上量化TargetVelocity前馈。

[INFERRED][HIGH] 实体只有进入满足Demand的终端Region后才使用解析Distance Band径向settle。Particle继续是唯一Pair/Obstacle/Bounds Hard安全层；Polar topology可保守避免无效路径，但不宣称代替Particle安全复验。

### 6.6 纯POD数据合同

```cpp
struct FCrowdDemoTargetPolarCell
{
    int32 StableCellKey;
    int32 RadialBandIndex;
    int32 AngularSectorIndex;
    int32 AngularSectorCount;
    int32 DemandRegionKey;
    FIntPoint QuantizedAnchorCm;
    bool bFeasible;
};

struct FCrowdDemoTargetPolarEdge
{
    int32 FromCellKey;
    int32 ToCellKey;
    int32 QuantizedCost;
};

struct FCrowdDemoTargetDemandRegion
{
    int32 StableRegionKey;
    int32 Capacity;
    int32 DesiredPopulation;
    int32 CurrentPopulation;
    int32 Deficit;
    int32 Surplus;
};

struct FCrowdDemoTargetPolarEdgeFlow
{
    int32 FromCellKey;
    int32 ToCellKey;
    int32 AgentQuota;
};

struct FCrowdDemoTargetRegionGuidanceResult
{
    int32 AgentId;
    int32 CurrentCellKey;
    int32 NextCellKey;
    int32 DemandRegionKey;
    FVector2f DesiredVelocity;
};
```

[INFERRED][HIGH] 这些数据由纯C++ kernel产生，并在PipelineSubsystem的稳定prepared SoA中交换。不为Polar Cell、Demand Region或Edge Flow制造伪per-entity Mass fragment。

### 6.7 整数运输目标

[INFERRED][HIGH] aggregate transport的字典序目标固定为：

1. [INFERRED][HIGH] 最大化送达欠占用Region的实体数。
2. [INFERRED][HIGH] 最小化量化Polar路径总成本。
3. [INFERRED][HIGH] 相同运输量/成本下，最小化对上一PlanEpoch EdgeFlowQuotas的改变。
4. [INFERRED][HIGH] 完全相同时按FromCellKey、ToCellKey、RegionKey稳定决胜。

[INFERRED][HIGH] EdgeCost第一版只包含量化几何移动距离、保守环境净空惩罚、当前Cell人口惩罚和偏离capability有效带的径向惩罚。不使用随机噪声、浮点概率或render-frame DeltaSeconds。

## 7. 移动Target

[INFERRED][HIGH] 每步只消费当前量化Target position/velocity。没有RingPoint或Slot world coordinate，因此不存在追逐旧位置。

[INFERRED][HIGH] 到达和稳定使用Agent—Target相对距离与相对速度；世界速度不要求为0。

## 8. Processor边界

```text
TargetFactApply
→ SharedFlowFieldBuild
→ FlowPreferredVelocity
→ TargetInfluenceGuidance
→ MovementPredict
→ ParticleConstraintSolve
→ MovementFinalize
```

[INFERRED][HIGH] TargetInfluenceGuidance是纯POD kernel；Coordinator不承载算法。MovementFinalize才写RoundSim state。

[INFERRED][HIGH] 目标processor顺序是`FlowPreferredVelocity → TargetPolarTopologyBuild → TargetRegionPopulationBuild → TargetRegionTransportSolve → TargetRegionGuidance → MovementPredict`。实际可以由一个Mass processor调用多个纯kernel，但Topology、Demand和Transport接口必须可独立自动化。

## 9. 确定性

[INFERRED][HIGH] 输入按AgentId排序；位置/距离/速度按现有1cm/1cm/s规范量化；settings、polar cell、原始/平滑权重、方向选择、切向速度、InfluenceWeight、径向修正和输出速度进入稳定hash。

[INFERRED][HIGH] correction replay恢复累计指标和样本后按原fixed-step重放，不保存会跨步漂移的局部目标点。

[INFERRED][HIGH] Polar Transport还必须按StableCellKey/RegionKey排序Cell、Edge和残量边；路径cost、capacity、demand和flow quota使用整数；相同cost按CellKey/RegionKey决胜；不依赖`TMap/TSet`迭代顺序。

[INFERRED][HIGH] rollback必须恢复PlanEpoch、Topology/Demand/Transport hash、EdgeFlowQuotas、prepared Agent guidance和相关累计器后再重放。

## 10. 当前实现与停止点

[COMPUTED][HIGH] 当前Source已实现Distance Band + Target Region Transport纯kernel、四阶段production processors、RoundRules、rollback、RoundResult指标和五类双端hash；旧Polar Density guidance processor已删除。

[COMPUTED][HIGH] 当前最终自动化证据为Target Region Transport `5/5`、Particle `23/23`、完整SF `43/43`。

[COMPUTED][HIGH] 8412终态只有16/20进入有效带、Angular Sector覆盖3/16；该结果保留为旧Polar Density历史失败证据，不再描述当前生产能力。

[COMPUTED][HIGH] 当前Source已实现Polar Region Topology、Demand、Aggregate Transport、PlanEpoch、Guidance、Validation、rollback和五类双端hash；旧Polar Density guidance processor已删除。

[COMPUTED][HIGH] 8417 Static与8418 Moving Small已通过；DebugGame、录像、100/500和异构Capability cohort尚未运行或实现。

[INFERRED][HIGH] 后续异构虫群必须按量化CapabilityProfileKey共享cohort级区域场；尺寸/质量由Particle属性决定，攻击距离只决定terminal band，不能把Melee/Ranged身份转换成Particle碰撞优先级。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 2026-07-15 异构距离带状态

[COMPUTED][HIGH] Melee/MidRange/Ranged 现已作为 Capability profile 的 Target Region terminal band 输入；它们不改变 Particle priority、Mobility 或碰撞责任。

[INFERRED][HIGH] 真实远程攻击必须在Distance Band之外由独立AttackPhase和Projectile链实现；Distance Band只回答“实体是否处于允许攻击的宏观距离”，不回答目标有效性、windup、发射、命中、击退、击飞或命中改色。业务与VAT合同见`RangedCombatVatAndHitResponseDesign.md`。

[COMPUTED][HIGH] 当前Distance Band运行证据不包含攻击事件或真实VAT表现，不能据此标记Ranged combat通过。

[COMPUTED][HIGH] T6S/T6M 尚未运行，因此内/中/外距离层的真实覆盖、Moving Target 跟随与停止后重入均保持未验收状态。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
