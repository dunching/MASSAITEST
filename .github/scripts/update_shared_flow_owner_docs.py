from pathlib import Path


def replace_exact(path: str, old: str, new: str, expected: int, label: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8-sig")
    count = text.count(old)
    if count != expected:
        raise RuntimeError(f"{label}: expected {expected} matches, found {count}")
    p.write_text(text.replace(old, new), encoding="utf-8")


replace_exact(
    "Docs/CurrentArchitecture.md",
    "### 17.1 WorkerInputSync 仍读取 RoundSimPipeline Shared Flow\n\n"
    "当前 `CrowdDemoWorkerInputSync.cpp` 构建 versioned resources 时仍访问：\n\n"
    "```text\n"
    "UCrowdDemoRoundSimPipelineSubsystem\n"
    "→ GetRuntimeSharedFlowField()\n"
    "```\n\n"
    "所以 `RoundSimPipelineSubsystem` 还不能被当成纯 Test Harness 删除。\n\n"
    "这属于：\n\n"
    "```text\n"
    "Authority 已迁移\n"
    "但部分 Input Resource Source 尚未迁移\n"
    "```\n\n"
    "是 WA8 需要优先断开的依赖。",
    "### 17.1 Shared Flow Primary Resource Owner 已迁出 RoundSimPipeline\n\n"
    "当前 Primary Shared Flow 的 GT-side runtime resource 已由 `UMassCrowdRuntimeSubsystem` 唯一持有：\n\n"
    "```text\n"
    "SharedFlow Build Stage / Host facts\n"
    "        ↓\n"
    "UMassCrowdRuntimeSubsystem\n"
    "        ↓ owns\n"
    "FCrowdMassSharedFlowResource\n"
    "        ↓\n"
    "WorkerInputSync / legacy Pipeline consumers\n"
    "```\n\n"
    "`CrowdDemoWorkerInputSync.cpp` 已不再 include、查询或读取 `UCrowdDemoRoundSimPipelineSubsystem`，Environment versioned resource 直接来自 RuntimeSubsystem。\n\n"
    "Target Prepared Resource 的 Shared Flow identity 也已从对象成员地址改为稳定 `CrowdWorkerResourceIds::Environment`。\n\n"
    "但这**不表示 RoundSimPipeline 已可删除**：Result Apply 后的 legacy Round frame、rollback/transaction、Target prepared state、metrics/diagnostics 等仍有真实消费者，是后续 WA8 工作。",
    1,
    "update current architecture shared flow ownership",
)

replace_exact(
    "Docs/FeatureChecklist.md",
    "| WorkerInput 与 RoundSimPipeline 完全解耦 | OPEN | WorkerInputSync 仍读取 `GetRuntimeSharedFlowField()`。 |",
    "| WorkerInput 与 RoundSimPipeline 输入资源解耦 | DONE | WorkerInputSync 已不再读取/查询 RoundSimPipeline；Primary Shared Flow resource 由 `UMassCrowdRuntimeSubsystem` 持有并直接发布为 Environment resource。 |",
    1,
    "update feature checklist worker input coupling",
)
replace_exact(
    "Docs/FeatureChecklist.md",
    "1. **WA8 Legacy Removal**：断开 RoundSimPipeline resource/transaction/rollback 依赖，删除失去消费者的 Stage 与重复实现。",
    "1. **WA8 Legacy Removal**：Primary Shared Flow resource owner 已迁出 RoundSimPipeline；继续清除 transaction/rollback、其他旧数据源、失去消费者的 Stage 与重复实现。",
    1,
    "update feature checklist WA8 gate",
)

replace_exact(
    "Docs/LegacyCodeInventory.md",
    "当前角色混杂：\n\n"
    "```text\n"
    "旧 Round 事务状态\n"
    "Prepared data\n"
    "rollback / transaction\n"
    "metrics / diagnostics\n"
    "Target / Flow / Particle 历史数据\n"
    "部分 Worker Input 仍使用的数据源\n"
    "```\n\n"
    "最重要的现存耦合：\n\n"
    "```text\n"
    "CrowdDemoWorkerInputSync.cpp\n"
    "  → BuildVersionedResources()\n"
    "  → World.GetSubsystem<UCrowdDemoRoundSimPipelineSubsystem>()\n"
    "  → GetRuntimeSharedFlowField()\n"
    "```\n\n"
    "结论：**不能直接删除。**\n\n"
    "迁移目标：Shared Flow / environment versioned resource 必须来自独立、明确的 Worker/Host Resource Provider，而不是 RoundSimPipeline。\n\n"
    "删除门：\n\n"
    "- Worker Input 不再读取 Pipeline；\n"
    "- Result Apply 不再依赖 Pipeline transaction/rollback source；\n"
    "- 必需 diagnostics 有新 owner；\n"
    "- tests 不再用 Pipeline 伪装生产状态。",
    "当前角色仍然混杂：\n\n"
    "```text\n"
    "旧 Round 事务状态\n"
    "Prepared data\n"
    "rollback / transaction\n"
    "metrics / diagnostics\n"
    "Target / Flow / Particle 历史/验收数据\n"
    "```\n\n"
    "已完成的 P0 子切片：\n\n"
    "```text\n"
    "Primary FCrowdMassSharedFlowResource owner\n"
    "RoundSimPipeline → UMassCrowdRuntimeSubsystem\n\n"
    "CrowdDemoWorkerInputSync\n"
    "不再 include/query RoundSimPipeline\n"
    "直接读取 RuntimeSubsystem SharedFlowResource\n\n"
    "Target Prepared ResourceId\n"
    "pointer address → CrowdWorkerResourceIds::Environment\n"
    "```\n\n"
    "结论：**Pipeline 仍不能直接删除。** Shared Flow 的 Worker Input ownership 耦合已关闭，但 Result Apply 后的 Round frame、rollback/transaction、Target prepared state 与 diagnostics 仍是真实运行时消费者。\n\n"
    "剩余删除门：\n\n"
    "- Result Apply 不再依赖 Pipeline transaction/rollback source；\n"
    "- 其他仍由 Pipeline 持有的生产输入/Prepared state 迁到明确 owner；\n"
    "- 必需 diagnostics 有长期 owner；\n"
    "- tests 不再用 Pipeline 伪装生产状态。",
    1,
    "update legacy inventory pipeline coupling",
)
replace_exact(
    "Docs/LegacyCodeInventory.md",
    "| P0 | RoundSimPipeline → Worker resource 数据源耦合 | 新架构仍直接依赖旧壳，是 WA8 核心 |\n"
    "| P0 | Round transaction / rollback / TryPrepareRoundApply | 阻止 Full Worker Authority 完整闭环 |",
    "| CLOSED slice | Primary SharedFlow → WorkerInput ownership | 已迁到 `UMassCrowdRuntimeSubsystem`；WorkerInput 不再读取 Pipeline |\n"
    "| P0 | Round transaction / rollback / TryPrepareRoundApply | 阻止 Full Worker Authority 完整闭环 |",
    1,
    "update legacy priority table",
)

replace_exact(
    "Docs/SourceConsistencyAudit.md",
    "| 15 | WA8 Legacy | 新 Worker 仍有旧 Round shell 残留 | WorkerInputSync 仍从 `RoundSimPipelineSubsystem` 读取 Runtime Shared Flow | SOURCE DEBT |",
    "| 15 | WA8 Legacy / SharedFlow owner | 新 Worker 不应从旧 Round shell 取生产资源 | Primary Shared Flow owner 已迁到 `UMassCrowdRuntimeSubsystem`，WorkerInputSync 已与 Pipeline 断开；Round transaction/rollback 等仍残留 | EXACT + SOURCE DEBT REMAINS |",
    1,
    "update audit summary row",
)
replace_exact(
    "Docs/SourceConsistencyAudit.md",
    "## 7. 审计发现 5：Worker Input 仍依赖旧 RoundSimPipeline 数据源\n\n"
    "### 源码事实\n\n"
    "`CrowdDemoWorkerInputSync.cpp` 在构建 versioned resources 时仍访问：\n\n"
    "```text\n"
    "UCrowdDemoRoundSimPipelineSubsystem\n"
    "```\n\n"
    "并读取：\n\n"
    "```text\n"
    "GetRuntimeSharedFlowField()\n"
    "```\n\n"
    "这意味着：\n\n"
    "> Worker 已经是 Simulation Authority，不代表旧 Pipeline 已经退出生产数据链。\n\n"
    "这里是非常典型的迁移态：\n\n"
    "```text\n"
    "Authority migrated\n"
    "Data source not fully migrated\n"
    "```\n\n"
    "状态：SOURCE DEBT。\n\n"
    "这应成为 WA8 的高优先级治理点，因为只要 Worker Input 仍需要 RoundSimPipeline，开发者就不能把 Pipeline 纯粹理解成 Test Harness。",
    "## 7. 审计发现 5：Primary Shared Flow Resource Owner 已完成迁移\n\n"
    "### 当前源码事实\n\n"
    "`FCrowdMassSharedFlowResource` 的 primary world resource 现在由：\n\n"
    "```text\n"
    "UMassCrowdRuntimeSubsystem\n"
    "```\n\n"
    "持有。Pipeline 的 static/dynamic SharedFlow build 通过 RuntimeSubsystem 调用同一个 `FCrowdMassSharedFlowWork::EnsureResource()`，自身只保留 Demo diagnostic view/counters。\n\n"
    "`CrowdDemoWorkerInputSync.cpp` 构建 Environment versioned resource 时直接读取：\n\n"
    "```text\n"
    "RuntimeSubsystem.GetSharedFlowResource().Field\n"
    "```\n\n"
    "并且不再 include 或查询 `UCrowdDemoRoundSimPipelineSubsystem`。Target Prepared Resource 的资源身份也使用稳定 `CrowdWorkerResourceIds::Environment`，不再使用成员地址。\n\n"
    "状态：该 P0 子切片已关闭。\n\n"
    "仍需注意：RoundSimPipeline 继续承担 legacy Round frame、rollback/transaction、Target prepared state、metrics/diagnostics，因此 WA8 整体仍是 SOURCE DEBT / OPEN。",
    1,
    "update source consistency audit finding",
)

replace_exact(
    "Docs/PhasePlan.md",
    "## 3.2 当前必须删除的 Legacy\n\n"
    "按以下顺序实施，不建立第三套 retained cache、兼容 wrapper 或 fallback：",
    "## 3.2 已完成的 WA8 子切片\n\n"
    "本轮已完成：\n\n"
    "```text\n"
    "Primary SharedFlow Runtime Resource ownership\n"
    "RoundSimPipeline → UMassCrowdRuntimeSubsystem\n\n"
    "WorkerInputSync → RuntimeSubsystem SharedFlow resource\n"
    "WorkerInputSync → RoundSimPipeline direct dependency = 0\n\n"
    "Target Prepared ResourceId\n"
    "pointer-address → CrowdWorkerResourceIds::Environment\n"
    "```\n\n"
    "SharedFlow 算法、Round Stage 顺序和 Round Transaction 本切片未改变。\n\n"
    "## 3.3 当前必须删除的 Legacy\n\n"
    "继续按以下顺序实施，不建立第三套 retained cache、兼容 wrapper 或 fallback：",
    1,
    "record completed phase plan slice",
)
replace_exact(
    "Docs/PhasePlan.md",
    "## 3.3 WA8 验证门",
    "## 3.4 WA8 验证门",
    1,
    "renumber WA8 validation section",
)

print("shared-flow owner documentation sync passed")
