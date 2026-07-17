# MassAI Crowd 通用粒子运动设计

## 1. 职责

[COMPUTED][HIGH] 当前生产链由 Shared Flow 决定世界空间宏观路线，Target Influence Distance Band + Polar Region Transport 决定目标附近的共享区域运输 Preferred，Particle负责局部Soft压力与Hard/Environment安全；该链已在8417 Static与8418 Moving Small进入生产验证。

[INFERRED][HIGH] 目标架构在宏观 Preferred 与 MovementPredict 之间增加通用 Local Predictive Interaction：它根据邻域短期轨迹选择局部可执行速度，不读取具体测试场景、Target/Portal语义或地图名称。

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

[INFERRED][HIGH] Region人口、宏观Edge quota和terminal供需属于Transport；邻域瞬时并发、轨迹冲突与公平让行属于Local Predictive Interaction；Particle不得通过更强Soft响应、修改Mobility或放宽HardDistance解决多个实体持续争抢同一anchor的问题。

[INFERRED][HIGH] Local Predictive Interaction 不使用固定 Cell 容量：若多个实体存在同时安全前进的速度，则全部通过；只有局部共同可行域不能保持所有实体进展时才选择有限期让行。该规则同样适用于开放交叉、窄通道、同目标接近和移动目标，不能按场景分支。

[INFERRED][HIGH] 若宏观Preferred连续把实体拉向已满区域，Particle每步推开后又重新靠近属于上层执行合同失败。Particle安全为0只证明没有穿透，不证明群体运输合理或最终已经稳定。

## 6. 当前实现

[COMPUTED][HIGH] 当前Source已实现显式Soft response、Environment Soft、稳定Pair/Contact、二维Hard/Swept/Obstacle/Bounds闭环、component量化、candidate/applied hash、rollback和fixture。

[COMPUTED][HIGH] 当前最终自动化证据为Target Region Transport `5/5`、Particle `23/23`和完整SF `43/43`通过。

[COMPUTED][HIGH] 8417 Static与8418 Moving Small均达到inside band=`20/20`、Plan/Guidance unrouted=0、Particle四类安全违规=0、rollback miss/mismatch=0及五类Transport hash双端一致；该证据只覆盖统一半径、统一Mobility和统一Target距离配置。

## 7. 异构粒子与能力边界（尚未实现）

[INFERRED][HIGH] SmallLight、Standard和LargeHeavy继续使用相同Particle kernel；差异只来自逐实体PhysicalRadius、HardSafetyGap、SoftMargin和Mobility输入，不增加职业专用碰撞规则。

[INFERRED][HIGH] Mobility仍是Particle的唯一逆质量权重。大型或重型实体只因更大HardDistance和更低Mobility更难被局部压力移动，不获得不可让行、无敌碰撞或更高局部通行优先级；Local Predictive首版使用同级pair责任和`BlockedAge/进展/AgentId`公平顺序。

[INFERRED][HIGH] Melee、MidRange和Ranged只通过Capability cohort选择不同Target terminal band；Particle不读取攻击类型，也不计算攻击范围、攻击事件或伤害。

[INFERRED][HIGH] 未来真实攻击由独立的Target/AttackPhase/Projectile/HitResponse链消费“已经到达有效terminal band”这一前提；击退水平位移仍必须回到统一Particle安全链，击飞Z状态和命中改色不得塞入Transit或Particle距离语义。详细合同见`RangedCombatVatAndHitResponseDesign.md`。

[COMPUTED][HIGH] 当前Transit生产代码尚未接入上述业务链，因此Ranged terminal band不能作为射击、投射物或受击能力已完成的证据。

[INFERRED][HIGH] 跨profile pair、靠墙传播、最大实体通道可行性和目标内/中/外分层必须先通过纯fixture与T6 Small真实关卡；当前8417/8418不能外推为异构联合能力已成立。

## 8. Local Predictive Interaction 目标边界

[COMPUTED][HIGH] 当前SoftPressure正式链尚未执行局部预测速度求解；旧ORCA只存在于历史场景2/3，且其输入混有PortalAdmission、PassingBand和SF4 route语义。

[INFERRED][HIGH] 新实现应复用旧ORCA中无场景语义的二维half-plane数值能力，但建立新的纯POD kernel、结果fragment、prepared SoA、hash和rollback；不得直接恢复旧processor或旧fallback。完整结构见`LocalPredictiveInteractionDesign.md`。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## 8. 2026-07-15 实现状态

[COMPUTED][HIGH] 三档 Particle profile、三档 Target capability、稳定 key、固定 FormationIndex 映射、cohort 构建和异构 Particle fixture 已实现；Capability 自动化 3/3 通过。

[COMPUTED][HIGH] T6 生产路径已接入逐实体 Radius/Gap/Margin/Mobility 与按 CapabilityProfileKey 的 Target Region cohort；T6A已通过，T6S只有旧口径通过，T6M仍为19/20，不能宣称异构Target稳定落位能力通过。

[COMPUTED][HIGH] T1 已把“传播层”实现为从 inserted source 出发、以实际 realized Soft correction pair 为边的稳定累计 BFS；实测 layer max=3、influenced agents=12。`FirstInfluencedIteration` 继续只保留 solver 迭代诊断语义。

[COMPUTED][HIGH] T1 external preferred 始终为 0；新平衡完全来自统一 Particle Soft/Hard 规则，不存在 PositionId、formation target 或旧坐标恢复 guidance。测试中的 active/inactive 只控制 Particle 参与，不改变 Mass 实体生命周期与 20/20 visual 提交。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
