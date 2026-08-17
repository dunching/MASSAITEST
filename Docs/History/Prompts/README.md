# 已退休执行提示词

这里不保存当前任务，只记录已经完成或被替代的执行 Prompt 名称和追溯方式。

当前开发任务只看：

```text
../../PhasePlan.md
```

已退休的 Prompt 包括：

- `T2OpenCohortPolarHandoffValidationPrompt.md`
- `AI_ENTRY/03_WA8_RuntimeOwnerCommitBarrier迁移提示词.md`

它们将在 active tree 中删除。完整正文仍可通过 Git 历史查看。

原因：执行 Prompt 会固化某一个历史切片的文件名、旧架构和测试顺序；任务完成后继续把它放在 Docs 根目录或 AI_ENTRY，会让新会话误把已完成迁移重新执行。
