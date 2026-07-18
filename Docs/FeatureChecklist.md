# MassAI Crowd Demo Feature Checklist

## 当前生产架构

- [x] [COMPUTED][HIGH] 正式场景入口只接受`SimRoundObstacle=0`与`SimRoundSoftPressure=1`。
- [x] [COMPUTED][HIGH] Shared Flow V2、动态Goal anchor与Integration重建。
- [x] [COMPUTED][HIGH] 可选Target Region Transport与Distance Band终态策略。
- [x] [COMPUTED][HIGH] 通用Local Predictive Interaction。
- [x] [COMPUTED][HIGH] Particle Soft/Hard/Swept/Obstacle/Bounds安全闭环。
- [x] [COMPUTED][HIGH] Server/Client同kernel、fixed-step correction、checkpoint chunks和SoftPressure rollback。
- [x] [COMPUTED][HIGH] client-only visual、VAT状态与20/20完整显示。
- [ ] [COMPUTED][HIGH] 真实GT/WORK批处理边界尚未实施。
- [x] [COMPUTED][HIGH] RoundResultHeader版本化NetSerialize、SoftPressure/Combat payload kind与2048字节门已实施并通过真实网络验证。
- [x] [COMPUTED][HIGH] Target Region不可变短期Plan与可回滚Quota执行态已分离；跨cell一次消费、多edge继续、execution hash和snapshot已接入。

## 旧架构清理

- [x] [COMPUTED][HIGH] 旧2/3 parser、console、地图配置和MassSubsystem入口关闭。
- [x] [COMPUTED][HIGH] 旧processor不再由fixed-step driver构造或执行。
- [x] [COMPUTED][HIGH] 旧SF2/SF3/SF4地图与专用生成脚本删除。
- [x] [COMPUTED][HIGH] 37项SF3/SF4历史自动化和1项旧PBD自动化退出当前测试集。
- [x] [COMPUTED][HIGH] 旧processor类、fragment、Pipeline state、metrics和kernel已物理删除。
- [x] [COMPUTED][HIGH] Mass archetype已移除旧Separation/PBD/Portal/Band/Position/Pursuit/ORCA fragments。

## 当前自动化与构建

- [x] [COMPUTED][HIGH] Development Editor通过。
- [x] [COMPUTED][HIGH] `CrowdDemo.SF`当前34/34通过；生命周期诊断6/6、Target Region Transport 6/6，Round时间合同、反射目标运动、两个只读反事实及路线进度诊断已纳入当前测试集；旧ORCA兼容性测试随旧求解器删除。
- [x] [COMPUTED][HIGH] 自动化日志无Fatal、Assertion、Ensure、`LogWindows: Error`或VIOLATION。
- [x] [COMPUTED][HIGH] 清理后的DebugGame Editor通过。

## 性能与视觉连续性

