# MassAI Crowd Demo Phase Plan

## 当前唯一阶段：20实体性能与视觉连续性基线

- [x] [COMPUTED][HIGH] 修正错误的solver/replay计时语义，接入完整fixed-step阶段、追帧、积压、client frame、visual processor、Sim/Display delta和collapsed-step指标。
- [x] [COMPUTED][HIGH] 静态Target Topology改为Round级缓存；静态Demand拆分不变Region事实与每步人口更新；短Plan内复用稳定source attachment；Shared Flow V2保存稳定Cell→Node索引，运行采样不再扫描全Navigation节点。
- [x] [COMPUTED][HIGH] 完整状态一致的correction使用零误差快速路径；不恢复runtime或重放fixed step。T2 8679处理54次，hit=54、miss/mismatch=0。
- [x] [COMPUTED][HIGH] client-only相邻SimState插值已接入；steady、catch-up与Round reset跳变已分开计数，不再用一个总数掩盖原因。
- [x] [COMPUTED][HIGH] T2 8701 DebugGame性能门通过：fixed-step p95=`9.841ms`、realtime=`1.000`、client frame p95=`6.748ms`、visual processor p95=`0.126ms`、collapsed p95=`1`、非correction跳变0；功能门仍为handoff/band/settled=`20/20`和coverage=`16/16`。
- [x] [COMPUTED][HIGH] 8700固定首个视觉异常为无新Sim状态时追赶`52.147cm`；显示插值改为同时使用模拟时间和空间距离后，8701不再复现。性能脚本异常路径现会同时清理Server与Client进程。
- [x] [COMPUTED][HIGH] T1–T8当前20实体CPU性能基线已完成；全部fixed-step/client/visual p95低于预声明预算，详细数值见`TestScenarioMatrix.md`。
- [x] [COMPUTED][HIGH] 8702/8705/8706完成第二轮通用热点收敛：Shared Flow V2移除逐厘米×Obstacle与blocked-cell×all-edge扫描；Particle移除安全量化与零残差闭包的空求解。T6 DebugGame首步Flow max=`57.473ms`、整步p95=`13.187ms`，Development整步p95=`4.132ms`。
- [ ] [COMPUTED][HIGH] 单进程DebugGame PIE性能门尚未关闭。旧版T6 Static客户端frame p95=`55.836ms`；修复后的独立DebugGame双端通过不等于同进程PIE通过，必须重新人工PIE复测并记录整帧p95。
- [ ] [COMPUTED][HIGH] 视觉连续性尚未关闭：T1稳定复现113次boundary-reset跳变，T6A出现17次非catch-up跳变；T6S两次运行分别为19和0，需先固定最早跳变fixture再决定是测试状态切换、correction分类还是visual时间合同问题。
- [ ] [COMPUTED][HIGH] 普通客户端仍有约`215ms`进程启动frame max和Round reset跳变；T6 Shared Flow算法首建已降至约`57ms`。动态T5M/T6M仍每步重建Topology，进入100/500前必须拆分静态拓扑与动态目标集成。
- [ ] [INFERRED][HIGH] 对Round reset的加载尖峰和视觉跳变做人工审片；不得用steady p95替代max/catch-up/reset证据。

[INFERRED][HIGH] 本阶段完成前不继续调T5/T6落位、不进入100/500或GT/WORK重构。

### 第一版清理（本轮）

- [x] [COMPUTED][HIGH] parser、console、GameMode配置和MassSubsystem只允许场景0/1。
- [x] [COMPUTED][HIGH] fixed-step driver不再构造或调度Traffic、Portal、PassingBand、Deterministic ORCA、Position/Holding/Commit、Elastic、旧PBD和旧SF4诊断processor。
- [x] [COMPUTED][HIGH] 删除SF2 FlowSeparation、SF3 CrowdTraffic和SF4 PursuitPositioning共8张地图。
- [x] [COMPUTED][HIGH] 删除SF3/SF4地图生成脚本，通用地图脚本不再生成FlowSeparation地图。
- [x] [COMPUTED][HIGH] 删除混合历史自动化文件，将4项当前基础测试迁入`CrowdDemoCoreDeterminismTests.cpp`。
- [x] [COMPUTED][HIGH] 收缩后的`CrowdDemo.SF`为26/26成功，Development Editor与`git diff --check`通过。

### 第二版清理（已完成主体）

