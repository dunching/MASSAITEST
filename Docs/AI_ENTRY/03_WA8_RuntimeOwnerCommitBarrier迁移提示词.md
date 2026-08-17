# WA8 下一步执行提示词：Runtime Owner Commit Barrier 所有权纠偏

## 目标

[INFERRED][HIGH] 将通用 Worker Result Owner Commit Barrier 从 `MassAICrowdDemo` 下沉到 `MassCrowdRuntime`，让 Demo 只保留 Host-specific Prepared Round Commit Plan 与 FinalValidate/NoFailApply adapter；同一切片删除旧 Demo Barrier 类型和文件，不保留兼容包装、别名、fallback 或双路径。

## 可直接使用的提示词

```text
请继续实施当前项目 WA8，但本轮只处理：

“Worker Result Owner Commit Barrier 从 Demo 模块下沉到 MassCrowdRuntime，并删除旧 Demo Barrier”

设计原则：

- Demo 是目标架构的快速验证宿主，不承担 Legacy 框架或 API 的向后兼容责任。
- 保留业务能力和测试证据，不保留旧类型、旧入口、wrapper、typedef、fallback 或双生产路径。
- MassCrowdRuntime 拥有通用 Worker Result Prepare/Token/Final Validate/唯一 Owner/no-fail Commit 协议。
- MassAICrowdDemo 只拥有 Host-specific Prepared Mass/Target/Resource/Behavior/Event Plan 与 adapter。
- Runtime 禁止引用 CrowdDemo、Round、Scenario、Demo Mass Plan、Demo Target/Resource Plan 或 Demo UObject。

开始前必须：

1. 完整阅读：
   - Docs/AI_ENTRY/02_状态恢复.md
   - Docs/CurrentArchitecture.md
   - Docs/MassCrowdSimulationPluginArchitecture.md
   - Docs/MassCrowdUnifiedRuntimeAndReplicationContract.md
   - Docs/FullWorkerAuthorityArchitecture.md
   - Docs/PhasePlan.md
   - Docs/TestScenarioMatrix.md
   - Docs/FeatureChecklist.md
   - 工作区 AGENTS.md；若不存在，明确记录
2. 用源码复核文档，检查并记录：
   - git status --short、git diff --stat、git diff --check
   - 当前分支、HEAD、未提交路径数量
   - 残留 UnrealEditor 进程
   - FCrowdDemoWorkerResultOwnerBarrier、Commit Token、Pending Finalize、FCrowdWorkerResultApplyProxy 的全部定义和调用链
   - MassAICrowdDemo 与 MassCrowdRuntime 的 Build.cs 依赖方向
   - 正常生产路径与测试路径的所有旧 Barrier 符号消费者

实现要求：

1. 在 MassCrowdRuntime 建立无 Demo 语义的：
   - Worker Result Commit Token
   - Commit result/rejection enum
   - Owner Commit Barrier
   - 必要的单槽候选约束或最小公共接口
2. 保持现有原子合同：
   - 所有 Token、Generation、Publish/Input/Event 水位、Stable View 和 Host Final Validate 在首次写入前完成
   - 首次写入后只允许预验证完成的 no-fail 操作
   - Proxy commit 与 Host apply 恰好一次
   - 表现、网络、Ordered Event 在状态提交后发布
   - Dirty Batch ACK 最后发生
   - 任意 Final Validate 失败时零部分提交
3. Demo 侧建立或保留一个 Host-specific Prepared Round Commit Plan，容纳 Demo Mass、Target/Resource、Behavior/Event Plan；通过回调或明确 adapter 接入 Runtime Barrier。
4. 同一切片物理删除：
   - CrowdDemoWorkerResultOwnerBarrier.h/.cpp
   - FCrowdDemoWorkerResultCommitToken
   - ECrowdDemoWorkerResultOwnerBarrierResult
   - FCrowdDemoWorkerResultOwnerBarrier
   - 绑定旧符号/文件名的测试和 include
5. 禁止创建兼容 typedef、转发 wrapper、旧文件 shim、双 Barrier 或 fallback。

本轮范围限制：

- 不改变 Target/Resource、Mass、Behavior、Ordered Event 的业务语义和实际提交顺序，除非通用接口拆分所必需。
- 不在本轮重写 rollback/correction；完整 rollback 数组留到下一切片，但不得为它增加兼容层。
- 不删除 TryPrepareRoundApply 或 Demo-local Round Transaction；它们在 Barrier 纠偏后的后续切片直接删除。
- 不修改 Particle、Work Ring、Time Wheel、Spatial Index、网络合同或固定 1/30 秒步长。
- 不运行完整 WA9 或完整场景矩阵。
- 使用 apply_patch，保留工作区累计未提交修改；禁止 reset、checkout、clean、stash、commit、push 或切换分支。

必须新增或更新测试：

1. Runtime 原子故障注入：Prepare 后 Generation、Publish/Event 水位、Stable View 或 Host Token 过期，Proxy、Host Mass callback、side effect、Dirty Batch 和 ACK 全部不变。
2. Runtime 成功路径：Token 构建一次、Proxy Final Validate/Commit 各一次、Host FinalValidate/Apply 各一次、Ordered Event/发布/ACK 不丢失不重复。
3. 插件边界门：MassCrowdRuntime 不含 CrowdDemo、Round、Scenario、Demo Prepared Plan 或 Demo UObject 引用。
4. Legacy 零门：旧 Demo Barrier 文件、类型、include、注册和测试消费者为零；明确称为源码符号/结构门，除非实际使用完整 C++ AST。
5. 复跑现有 Target/Resource stale revision、invalid Owner、非法引用和重复项故障门。
6. 最小全 Production T8：Golden Hash、Ordered Event、Combat/Projectile 计数和 Dirty ACK 不变。

验证顺序：

1. git diff --check。
2. Development Editor DisableUnity 构建。
3. MassCrowd Runtime WorkerResultApply/OwnerBarrier 定向自动化。
4. CrowdDemo Architecture/ResultApply/TargetResource 故障注入。
5. 插件边界与 Legacy 零符号门。
6. 最小全 Production T8 Golden。
7. 定向门全部通过后才考虑更大场景；本轮不要运行完整矩阵或 WA9。

完成后更新：

- Docs/AI_ENTRY/02_状态恢复.md
- Docs/CurrentArchitecture.md
- Docs/MassCrowdSimulationPluginArchitecture.md
- Docs/MassCrowdUnifiedRuntimeAndReplicationContract.md
- Docs/FullWorkerAuthorityArchitecture.md
- Docs/PhasePlan.md
- Docs/TestScenarioMatrix.md
- Docs/FeatureChecklist.md

最终汇报必须包含：

- 代码核验发现的文档漂移
- 原 Barrier 模块/调用链与迁移后的模块/调用链
- Runtime 公共类型和 Demo Host-specific 类型的边界
- 删除的旧文件、类型、include、测试绑定和兼容入口
- 第一次写入前的最终验证项与实际提交顺序
- 每项测试真实通过数、耗时和场景指标；未运行明确写“未运行”
- 正式 runner 若为 0/1，不得用内部日志改报通过
- 当前分支、HEAD、git status、未提交路径数量
- 确认没有 reset、checkout、clean、stash、commit、push 或切换分支
- 下一切片直接处理完整 rollback 数组并删除旧数据源；之后删除 TryPrepareRoundApply 与 Demo-local Round Transaction
- 最后列出 [RULES I BROKE]

证据规则：

- 每项事实和结论使用 [KNOWN]/[COMPUTED]/[INFERRED]/[COMMON]/[FRAME]/[GUESS] 与置信度标签。
- 不知道时第一行写“我不知道。”
- 不得编造测试结果、调用链、文件行号或通过数量。
```

## 当前执行状态

[COMPUTED][HIGH] 该提示词尚未执行；当前代码仍由 Demo 模块定义 Owner Barrier，完整 rollback 数组、`TryPrepareRoundApply` 和 Demo-local Round Transaction 仍存在。