- [x] [COMPUTED][HIGH] fixed-step整步及8组阶段p95/max、每Game Frame步数、catch-up、积压、realtime factor和rollback replay CPU计时已接入。
- [x] [COMPUTED][HIGH] client frame、visual processor、submit interval、Sim/Display delta、collapsed steps、ISM rebuild、普通跳变和Round reset跳变已分开记录。
- [x] [COMPUTED][HIGH] T2静态Topology为Round级`1 build/900 cache hits`；Shared Flow V2 Cell→Node缓存、短Plan source attachment复用和确定性queue shortest-path已消除DebugGame持续全图扫描。
- [x] [COMPUTED][HIGH] T2 correction零误差快速路径54次，snapshot miss/mismatch=0；测试门不再强制制造无意义replay。
- [x] [COMPUTED][HIGH] T2 8701 DebugGame固定步p95=`9.841ms`、client frame p95=`6.748ms`、visual processor p95=`0.126ms`、realtime=`1.000`、collapsed p95=`1`、非correction跳变0。
- [x] [COMPUTED][HIGH] 距离感知视觉插值已修复8700的`sim_delta=0`、单帧追赶`52.147cm`异常；8701普通移动连续性门通过。
- [ ] [COMPUTED][HIGH] T2仍有client frame max=`237.320ms`和Round reset 20个实体的约`699.516cm`跳变样本；当前不能写成“加载、Round切换和普通移动全部完全平滑”。
- [x] [COMPUTED][HIGH] T1–T8当前20实体独立Server/Client进程CPU性能基线完成；全部fixed-step/client/visual p95低于`33.333/33.333/16.667ms`门，simulation realtime factor达到1且fixed-step上限命中0。
- [x] [COMPUTED][HIGH] Shared Flow V2构图的逐厘米障碍复查与blocked-cell全edge扫描已移除；T6 DebugGame首步Flow max从`1113.375ms`降至`57.473ms`且hash不变。
- [x] [COMPUTED][HIGH] Particle最近安全量化与零残差closure快路径已接入；T6 DebugGame Particle/整步p95降至`6.239/13.187ms`，Development为`1.812/4.132ms`。
- [ ] [COMPUTED][HIGH] 单进程DebugGame PIE仍缺少修复后实测；旧版多场景低于30FPS，不能由独立进程门替代。
- [x] [COMPUTED][HIGH] 异构静态Target现为Topology `7 build/6300 hit`、Demand `7 full/6300 population update`；五类Transport hash与能力结果保持一致。
- [ ] [COMPUTED][HIGH] 视觉连续性未全部通过：T1 8682为113次steady跳变，T6A 8687为17次；T6S 8688/8689分别为19/0，必须以最早稳定fixture归因，不能以一次通过覆盖一次失败。
- [ ] [COMPUTED][HIGH] 普通客户端启动frame max仍约`215ms`，Round reset仍有大跳变；steady p95通过不等于编辑器启动和Round切换全程流畅。

## 20实体场景

- [x] [COMPUTED][HIGH] T1端口8645完成6阶段，candidate/applied hash双端一致，网络结果头739字节。
- [x] [COMPUTED][HIGH] T2端口8636达到handoff/band/settled=`20/20`、coverage=`16/16`，旧Flow owner口径不再误判Transport阶段。
- [x] [COMPUTED][HIGH] T3当前技术与能力自动化通过；当前二进制人工审片待补。
- [x] [COMPUTED][HIGH] T4当前技术与能力自动化通过；当前二进制人工审片待补。
- [ ] [COMPUTED][HIGH] T5 Static端口8653达到inside=`20/20`、coverage=`16/16`且技术门通过，但`settling_steps=-1`，能力门未完整关闭。
- [ ] [COMPUTED][HIGH] T5 Moving端口8646达到inside=`20/20`、coverage=`12/12`，但仍有2个在途Supply且连续稳定窗口为0。
- [x] [COMPUTED][HIGH] T6 Transit端口8627完成`20/20`且安全违规=0。
- [x] [COMPUTED][HIGH] T6 Static端口8639 aggregate inside/coverage=`20/20`，7个CapabilityProfile均达到各自人口覆盖。
- [x] [COMPUTED][HIGH] T6 Moving端口8670采用名义30秒+15秒宽限，连续两轮inside/aggregate coverage=`20/20`；7个Capability Profile逐项满足人口与可行Region门。
- [x] [COMPUTED][HIGH] 生命周期生产合同已接入：cost-only不失效短Plan、合法claim优先迁移、剩余quota重算和原子替换均有纯测试覆盖。
- [x] [COMPUTED][HIGH] 8658双端生命周期hash=`2238624409`，重建883次且原因计数闭合；claim为active/geometry/supply/new-plan/migrated/completed/dropped=`520/480/346/346/346/115/0`。
- [x] [COMPUTED][HIGH] correction rollback=`53/0/0/64`且验证通过，checkpoint/interval position error p95=`0cm`，Particle Hard/Swept/Obstacle/Bounds与invalid/fallback均为0。
- [x] [COMPUTED][HIGH] 生命周期修复已消除仍有效claim丢失，但不是T6M 20/20的充分条件；8659已完成401次Supply无出口与最终Region 13的直接fixture关联。
- [x] [COMPUTED][HIGH] 8659最终Region fixture已直接命中Profile `2217961739`/Region 13：step 883、selection kind 1、hash `4114446767`；旧/新Plan均能路由该Region且有效claim未丢失。
- [x] [COMPUTED][HIGH] 8659最后90步Region 13的Demand/Plan/Guidance/retention gap=`0/4/81/51`，进入/离开=`40/38`；该直接证据已进入8661反事实归因。
- [x] [COMPUTED][HIGH] 8661只读反事实双端一致：缺失73步均有合法在途实体，attachment recovered=0、terminal restored=0、population violation=0，outcome=`Neither`。
- [x] [COMPUTED][HIGH] 最终Agent 5以目标相对速度`301.496cm/s`正常前进，剩2条edge；最后窗口剩余edge从最多9条总体下降到2条，不是1cm/s级刹停。
- [x] [COMPUTED][HIGH] T6M时间合同已显式区分名义30秒与15秒宽限；旧30秒结果继续记录为19/20，45秒连续两轮为20/20，生产Transport/Particle参数未改变。
- [x] [COMPUTED][HIGH] 8671两轮input/initial/Mass AgentState/dynamic-flow round/Particle candidate/Transport/生命周期hash跨轮一致且逐轮双端一致；AgentState hash=`1956985324`，checkpoint与interval error p95=`0cm`，rollback miss/mismatch=0。
- [ ] [COMPUTED][HIGH] T6M当前版人工审片未完成；技术与能力通过不能替代视觉稳定性检查。
- [x] [COMPUTED][HIGH] T7历史技术与人工视觉门通过；清理后回归待补。
- [x] [COMPUTED][HIGH] T8端口8632完成50次spawn/impact/damage、duplicate=0且安全违规=0；不代表规模化Projectile架构。
- [ ] [COMPUTED][HIGH] T9未实施。
- [ ] [COMPUTED][HIGH] T10未实施。

