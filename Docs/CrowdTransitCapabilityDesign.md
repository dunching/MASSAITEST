# 群体软排斥与自然穿行能力设计

## 2026-07-14 当前验证结论

[COMPUTED][HIGH] SoftPressure现使用量化感知的环境容量分配关闭Hard/Swept/Obstacle/Bounds安全链；8368 Agent 4/12完整fixture已从`93.723cm`修复到`94.493cm`。独立rollback和candidate/applied hash v2均已通过自动化与8371双端实测。

[COMPUTED][HIGH] 该机制没有证明Small能力通过。8371在step 155的Agent 5/13上仍只有`93.021cm`；沿当前分离法线的Obstacle/Bounds可实现容量不足，固定8轮后返回invalid。不得用Soft push、全群静默刹车后的applied安全状态或旧ORCA路径掩盖该candidate失败。

[INFERRED][HIGH] 下一项算法问题是补齐粒子—环境Soft合同，并研究障碍附近多个二维Hard约束的共同可行域，而不是再次增加单法线投影次数。任何后续方案都必须先重放8371完整20实体fixture，并保持94cm、8+8轮、30Hz和Obstacle/Bounds门不变。

## 0. 当前唯一设计：通用Soft-Pair Pressure

[INFERRED][HIGH] 本节覆盖下方所有Priority ORCA、Transit Source、Joint Velocity、Elastic Shadow和Position/Holding恢复路线。下方内容保留为历史实验与反证，不再定义生产架构。

### 0.1 数据语义

[INFERRED][HIGH] 每个实体只向局部群体solver提供稳定POD事实：`AgentId、Position、PredictedPosition、PhysicalRadius、HardSafetyGap、SoftMargin、Mobility/InverseMass`。不存在`bTransitSource`、`TransitDirection`、`TransitDeltaVelocity`或“需要恢复的阵型槽位”。

```text
HardPairDistance = RadiusA + RadiusB + max(HardGapA, HardGapB)
SoftPairDistance = HardPairDistance + SoftMarginA + SoftMarginB

WallHardDistance = RadiusA + HardGapA
WallSoftDistance = WallHardDistance + SoftMarginA
```

[INFERRED][HIGH] 粒子pair距离小于SoftPairDistance、或粒子到环境距离小于WallSoftDistance时产生有限、可压缩的软压力；低于相应HardDistance时产生不可接受的硬约束。Soft使用显式fixed-step response，不依赖“固定迭代没有做完”偶然产生柔软效果；SoftError可以长期非零且不能使solver invalid。

```text
PerStepResponse = 1 - exp(-SoftResponsePerSecond * FixedStepSeconds)
SoftCorrection  = SoftError * PerIterationResponse * MobilityShare
```

[COMPUTED][HIGH] 当前P0的`SoftResponsePerSecond=8/s`、30Hz对应整步约`23.4%`响应；8轮只负责稳定分配该整步响应，不把SoftDistance当作本步必须完全恢复的硬壳。

[INFERRED][HIGH] Mobility只决定双方承担多少修正：普通实体通常对称分担，低Mobility实体移动更少，零Mobility静态环境完全不动。它不是ORCA priority，也不允许删除任一方的Hard约束。环境Soft/Hard只约束墙面法向，合法切向运动必须保留。

[INFERRED][HIGH] 该设计采用类似Boids的最小局部规则原则，但不照搬经典Boids行为集合。共享Flow/Goal/Input已经生成DesiredVelocity；局部solver不再增加“穿行者”“靠墙者”“让路者”等身份，只依据几何、质量和接触关系产生群体结果。

### 0.2 目标流水线

```text
Shared Flow / Goal / Player Input
→ Preferred Velocity
→ MovementPredict
→ Deterministic Particle Constraint Solve
   ├─ Soft pair relaxation（可压缩）
   ├─ Soft environment pressure（可压缩，法向）
   ├─ Hard pair separation（不可压缩）
   ├─ 主循环：Pair Soft / Environment Soft / Pair Hard / Swept / Environment Hard
   ├─ 位置量化
   └─ 固定安全闭环：Pair Hard / Swept / Environment Hard（不含Soft）
→ MovementFinalize
→ Authority / Client Commit
```

[INFERRED][HIGH] 主循环的目标是把粒子pair与环境contact放入同一局部预测位置问题；量化后只闭合不可压缩安全约束。简单规则不意味着简单串行覆盖：当多个pair与墙体同时生效时，solver必须稳定找到共同可行结果，或明确证明当前约束集合无解。

[INFERRED][HIGH] Flow或目标输入推动整个群体；新生成实体仅凭自己的半径和软壳挤压邻居。邻居被移位后又会与下一层形成SoftError，因此传播无需显式component、完整通道、ring-entry状态或联合速度规划。

### 0.3 平衡与恢复边界

[COMMON][HIGH] 纯有限距离排斥只会消除局部软压缩，不会记住原始矩阵。

[INFERRED][HIGH] 盒体内密度足够高时，移除中心实体后，边界压力和剩余pair约束会促使群体形成新的近均匀平衡；开放空间若已经没有SoftError，实体可以停在被推开后的新位置。这正是当前目标，不应额外加入Formation Recovery。

### 0.4 验收场景

[INFERRED][HIGH] 最小纯fixture固定为：100实体盒体静置分散、中心插入、中心移除、开放空间插入/移除、单实体ring-entry、ring-exit、两个移动实体穿群、窄口压缩、单侧垂直墙、单侧斜墙、双侧墙、靠墙压力传播和8371完整重放。所有fixture覆盖输入反序、量化和双轮hash；这些名称只属于测试，不进入生产场景判断。

[INFERRED][HIGH] 核心指标为SoftError p50/p95/max、Hard/Swept/Obstacle/Bounds violation、受影响实体数、`first_influenced_iteration_max`、settling time、动能和solver p95。`first_influenced_iteration_max`只表示实体首次实际修正所在solver iteration，不是空间传播层；若未来需要传播层，必须从初始SoftError pair邻接图做BFS。

### 0.5 2026-07-14 生产接入与准确停点

[COMPUTED][HIGH] Particle Constraint纯内核、17项fixture、`SimRoundSoftPressure=1`生产链、RoundResult candidate/applied metrics、rollback与双端hash已经实现；Development、DebugGame Editor及完整`CrowdDemo.SF` 42/42通过。

[COMPUTED][HIGH] 基础正式阵型采用128cm附近初始间距；95cm阵型保留为Stress Gate。盒体、开放移除、ring entry/exit、障碍冲突、量化安全和invalid candidate/applied语义均有对应自动化，不再由测试名称代替测试内容。

