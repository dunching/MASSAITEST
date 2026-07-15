# MassAI Crowd Demo Phase Plan

## 当前阶段：T5 Target Region Transport 收敛

[COMPUTED][HIGH] 本阶段已完成纯 kernel、生产 processor、rollback/hash、Static P0 门和旧 Density 正式 processor 删除。

### 已完成

- [x] [COMPUTED][HIGH] 修复 Target Influence 诊断的 Plan 激活锁定、采样和 rollback 基础设施。
- [x] [COMPUTED][HIGH] 实现稳定 Polar Topology：8/16/32/64 扇区、跨环 overlap edge、环境与 swept edge 过滤。
- [x] [COMPUTED][HIGH] 实现固定 16 Region 的 Capacity/Current/Desired/Deficit/Surplus。
- [x] [COMPUTED][HIGH] 实现整数 deterministic min-cost transport、旧 quota 复用和 AgentId 稳定 guidance。
- [x] [COMPUTED][HIGH] 用 Shared Flow V2 next-cell 链实现远距离 source attachment。
- [x] [COMPUTED][HIGH] 接入 Topology/Population/Transport/Guidance 四个双端 processors。
- [x] [COMPUTED][HIGH] 将 prepared SoA、PlanEpoch、quota、hash、重建计数和 solver 样本接入 correction rollback。
- [x] [COMPUTED][HIGH] Target Region Transport 自动化 4/4、Particle 23/23、完整 SF 43/43 和 Development 通过。
- [x] [COMPUTED][HIGH] 8414 Static P0 通过 20/20 有效带、16/16 可行 Region、unrouted=0 和全部安全/同步门。
- [x] [COMPUTED][HIGH] 旧 Polar Density guidance processor 已从生产代码删除，Transport 不含旧 Density fallback。

### 动态图合同收敛

- [x] [COMPUTED][HIGH] 8416固定step 331完整20实体fixture，唯一归因为terminal-anchor source被sink直接吸收。
- [x] [COMPUTED][HIGH] FeasibleGraphHash覆盖完整Cell/Edge执行结构和实际soft clearance成本。
- [x] [COMPUTED][HIGH] Plan validator与Guidance quota consumption合同已接入同一boundary重建。
- [x] [COMPUTED][HIGH] Round-sticky valid、Plan/Guidance拆分指标、Validation hash与rollback已接入。
- [x] [COMPUTED][HIGH] 8417 Static与8418 Moving Small均通过。
- [ ] [COMPUTED][HIGH] 按本任务边界未运行100/500、DebugGame正式门、录像或FFmpeg。

## 下一任务

[INFERRED][HIGH] 下一任务可独立设计100/500规模门、DebugGame与录像；不得把Small通过外推为规模和WORK调度已经成立。

[INFERRED][HIGH] TransportSpeed、PlanLifetime、Region数量、Particle参数以及旧Polar Density/Slot/owner均未因本次修复改变。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