- [x] [COMPUTED][HIGH] 旧processor类块、Pipeline状态、Mass fragment已物理删除。
- [x] [COMPUTED][HIGH] 旧纯kernel、RVO2适配及SF3/SF4/Elastic/Joint CLI已删除。
- [x] [COMPUTED][HIGH] `FCrowdDemoRoundRules`、Shared Flow metrics和RoundResult旧字段已收缩。
- [x] [COMPUTED][HIGH] RoundResultHeader版本化NetSerialize与2048字节实测硬门已完成；代表性T6/Combat为1193/598字节，真实网络T1/T5M为739/861字节。

### 清理后的回归门

- [x] [COMPUTED][HIGH] Development与DebugGame Editor。
- [x] [COMPUTED][HIGH] Round时间合同与反射目标运动接入后的全部自动化34/34；生命周期定向6/6、Transport定向6/6。
- [ ] [INFERRED][HIGH] SF1 Single/Cohort当前Flow V2回归。
- [ ] [COMPUTED][HIGH] 20实体回归仍未完全关闭：T1、T2、T6 Static及T6 Moving技术与能力门已通过；T5 Static/Moving覆盖完整但稳定窗口未成立，当前版人工审片仍待补。
- [x] [COMPUTED][HIGH] `rg`确认旧场景、processor、fragment和CLI无生产引用；旧名称只保留parser拒绝测试。

### Transport执行态（已实施，能力未关闭）

- [x] [COMPUTED][HIGH] `FlowPlan`与`QuotaExecutionState`分离；短期edge claim、跨cell一次消费、下一段领取和稳定execution hash已接入。
- [x] [COMPUTED][HIGH] Pipeline prepared状态与SoftPressure rollback snapshot覆盖执行态；多edge与rollback replay纯测试通过。
- [x] [COMPUTED][HIGH] T5 Static 8653安全通过并达到inside/coverage=`20/20`、`16/16`。
- [ ] [COMPUTED][HIGH] T5 Static连续稳定窗口仍未成立，`settling_steps=-1`。
- [x] [COMPUTED][HIGH] T6 Moving旧30秒8658为`19/20`；显式30+15秒合同接入后，8670连续两轮均为inside/coverage=`20/20`。
- [x] [COMPUTED][HIGH] 2010次Plan rebuild已按原因闭合：229 lifetime、0 target revision、1559 feasible graph、0 membership、31 demand satisfied、184 execution invalid、7 initial invalid。
- [x] [COMPUTED][HIGH] 1559次graph rebuild中1208次仅成本变化，351次Cell可行性变化、351次Edge集合变化；Plan age p50/p95/max=`1/15/15`，提前重建1773次。
- [x] [COMPUTED][HIGH] active/geometry-eligible/new-plan-eligible/dropped-still-feasible claim=`1080/1033/851/1033`；execution invalid子类=`7/41/0/392/3`。
- [x] [COMPUTED][HIGH] 双端生命周期hash=`3232819302`，fixture hash=`2766809658`；step 323 fixture已写入并携带最终缺失Region 3。

### Transport生命周期生产合同（已实施，能力仍未关闭）

- [x] [COMPUTED][HIGH] Edge成本变化不再改变`FeasibleGraphHash`，因此不会单独废弃15步不可变短Plan；Cell可行性、Terminal/Region映射或Edge集合变化仍会使Plan失效。
- [x] [COMPUTED][HIGH] Plan替换时先冻结仍在source cell、edge仍存在且Supply仍有效的claim，再以这些claim变换剩余Supply并重算剩余quota；新Plan和新执行态在同一boundary原子发布。
- [x] [COMPUTED][HIGH] 若完整冻结集合无可行流，按稳定AgentId顺序确定性释放最少前缀之外的claim；结果、迁移/释放计数和hash不依赖输入顺序。
- [x] [COMPUTED][HIGH] 8658重建从2010降到883；原因闭合为`238/0/351/0/51/236/7`。346个仍为Supply的有效claim全部迁移，115个到达ToCell的claim在替换边界完成，实际有效claim丢失为0。
- [ ] [COMPUTED][HIGH] 8658仍为inside=`20/20`、coverage=`19/20`；执行态累计401次Supply无可用出口。当前fixture为step 1的最早premature rebuild，只能证明缺失cohort早期已有3个Supply无出口，不能单独证明最终Region缺口的因果。

## 后续阶段

- [x] [COMPUTED][HIGH] 8659已捕获与最终Profile `2217961739`、Region 13直接关联的最后一次Supply无出口fixture：step 883、selection kind 1、hash `4114446767`。旧/新Plan均通向Region 13，Agent 5 claim成功迁移，新Plan同boundary为Agent 4补出出口。

- [x] [COMPUTED][HIGH] 8659连续两轮均为inside=`20/20`、coverage=`19/20`；两轮Transport与生命周期hash一致，Particle安全违规0，rollback miss/mismatch 0，初态hash一致。