[COMPUTED][HIGH] 8368 Small 20在step 93出现candidate Hard/Swept=`1/1`后立即固定fixture并停止。首失败pair Agent 4/12在Obstacle前约`95.26cm`，Obstacle reproject后为`93.723cm`，相对94cm硬距离缺`0.277cm`。

[COMPUTED][HIGH] 本boundary candidate/applied/fixture hash=`3480157140/1765689391/3809298193`且双端一致；实际fallback状态四类安全violation为0，invalid/fallback=`1/1`。fixture明确区分Predict、Soft、Hard、Swept、Obstacle、Quantized、FinalSafety和Applied，不再把被拒绝candidate当作实际群体状态。

[COMPUTED][HIGH] 8368旧fixture已关闭；8371在step 155固定Agent 5/13新fixture，endpoint/swept=`93.021cm`且candidate Hard/Swept=`2/2`。当前环境只按`PhysicalRadius+HardSafetyGap`执行Hard投影，没有消费SoftMargin；正式Mobility也全部为1。

[COMPUTED][HIGH] 旧Transit/ORCA/Position/Holding/Elastic/Shadow/Joint路径因删除前门未成立而保留。下一步先增加环境Soft contact，再验证粒子/环境二维Hard共同闭环；Small通过前不删除旧路径，不运行100/500。

## 1. 文档职责与状态

[INFERRED][HIGH] 本文件定义“一个高优先级、不同尺寸或外部控制的实体如何安全穿行密集集群，以及集群随后如何恢复结构”的长期目标、证据边界、需求拆分和候选实现方向。

[COMPUTED][HIGH] 本文件是目标与设计文档，不是已实现事实；当前 SF4 Static Small 仍为物理满足 `19/20`，并存在 ORCA infeasible/stop violation，不能据此写成群体穿行能力已经通过。

[INFERRED][HIGH] 该能力比“修复 SF4 最后一个未落位实体”更一般。SF4 可以提供真实失败 fixture 和第一批验证场景，但不应把玩家穿群、大尺寸单位穿群和编队恢复全部塞入 SF4 的 Position/Holding 特例。

## 2. 参考视频登记与证据边界

[KNOWN][HIGH] 本次参考视频的原始文件为：

```text
C:\Users\dunch\Videos\NVIDIA\Desktop\Desktop 2026.07.13 - 14.27.46.01.mp4
```

[COMPUTED][HIGH] 使用工程 `Tools/FFmpeg/Win64/bin/ffprobe.exe` 只读分析得到：视频时长约 `255.799s`，分辨率 `2560×1440`，视频编码为 HEVC，约 `30fps`、`7674` 帧，并包含 AAC 音频；它是播放网页视频时形成的屏幕录制。

[COMPUTED][HIGH] 原始文件大小为 `586896487` bytes，最后写入时间为 `2026-07-13 14:32:04 +08:00`，SHA-256 为 `5FFAF3AA3E2B7FFFEE4D9910F1E9762BBF7D889150F3E41C525B0347742EDBB9`。该指纹用于区分同名录像；视频本身没有复制进 Git 仓库。

[COMPUTED][HIGH] 与本能力直接相关的可见片段位于录屏约 `208s–225s`：

| 录屏时间 | 可见事实 |
|---:|---|
| 约 208s | [COMPUTED][HIGH] 大量小型单位形成近似圆形或椭圆形密集阵型，数个大型履带单位从阵型外侧接近。 |
| 约 211s | [COMPUTED][HIGH] 画面字幕出现“大质量单位穿过小质量单位”，大型单位开始进入小型单位群。 |
| 约 214s–217s | [COMPUTED][HIGH] 大型单位保持明显的整体推进趋势；其邻近的小型单位产生局部疏散、空隙和重新分布，而不是整个群体完全静止。 |
| 约 220s–225s | [COMPUTED][HIGH] 画面字幕出现“多尺寸单位最优阵型重组”；大型单位进入阵型内部后，小型单位在其周围重新形成连续、可读的整体结构。 |

[COMPUTED][HIGH] 该片段清楚展示的是“进入并融入群体后重组”；它没有完整展示大型单位从另一侧离开群体，也没有展示小型单位恢复到逐一相同的原始 slot。

[COMPUTED][HIGH] 静态抽帧只能确认画面中的相对运动、空隙和重组趋势，不能证明底层碰撞胶囊在所有帧严格无重叠，也不能证明服务器/客户端确定性、固定步长或性能指标。

[COMPUTED][HIGH] 视频没有暴露内部代码、约束、导航数据或 solver 输出，因此不能据此声称它使用了 ORCA、RVO2、FlowField、联合速度优化、密度场、slot 或任何具名算法。

[INFERRED][HIGH] 该视频作为目标效果证据的有效信息是：高优先级或大尺寸实体推进时，周围群体能够协同释放局部空间；推进结束或进入稳定区域后，群体能够重新形成连续结构。算法选择必须由本 Demo 的确定性、安全性和规模验证决定，而不是从视频外观反推。

## 3. 根本需求

[INFERRED][HIGH] 根本需求不是“让某一个被包围的 Agent 获得特殊通道”，而是建立通用的局部群体穿行能力：

```text
穿行实体保持明确的主要运动意图
→ 邻近群体协同调整速度并释放空间
→ 所有实体持续满足局部碰撞与障碍安全
→ 穿行完成或局部压力解除
→ 被扰动实体回到自己的群体目标、Position 或 Holding
```

[INFERRED][HIGH] 穿行实体可以是高优先级虫子、正在 Commit 的实体、大尺寸单位、玩家控制角色或车辆。它们的业务来源不同，但对局部运动层提出的是同一类问题：谁更应保持期望速度、谁应承担更多避让责任，以及邻居如何共同形成可通行空隙。

[INFERRED][HIGH] 该能力必须覆盖两个方向：从集群外进入内部，以及从集群内部向外离开。只验证“进入最终站位”不能代表玩家或大型单位能够持续穿过虫群。

## 4. 需求拆分

### 4.1 穿行意图与优先级事实

[INFERRED][HIGH] 上层只提供稳定事实：实体的 preferred velocity、半径、最大速度、业务优先级和是否为外部控制速度；局部 solver 不应自行决定攻击目标、最终 Position 或玩家输入。

[INFERRED][HIGH] 优先级的含义是“偏离自身 preferred velocity 的代价不同”，不是允许高优先级实体穿透其他实体。任何优先级仍必须经过碰撞、障碍和 Target exclusion 安全验证。

### 4.2 局部交互集合

[INFERRED][HIGH] 只应联合处理与穿行实体在空间和预测时间内相关的局部连通分量，而不是对全部 500 个实体进行单一全局速度优化。

