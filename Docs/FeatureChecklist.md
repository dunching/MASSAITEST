# MassAI Crowd Demo 功能检查表

## 核心模拟与架构

- [x] [COMPUTED][HIGH] 顶层parser只接受0/1及`SimRoundObstacle/SimRoundSoftPressure`。
- [x] [COMPUTED][HIGH] TargetApproach、TargetSlotLayout和旧Polar Density生产引用已删除。
- [x] [COMPUTED][HIGH] Flow、Target Region和Business输出独立candidate；唯一Guidance Compose写自主速度。
- [x] [COMPUTED][HIGH] Local Predictive与Particle不反向改写自主向量或Facing。
- [x] [COMPUTED][HIGH] Rollback使用不可变资源引用与可变执行态，correction仍只在fixed boundary应用。
- [x] [COMPUTED][HIGH] Compose、Local Predictive和Particle已接入不可变POD WORK。
- [ ] [COMPUTED][HIGH] 整个boundary单次Mass读取、完整GT原子提交尚未完成。
- [ ] [COMPUTED][HIGH] Mass archetype尚未按Base/Target/Combat/Projectile能力拆分。

## 自动化与构建

- [x] [COMPUTED][HIGH] Development Editor通过。
- [x] [COMPUTED][HIGH] DebugGame Editor通过。
- [x] [COMPUTED][HIGH] 当前95/95项`CrowdDemo`自动化通过。
- [x] [COMPUTED][HIGH] RoundResultHeader版本化NetSerialize与2048字节门通过。

## 20实体能力与性能

- [x] [COMPUTED][HIGH] T1六阶段/传播/settling通过；普通视觉不连续=0，测试reset单列。
- [x] [COMPUTED][HIGH] T2 handoff/band/settled=`20/20/20`，coverage=`16/16`。
- [x] [COMPUTED][HIGH] T3双cohort=`10/10`、deadlock=0。
- [x] [COMPUTED][HIGH] T4 wall/corridor/completed/settled=`20/20/20/20`。
- [x] [COMPUTED][HIGH] T5S inside=`20/20`、coverage=`16/16`；当前版性能门通过。
- [x] [COMPUTED][HIGH] T6A corridor/completed/inside/coverage=`20/20/20/20`；T6S七类profile技术门通过。
- [x] [COMPUTED][HIGH] T7热复跑和T8性能/技术门通过。
- [ ] [COMPUTED][HIGH] T7冷启动曾出现112.235ms client frame p95，稳定性能门未关闭。
- [ ] [COMPUTED][HIGH] T5M与T6M尚未用收敛后代码复跑。
- [ ] [COMPUTED][HIGH] 单进程DebugGame PIE和当前版人工审片未完成。
- [ ] [COMPUTED][HIGH] 100/500当前组合未验收。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
