# MassAI Crowd Demo 测试场景矩阵

## 1. 统一验收层级

[INFERRED][HIGH] 每个场景必须分开记录五类结论：纯kernel自动化、双端技术门、场景能力门、性能门、视觉审片。低层通过不能替代高层能力结果。

[COMPUTED][HIGH] 所有SoftPressure场景共享以下技术门：Hard/Swept/Obstacle/Bounds=`0`、invalid/fallback=`0`、Server/Client hash一致、rollback miss/mismatch=`0`、位置误差p95 `<1cm`、agents=visible、无Fatal/Assertion/Ensure/`LogWindows: Error`/VIOLATION。

## 2. 当前20实体矩阵

| ID | 场景职责 | 自动化 | 技术/能力 | 性能 | 当前版视觉 |
|---|---|---|---|---|---|
| T1 | 开放空间插入、传播、移除和新平衡 | [COMPUTED][HIGH] 通过 | [COMPUTED][HIGH] 6阶段、layer3与两类settling通过 | [COMPUTED][HIGH] 8682 CPU门通过：fixed-step/client/visual p95=`5.437/7.149/0.095ms` | [COMPUTED][HIGH] 未通过：113次非catch-up跳变；该场景存在staging↔active的显式boundary reset，需重新设计可见过渡或单独声明测试跳变合同 |
| T2 | 开放cohort远程移动与目标handoff | [COMPUTED][HIGH] 通过 | [COMPUTED][HIGH] 8701安全/同步通过，handoff/band/settled=20，coverage=16/16 | [COMPUTED][HIGH] DebugGame通过：fixed-step p95=9.841ms，client=6.748ms，visual=0.126ms，realtime=1.0 | [COMPUTED][HIGH] steady/catch-up discontinuity=`0/0`；Round reset跳变待人工判断 |
| T3 | 开放空间双向交换 | [COMPUTED][HIGH] 通过 | [COMPUTED][HIGH] 20/20通过 | [COMPUTED][HIGH] 8683通过：fixed-step/client/visual p95=`7.065/8.462/0.092ms` | [COMPUTED][HIGH] steady/catch-up discontinuity=`0/0`；人工审片待补 |
| T4 | 障碍走廊与局部汇入 | [COMPUTED][HIGH] 通过 | [COMPUTED][HIGH] 20/20通过 | [COMPUTED][HIGH] 8684通过：fixed-step/client/visual p95=`7.687/8.673/0.090ms` | [COMPUTED][HIGH] steady/catch-up discontinuity=`0/0`；人工审片待补 |
| T5S | 静态目标区域分布和稳定落位 | [COMPUTED][HIGH] 通过 | [COMPUTED][HIGH] inside20、coverage16/16；稳定窗口未成立 | [COMPUTED][HIGH] 8685通过：fixed-step/client/visual p95=`9.217/8.647/0.091ms`；Topology=`1/900` | [COMPUTED][HIGH] steady/catch-up discontinuity=`0/0`；连续稳定落位与人工审片仍未完成 |
| T5M | 移动目标区域分布 | [COMPUTED][HIGH] 纯kernel通过 | [COMPUTED][HIGH] inside20、coverage12/12；连续稳定窗口未成立 | [COMPUTED][HIGH] 8686通过：fixed-step/client/visual p95=`13.555/10.056/0.093ms`；Topology每步重建901次 | [COMPUTED][HIGH] steady/catch-up discontinuity=`0/0`；连续稳定落位与人工审片仍未完成 |
| T6A | 异构实体通过走廊后按各自距离能力自然落位 | [COMPUTED][HIGH] 通过 | [COMPUTED][HIGH] 8719 corridor/inside/coverage=`20/20/20`，7类Capability Profile逐项满足 | [COMPUTED][HIGH] 8719 fixed-step p95=`3.278ms`、realtime=`1.001` | [INFERRED][HIGH] 离屏技术门通过；新朝向、Region标记与终态分布仍待可见PIE人工审片 |
| T6S | 异构实体静态目标距离带 | [COMPUTED][HIGH] 通过 | [COMPUTED][HIGH] aggregate inside/coverage=20/20 | [COMPUTED][HIGH] 8689通过：fixed-step/client/visual p95=`9.237/9.979/0.107ms`；Topology=`7/6300`、Demand full/update=`7/6300` | [COMPUTED][HIGH] 本次steady/catch-up discontinuity=`0/0`；8688曾出现19次steady跳变，说明单次结果不稳定，人工审片待补 |
| T6M | 异构实体移动目标距离带 | [COMPUTED][HIGH] 通过 | [COMPUTED][HIGH] 30+15秒合同inside/coverage=20/20 | [COMPUTED][HIGH] 8681通过：fixed-step/client/visual p95=`14.587/14.452/0.092ms`；Topology每cohort每步构建9457次 | [COMPUTED][HIGH] steady discontinuity=0、catch-up discontinuity=19；人工审片待补 |
| T7 | VAT、受击、击退、击飞与死亡 | [COMPUTED][HIGH] 通过 | [COMPUTED][HIGH] 20实体实验能力通过 | [COMPUTED][HIGH] 8690通过：fixed-step/client/visual p95=`6.060/7.528/0.101ms` | [COMPUTED][HIGH] steady/catch-up discontinuity=`0/0`；当前版人工审片待补 |
| T8 | 远程攻击、Projectile与swept hit | [COMPUTED][HIGH] 通过 | [COMPUTED][HIGH] 8691为50次spawn/impact/damage、duplicate=0 | [COMPUTED][HIGH] 8691通过：fixed-step/client/visual p95=`5.797/7.468/0.101ms` | [COMPUTED][HIGH] steady/catch-up discontinuity=`0/0`；当前版人工审片待补 |
| T9 | 混合战斗集成 | [COMPUTED][HIGH] 未实施 | [COMPUTED][HIGH] 未实施 | [COMPUTED][HIGH] 未实施 | [COMPUTED][HIGH] 未实施 |
| T10 | 最小玩法沙盒和插件宿主 | [COMPUTED][HIGH] 未实施 | [COMPUTED][HIGH] 未实施 | [COMPUTED][HIGH] 未实施 | [COMPUTED][HIGH] 未实施 |