[INFERRED][HIGH] 局部集合必须由稳定 spatial grid、AgentId 和稳定 pair 顺序构造。多个高优先级实体的邻域可能合并为较大分量，因此实现不能预设所有分量永远只有 2–8 个实体；超出已验证规模时必须有明确的确定性回退和指标。

### 4.3 自适应群体间距

[INFERRED][HIGH] 群体需要保持可读间距，但不能把所有实体对之间的同一固定距离设为永久硬约束。固定宽间距会使窄口和高密度区域更容易无解；只有碰撞安全距离必须始终满足，视觉与编队间距应允许有界压缩和恢复。

[INFERRED][HIGH] 距离语义必须拆成四层：

| 数据语义 | 职责 |
|---|---|
| `PhysicalRadiusCm` | [INFERRED][HIGH] 实体真实物理半径，不因密度、优先级或通道改变。 |
| `HardSafetyGapCm` | [INFERRED][HIGH] ORCA/联合约束与PBD必须保护的额外硬安全间隙。 |
| `PreferredSpacingGapCm` | [INFERRED][HIGH] 普通群体流动希望保留的软间距；可以在受控条件下压缩，但不能取代硬安全。 |
| `AssignedSpacingCm` | [INFERRED][HIGH] Position/Holding/Formation布局给出的结构距离，用于稳定站位和穿行后的恢复。 |

[INFERRED][HIGH] 对实体 pair `i,j`，第一版使用以下派生距离：

```text
HardPairDistance(i,j)
= Radius_i + Radius_j + HardSafetyGap(i,j)

PreferredPairDistance(i,j)
= HardPairDistance(i,j) + PreferredSpacingGap(i,j) × ContextScale(i,j)
```

[INFERRED][HIGH] `ContextScale` 是量化、确定性的环境压缩系数：开放区域接近完整软间距；窄口或已证明的高密度瓶颈可以逐步下降；最低只能把软间距压缩为零，绝不能让 `PreferredPairDistance` 小于 `HardPairDistance`。

[INFERRED][HIGH] 第一版不预声明具体厘米值和压缩曲线。参数必须由实体半径、现有 Small fixture、窄口宽度和视频启发的多尺寸 fixture 共同门控；没有运行证据前不得把视觉经验写成正式默认值。

[INFERRED][HIGH] Stable/Reserve实体的结构恢复优先消费Assigned Position/Holding，而不是依靠pair排斥把它们推回近似圆环。普通移动实体没有Assigned目标时，才由`PreferredSpacingGapCm`提供松散群体间距。

[INFERRED][HIGH] 高优先级或大尺寸实体穿行时，不直接永久放大所有邻居的硬半径。穿行者的真实半径继续进入硬约束；额外净空通过局部联合求解中的软间距损失和优先级运动权重形成，使邻居在存在空间时提前让行，在没有空间时仍可压缩到硬安全边界。

[INFERRED][HIGH] 软间距不能同时由多个互不知情的阶段重复施力。局部联合分量内由联合solver统一处理spacing slack；分量外若保留Soft Separation，只能处理未进入联合分量的普通pair，并且同一pair在同一fixed-step只能有一个软间距owner。

### 4.4 协作式速度响应

[INFERRED][HIGH] 对一个后方更快或优先级更高的实体，仅把它减速到零不是期望效果；如果周围存在安全空间，低优先级邻居应承担更大的侧移或减速责任，并且多个邻居可能需要同时改变速度才能形成空隙。

[INFERRED][HIGH] 所需输出不是预先脚本化的“退圈—放行—围回去”状态机，而是一组在当前 fixed-step 同时成立的局部速度。不同邻居可以选择不同方向和幅度，形成连续的让行运动。

### 4.5 硬安全链

[INFERRED][HIGH] 联合速度结果仍必须经过 Obstacle Constraint、Hard PBD 和 Obstacle Reproject；这些阶段只负责安全兜底，不能把 solver 大量不可行或零速掩盖成能力通过。

[INFERRED][HIGH] 若联合结果在后续阶段被持续归零，应分别记录 ORCA/联合求解输出、Obstacle 输出、PBD/Reproject 修正和 Final velocity，不能把原因统一归到“邻居不让路”。

### 4.6 穿行后的恢复

[INFERRED][HIGH] 恢复不应首先增加新的 Open/Close/Return 业务状态。现有 Flow、Position、Holding 和 Stable/Reserve 的 preferred velocity 已经提供“被推开后往哪里回来”的基础事实；局部 solver 解除穿行压力后，实体应自然重新消费这些事实。

[INFERRED][HIGH] 只有实测证明自然恢复产生长期空洞、换位振荡或 assignment 冲突时，才设计独立的重组或重分配规则。

### 4.7 双端确定性与视觉真实性

[INFERRED][HIGH] Server/Client 必须消费相同量化输入、稳定 pair/component 顺序和固定迭代次数，并比较局部求解 hash。客户端视觉只显示 client sim state，不得用额外偏移制造让行或恢复效果。

## 5. 当前实现重新审视

### 5.1 已有 Generic Priority ORCA

[COMPUTED][HIGH] 当前生产实现已经把优先级定义为 `(PortalPriority, LocalPriority)` 字典序；SF4 中 Commit=`Committed`，Stable/Reserve=`Yielding`，其余为 `Normal`。高低优先级 pair 的责任为 `25%/75%`，同级为 `50%/50%`，双方约束均保留。

[COMPUTED][HIGH] Static Small 单轮真实触发了 `19247` 个非对称 pair constraint，priority hash 双端一致；但最终只物理满足 `19/20`，Agent 6 的 preferred/ORCA/final 为 `(79,796)/(0,78)/(0,0)cm/s`，且 ORCA `stop_violates=427`。

[INFERRED][HIGH] 这证明“责任比例”已经生效，但没有证明它能够为被包围或穿群实体生成协作式空隙。Pairwise 责任偏置仍可能得到“高优先级实体显著减速、低优先级实体只做局部最小改变”的结果，也不能直接协调三个以上邻居共同移动。

### 5.2 走廊或引导场方案

[INFERRED][MED] 可以在高优先级实体前方生成短期局部让行方向或排斥场，使邻居向走廊两侧移动。该方案计算便宜、易批处理，但走廊宽度、方向冲突、多个穿行者交叉和恢复时机容易演变为额外状态与启发式规则。

[INFERRED][HIGH] 该方案可以作为未来性能回退或宏观 guidance，但不应在尚未证明局部联合速度求解必要性前直接成为 SF4 特例。

### 5.3 局部联合速度求解

[INFERRED][HIGH] 当前更符合目标效果的实验方向是：对一个局部连通分量同时求解所有实体的二维速度，使 pair 安全约束作用于相对速度，并按优先级最小化各实体偏离 preferred velocity 的总代价。

