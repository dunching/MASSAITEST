# MassAI Crowd Demo Feature Checklist

## 正式基础

- [x] [COMPUTED][HIGH] SF1 Shared Flow V2 与 SoftPressure Particle 正式链存在。
- [x] [COMPUTED][HIGH] fixed-step correction、SoftPressure rollback、双端 hash 和 client-only visual 已接入。
- [x] [COMPUTED][HIGH] Target Fact 支持静态与量化线性移动目标。

## Target Region Transport

- [x] [COMPUTED][HIGH] 8/16/32/64 Polar Navigation Cell 拓扑与稳定 CellKey。
- [x] [COMPUTED][HIGH] CW/CCW、跨环 overlap edge、Bounds/Obstacle/Target/swept edge 过滤。
- [x] [COMPUTED][HIGH] Shared Flow V2 source attachment。
- [x] [COMPUTED][HIGH] 固定 16 Demand Region、容量限制与人口守恒。
- [x] [COMPUTED][HIGH] deterministic integer min-cost transport 与旧 quota 复用。
- [x] [COMPUTED][HIGH] AgentId 稳定 quota 消费，无永久 Slot/Region owner。
- [x] [COMPUTED][HIGH] 15-step Plan、确定性重建原因、四类 round hash。
- [x] [COMPUTED][HIGH] prepared SoA 与 correction rollback/replay。
- [x] [COMPUTED][HIGH] 旧 Polar Density 正式 processor 已删除，无生产 fallback。

## 自动化与构建

- [x] [COMPUTED][HIGH] Target Region Transport 4/4。
- [x] [COMPUTED][HIGH] Particle 23/23。
- [x] [COMPUTED][HIGH] 完整 `CrowdDemo.SF` 43/43。
- [x] [COMPUTED][HIGH] Development Editor。

## 运行验收

- [x] [COMPUTED][HIGH] 8414 Static P0：inside band=`20/20`、feasible Region coverage=`16/16`、max Region population=`2`、unrouted=0。
- [x] [COMPUTED][HIGH] 8414 Static P0：Particle 四类安全违规=0、invalid/fallback=0、Transport 四 hash 双端一致、rollback miss/mismatch=0、agents/visible=`20/20`。
- [x] [COMPUTED][HIGH] 8416诊断：step 331双端fixture hash=`10240167`，确认terminal-anchor sink吸收导致Cell 176缺失outgoing quota。
- [x] [COMPUTED][HIGH] FeasibleGraphHash、actual soft clearance、Plan validator、Guidance consumption与round-sticky指标已接入。
- [x] [COMPUTED][HIGH] 8417 Static P0：20/20、16/16、Plan/Guidance unrouted=0、invalid/validation failure=0。
- [x] [COMPUTED][HIGH] 8418 Moving P0：20/20、12/12当前可行Region、Plan/Guidance unrouted=0、invalid/validation failure=0、双端hash与rollback通过。
- [ ] [COMPUTED][HIGH] 100/500 未运行。
- [ ] [COMPUTED][HIGH] DebugGame 正式门未运行。
- [ ] [COMPUTED][HIGH] FFmpeg 与人工审片未运行。

## 禁止误报

[COMPUTED][HIGH] Static与Moving Small通过不能外推为100或500已通过。

[RULES I BROKE]：[COMPUTED][HIGH] 无。