## 3. 20实体性能与连续性门

[COMPUTED][HIGH] 算法性能门：fixed-step p95 `<=33.333ms`、simulation realtime factor `>=0.95`、fixed-step上限命中=`0`。

[COMPUTED][HIGH] 客户端steady门：client frame p95 `<=33.333ms`、visual processor p95 `<=16.667ms`、collapsed steps p95 `<=1`、非correction且非catch-up discontinuity=`0`。

[INFERRED][HIGH] client frame max、stage max、catch-up discontinuity和Round reset jump必须单列证据，但当前不与steady p95合并成一个真假结论；人工审片负责判断加载/重置跳变是否可接受。

[COMPUTED][HIGH] 历史矩阵的多数场景曾存在约`220–310ms`启动或首轮构建max尖峰；通用Flow构图修复后T6算法首建已降至约`57ms`，但普通客户端仍记录约`215ms`进程启动frame max。因此“稳态预算通过”仍不能改写为“编辑器启动全程无卡顿”。

[COMPUTED][HIGH] 上表性能值来自独立Server/Client进程，不能代表单进程PIE。用户旧版单进程DebugGame PIE在T2/T6A/T6S观察到client frame p95约`25.1/46.7/55.8ms`；修复后T6独立DebugGame整步p95=`13.187ms`、Development=`4.132ms`，但单进程PIE仍待复测。

[INFERRED][HIGH] 新增PIE门：同一编辑器进程同时运行Server/Client时，实际client frame p95必须`<=33.333ms`、collapsed-step p95 `<=1`、simulation realtime factor `>=0.95`；不得用“两端各自低于33.333ms”代替，因为两套Game Thread成本在PIE中串行相加。

## 4. 当前自动化口径

[COMPUTED][HIGH] 2026-07-18，`CrowdDemo.SF`为34项，34/34成功；包含RoundResultHeader NetSerialize、2048字节门、Round时间合同和有界反射目标运动。

[COMPUTED][HIGH] 旧SF3/SF4的37项自动化和旧Hard PBD的1项自动化已经退出当前测试集；历史结果仍可从Git历史和`Docs/History`查阅，不再计入当前完整SF通过数。

## 5. 规模矩阵

| 规模 | 当前状态 |
|---|---|
| 20 | [COMPUTED][HIGH] 当前唯一正式验收规模；T6M 45秒coverage已关闭，仍需关闭T5S/T5M连续稳定窗口与当前版本人工审片。 |
| 100 | [COMPUTED][HIGH] 当前SoftPressure/Transport/Local Predictive组合未正式验收。 |
| 500 | [COMPUTED][HIGH] 当前组合未正式验收；旧SF1/SF2结果不可替代当前Flow V2和Local Predictive证据。 |

## 6. 当前停止点

[INFERRED][HIGH] 第二版旧代码、结果头、可回滚quota执行态与T6M时间合同已完成；下一步关闭T5 Target稳定窗口并补当前版人工审片，之后才能进行GT/WORK拆分和100/500规模门。

## 7. T6 完成语义与最新证据

[COMPUTED][HIGH] T4仍是纯Transit测试；T6A `HeterogeneousTransit`现在是组合能力测试：先要求全体安全穿过走廊，再启用Target Region Transport按Capability Profile落位。旧8707“出口停车即完成”仅保留为历史分阶段证据，不再是T6A最终能力门。

[COMPUTED][HIGH] 8719 T6A达到wall/corridor/completed=`20/20/20`、inside/coverage=`20/20`、max Region population=`1`、final deadlock=`0`；Transport五类hash双端一致，客户端rollback=`80/0/0`且80次均为零误差快速路径，checkpoint/interval/yaw/velocity误差p95均为0，agents=visible=`20/20`。

[COMPUTED][HIGH] T6 Static才是异构目标距离带落位测试。8708的90-step稳定窗口达到inside minimum=`20`、coverage=`20/20`、terminal chatter=0、position peak-to-peak p95/max=`0/0cm`。

[COMPUTED][HIGH] 客户端显示同样区分两类合同：只有真实TargetSlotLayout显示精确逐Slot位置；T6 Static当前Region Transport只显示有效距离环、Region分区和可行terminal cell。人工审片必须据此判断，不得要求一个实体占一个不存在的永久Slot。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