[INFERRED][HIGH] 概念目标为：

```text
最小化：
Σ motion_weight_i × |velocity_i - preferred_i|²
+ Σ spacing_weight_ij × spacing_slack_ij²
+ Σ recovery_weight_i × |position_i + fixed_dt × velocity_i - assigned_position_i|²

同时满足：
- 每个实体速度不超过 MaxSpeed
- 每个相关 pair 的预测相对运动安全
- 外部控制实体可固定速度或使用极高保持权重
- spacing_slack只能违反软间距，不能违反HardPairDistance
- 量化后的最终速度仍通过全部约束
```

[INFERRED][HIGH] `weight` 只表达谁更应保持原意图：Commit/玩家/大型穿行者较高，普通移动实体居中，Stable/Reserve 较低。它不直接指定低优先级实体向哪一侧移动；具体侧移方向由局部几何、preferred velocity 和全部 pair 约束共同决定。

[INFERRED][HIGH] 为避免第一版直接进入含离散避让侧选择的全局非凸优化，可复用现有确定性 ORCA pair geometry 固定每个 pair 的安全半平面方向，再在局部分量内联合求解速度。该路径保留现有邻居、量化和安全语义，同时验证“同时求多个实体速度”是否确实比逐实体 LP 更能打开空隙。

[INFERRED][HIGH] 第一版软间距也不直接优化欧氏距离范数，而是沿稳定pair normal线性化下一fixed-step的相对间距，并为不足`PreferredPairDistance`的部分引入非负spacing slack。slack进入目标函数，`HardPairDistance`继续作为不可违反的约束；这样可以在固定pair方向后保持局部连续优化问题，不引入“必须从左还是从右绕”的新离散状态。

[INFERRED][HIGH] 当两实体中心重合或pair normal数值退化时，必须复用现有ORCA稳定normal；若仍无有效normal，则按AgentId稳定派生方向并计入degenerate指标。不得使用render-frame随机方向打散。

[INFERRED][HIGH] 该方向的复杂度高于逐实体 ORCA，但只在局部分量执行；第一阶段必须用 2–8 实体纯 fixture 验证正确性，不宣称已满足 100/500 规模。真实运行还必须报告分量大小 p95/max 和 solver 时间，不能假设局部集合永远很小。

### 5.4 Soft Separation 与联合spacing的边界

[COMPUTED][HIGH] 当前工程已有Soft Separation内核与ORCA/PBD设施，但它们的历史目标主要是局部重叠控制，不等价于本节定义的开放区舒适间距、瓶颈压缩和穿行后恢复。

[INFERRED][HIGH] 第一版不把Soft Separation push直接叠加到联合solver输出。正确顺序是先在prepared输入中生成普通preferred velocity，再由局部联合solver同时处理运动保持和spacing slack；只有未进入任何联合分量的普通pair才允许继续使用轻量Soft Separation。

[INFERRED][HIGH] 如果后续性能证明确实不能对普通pair运行联合solver，可以把相同`PreferredSpacingGapCm/ContextScale`语义下沉为确定性Soft Separation近似，但必须使用相同pair owner规则和独立回归，不能让两个实现同时修正同一pair。

### 5.5 全局调度或完整多智能体路径规划

[INFERRED][HIGH] 对全部实体进行完整时空路径联合规划、逐格预约或全局混合整数优化可以表达更复杂的穿群问题，但会把连续局部避让升级为全局路径所有权系统，与当前 Demo 的“共享导航 + 局部修正”边界冲突。

[INFERRED][HIGH] 在局部联合速度 fixture 尚未证明失败前，不选择该路线。

## 6. 推荐的第一阶段实现边界

[INFERRED][HIGH] 第一阶段只新增纯 C++ fixture kernel，与当前 Priority ORCA 并列比较，不替换生产 processor：

```text
稳定局部输入
→ 派生HardPairDistance与PreferredPairDistance
→ 复用现有pair geometry构造硬相对速度约束
→ 为软间距构造spacing slack rows
→ 构造局部连通分量
→ 确定性联合速度求解
→ 1cm/s量化与全部约束复验
→ 与当前 Priority ORCA 结果对照
```

[INFERRED][HIGH] 求解顺序、pair 顺序、相同代价决胜、迭代次数和量化修复必须固定；任何数值失败或量化后不安全均返回显式 infeasible，不得无条件接受 stop。

[INFERRED][HIGH] 超过已验证分量规模时，第一版只允许记录 oversize 并回退当前 Priority ORCA；该回退只是安全兼容行为，不代表大分量穿行能力通过。

[INFERRED][HIGH] 生产接入前必须先得到两个证据：真实 Agent 6 最小 fixture 中联合 solver 能找到当前 ORCA 未找到的安全正向速度；视频启发的多尺寸局部 fixture 中，大尺寸/高优先级实体能够推进，周围至少两个低优先级实体协同让行，并在压力解除后由既有 preferred velocity 恢复。

[INFERRED][HIGH] 如果 Agent 6 的联合 fixture 仍不可行，或联合输出随后仍被 Obstacle/PBD/Finalize 归零，则停止该分支并修正真实下游约束；不能因为目标效果喜欢联合求解就强行接入生产。

### 6.1 分阶段实施路径

1. [INFERRED][HIGH] 建立纯间距数据合同和派生函数，只验证四层距离语义、ContextScale边界、pair owner、稳定hash和输入反序；不接入Mass。
2. [INFERRED][HIGH] 增加单pair与普通群体fixture，验证开放区保持软间距、窄口只压缩软间距、HardPairDistance永不压缩、Assigned Position恢复优先于普通spacing。
3. [INFERRED][HIGH] 在2–8实体纯局部分量中加入spacing slack与联合速度目标，对比当前Priority ORCA；覆盖进入/离开包围圈和多实体同时让行。
4. [INFERRED][HIGH] 捕获并重放Agent 6真实fixture，分别比较preferred、Priority ORCA、joint output、Obstacle、PBD/Reproject和final速度；无法唯一归因时停止。
5. [INFERRED][HIGH] 纯fixture通过后才设计shadow模式prepared SoA：同一boundary计算但不消费joint结果，只比较hash、可行性、间距缺口、forward speed和solver时间。
6. [INFERRED][HIGH] shadow证据通过后另行授权Static Small生产接入，原子发布局部结果并补齐rollback、双端hash与聚合指标；失败回退上一份完整安全结果，不部分应用。
7. [INFERRED][HIGH] Static Small安全和能力门通过后，才设计独立的多尺寸穿行场景；20实体通过不能直接进入100/500或玩家业务接入。

## 7. 最小 fixture 与验收场景