## 规模与长期能力

- [ ] [COMPUTED][HIGH] 当前SoftPressure组合尚未正式验收100/500。
- [ ] [COMPUTED][HIGH] 自由游荡、动态spawn/despawn和持续membership变化尚未实现。
- [ ] [COMPUTED][HIGH] Mass-native Projectile权威状态、空间宽相和移动目标relative sweep尚未实现。
- [ ] [COMPUTED][HIGH] NavMesh分层Surface Graph与最小宿主插件迁移尚未实现。

## T6 出口与客户端验收可视化

- [x] [COMPUTED][HIGH] 真实TargetSlotLayout可在client绘制逐Slot最终位置；未启用Slot时不会制造虚假的Agent→Position标记。
- [x] [COMPUTED][HIGH] Region Transport client标记只表达有效距离带、Region边界与可行terminal cell。
- [x] [COMPUTED][HIGH] T6 Transit 8707完成/稳定=`20/20`，group completion/settled step=`298/329`，deadlock与安全违规为0。
- [x] [COMPUTED][HIGH] T6 Static 8708 Target Stability窗口双端一致，inside/coverage=`20/20`，terminal chatter与位置peak-to-peak均为0。
- [ ] [INFERRED][HIGH] client标记的颜色、可见性及与最终实体位置的对应关系尚待可见PIE人工验收。

[RULES I BROKE]：[COMPUTED][HIGH] 无。

## T6A Target Handoff 与 Facing

- [x] [COMPUTED][HIGH] T6A走廊20/20完成后才启用Target Region stages，不再把出口直线保持误报为最终站位。
- [x] [COMPUTED][HIGH] Ranged AcquireThenHold只允许非Supply Terminal取得；目标靠近不主动后退，Region超额或超过Maximum+100cm释放。
- [x] [COMPUTED][HIGH] Local Predictive、Particle和Obstacle修正不参与自主朝向；最终稳定前朝Preferred，最终稳定后朝目标。
- [x] [COMPUTED][HIGH] 朝向每fixed-step最大变化12度，即360度/秒；输入乱序自动化hash一致。
- [x] [COMPUTED][HIGH] 8719 Server/Client corridor/inside/coverage=`20/20/20`、安全违规0、五类Transport hash匹配、rollback=`80/0/0`、agents=visible=`20/20`。
- [ ] [INFERRED][HIGH] 可见PIE人工审片尚未完成，不能把离屏技术结果写成视觉观感已通过。
