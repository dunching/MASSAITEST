# MassAI Crowd 通用粒子运动设计

## 1. 职责

[COMPUTED][HIGH] Shared Flow决定世界空间宏观路线，Target Influence Distance Band + Polar Region Transport决定目标附近的共享区域运输Preferred，Particle只负责局部Soft压力与Hard/Environment安全；该链已在8417 Static与8418 Moving Small进入生产验证。

## 2. 统一数据

```text
AgentId
Start/PredictedPosition
PhysicalRadius
HardSafetyGap
SoftMargin
Mobility
```

[INFERRED][HIGH] Mobility是唯一修正责任权重。不得增加穿行者、靠墙者、Slot owner或另一套priority来改变同一pair的安全语义。

## 3. 距离

```text
PairHardDistance =
    RadiusA + RadiusB + max(HardGapA,HardGapB)

PairSoftDistance =
    PairHardDistance + SoftMarginA + SoftMarginB

WallHardDistance = RadiusA + HardGapA
WallSoftDistance = WallHardDistance + SoftMarginA
```

[INFERRED][HIGH] Soft允许长期非零；Hard、Swept、Obstacle和Bounds决定candidate有效性。环境Mobility为0，合法切向运动必须保留。

## 4. Fixed-step

```text
DesiredVelocity
→ Predict
→ Pair/Environment Soft
→ Pair Hard + Swept + Obstacle + Bounds共同闭环
→ Quantize
→ Final Safety
→ Applied
```

[INFERRED][HIGH] candidate失败必须固定fixture并使能力运行失败；安全静止只能作为紧急输出。

## 5. 与Target Influence边界

[INFERRED][HIGH] Distance Band/Polar Region Transport只生成宏观Preferred，不计算pair推力或Hard安全。Particle不判断攻击距离、Region容量或运输终点，也不把实体恢复到某条Ring。

[INFERRED][HIGH] Polar topology可按capability cohort使用`PhysicalRadius + HardSafetyGap`构建保守可行Cell/Edge；Particle仍使用实时pair/contact事实完成最终安全闭环。大实体不需要永久跨环占用或Slot owner。

## 6. 当前实现

[COMPUTED][HIGH] 当前Source已实现显式Soft response、Environment Soft、稳定Pair/Contact、二维Hard/Swept/Obstacle/Bounds闭环、component量化、candidate/applied hash、rollback和fixture。

[COMPUTED][HIGH] Target Influence `2/2`、Particle `23/23`和完整SF `42/42`通过。8410联合运行保持Particle四类安全、invalid/fallback和双端hash差异为0。

[COMPUTED][HIGH] 8410的14/20有效带与4/16 Angular覆盖是宏观Target局部趋势能力不足，不是Particle Hard安全失败；本轮不向Particle增加Angular调度、Slot身份或场景fallback。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