| 场景 | 第一阶段要证明的事实 |
|---|---|
| 后方快实体追上前方慢实体 | [INFERRED][HIGH] 存在横向空间时，不能只让后方高优先级实体长期减速；低优先级实体应承担更多让行。 |
| 高优先级实体进入五实体环 | [INFERRED][HIGH] 至少两个外围实体协同移动并形成安全入口，不要求预设退圈脚本。 |
| 高优先级实体从环内离开 | [INFERRED][HIGH] solver 能为出口方向形成局部空隙，不能把内部实体永久困住。 |
| 固定速度玩家穿过虫群 | [INFERRED][HIGH] 玩家速度作为外部事实时，虫群承担避让且保持无 penetration。 |
| 多尺寸单位进入并融入 | [INFERRED][HIGH] 复现参考视频 `208s–225s` 的抽象效果：大半径实体推进，小实体局部释放空间，之后恢复连续结构。 |
| 两个穿行者交叉 | [INFERRED][HIGH] 局部分量合并时结果仍确定、安全；超过能力边界时显式 oversize/fallback。 |
| Agent 6 真实最小 fixture | [INFERRED][HIGH] 用真实约束判定当前失败究竟是逐实体 ORCA 局限，还是下游 Obstacle/PBD/Finalize 问题。 |
| 开放区域普通群体 | [INFERRED][HIGH] 无穿行压力时保持PreferredSpacing，不能只满足不重叠。 |
| 同一群体通过窄口 | [INFERRED][HIGH] 只压缩软间距并保持HardPairDistance，出口后恢复PreferredSpacing。 |
| Stable/Reserve被临时推开 | [INFERRED][HIGH] 穿行压力解除后优先回到Assigned Position/Holding，不依赖永久pair排斥维持结构。 |

## 8. 指标与停止门

[INFERRED][HIGH] 第一批紧凑指标应包括：

- [INFERRED][HIGH] `transit_forward_speed_ratio_p50/p95`：穿行实体最终沿 preferred 方向速度与期望速度之比。
- [INFERRED][HIGH] `transit_zero_speed_step_count`：存在非零 preferred 时被求解为近零速度的 fixed-step 数。
- [INFERRED][HIGH] `local_joint_component_count/p95/max` 与 `local_joint_oversize_count`。
- [INFERRED][HIGH] `joint_feasible_count`、`joint_infeasible_count`、`quantized_validation_failure_count`。
- [INFERRED][HIGH] `yielding_agent_count_p50/p95/max` 与低优先级实体最大位移。
- [INFERRED][HIGH] `preferred_spacing_deficit_cm_p50/p95/max`、`preferred_spacing_satisfied_pair_count`和`spacing_compressed_pair_count`。
- [INFERRED][HIGH] `hard_pair_distance_violation_count`与`spacing_pair_double_owner_count`，两者硬门均为0。
- [INFERRED][HIGH] `spacing_recovery_time_seconds_p50/p95/max`与压力解除后的最终间距缺口。
- [INFERRED][HIGH] pair/obstacle/Target exclusion violation、Hard PBD correction 和 penetration。
- [INFERRED][HIGH] 穿行压力解除后的 `recovery_time_seconds_p50/p95/max` 与 physically-satisfied Position/Holding 数量。
- [INFERRED][HIGH] server/client joint hash、solver ms p95、agents 与 visible instances。

[INFERRED][HIGH] 不输出 per-agent 常态日志，不引入 timeline trace 或通用 A/B 框架；只允许失败 fixture 和每轮聚合。

[INFERRED][HIGH] 纯 fixture 未同时证明安全、确定性和相对当前 Priority ORCA 的正向速度改善前，不修改生产 ORCA。生产接入后若最终 penetration、Target crossing、双端 hash、correction 或 visible instances 任一回退，立即撤销生产分支。

## 9. 与 SF4 的边界

[COMPUTED][HIGH] SF4 已经拥有联合 Position/Holding assignment、Commit residual capacity、Generic Priority ORCA 和恢复目标事实；这些设施可以作为穿行实验的输入与恢复基础。

[COMPUTED][HIGH] SF4 当前未证明：包围群体能够为 Commit 实体协同打开空间；玩家或大尺寸实体能够持续穿群；局部联合速度求解优于现有 Priority ORCA；100/500 规模下局部分量大小与性能可控。

[INFERRED][HIGH] 下一步应由 SF4 输出 Agent 6 的真实最小约束 fixture，并由本文件定义的纯局部联合 solver 做对照。只有证据成立后，才另行设计 processor 接入、rollback、双端 hash 和正式场景；本文件不授权直接修改生产行为。

## 10. 明确不在本阶段实施

- [INFERRED][HIGH] 不实现玩家控制器或大型车辆业务逻辑。
- [INFERRED][HIGH] 不新增退圈、开门、关门、回圈等专用状态机。
- [INFERRED][HIGH] 不替换 SF1/SF2/SF3，不修改地图、Lighting、fixed-step、网络或 PBD 参数。
- [INFERRED][HIGH] 不进行全 500 实体全局联合优化，不宣称 2–8 实体 fixture 代表大规模性能通过。
- [INFERRED][HIGH] 不从参考视频反推具名算法，也不把目标效果写成当前能力。

## 11. 第一版纯内核数值契约

[COMPUTED][HIGH] 第一版实验实现为纯 C++ `FCrowdDemoJointVelocityKernel`，尚未接入 Mass processor 或生产 RoundSim。它复用 `FCrowdDemoDeterministicOrcaKernel` 的 pair 几何，但把责任比例之前的 pair 事实显式表示为耦合约束：

```text
dot((VelocityA - VelocityB) - RelativeVelocityPoint, Normal) >= 0
```

[INFERRED][HIGH] 局部分量只从明确的 transit seed 沿稳定 pair 图闭包生成；Agent、pair 和 component 分别按 AgentId、`(MinAgentId, MaxAgentId)` 与最小 AgentId 排序。第一版硬上限为 8 个实体，超限时整个分量原子回退到既有 Priority ORCA 速度，不截断分量。

[INFERRED][HIGH] 求解使用固定 32 轮的确定性加权投影：依次恢复 preferred/assigned velocity、按 Q8 spacing weight 压缩或恢复 soft spacing、投影 `HardPairDistance`、投影耦合 ORCA half-plane，最后限制每个实体 MaxSpeed。运动权重越高，实体承担的修正越少；external fixed velocity 的 mobility 为零。

[INFERRED][HIGH] 该数值路径没有宣称得到全局最优解。它的第一阶段用途是验证“多实体同时改变速度”能否在纯 fixture 中严格优于逐实体 Priority ORCA。输入位置先量化到 1cm；位置、速度、半径和恢复目标绝对值不得超过 `1e7`，Q8 权重不得超过 `1048576`，非有限或越界输入直接判为 invalid，避免投影乘除产生未受控溢出。

