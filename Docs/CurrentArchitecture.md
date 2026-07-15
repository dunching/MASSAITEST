# MassAI Crowd Demo 当前架构

## 1. 当前事实源

[COMPUTED][HIGH] 当前正式基础由 SF1 Shared Flow V2、SoftPressure Particle 约束、Target Fact、Target Region Transport、fixed-step correction/rollback 和 client-only visual 组成。

[COMPUTED][HIGH] T5 Static/Moving 使用同一套 Target Region Transport 生产链；旧 Polar Density processor 已从正式 processor 集与源码声明中移除，不再作为 fallback 或 A/B 分支。

## 2. 双端 fixed-step processor 顺序

```text
RoundPlanApply
→ TargetFactApply
→ SharedFlowFieldBuild
→ FlowPreferredVelocity
→ TargetPolarTopologyBuild
→ TargetRegionPopulationBuild
→ TargetRegionTransportSolve
→ TargetRegionGuidance
→ MovementPredict
→ ParticleConstraintSolve
→ MovementFinalize
→ AuthorityCommit / ClientPredictionCommit
```

[COMPUTED][HIGH] Server 和 Client 使用同一组 Mass processors 与纯 C++ kernels；Coordinator 只负责 RoundPlan、RoundResult、correction/checkpoint、readiness 和紧凑汇总，不承载 Topology、Demand、Transport 或 Particle 算法。

[COMPUTED][HIGH] `MovementFinalize` 是 `FCrowdDemoRoundSimStateFragment` 的唯一 fixed-step 写入边界；客户端 visual 只读取 client sim state，不计算 gameplay movement。

## 3. Target Region Transport

[COMPUTED][HIGH] 纯 kernel 接口分为 `BuildTopology → BuildDemand → SolveTransport → BuildGuidance`。

[COMPUTED][HIGH] Polar Navigation Cell 的扇区数按半径使用 8/16/32/64；Radial Band 为 100cm；Demand 统计固定使用 16 个 Region，因此导航分辨率与验收口径彼此独立。

[COMPUTED][HIGH] Topology 生成稳定 CellKey、CW/CCW 边和角区间重叠的跨环边，并用 FlowBounds、52cm 硬膨胀障碍、Target hard exclusion 及 swept segment 过滤不可行 Cell/Edge。

[COMPUTED][HIGH] 远距离实体通过 Shared Flow V2 的稳定 next-cell 链寻找首个安全 Polar source attachment；它们不再要求从出生点直线穿越障碍连接 Polar domain。

[COMPUTED][HIGH] Demand 计算 Current/Desired/Deficit/Surplus 和环境容量；整数 min-cost flow 依次保证最大运输量、最小物理成本、旧 quota 复用和稳定 key 决胜。

[COMPUTED][HIGH] 同 Cell 实体按 AgentId 消费按 ToCellKey 排序的出口 quota；结果不生成永久 Slot、PositionId 或 per-agent Region owner。

[COMPUTED][HIGH] Plan 正常寿命为 15 fixed steps；Target、完整 FeasibleGraphHash、membership、Demand 满足或 validator 失败会触发同一 boundary 确定性重建。Topology、Demand、Transport、Guidance 与 Validation 五类 round hash 均进入 RoundResult 双端比较。

[COMPUTED][HIGH] `FeasibleGraphHash`不折叠Target世界坐标，而是折叠全部Cell可行/终端/Region事实，以及排序后的可行Edge、实际soft clearance cost、径向cost与跨环标志。

[COMPUTED][HIGH] Plan validator检查revision、graph、membership、Edge存在/可行、Supply outgoing quota、正quota可达性与流量守恒；有效Plan必须保证Guidance unrouted为0。

## 4. Particle 与网络边界

[COMPUTED][HIGH] MovementPredict 只积分 Transport/Flow 输出的限速 preferred；Particle kernel 继续负责 Pair Soft、Hard/Swept、Environment/Bounds 和量化后安全闭环。

[COMPUTED][HIGH] SoftPressure rollback snapshot 已包含 Target Region Topology、Demand、Plan、quota、Guidance、PlanEpoch、四类 hash、重建计数和 solver 样本长度；correction 在 fixed-step boundary 原子恢复后重放。

[COMPUTED][HIGH] RoundResult 报告 feasible cell/edge/region、coverage、inside band、region population、routed/unrouted、cost、quota change、PlanEpoch、重建原因、solver p95 和四类 hash。

## 5. 当前技术证据

[COMPUTED][HIGH] Target Region Transport 定向自动化 4/4、Particle 23/23 和完整 `CrowdDemo.SF` 43/43 已通过；Development Editor 已通过。

[COMPUTED][HIGH] 8414 Static P0 Round 1 通过：feasible cells/edges/regions=`276/1054/16`，raw/feasible coverage=`16/16`，inside band=`20/20`，最大 Region 人口=`2`，routed/unrouted=`0/0`，solver p95=`6.769ms`。

[COMPUTED][HIGH] 8414 Server/Client 四类 hash 完全一致：Topology=`2545299674`、Demand=`3117978081`、Transport=`2519143770`、Guidance=`1128002910`；Particle Hard/Swept/Obstacle/Bounds、invalid/fallback 均为 0，rollback hit/miss/mismatch=`53/0/0`，agents/visible=`20/20`。

[COMPUTED][HIGH] 当前 Shared Flow V2 hash 为 `520862038`；旧 SF1 事实中的 `267519150` 属于更早的连接/膨胀合同，不能继续写成当前运行值。

## 6. 动态图修复与当前停止点

[COMPUTED][HIGH] 8416完整fixture确认：Agent 15实际目标距离约850.37cm、`bTerminal=false`，但附着的Cell 176锚点半径850cm、`bTerminal=true`；旧solver把它直接吸收到terminal sink，Plan没有生成outgoing quota，validator唯一失败为`InsufficientOutgoingQuotaCellCount=1`，双端fixture hash=`10240167`。

[COMPUTED][HIGH] 通用修复规定：位于terminal-anchor的Supply也必须先经过至少一条真实Topology Edge，禁止source直接被terminal sink吸收；修复没有AgentId、CellId或场景特判。

[COMPUTED][HIGH] 8417 Static通过：inside band=`20/20`、coverage=`16/16`、最大Region人口=`2`、Plan/Guidance unrouted=0、invalid/validation failure=0，Validation hash=`1106829831`双端一致。

[COMPUTED][HIGH] 8418 Moving通过：inside band=`20/20`、feasible coverage=`12/12`、最大Region人口=`2`、Plan/Guidance unrouted=0、invalid/validation failure=0，五类hash双端一致，Particle四类安全违规=0，client rollback hit/miss/mismatch=`53/0/0`。

[COMPUTED][HIGH] 本任务按计划在Moving Small通过后停止；未运行100/500、DebugGame正式门、录像或FFmpeg，也未恢复旧Density。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
