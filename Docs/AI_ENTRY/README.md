# AI_ENTRY 使用说明

`AI_ENTRY` 只用于**快速恢复上下文**，不再保存完整架构时间线或逐切片执行日志。

## 当前入口

```text
README.md
   ↓
02_状态恢复.md
   ↓
../CurrentArchitecture.md
../TargetArchitecture.md
../PhasePlan.md
../FeatureChecklist.md
../TestScenarioMatrix.md
```

## 文件职责

| 文件 | 职责 |
|---|---|
| `README.md` | 告诉新的 AI / 开发者应该从哪里开始。 |
| `02_状态恢复.md` | 当前项目的短恢复快照：项目定义、当前架构、当前 OPEN Gate、关键测试事实。 |
| `03_WA8_RuntimeOwnerCommitBarrier迁移提示词.md` | 已完成任务的历史提示词入口，不再执行。 |

## 规则

1. `AI_ENTRY` 不拥有架构事实优先级。
2. 当前代码事实以 `../CurrentArchitecture.md` 为准。
3. 最终方向以 `../TargetArchitecture.md` 为准。
4. 当前实施顺序以 `../PhasePlan.md` 为准。
5. 完成状态与测试证据分别以 `../FeatureChecklist.md`、`../TestScenarioMatrix.md` 为准。
6. 恢复文件与核心文档冲突时，核心文档优先。
7. 已完成提示词不得继续作为当前开发任务直接执行。
8. 历史大段恢复日志通过 Git 历史追溯，不再向 `AI_ENTRY` 继续追加。

这样可以避免一次新的 AI 会话先阅读数十 KB 过期迁移记录，再错误恢复已经被替代的架构。