[INFERRED][HIGH] 固定轮数结束后必须完整验证连续解，再把速度量化到 1cm/s 并重新验证 MaxSpeed、HardPairDistance 与全部耦合约束。直接量化失败时，以连续解周围每轴 `-1/0/+1` quantum 生成候选，按距离、X、Y 稳定排序，并用稳定 AgentId 顺序做最多 `65536` 次候选尝试；找到完整分量可行解才原子采用。搜索无解或达到固定上限时整个分量回退，不部分应用。

[INFERRED][HIGH] 失败状态必须区分 `HardInfeasible`、`IterationLimit`、`NumericalFailure`、`QuantizedValidationFailure`、`OversizeFallback` 和 `InvalidInput`。同一 pair 在同一 fixed-step 只能由 JointSolver 或 SoftSeparation 之一处理；检测到双 owner 时输入无效，不允许叠加两次 soft spacing 力。

[COMPUTED][HIGH] 以上纯内核契约已通过 `CrowdDemo.SF4.Transit.AdaptiveSpacing` 与 `CrowdDemo.SF4.Transit.JointVelocity`，但没有 shadow 或生产接入证据，不能写成 SF4 穿行能力已通过。

## 12. 纯 fixture、Agent 6 归因与停止点

[COMPUTED][HIGH] 纯 fixture 已覆盖不同半径HardPairDistance、ContextScale 0/半/满、开放区软间距保持与恢复、窄口只压缩软间距、Assigned恢复、canonical Priority ORCA重构、后方高优先级追赶、进入/离开五实体环、固定外部速度、大半径穿小实体群、至少两个让行实体、双transit seed合并、9实体oversize回退、中心重合稳定normal、Agent/pair反序、量化3×3修复、量化无解和完整分量原子回退。

[COMPUTED][HIGH] 最小对照fixture中，逐实体Priority half-plane把高优先级实体正向速度压到0；Joint solver让穿行者和前方低优先级实体同时获得正向速度，量化后HardPairDistance violation=0。该结果只证明2–8实体纯fixture中的局部协同机制成立，不证明真实SF4或100/500规模成立。

[COMPUTED][HIGH] `-CrowdDemoTransitJointDiagnostic` 默认关闭，只在Round 1最终boundary捕获一次。它采集Agent 6的Preferred/Priority ORCA/MovementPredict/Obstacle/PBD/Reproject/Finalize链，沿当前ORCA pair图做完整闭包；server写量化JSON，client只比较紧凑hash。诊断参数在8328与最终8331运行前固定为Hard gap=0、Preferred gap=0、ContextScale=0，没有改变生产速度。

[COMPUTED][HIGH] 8328真实结果：Agent 6 preferred=`(79,796)`cm/s、Priority ORCA=`(0,78)`cm/s、MovementPredict speed=`78`cm/s、ObstacleConstraint speed=`0`cm/s、PBD/Reproject/Finalize speed均为`0`cm/s；首次归零阶段是`ObstacleConstraint`。完整约束闭包为`11 agents / 27 pairs`，Agent 6自身Priority constraints=4。

[COMPUTED][HIGH] fixture hash在server/client均为`3427263131`，match=1；JSON位于`Saved/CrowdDemo/CrowdDemo_8328_20260713_154506/crowd_transit_joint_fixture.json`。由于11>第一版上限8，文件只保存稳定紧凑summary，不截断或伪造2–8实体子fixture；该次诊断按设计输出`fixture_too_large`与VIOLATION。

[COMPUTED][HIGH] 修正oversize状态语义后的最终复测目录为`Saved/CrowdDemo/CrowdDemo_8331_20260713_155246`：fixture hash和速度链保持不变，`joint_status=OversizeFallback(5)`，客户端hash match=1，checkpoint/interval p95=`0.059/0.064cm`、agents=visible=20、双端penetration=0。该运行出现`revision_gap_total=1`且按设计包含oversize VIOLATION，因此只作为归因复核，不是完整安全验收通过证据。

[INFERRED][HIGH] 当前真实失败不能归因成“Priority ORCA完全不给速度”：ORCA和MovementPredict仍保留78cm/s，速度是在ObstacleConstraint阶段归零。即使未来扩大Joint component上限，直接替换ORCA也不能证明最终运动会改善；必须先独立诊断ObstacleConstraint为何把该方向归零。

## 13. Obstacle 109归因与Target-aware Holding handoff

[COMPUTED][HIGH] 8332只读诊断把Agent 6的当前fixed-step段定位到Obstacle 109：start/proposed=`(100.482,451.568)/(100.482,454.168)`cm，膨胀边界为`[-2448,452]–[1048,748]`cm，entry/exit t=`0.165992/1.0`，start/end inside=`0/1`，SlideX/SlideY clear=`1/0`，FlowBounds delta=0。完整`Current→Holding`段同样命中109，entry/exit t=`0.000866/0.594729`，两端均在障碍外但segment不clear。server/client fixture hash=`3799203711`一致。

[COMPUTED][HIGH] SF4 handoff现按`Flow Reachable && distance<=1200cm && FlowBounds不裁剪 && Current→Holding swept segment clear && Holding endpoint不在膨胀障碍内`判定。Holding状态下该条件后来失效时原子退回Pursuit并恢复消费Flow；ObstacleConstraint、PBD、ORCA和Holding直线Arrive本身未放宽。

[COMPUTED][HIGH] 8334复测中Agent 6已绕过Obstacle 109并进入Commit，当前位置/当前段/完整Holding段均clear，双端fixture hash=`4077451622`。安全门为target crossing=0、双端penetration=0、severe p95=0、corridor=20、deadlock=0、revision gap=0、checkpoint/interval p95=`0.064/0.064cm`、agents=visible=20、主hash match。

[COMPUTED][HIGH] 整体能力仍未通过并出现新的分布：physically satisfied=`16/20`，状态`Pursuit/Holding/Commit/Stable/Reserve/Reacquire=0/2/1/6/11/0`。旧诊断只列出5、8、15，遗漏了已离开目标容差的Reserve实体14；8336/8338的完整闭合fixture已将未满足集合修正为5、8、14、15。该结果不能声明Crowd Transit或SF4 Static能力通过。

[COMPUTED][HIGH] 按双重停止门，本任务没有进入shadow、JointApply、DebugGame、正式两轮或录像，也没有修改SF1/SF2/SF3、地图/Lighting、网络、PBD、Commit Gate、Position/Holding assignment、client visual或生产processor顺序。