[COMPUTED][HIGH] 最后90步对Region 13的Demand/Plan/Guidance/retention gap=`0/4/81/51`，进入/离开=`40/38`。因此当前不是Demand缺失、无路可求或claim丢失，而是动态图切换后的执行Guidance连续性或终端保留/行程时间仍未闭合。

- [x] [COMPUTED][HIGH] 已在8659事实窗口上完成两个只读反事实：保持跨图source attachment连续，以及只在同Region/无surplus/无双占时保持terminal。

- [x] [COMPUTED][HIGH] 8661两个反事实均未成立：attachment recovered guidance=`0`，terminal hold/recovered=`0/0`，outcome=`Neither`，人口守恒违规0。
- [x] [COMPUTED][HIGH] Region 13缺失73步同时存在合法在途路线73步；最终Agent 5还剩2条edge，相对目标速度`301.496cm/s`，剩余edge窗口最小/最大=`2/9`、下降/增加/保持=`7/1/64`。
- [x] [COMPUTED][HIGH] 该结果排除跨图attachment丢失、terminal错误释放、局部速度骤降和Particle安全失败作为T6M当前19/20的生产修复分支。

- [x] [COMPUTED][HIGH] Round时间合同已实施：T6M名义30秒、固定15秒宽限；普通SoftPressure仍为30秒。8662/8665暴露旧单向移动目标越界，随后以通用FlowBounds内反射运动修复测试环境合同。

- [x] [COMPUTED][HIGH] 8671连续两轮均为inside/coverage=`20/20`，7个Capability Profile逐项通过；Mass AgentState hash=`1956985324`、动态Flow round hash、初态hash、Particle candidate及Transport/生命周期hash跨轮一致，双端checkpoint/interval error p95=`0cm`，rollback miss/mismatch=0。

[INFERRED][HIGH] 下一唯一阶段是T5 Static/Moving连续稳定窗口归因和当前版人工审片。GT/WORK与100/500仍不在本阶段；不得把T6M的45秒通过改写成严格30秒通过。

[INFERRED][HIGH] Projectile插件、T9/T10、自由游荡、动态spawn/despawn和NavMesh分层资产均不属于当前清理阶段。

## T6 出口与站位验收收尾

- [x] [COMPUTED][HIGH] 客户端只读验收标记已按语义拆分：真实Slot显示逐位置标记；Region Transport只显示有效距离环、Region边界和可行terminal cell。
- [x] [COMPUTED][HIGH] T4/T6 Transit已加入全群完成后的稳定出口合同与15步低速final-settled门；定向自动化4/4及完整`CrowdDemo.SF` 34/34通过。
- [x] [COMPUTED][HIGH] 8707 T6 Transit单轮达到completed/final-settled=`20/20`，安全、同步、correction和性能门通过。
- [x] [COMPUTED][HIGH] 8708 T6 Static启用Target Stability诊断后达到inside/coverage=`20/20`，90步窗口无Region争抢、terminal chatter或位置抖动，双端hash一致。
- [ ] [INFERRED][HIGH] 在可见客户端PIE中确认标记颜色、位置与实体终态一致，并录制完整T6 Static画面；离屏自动运行不能替代该人工视觉门。

[INFERRED][HIGH] 当前不再把T6 Transit的“完成出口”与T6 Static的“目标周围稳定落位”混为同一验收项。下一步先关闭客户端标记人工审片，再回到T5稳定窗口；不因本轮结果进入100/500。

## T6A 组合能力与朝向收尾

- [x] [COMPUTED][HIGH] T6A已从“出口停车”收敛为`走廊通过 → Target Region分流 → 自然落位`，T4纯Transit分支未改变。
- [x] [COMPUTED][HIGH] Ranged `AcquireThenHold`已实现需求资格门、目标靠近不主动后退、Maximum+100cm与Region超额释放。
- [x] [COMPUTED][HIGH] 朝向已拆出纯kernel：终态前朝自主Preferred，避让/Particle修正不改自主朝向；只有连续15步最终稳定后朝向目标，限速360度/秒。
- [x] [COMPUTED][HIGH] Development、DebugGame Editor和完整`CrowdDemo.SF` 35/35通过。
- [x] [COMPUTED][HIGH] 8719双端T6A达到corridor/inside/coverage=`20/20/20`，Transport/Particle/correction/visible/performance技术门通过。
- [ ] [INFERRED][HIGH] 在可见PIE中审查终态Region分布、Ranged目标靠近时不后退、移动中朝自主方向、最终落位后朝目标及360度/秒转向观感。

[INFERRED][HIGH] 当前停止点是上述人工视觉门；本轮不进入100/500、Moving Target扩展或新的局部避让算法。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
