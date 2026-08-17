from pathlib import Path
import re

ROOTS = [Path("Source"), Path("Plugins")]
REPORT = Path("round_transaction_audit.txt")
SYMBOLS = [
    "SoftPressureRollbackHistory",
    "RecordSoftPressureRollbackSnapshot",
    "CompleteSoftPressureRollbackCombatState",
    "FindSoftPressureRollbackSnapshot",
    "IsSoftPressureRollbackSnapshotReadyForReplay",
    "RestoreSoftPressureRuntime",
    "GetSoftPressureRollbackSnapshot",
    "RollbackSnapshot",
    "BeginRollbackReplay",
    "RollbackReplayed",
    "BeginBoundaryTransaction",
    "TryPrepareRoundApply",
    "BoundaryOrchestrator",
    "FCrowdDemoRoundWorkBatch",
    "StageBoundaryBusinessWork",
    "DispatchBoundarySoftPressureWorkGraph",
    "DispatchBoundaryFacingWork",
    "TrySubmitWorkerV2ClockIntentEarly",
    "IsFullWorkerProductionMode",
    "AdvanceRoundWorkerFrame",
]

TEXT_SUFFIXES = {".h", ".hpp", ".cpp", ".inl", ".cs", ".md", ".py", ".yml", ".yaml"}
files = []
for root in ROOTS:
    if root.exists():
        files.extend(p for p in root.rglob("*") if p.is_file() and p.suffix.lower() in TEXT_SUFFIXES)

lines = []
lines.append("ROUND TRANSACTION CUT AUDIT\n")
for symbol in SYMBOLS:
    matches = []
    for path in files:
        try:
            text = path.read_text(encoding="utf-8-sig")
        except UnicodeDecodeError:
            continue
        for lineno, line in enumerate(text.splitlines(), 1):
            if symbol in line:
                matches.append(f"{path}:{lineno}:{line.strip()}")
    lines.append(f"\n## {symbol} ({len(matches)})\n")
    lines.extend(m + "\n" for m in matches)

# Emit bounded contexts for critical control-flow anchors.
contexts = [
    (Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimProcessors.cpp"), "bool AdvanceRoundWorkerFrame", 260),
    (Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.cpp"), "UCrowdDemoRoundSimPipelineSubsystem::TryPrepareRoundApply", 220),
    (Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.cpp"), "UCrowdDemoRoundSimPipelineSubsystem::BeginBoundaryTransaction", 80),
    (Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.cpp"), "UCrowdDemoRoundSimPipelineSubsystem::TrySubmitWorkerV2ClockIntentEarly", 180),
    (Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.cpp"), "UCrowdDemoRoundSimPipelineSubsystem::RecordSoftPressureRollbackSnapshot", 220),
    (Path("Source/MassAICrowdDemo/Mass/CrowdDemoRoundSimPipelineSubsystem.cpp"), "UCrowdDemoRoundSimPipelineSubsystem::RestoreSoftPressureRuntime", 220),
]
for path, needle, max_lines in contexts:
    lines.append(f"\n\n===== CONTEXT {path} :: {needle} =====\n")
    if not path.exists():
        lines.append("MISSING FILE\n")
        continue
    src = path.read_text(encoding="utf-8-sig").splitlines()
    index = next((i for i, line in enumerate(src) if needle in line), None)
    if index is None:
        lines.append("MISSING NEEDLE\n")
        continue
    start = max(0, index - 8)
    end = min(len(src), index + max_lines)
    for i in range(start, end):
        lines.append(f"{i+1:05d}: {src[i]}\n")

REPORT.write_text("".join(lines), encoding="utf-8")
print(f"wrote {REPORT} with {len(lines)} records")