## 14. Capacity-by-Construction与Joint Clearance Shadow（2026-07-13）

[INFERRED][HIGH] 单靠高密度布局和reactive Priority ORCA不能构造穿行容量：它只能在拥挤已经形成后分配局部责任，不能预先保证相邻实体之间存在普通实体可通过的连续开口。本阶段因此把软间距、穿行胶囊和局部分量联合求速拆成显式几何事实，但仍保留HardPairDistance、Obstacle、FlowBounds和Target exclusion为硬门。

[COMPUTED][HIGH] 通用量化关系为：`HardPairDistance=RadiusA+RadiusB+HardSafetyGap`；`RequiredTransitAperture=RadiusA+RadiusB+2*NominalTransitRadius+2*HardSafetyGap`；`BaselinePairDistance=max(HardPairDistance, RequiredTransitAperture-YieldBudgetA-YieldBudgetB)`；`PreferredSpacingGap=max(0, BaselinePairDistance-HardPairDistance)`。

[COMPUTED][HIGH] Static Small P0取Radius=`42cm`、HardSafetyGap=`10cm`、NominalTransitRadius=`42cm`、每侧YieldBudget=`30cm`，所以Hard/Required/Baseline/PreferredGap分别为`94/188/128/34cm`。该模型只声明两侧各让行不超过30cm时具有形成188cm入口的几何预算，不证明多层包围或多穿行者一定成功。

[COMPUTED][HIGH] 纯容量测试在当前Static几何中按128cm结构间距构造出Position capacity=`97`、Holding capacity=`61`，均不小于20；capacity hash=`4238641562`。这是稳定贪心构造出的下界，不是对全局最大容量的证明。Moving实体消费PreferredPairDistance；Position/Holding候选消费BaselinePairDistance，二者没有混成同一个离散容量指标。

[COMPUTED][HIGH] `FCrowdDemoJointVelocityKernel`现可从Commit等transit seed生成量化swept capsule，按共享邻居/pair图形成完整component，并对yielding实体施加二维侧移型soft clearance；Hard pair、canonical relative velocity、MaxSpeed、Obstacle/FlowBounds和Target exclusion仍需量化后复验。component内pair由Joint唯一拥有soft spacing，component外才允许其他soft spacing路径，double owner为非法输入。

[COMPUTED][HIGH] 纯自动化按2、5、8、12、20实体依次覆盖完整component、输入反序、无截断、hard safety与量化验证；`CrowdDemo.SF4.Transit` 2/2、`CrowdDemo.SF4.Positioning` 3/3、完整`CrowdDemo.SF` 35/35及Development Editor均通过。该纯测试仍不等于真实RoundSim能力。

[COMPUTED][HIGH] 默认关闭的`-CrowdDemoTransitCapacityShadow`在正式Priority ORCA之后构造Adaptive Spacing、transit intents/components并运行Joint solver，但不消费Joint速度、不写Mass fragment。rollback snapshot覆盖prepared agents/pairs/components/results、summary、hash与solver样本长度；Coordinator只复制紧凑指标。

[COMPUTED][HIGH] 8338真实Shadow单轮的server汇总为：components=`752`、max component=`20`、bucket 2/5/8/12/20=`0/0/121/30/601`、solved/infeasible=`49/703`、oversize/numerical/quantized=`0/0/0`、yielding=`1021`、hard/obstacle/flow/target/double-owner violation=`1317/1/0/33/0`、forward ratio Q15=`0`、spacing/aperture/clearance deficit max=`58.843/52.000/93.058cm`、max yield=`26.669cm`、solver p95=`1.029ms`、server hash=`1772333529`。

[COMPUTED][HIGH] 8338客户端在本次运行结束前未完成RoundResult checkpoint消费，`revision_gap_total=15`，因此server/client Shadow hash一致性没有建立。该传输失败与server侧Joint几何硬失败是两个独立失败项；任何一个都足以阻止生产接入。

[COMPUTED][HIGH] Shadow硬门明确失败：703个infeasible、1317个hard violation、1个obstacle violation、33个Target violation且forward ratio为0。虽然max yield未超过30cm、oversize/quantized/double owner为0，但不能抵消硬失败。因此未新增可用的`-CrowdDemoTransitCapacityApply`，正式pipeline和Movement行为保持不变。
## 2026-07-13 Elastic Crowd Shadow 科学复审与停止点

[COMPUTED][HIGH] 8351 混用了正式 Preferred 与可跨步延续的 Shadow 状态，并把 Baseline ORCA 中间速度与 Elastic Reproject 最终速度比较；它不满足同一初始快照、同一安全链和同阶段比较，保留为历史无效实验，不再用于评价 Elastic 能力。

[COMPUTED][HIGH] 新 `FCrowdDemoElasticShadowKernel` 对同一正式 fixed-step snapshot 构造临时 Baseline/Elastic Twin-Step；两支都执行 `Preferred → ORCA → Predict → Obstacle → PBD1 → PBD2 → PBD3 → Reproject`，唯一差异是 Elastic 在 ORCA 前调整非 Source Preferred。临时世界在当步结束后销毁，不携带位置或速度到下一正式步，也不写 Mass fragment。

[COMPUTED][HIGH] Hard PBD 的可选诊断从同一次 `Solve()` 记录三轮位置、pair correction、agent total correction与hash；关闭和开启诊断的最终结果、pair集与summary由自动化证明一致。Obstacle阶段同时记录既有constraint诊断与正式constraint结果，Target/HardPair分别统一使用`TargetRadius+TargetGap+AgentRadius`和`RadiusA+RadiusB+HardSafetyGap`。

[COMPUTED][HIGH] Parallel Rollout 从首个有效Source boundary冻结membership、角色、assignment、target revision、FlowField与环境；Baseline/Elastic各持独立世界180步，前90步Elastic启用Transit Influence，后90步仅保留adaptive spacing。两支每步从各自世界重建guidance并执行相同八阶段安全链，不读后续正式位置、速度或Preferred。

[COMPUTED][HIGH] Development Editor、Shadow定向自动化4/4与完整`CrowdDemo.SF` 40/40通过。PBD诊断等价、Twin snapshot隔离、Source Preferred不变、首失败稳定排序/闭包、180步切换、rollback和双端hash均有自动化覆盖。

[COMPUTED][HIGH] 8352原P0单轮的server/client八阶段hash、fixture元数据与rollout hash一致，`revision_gap=0`、agents=visible=`20`、双端obstacle penetration=`0`、correction p95约`0.06cm`；没有Demo运行期Fatal、Assertion、Ensure、`LogWindows: Error`或VIOLATION。

[COMPUTED][HIGH] Static Small初始阵型相邻间距约`90cm`，而本实验HardPairDistance=`42+42+10=94cm`，所以step 0存在两支共同继承的hard overlap。它们只计入`InheritedAtStepStart/SharedByBoth`，不允许覆盖后续首个`ElasticIntroduced/ElasticWorsened`因果fixture。

[COMPUTED][HIGH] 首个可归因失败固定在step=`29`、stage=`ORCA`、kind=`OrcaStopViolation`、attribution=`ElasticWorsened`。Agent 1的同一输入事实为start=`(88,-2507)cm`、base preferred=`(595,535)cm/s`；Baseline ORCA输出`(643,462)cm/s`且可行，Elastic Preferred仅变为`(593,517)cm/s`，Elastic ORCA却输出`(0,0)`且infeasible。完整20实体闭包fixture hash=`4215325188`，包含两支八阶段、14条Agent 1 ORCA约束、Obstacle/Reproject诊断和各3轮PBD诊断。

[COMPUTED][HIGH] Parallel recovery表面结果为eligible=`6`、Baseline/Elastic completed=`5/6`、permanent holes=`1/0`、end-error p95=`116.013/29.706cm`；但安全结果同时为HardPair=`2/4`、Target=`358/365`、ORCA stop violation=`147/109`。因此Elastic的恢复改善不能越过安全失败被写成能力通过。

[INFERRED][HIGH] 当前唯一可信停止点是Elastic Preferred的小幅变化触发了ORCA可行性/回退不连续，且独立rollout仍有HardPair、Target和ORCA stop安全失败。没有新的ORCA阶段fixture修复与同链复验前，禁止接Production Elastic，也不以调Elastic参数掩盖该首失败。

### Step 29 ORCA量化重放与局部修复

[COMPUTED][HIGH] hash=`4215325188`的step 29 fixture已补齐MaxSpeed、epsilon、quantum和Q15法线，重新捕获后hash不变。Baseline/Elastic的14条constraint逐项float完全一致，已知Baseline速度`(643,462)cm/s`位于800cm/s速度圆内，并满足Elastic全部约束；最小half-plane residual=`-0.095889cm/s`仍在`0.1cm/s`epsilon内。

[COMPUTED][HIGH] Elastic连续LP成功并得到约`(632,455)cm/s`，失败不在连续half-plane或速度圆，而在旧量化器只搜索连续解周围3×3个1cm/s格点。该局部邻域为空后，fallback只测试Preferred、50% Flow、35% Portal和Stop，未覆盖约束多边形中已知存在的远端量化可行区域。

[COMPUTED][HIGH] 修复仅在“连续LP可行且局部3×3为空”时启用确定性几何量化恢复：枚举half-plane投影、half-plane交点和line-circle交点周围3×3格点，按距连续解、距Preferred、X/Y字典序选择满足速度圆与全部约束的1cm/s候选。没有放宽epsilon、半径、HardPair或Target规则，也没有把Baseline速度写成特例。

[COMPUTED][HIGH] 固定fixture、constraint反序和Preferred `(592,516)/(593,517)/(594,518)`微扰回归均得到非零可行速度；ORCA自动化10/10和完整`CrowdDemo.SF` 41/41通过。真正没有1cm/s格点的sub-centimeter strip仍返回`NoSolution`。

[INFERRED][HIGH] 该结果只关闭step29的“连续可行、局部量化漏解、fallback stop”缺陷；尚未重新运行真实P0单轮，因此不能声称后续首失败、Parallel安全门或Production Elastic已经通过。

### 修复后P0 Static Small单轮

[COMPUTED][HIGH] 相同地图、P0、20实体、30Hz、30秒与readiness在8352重新运行。旧step29 ORCA stop首失败消失；新的最早因果失败后移到step=`40`、stage=`Reproject`、kind=`HardPair`、attribution=`ElasticWorsened`，pair为Agent 8–18，fixture hash=`410502020`。

[COMPUTED][HIGH] step40 Baseline/Elastic Reproject pair距离为`90.477/89.811cm`，相对94cm HardPairDistance的penetration为`3.523/4.189cm`，Elastic恶化约`0.666cm`。因此HardPair违规仍存在，当前首失败已从ORCA量化漏解转移到PBD后Obstacle Reproject的残余pair safety。

[COMPUTED][HIGH] Twin最终Baseline/Elastic Source forward Q15由旧`12515/12538`升到`18108/18131`；两支绝对值各增加5593，但Elastic相对Baseline仍只领先23，优势没有扩大。Twin最终HardPair累计由`224/221`降至`189/191`，两支都下降，但Elastic从少3变为多2。

[COMPUTED][HIGH] Parallel的Baseline/Elastic ORCA stop violation由`147/109`降为`126/93`；HardPair由`2/4`变为`3/3`，Elastic下降1但Baseline增加1；Target由`358/365`升为`541/431`，两支绝对值均恶化。Parallel Source forward由`12698/23099`变为`17857/13205`，Elastic从领先转为落后4652。

[COMPUTED][HIGH] 正式安全链保持agents=visible=`20`、corridor=`20`、deadlock=`0`、penetration=`0`、revision gap=`0`、checkpoint/interval p95=`0.057/0.063cm`、双端hash match且坏日志为0；但goal由旧`6`降为`5`。

[INFERRED][HIGH] 量化恢复显著减少了ORCA stop，但没有让整体Elastic安全或站位能力通过；下一归因只能针对hash 410502020的step40 Reproject HardPair残余，不能接Production或继续用ORCA stop下降替代HardPair/Target/goal失败。
## 15. Elastic Shadow的Obstacle/PBD联合安全边界（2026-07-13）

[COMPUTED][HIGH] 独立ObstacleReproject与equal-mass PBD不是闭合的联合约束求解：墙体可能撤销靠墙实体的PBD半修正，使pair penetration在Reproject重新出现。hash=`410502020`正是该类2实体局部witness。

[COMPUTED][HIGH] 当前只在Elastic Shadow实验链加入确定性safety polish。候选修正必须是swept obstacle/FlowBounds可实现且Target-clear，并且严格改善全局HardPair字典序；它不改变正式PBD三轮、正式ObstacleReproject或MovementFinalize。

[COMPUTED][HIGH] 20实体量化fixture证明单侧重分配可在不穿Obstacle 101、不制造第三方HardPair的情况下修复Agent 8–18；完整P0则把Twin Reproject累计HardPair降至Baseline/Elastic=`7/6`，尚未归零。

[INFERRED][HIGH] 该证据不足以把polish移入生产。生产候选至少还需：最早终态失败fixture、全轮终态HardPair=0、Parallel两支安全门、双端hash与rollback、以及不牺牲Source forward/Target的证据。
